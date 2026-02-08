/*
 * MQCharInfo - Peer state, payload build, and Stacks/StacksPet (NetBots-style).
 */

#include "CharInfo.h"
#include "mq/Plugin.h"

#include <eqlib/game/Constants.h>
#include <eqlib/game/EQData.h>
#include <eqlib/game/Spells.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace charinfo {

// State bits (match NetBots eStates for client-side State[] conversion).
namespace StateBits {
	constexpr uint32_t STAND    = 0x00000010;
	constexpr uint32_t SIT     = 0x00000020;
	constexpr uint32_t DUCK    = 0x00000004;
	constexpr uint32_t BIND    = 0x00000008;
	constexpr uint32_t FEIGN   = 0x00000002;
	constexpr uint32_t DEAD    = 0x00000001;
	constexpr uint32_t MOUNT   = 0x00000040;
	constexpr uint32_t STUN    = 0x00000800;
	constexpr uint32_t MOVING  = 0x00000400;
	constexpr uint32_t LEV     = 0x00010000;
	constexpr uint32_t HOVER   = 0x00100000;
	constexpr uint32_t ATTACK  = 0x00000200;
	constexpr uint32_t INVIS   = 0x00000080;
	constexpr uint32_t ITU     = 0x00000100;
	constexpr uint32_t AFK     = 0x00008000;
	constexpr uint32_t GROUP   = 0x00002000;
	constexpr uint32_t RAID    = 0x00001000;
	constexpr uint32_t LFG     = 0x00004000;
	constexpr uint32_t HASPETAA = 0x01000000;
}

// State bit -> display name (order defines State[] in Lua).
static const struct { uint32_t bit; const char* name; } kStateBitNames[] = {
	{ 0x00000001, "DEAD" },    { 0x00000002, "FEIGN" },   { 0x00000004, "DUCK" },
	{ 0x00000008, "BIND" },    { 0x00000010, "STAND" },   { 0x00000020, "SIT" },
	{ 0x00000040, "MOUNT" },   { 0x00000080, "INVIS" },   { 0x00000100, "InvisToUndead" },
	{ 0x00000200, "ATTACK" },  { 0x00000400, "MOVING" },  { 0x00000800, "STUN" },
	{ 0x00001000, "RAID" },    { 0x00002000, "GROUP" },   { 0x00004000, "LFG" },
	{ 0x00008000, "AFK" },     { 0x00010000, "LEVITATING" }, { 0x00020000, "AutoFire" },
	{ 0x00040000, "WantAggro" }, { 0x00080000, "HaveAggro" },
	{ 0x00100000, "HOVER" },   { 0x00200000, "NavigationActive" }, { 0x00400000, "NavigationPaused" },
	{ 0x00800000, "BotActive" }, { 0x01000000, "PetAffinity" },
};
static const size_t kNumStateBitNames = sizeof(kStateBitNames) / sizeof(kStateBitNames[0]);

// Buff state bits: first 24 detrimental, then beneficial (order defines BuffState[] in Lua).
static const struct { uint32_t bit; const char* name; } kBuffBitNames[] = {
	{ 0x00000001, "Slowed" },   { 0x00000002, "Rooted" },   { 0x00000004, "Mesmerized" },
	{ 0x00000008, "Crippled" },  { 0x00000010, "Maloed" },  { 0x00000020, "Tashed" },
	{ 0x00000040, "Snared" },    { 0x00000080, "RevDSed" },  { 0x00000100, "Charmed" },
	{ 0x00000200, "Diseased" },  { 0x00000400, "Poisoned" }, { 0x00000800, "Cursed" },
	{ 0x00001000, "Corrupted" }, { 0x00002000, "Blinded" },  { 0x00004000, "CastingLevel" },
	{ 0x00008000, "EnduDrain" }, { 0x00010000, "Feared" },   { 0x00020000, "Healing" },
	{ 0x00040000, "Invulnerable" }, { 0x00080000, "LifeDrain" }, { 0x00100000, "ManaDrain" },
	{ 0x00200000, "Resistance" }, { 0x00400000, "Silenced" }, { 0x00800000, "SpellCost" },
	{ 0x01000000, "SpellDamage" }, { 0x02000000, "SpellSlowed" }, { 0x04000000, "Trigger" },
	{ 0x00000001, "DSed" },     { 0x00000002, "Aego" },     { 0x00000004, "Skin" },
	{ 0x00000008, "Focus" },    { 0x00000010, "Regen" },    { 0x00000020, "Symbol" },
	{ 0x00000040, "Clarity" },  { 0x00000080, "Pred" },     { 0x00000100, "Strength" },
	{ 0x00000200, "Brells" },   { 0x00000400, "SV" },       { 0x00000800, "SE" },
	{ 0x00001000, "HybridHP" }, { 0x00002000, "Growth" },   { 0x00004000, "Shining" },
	{ 0x00008000, "Hasted" },
};
static const size_t kNumBuffBitNames = sizeof(kBuffBitNames) / sizeof(kBuffBitNames[0]);
static const size_t kNumDetrBuffBits = 24u;

std::vector<std::string> StateBitsToStrings(uint32_t state_bits)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < kNumStateBitNames && state_bits != 0; i++) {
		if (state_bits & kStateBitNames[i].bit)
			out.push_back(kStateBitNames[i].name);
	}
	return out;
}

std::vector<std::string> BuffStateBitsToStrings(uint32_t detr_bits, uint32_t bene_bits)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < kNumDetrBuffBits; i++) {
		if (detr_bits & kBuffBitNames[i].bit)
			out.push_back(kBuffBitNames[i].name);
	}
	for (size_t i = kNumDetrBuffBits; i < kNumBuffBitNames; i++) {
		if (bene_bits & kBuffBitNames[i].bit)
			out.push_back(kBuffBitNames[i].name);
	}
	return out;
}

static PeerMap s_peers;
static std::mutex s_peersMutex;

PeerMap& GetPeers() {
	return s_peers;
}

std::mutex& GetPeersMutex() {
	return s_peersMutex;
}

static void AddBuffEntry(mq::proto::charinfo::CharInfoPublish* msg,
	int spellId, int duration, bool is_long_buff, int* detrimentals)
{
	EQ_Spell* spell = GetSpellByID(spellId);
	if (!spell || spell->ID <= 0)
		return;

	auto* entry = is_long_buff ? msg->add_buff() : msg->add_short_buff();
	entry->set_duration(duration);
	auto* si = entry->mutable_spell();
	si->set_id(spell->ID);
	if (spell->Name[0])
		si->set_name(spell->Name);
	si->set_category(spell->Category);
	si->set_level(static_cast<int>(spell->GetSpellLevelNeeded(GetPcProfile()->Class)));

	if (spell->SpellType == SpellType_Detrimental && detrimentals)
		(*detrimentals)++;
}

bool BuildPublishPayload(mq::proto::charinfo::CharInfoPublish* out)
{
	if (!pLocalPlayer || !GetPcProfile() || !pZoneInfo)
		return false;

	PcProfile* profile = GetPcProfile();
	out->set_sender(pLocalPlayer->DisplayedName);
	out->set_name(pLocalPlayer->DisplayedName);
	out->set_id(pLocalPlayer->SpawnID);
	out->set_level(pLocalPlayer->Level);

	int classId = pLocalPlayer->GetClass();
	auto* ci = out->mutable_class_info();
	ci->set_id(classId);
	ci->set_name(GetClassDesc(classId));
	constexpr int numClasses = 17;
	if (classId >= 0 && classId < numClasses)
		ci->set_short_name(ClassInfo[classId].ShortName);

	out->set_pct_hps(pLocalPlayer->HPMax == 0 ? 0 : static_cast<int>(pLocalPlayer->HPCurrent * 100 / pLocalPlayer->HPMax));
	out->set_pct_mana(pLocalPlayer->ManaCurrent >= 0 && pLocalPlayer->ManaMax > 0
		? static_cast<int>(pLocalPlayer->ManaCurrent * 100 / pLocalPlayer->ManaMax) : 0);

	// Target ID and PctHPs: same as NetBots MakeTARGT (MQ2NetBots.cpp) and MQ2SpawnType PctHPs
	if (pTarget && pTarget->SpawnID) {
		auto* ti = out->mutable_target();
		ti->set_id(pTarget->SpawnID);
		if (pTarget->DisplayedName[0])
			ti->set_name(pTarget->DisplayedName);
		out->set_target_hp(pTarget->HPMax == 0 ? 0 : static_cast<int>(pTarget->HPCurrent * 100 / pTarget->HPMax));
	}

	auto* zi = out->mutable_zone();
	zi->set_id(pZoneInfo->ZoneID);
	if (pZoneInfo->ShortName[0])
		zi->set_short_name(pZoneInfo->ShortName);
	if (pZoneInfo->LongName[0])
		zi->set_name(pZoneInfo->LongName);

	int detrimentals = 0;
	int usedSlots = 0;

	for (int i = 0; i < NUM_LONG_BUFFS; i++) {
		int spellId = profile->GetEffect(i).SpellID;
		if (spellId <= 0)
			continue;
		usedSlots++;
		int dur = static_cast<int>(GetSpellBuffTimer(spellId));
		if (dur < 0) dur = -1;
		AddBuffEntry(out, spellId, dur, true, &detrimentals);
	}

	for (int i = 0; i < NUM_SHORT_BUFFS; i++) {
		int spellId = profile->GetTempEffect(i).SpellID;
		if (spellId <= 0)
			continue;
		int dur = static_cast<int>(GetSpellBuffTimer(spellId));
		if (dur < 0) dur = -1;
		AddBuffEntry(out, spellId, dur, false, &detrimentals);
	}

	out->set_free_buff_slots(GetCharMaxBuffSlots() - usedSlots);
	out->set_detrimentals(detrimentals);
	out->set_count_poison(static_cast<int>(GetMySpellCounters(SPA_POISON)));
	out->set_count_disease(static_cast<int>(GetMySpellCounters(SPA_DISEASE)));
	out->set_count_curse(static_cast<int>(GetMySpellCounters(SPA_CURSE)));
	out->set_count_corruption(static_cast<int>(GetMySpellCounters(SPA_CORRUPTION)));

	if (pLocalPlayer->PetID && pPetInfoWnd) {
		for (int i = 0; i < MAX_TOTAL_BUFFS_NPC; i++) {
			int spellId = pPetInfoWnd->GetBuff(i);
			if (spellId <= 0)
				continue;
			int dur = static_cast<int>(pPetInfoWnd->GetBuffTimer(i));
			if (dur < 0) dur = -1;
			auto* entry = out->add_pet_buff();
			entry->set_duration(dur);
			EQ_Spell* spell = GetSpellByID(spellId);
			if (spell && spell->ID > 0) {
				auto* si = entry->mutable_spell();
				si->set_id(spell->ID);
				if (spell->Name[0])
					si->set_name(spell->Name);
				si->set_category(spell->Category);
				si->set_level(static_cast<int>(spell->GetSpellLevelNeeded(GetPcProfile()->Class)));
			}
		}
		PlayerClient* pet = GetSpawnByID(pLocalPlayer->PetID);
		if (pet && pet->HPMax > 0)
			out->set_pet_hp(static_cast<int>(pet->HPCurrent * 100 / pet->HPMax));
	}

	if (pLocalPlayer->EnduranceMax >= 0)
		out->set_max_endurance(static_cast<int>(pLocalPlayer->EnduranceMax));

	// --- New top-level vitals ---
	out->set_current_hp(pLocalPlayer->HPCurrent);
	out->set_max_hp(pLocalPlayer->HPMax);
	out->set_current_mana(pLocalPlayer->ManaCurrent >= 0 ? pLocalPlayer->ManaCurrent : 0);
	out->set_max_mana(pLocalPlayer->ManaMax > 0 ? pLocalPlayer->ManaMax : 0);
	out->set_current_endurance(pLocalPlayer->EnduranceCurrent >= 0 ? pLocalPlayer->EnduranceCurrent : 0);
	if (pLocalPlayer->EnduranceMax > 0)
		out->set_pct_endurance(static_cast<int32_t>(pLocalPlayer->EnduranceCurrent * 100 / pLocalPlayer->EnduranceMax));
	else
		out->set_pct_endurance(0);

	// --- Pet (top-level) ---
	out->set_pet_id(pLocalPlayer->PetID);
	// Pet affinity (has Pet AA): would require GetAARankByName; set false here.
	if (pLocalPlayer->PetID && pPetInfoWnd)
		out->set_pet_affinity(false);

	// --- Zone position (x, y, z, heading, instance_id) ---
	zi->set_instance_id(pLocalPC ? static_cast<int32_t>(pLocalPC->instance) : 0);
	zi->set_x(pLocalPlayer->X);
	zi->set_y(pLocalPlayer->Y);
	zi->set_z(pLocalPlayer->Z);
	zi->set_heading(pLocalPlayer->Heading);

	// --- State bits (client converts to State[] string array) ---
	uint32_t stateBits = 0;
	switch (pLocalPlayer->StandState) {
		case STANDSTATE_STAND: stateBits |= StateBits::STAND; break;
		case STANDSTATE_SIT:   stateBits |= StateBits::SIT;   break;
		case STANDSTATE_DUCK:  stateBits |= StateBits::DUCK;  break;
		case STANDSTATE_BIND:  stateBits |= StateBits::BIND;  break;
		case STANDSTATE_FEIGN: stateBits |= StateBits::FEIGN; break;
		case STANDSTATE_DEAD:  stateBits |= StateBits::DEAD;  break;
		default: break;
	}
	if (pEverQuestInfo && pEverQuestInfo->bAutoAttack)
		stateBits |= StateBits::ATTACK;
	if (pLocalPlayer->Mount)
		stateBits |= StateBits::MOUNT;
	if (std::fabs(pLocalPlayer->SpeedRun) > 0.0f)
		stateBits |= StateBits::MOVING;
	if (pLocalPlayer->AFK)
		stateBits |= StateBits::AFK;
	if (pLocalPlayer->LFG)
		stateBits |= StateBits::LFG;
	if (pLocalPlayer->RespawnTimer != 0)
		stateBits |= StateBits::HOVER;
	if (pLocalPlayer->PlayerState & 0x20)
		stateBits |= StateBits::STUN;
	if (pLocalPlayer->mPlayerPhysicsClient.Levitate == 2)
		stateBits |= StateBits::LEV;
	if (pLocalPC && pLocalPC->pGroupInfo)
		stateBits |= StateBits::GROUP;
	if (pRaid && pRaid->RaidMemberCount)
		stateBits |= StateBits::RAID;
	// Invis / ITU: check HideMode or buff-based (simplified: HideMode)
	if ((pLocalPlayer->HideMode & 0x01))
		stateBits |= StateBits::INVIS;
	out->set_state_bits(stateBits);

	// --- Casting spell ---
	if (pLocalPlayer->CastingData.SpellID > 0)
		out->set_casting_spell_id(pLocalPlayer->CastingData.SpellID);

	// --- Combat state ---
	out->set_combat_state(static_cast<int32_t>(GetCombatState()));

	// --- Detrimentals: no_cure, life_drain, mana_drain, endu_drain (0 = not computed here) ---
	// detr_state_bits / bene_state_bits: 0 = client will show empty BuffState[]
	out->set_detr_state_bits(0);
	out->set_bene_state_bits(0);

	// --- Gems (ordered spell IDs) ---
	PcProfile* prof = GetPcProfile();
	if (prof) {
		for (int i = 0; i < NUM_SPELL_GEMS; i++) {
			int spellId = prof->GetMemorizedSpell(i);
			if (spellId > 0)
				out->add_gem(spellId);
			else
				out->add_gem(0);
		}
	}

	// --- Version ---
	out->set_version(CHARINFO_VERSION);

	// --- Experience ---
	if (pLocalPC) {
		auto* exp = out->mutable_experience();
		exp->set_pct_exp(static_cast<float>(pLocalPC->Exp) / EXP_TO_PCT_RATIO);
		exp->set_pct_aa_exp(static_cast<float>(pLocalPC->AAExp) / EXP_TO_PCT_RATIO);
		exp->set_pct_group_leader_exp(0.f); // optional: GroupLeadershipExp if available
		PcProfile* pp = GetPcProfile();
		if (pp) {
			exp->set_aa_spent(pp->AAPointsSpent);
			exp->set_aa_unused(pp->AAPoints);
			int assigned = 0;
			for (int i = 0; i < 6; i++) assigned += pp->AAPointsAssigned[i];
			exp->set_aa_assigned(assigned);
			exp->set_total_aa(pp->AAPoints + pp->AAPointsSpent);
		}
	}

	// --- MakeCamp (only when MQ2MoveUtils loaded; null guards) ---
	if (IsPluginLoaded("MQ2MoveUtils")) {
		char buf[256];
		strcpy_s(buf, "${Select[${MakeCamp.Status},ON,PAUSED]}:${MakeCamp.AnchorX}:${MakeCamp.AnchorY}:${MakeCamp.CampRadius}:${MakeCamp.CampDist}");
		if (ParseMacroData(buf, sizeof(buf))) {
			auto* mc = out->mutable_make_camp();
			float x = 0, y = 0, r = 0, d = 0;
			int status = 0;
			if (std::sscanf(buf, "%d:%f:%f:%f:%f", &status, &x, &y, &r, &d) >= 5) {
				mc->set_status(status);
				mc->set_x(x);
				mc->set_y(y);
				mc->set_radius(r);
				mc->set_distance(d);
			}
		}
	}

	// --- Macro ---
	{
		auto* macro = out->mutable_macro();
		int macroState = 0; // MACRO_NONE
		if (gszMacroName[0]) {
			macroState = 1; // MACRO_RUNNING
			if (MQMacroBlockPtr pBlock = GetCurrentMacroBlock()) {
				if (pBlock->Paused)
					macroState = 2; // MACRO_PAUSED
			}
		}
		macro->set_macro_state(macroState);
		macro->set_macro_name(gszMacroName);
	}

	// --- Free inventory (by size 0..4) ---
	{
		int freeSlots[5] = { 0 };
		int slotMax = 4;
		PcProfile* pp = GetPcProfile();
		if (pp) {
			for (int slot = InvSlot_FirstBagSlot; slot <= GetHighestAvailableBagSlot(); slot++) {
				if (ItemPtr pItem = pp->InventoryContainer.GetItem(slot)) {
					if (pItem->IsContainer()) {
						int cap = static_cast<int>(pItem->GetItemDefinition()->SizeCapacity);
						int iSize = (cap >= 0 && cap <= slotMax) ? cap : slotMax;
						freeSlots[iSize] += pItem->GetHeldItems().GetSize() - pItem->GetHeldItems().GetCount();
					}
				} else {
					freeSlots[slotMax]++;
				}
			}
			for (int s = slotMax - 1; s >= 0; s--)
				freeSlots[s] += freeSlots[s + 1];
		}
		for (int i = 0; i < 5; i++)
			out->add_free_inventory(freeSlots[i]);
	}

	return true;
}

bool StacksForPeer(const mq::proto::charinfo::CharInfoPublish& peer, const char* spellNameOrId)
{
	if (!spellNameOrId || !spellNameOrId[0])
		return false;
	EQ_Spell* testSpell = GetSpellByName(spellNameOrId);
	if (!testSpell)
		return false;

	for (int i = 0; i < peer.buff_size(); i++) {
		int buffId = peer.buff(i).spell().id();
		if (buffId <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(buffId);
		if (!buffSpell) continue;
		if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
			return false;
	}
	for (int i = 0; i < peer.short_buff_size(); i++) {
		int buffId = peer.short_buff(i).spell().id();
		if (buffId <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(buffId);
		if (!buffSpell) continue;
		if (!IsBardSong(buffSpell) && !(IsSPAEffect(testSpell, SPA_CHANGE_FORM) && !testSpell->DurationWindow)) {
			if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
				return false;
		}
	}
	return true;
}

bool StacksPetForPeer(const mq::proto::charinfo::CharInfoPublish& peer, const char* spellNameOrId)
{
	if (!spellNameOrId || !spellNameOrId[0])
		return false;
	EQ_Spell* testSpell = GetSpellByName(spellNameOrId);
	if (!testSpell)
		return false;

	for (int i = 0; i < peer.pet_buff_size(); i++) {
		int buffId = peer.pet_buff(i).spell().id();
		if (buffId <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(buffId);
		if (!buffSpell) continue;
		if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
			return false;
	}
	return true;
}

} // namespace charinfo
