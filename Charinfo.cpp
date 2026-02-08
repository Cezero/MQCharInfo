/*
 * MQCharinfo - Peer state, payload build, and Stacks/StacksPet (NetBots-style).
 */

#include "Charinfo.h"
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

PeerMap& GetPeers() {
	return s_peers;
}

static void SetZoneDistanceFromLocal(PeerZoneInfo& zone)
{
	if (!pLocalPlayer || !pLocalPC) return;
	if (pLocalPC->zoneId != zone.id) return;
	if (static_cast<uint16_t>(pLocalPC->instance) != static_cast<uint16_t>(zone.instance_id)) return;
	float dX = pLocalPlayer->X - zone.x;
	float dY = pLocalPlayer->Y - zone.y;
	float dZ = pLocalPlayer->Z - zone.z;
	zone.distance = static_cast<double>(std::sqrtf(dX * dX + dY * dY + dZ * dZ));
}

CharinfoPeer FromPublish(const mq::proto::charinfo::CharinfoPublish& pub)
{
	CharinfoPeer p;
	p.name = pub.name();
	p.id = pub.id();
	p.level = pub.level();
	p.pct_hps = pub.pct_hps();
	p.pct_mana = pub.pct_mana();
	p.target_hp = pub.target_hp();
	p.free_buff_slots = pub.free_buff_slots();
	p.detrimentals = pub.detrimentals();
	p.count_poison = pub.count_poison();
	p.count_disease = pub.count_disease();
	p.count_curse = pub.count_curse();
	p.count_corruption = pub.count_corruption();
	p.pet_hp = pub.pet_hp();
	p.max_endurance = pub.max_endurance();
	p.current_hp = pub.current_hp();
	p.max_hp = pub.max_hp();
	p.current_mana = pub.current_mana();
	p.max_mana = pub.max_mana();
	p.current_endurance = pub.current_endurance();
	p.pct_endurance = pub.pct_endurance();
	p.pet_id = pub.pet_id();
	p.pet_affinity = pub.pet_affinity();
	p.no_cure = pub.no_cure();
	p.life_drain = pub.life_drain();
	p.mana_drain = pub.mana_drain();
	p.endu_drain = pub.endu_drain();
	p.state_bits = pub.state_bits();
	p.detr_state_bits = pub.detr_state_bits();
	p.bene_state_bits = pub.bene_state_bits();
	p.casting_spell_id = pub.casting_spell_id();
	p.combat_state = pub.combat_state();
	p.version = pub.version();

	p.class_info.name = pub.class_info().name();
	p.class_info.short_name = pub.class_info().short_name();
	p.class_info.id = pub.class_info().id();

	p.target.name = pub.target().name();
	p.target.id = pub.target().id();

	p.zone.name = pub.zone().name();
	p.zone.short_name = pub.zone().short_name();
	p.zone.id = pub.zone().id();
	p.zone.instance_id = pub.zone().instance_id();
	p.zone.x = pub.zone().x();
	p.zone.y = pub.zone().y();
	p.zone.z = pub.zone().z();
	p.zone.heading = pub.zone().heading();
	p.zone.distance = -1.0;
	SetZoneDistanceFromLocal(p.zone);

	const int buffSize = pub.buff_spells_size();
	const int durSize = pub.buff_durations_size();
	p.buff.clear();
	p.buff.reserve(static_cast<size_t>(buffSize));
	for (int i = 0; i < buffSize; i++) {
		PeerBuffEntry e;
		e.spell.name = pub.buff_spells(i).name();
		e.spell.id = pub.buff_spells(i).id();
		e.spell.category = pub.buff_spells(i).category();
		e.spell.level = pub.buff_spells(i).level();
		e.duration = (i < durSize) ? pub.buff_durations(i) : -1;
		p.buff.push_back(std::move(e));
	}
	const int shortSpellSize = pub.short_buff_spells_size();
	const int shortDurSize = pub.short_buff_durations_size();
	p.short_buff.clear();
	p.short_buff.reserve(static_cast<size_t>(shortSpellSize));
	for (int i = 0; i < shortSpellSize; i++) {
		PeerBuffEntry e;
		e.spell.name = pub.short_buff_spells(i).name();
		e.spell.id = pub.short_buff_spells(i).id();
		e.spell.category = pub.short_buff_spells(i).category();
		e.spell.level = pub.short_buff_spells(i).level();
		e.duration = (i < shortDurSize) ? pub.short_buff_durations(i) : -1;
		p.short_buff.push_back(std::move(e));
	}
	const int petSpellSize = pub.pet_buff_spells_size();
	const int petDurSize = pub.pet_buff_durations_size();
	p.pet_buff.clear();
	p.pet_buff.reserve(static_cast<size_t>(petSpellSize));
	for (int i = 0; i < petSpellSize; i++) {
		PeerBuffEntry e;
		e.spell.name = pub.pet_buff_spells(i).name();
		e.spell.id = pub.pet_buff_spells(i).id();
		e.spell.category = pub.pet_buff_spells(i).category();
		e.spell.level = pub.pet_buff_spells(i).level();
		e.duration = (i < petDurSize) ? pub.pet_buff_durations(i) : -1;
		p.pet_buff.push_back(std::move(e));
	}

	p.state = StateBitsToStrings(pub.state_bits());
	p.buff_state = BuffStateBitsToStrings(pub.detr_state_bits(), pub.bene_state_bits());

	PcProfile* profile = GetPcProfile();
	p.gems.clear();
	p.gems.reserve(static_cast<size_t>(pub.gem_size()));
	for (int i = 0; i < pub.gem_size(); i++) {
		PeerGemEntry ge;
		ge.id = pub.gem(i);
		if (EQ_Spell* spell = GetSpellByID(pub.gem(i))) {
			ge.name = spell->Name[0] ? spell->Name : "";
			ge.category = spell->Category;
			ge.level = profile ? static_cast<int32_t>(spell->GetSpellLevelNeeded(profile->Class)) : 0;
		} else {
			ge.name = "";
			ge.category = 0;
			ge.level = 0;
		}
		p.gems.push_back(std::move(ge));
	}

	p.free_inventory.clear();
	p.free_inventory.reserve(static_cast<size_t>(pub.free_inventory_size()));
	for (int i = 0; i < pub.free_inventory_size(); i++)
		p.free_inventory.push_back(pub.free_inventory(i));

	if (pub.has_experience()) {
		PeerExperienceInfo ex;
		ex.pct_exp = pub.experience().pct_exp();
		ex.pct_aa_exp = pub.experience().pct_aa_exp();
		ex.pct_group_leader_exp = pub.experience().pct_group_leader_exp();
		ex.total_aa = pub.experience().total_aa();
		ex.aa_spent = pub.experience().aa_spent();
		ex.aa_unused = pub.experience().aa_unused();
		ex.aa_assigned = pub.experience().aa_assigned();
		p.has_experience = true;
		p.experience = ex;
	} else {
		p.has_experience = false;
	}
	if (pub.has_make_camp()) {
		PeerMakeCampInfo mc;
		mc.status = pub.make_camp().status();
		mc.x = pub.make_camp().x();
		mc.y = pub.make_camp().y();
		mc.radius = pub.make_camp().radius();
		mc.distance = pub.make_camp().distance();
		p.has_make_camp = true;
		p.make_camp = mc;
	} else {
		p.has_make_camp = false;
	}
	if (pub.has_macro()) {
		PeerMacroInfo mac;
		mac.macro_state = pub.macro().macro_state();
		mac.macro_name = pub.macro().macro_name();
		p.has_macro = true;
		p.macro = mac;
	} else {
		p.has_macro = false;
	}
	return p;
}

static void AddBuffEntry(mq::proto::charinfo::CharinfoPublish* msg,
	int spellId, int duration, bool is_long_buff, int* detrimentals)
{
	EQ_Spell* spell = GetSpellByID(spellId);
	if (!spell || spell->ID <= 0)
		return;

	auto* si = is_long_buff ? msg->add_buff_spells() : msg->add_short_buff_spells();
	si->set_id(spell->ID);
	if (spell->Name[0])
		si->set_name(spell->Name);
	si->set_category(spell->Category);
	si->set_level(static_cast<int>(spell->GetSpellLevelNeeded(GetPcProfile()->Class)));
	if (is_long_buff)
		msg->add_buff_durations(duration);
	else
		msg->add_short_buff_durations(duration);

	if (spell->SpellType == SpellType_Detrimental && detrimentals)
		(*detrimentals)++;
}

bool BuildPublishPayload(mq::proto::charinfo::CharinfoPublish* out)
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
			EQ_Spell* spell = GetSpellByID(spellId);
			if (spell && spell->ID > 0) {
				auto* si = out->add_pet_buff_spells();
				si->set_id(spell->ID);
				if (spell->Name[0])
					si->set_name(spell->Name);
				si->set_category(spell->Category);
				si->set_level(static_cast<int>(spell->GetSpellLevelNeeded(GetPcProfile()->Class)));
				out->add_pet_buff_durations(dur);
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

namespace {

using FieldId = mq::proto::charinfo::CharinfoFieldId;

static bool SpellInfoEqual(const mq::proto::charinfo::SpellInfo& a, const mq::proto::charinfo::SpellInfo& b) {
	return a.id() == b.id() && a.name() == b.name() && a.category() == b.category() && a.level() == b.level();
}

static bool BuffSpellsEqual(const mq::proto::charinfo::CharinfoPublish& cur, const mq::proto::charinfo::CharinfoPublish& prev,
	int (mq::proto::charinfo::CharinfoPublish::*size)() const,
	const mq::proto::charinfo::SpellInfo& (mq::proto::charinfo::CharinfoPublish::*get)(int) const)
{
	int n = (cur.*size)();
	if ((prev.*size)() != n) return false;
	for (int i = 0; i < n; i++)
		if (!SpellInfoEqual((cur.*get)(i), (prev.*get)(i))) return false;
	return true;
}

static bool Int32RepeatedEqual(const mq::proto::charinfo::CharinfoPublish& cur, const mq::proto::charinfo::CharinfoPublish& prev,
	int (mq::proto::charinfo::CharinfoPublish::*size)() const,
	int32_t (mq::proto::charinfo::CharinfoPublish::*get)(int) const)
{
	int n = (cur.*size)();
	if ((prev.*size)() != n) return false;
	for (int i = 0; i < n; i++)
		if ((cur.*get)(i) != (prev.*get)(i)) return false;
	return true;
}

} // namespace

bool BuildUpdatePayload(const mq::proto::charinfo::CharinfoPublish& current,
	const mq::proto::charinfo::CharinfoPublish& previous,
	mq::proto::charinfo::CharinfoUpdate* out)
{
	using Id = mq::proto::charinfo::CharinfoFieldId;
	bool any = false;

#define ADD_SCALAR_I32(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_i32(current.field()); any = true; } } while(0)
#define ADD_SCALAR_I64(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_i64(current.field()); any = true; } } while(0)
#define ADD_SCALAR_F(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_f(current.field()); any = true; } } while(0)
#define ADD_SCALAR_STR(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_str(current.field()); any = true; } } while(0)
#define ADD_SCALAR_BITS(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_bits(current.field()); any = true; } } while(0)
#define ADD_SCALAR_B(field, id) do { if (current.field() != previous.field()) { auto* u = out->add_updates(); u->set_field_id(id); u->set_b(current.field()); any = true; } } while(0)

	ADD_SCALAR_STR(sender, Id::FIELD_sender);
	ADD_SCALAR_STR(name, Id::FIELD_name);
	ADD_SCALAR_I32(id, Id::FIELD_id);
	ADD_SCALAR_I32(level, Id::FIELD_level);
	if (!current.class_info().SerializeAsString().empty() || !previous.class_info().SerializeAsString().empty()) {
		if (current.class_info().SerializeAsString() != previous.class_info().SerializeAsString()) {
			auto* u = out->add_updates(); u->set_field_id(Id::FIELD_class_info); *u->mutable_class_info() = current.class_info(); any = true;
		}
	}
	ADD_SCALAR_I32(pct_hps, Id::FIELD_pct_hps);
	ADD_SCALAR_I32(pct_mana, Id::FIELD_pct_mana);
	if (current.target().SerializeAsString() != previous.target().SerializeAsString()) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_target); *u->mutable_target() = current.target(); any = true;
	}
	ADD_SCALAR_I32(target_hp, Id::FIELD_target_hp);
	if (current.zone().SerializeAsString() != previous.zone().SerializeAsString()) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_zone); *u->mutable_zone() = current.zone(); any = true;
	}
	if (current.buff_spells_size() != previous.buff_spells_size() ||
	    !BuffSpellsEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::buff_spells_size, &mq::proto::charinfo::CharinfoPublish::buff_spells)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_buff_spells);
		auto* list = u->mutable_spell_list(); for (int i = 0; i < current.buff_spells_size(); i++) *list->add_spell() = current.buff_spells(i); any = true;
	}
	if (!Int32RepeatedEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::buff_durations_size, &mq::proto::charinfo::CharinfoPublish::buff_durations)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_buff_durations);
		auto* list = u->mutable_int32_list(); for (int i = 0; i < current.buff_durations_size(); i++) list->add_value(current.buff_durations(i)); any = true;
	}
	if (current.short_buff_spells_size() != previous.short_buff_spells_size() ||
	    !BuffSpellsEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::short_buff_spells_size, &mq::proto::charinfo::CharinfoPublish::short_buff_spells)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_short_buff_spells);
		auto* list = u->mutable_spell_list(); for (int i = 0; i < current.short_buff_spells_size(); i++) *list->add_spell() = current.short_buff_spells(i); any = true;
	}
	if (!Int32RepeatedEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::short_buff_durations_size, &mq::proto::charinfo::CharinfoPublish::short_buff_durations)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_short_buff_durations);
		auto* list = u->mutable_int32_list(); for (int i = 0; i < current.short_buff_durations_size(); i++) list->add_value(current.short_buff_durations(i)); any = true;
	}
	if (current.pet_buff_spells_size() != previous.pet_buff_spells_size() ||
	    !BuffSpellsEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::pet_buff_spells_size, &mq::proto::charinfo::CharinfoPublish::pet_buff_spells)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_pet_buff_spells);
		auto* list = u->mutable_spell_list(); for (int i = 0; i < current.pet_buff_spells_size(); i++) *list->add_spell() = current.pet_buff_spells(i); any = true;
	}
	if (!Int32RepeatedEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::pet_buff_durations_size, &mq::proto::charinfo::CharinfoPublish::pet_buff_durations)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_pet_buff_durations);
		auto* list = u->mutable_int32_list(); for (int i = 0; i < current.pet_buff_durations_size(); i++) list->add_value(current.pet_buff_durations(i)); any = true;
	}
	ADD_SCALAR_I32(free_buff_slots, Id::FIELD_free_buff_slots);
	ADD_SCALAR_I32(detrimentals, Id::FIELD_detrimentals);
	ADD_SCALAR_I32(count_poison, Id::FIELD_count_poison);
	ADD_SCALAR_I32(count_disease, Id::FIELD_count_disease);
	ADD_SCALAR_I32(count_curse, Id::FIELD_count_curse);
	ADD_SCALAR_I32(count_corruption, Id::FIELD_count_corruption);
	ADD_SCALAR_I32(pet_hp, Id::FIELD_pet_hp);
	ADD_SCALAR_I32(max_endurance, Id::FIELD_max_endurance);
	ADD_SCALAR_I32(current_hp, Id::FIELD_current_hp);
	ADD_SCALAR_I32(max_hp, Id::FIELD_max_hp);
	ADD_SCALAR_I32(current_mana, Id::FIELD_current_mana);
	ADD_SCALAR_I32(max_mana, Id::FIELD_max_mana);
	ADD_SCALAR_I32(current_endurance, Id::FIELD_current_endurance);
	ADD_SCALAR_I32(pct_endurance, Id::FIELD_pct_endurance);
	ADD_SCALAR_I32(pet_id, Id::FIELD_pet_id);
	ADD_SCALAR_B(pet_affinity, Id::FIELD_pet_affinity);
	ADD_SCALAR_I64(no_cure, Id::FIELD_no_cure);
	ADD_SCALAR_I64(life_drain, Id::FIELD_life_drain);
	ADD_SCALAR_I64(mana_drain, Id::FIELD_mana_drain);
	ADD_SCALAR_I64(endu_drain, Id::FIELD_endu_drain);
	ADD_SCALAR_BITS(state_bits, Id::FIELD_state_bits);
	ADD_SCALAR_BITS(detr_state_bits, Id::FIELD_detr_state_bits);
	ADD_SCALAR_BITS(bene_state_bits, Id::FIELD_bene_state_bits);
	ADD_SCALAR_I32(casting_spell_id, Id::FIELD_casting_spell_id);
	ADD_SCALAR_I32(combat_state, Id::FIELD_combat_state);
	if (!Int32RepeatedEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::gem_size, &mq::proto::charinfo::CharinfoPublish::gem)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_gem);
		auto* list = u->mutable_int32_list(); for (int i = 0; i < current.gem_size(); i++) list->add_value(current.gem(i)); any = true;
	}
	ADD_SCALAR_F(version, Id::FIELD_version);
	if (current.has_experience() || previous.has_experience()) {
		if (current.experience().SerializeAsString() != previous.experience().SerializeAsString()) {
			auto* u = out->add_updates(); u->set_field_id(Id::FIELD_experience); *u->mutable_experience() = current.experience(); any = true;
		}
	}
	if (current.has_make_camp() || previous.has_make_camp()) {
		if (current.make_camp().SerializeAsString() != previous.make_camp().SerializeAsString()) {
			auto* u = out->add_updates(); u->set_field_id(Id::FIELD_make_camp); *u->mutable_make_camp() = current.make_camp(); any = true;
		}
	}
	if (current.has_macro() || previous.has_macro()) {
		if (current.macro().SerializeAsString() != previous.macro().SerializeAsString()) {
			auto* u = out->add_updates(); u->set_field_id(Id::FIELD_macro); *u->mutable_macro() = current.macro(); any = true;
		}
	}
	if (!Int32RepeatedEqual(current, previous, &mq::proto::charinfo::CharinfoPublish::free_inventory_size, &mq::proto::charinfo::CharinfoPublish::free_inventory)) {
		auto* u = out->add_updates(); u->set_field_id(Id::FIELD_free_inventory);
		auto* list = u->mutable_int32_list(); for (int i = 0; i < current.free_inventory_size(); i++) list->add_value(current.free_inventory(i)); any = true;
	}

#undef ADD_SCALAR_I32
#undef ADD_SCALAR_I64
#undef ADD_SCALAR_F
#undef ADD_SCALAR_STR
#undef ADD_SCALAR_BITS
#undef ADD_SCALAR_B

	return any;
}

bool ApplyFieldUpdate(const mq::proto::charinfo::FieldUpdate& update,
	mq::proto::charinfo::CharinfoPublish* peer)
{
	using Id = mq::proto::charinfo::CharinfoFieldId;
	switch (update.field_id()) {
	case Id::FIELD_sender: if (update.has_str()) peer->set_sender(update.str()); break;
	case Id::FIELD_name: if (update.has_str()) peer->set_name(update.str()); break;
	case Id::FIELD_id: if (update.has_i32()) peer->set_id(update.i32()); break;
	case Id::FIELD_level: if (update.has_i32()) peer->set_level(update.i32()); break;
	case Id::FIELD_class_info: if (update.has_class_info()) *peer->mutable_class_info() = update.class_info(); break;
	case Id::FIELD_pct_hps: if (update.has_i32()) peer->set_pct_hps(update.i32()); break;
	case Id::FIELD_pct_mana: if (update.has_i32()) peer->set_pct_mana(update.i32()); break;
	case Id::FIELD_target: if (update.has_target()) *peer->mutable_target() = update.target(); break;
	case Id::FIELD_target_hp: if (update.has_i32()) peer->set_target_hp(update.i32()); break;
	case Id::FIELD_zone: if (update.has_zone()) *peer->mutable_zone() = update.zone(); break;
	case Id::FIELD_buff_spells:
		if (update.has_spell_list()) {
			peer->clear_buff_spells();
			for (int i = 0; i < update.spell_list().spell_size(); i++)
				*peer->add_buff_spells() = update.spell_list().spell(i);
		}
		break;
	case Id::FIELD_buff_durations:
		if (update.has_int32_list()) {
			peer->clear_buff_durations();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->add_buff_durations(update.int32_list().value(i));
		}
		break;
	case Id::FIELD_short_buff_spells:
		if (update.has_spell_list()) {
			peer->clear_short_buff_spells();
			for (int i = 0; i < update.spell_list().spell_size(); i++)
				*peer->add_short_buff_spells() = update.spell_list().spell(i);
		}
		break;
	case Id::FIELD_short_buff_durations:
		if (update.has_int32_list()) {
			peer->clear_short_buff_durations();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->add_short_buff_durations(update.int32_list().value(i));
		}
		break;
	case Id::FIELD_pet_buff_spells:
		if (update.has_spell_list()) {
			peer->clear_pet_buff_spells();
			for (int i = 0; i < update.spell_list().spell_size(); i++)
				*peer->add_pet_buff_spells() = update.spell_list().spell(i);
		}
		break;
	case Id::FIELD_pet_buff_durations:
		if (update.has_int32_list()) {
			peer->clear_pet_buff_durations();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->add_pet_buff_durations(update.int32_list().value(i));
		}
		break;
	case Id::FIELD_free_buff_slots: if (update.has_i32()) peer->set_free_buff_slots(update.i32()); break;
	case Id::FIELD_detrimentals: if (update.has_i32()) peer->set_detrimentals(update.i32()); break;
	case Id::FIELD_count_poison: if (update.has_i32()) peer->set_count_poison(update.i32()); break;
	case Id::FIELD_count_disease: if (update.has_i32()) peer->set_count_disease(update.i32()); break;
	case Id::FIELD_count_curse: if (update.has_i32()) peer->set_count_curse(update.i32()); break;
	case Id::FIELD_count_corruption: if (update.has_i32()) peer->set_count_corruption(update.i32()); break;
	case Id::FIELD_pet_hp: if (update.has_i32()) peer->set_pet_hp(update.i32()); break;
	case Id::FIELD_max_endurance: if (update.has_i32()) peer->set_max_endurance(update.i32()); break;
	case Id::FIELD_current_hp: if (update.has_i32()) peer->set_current_hp(update.i32()); break;
	case Id::FIELD_max_hp: if (update.has_i32()) peer->set_max_hp(update.i32()); break;
	case Id::FIELD_current_mana: if (update.has_i32()) peer->set_current_mana(update.i32()); break;
	case Id::FIELD_max_mana: if (update.has_i32()) peer->set_max_mana(update.i32()); break;
	case Id::FIELD_current_endurance: if (update.has_i32()) peer->set_current_endurance(update.i32()); break;
	case Id::FIELD_pct_endurance: if (update.has_i32()) peer->set_pct_endurance(update.i32()); break;
	case Id::FIELD_pet_id: if (update.has_i32()) peer->set_pet_id(update.i32()); break;
	case Id::FIELD_pet_affinity: if (update.has_b()) peer->set_pet_affinity(update.b()); break;
	case Id::FIELD_no_cure: if (update.has_i64()) peer->set_no_cure(update.i64()); break;
	case Id::FIELD_life_drain: if (update.has_i64()) peer->set_life_drain(update.i64()); break;
	case Id::FIELD_mana_drain: if (update.has_i64()) peer->set_mana_drain(update.i64()); break;
	case Id::FIELD_endu_drain: if (update.has_i64()) peer->set_endu_drain(update.i64()); break;
	case Id::FIELD_state_bits: if (update.has_bits()) peer->set_state_bits(update.bits()); break;
	case Id::FIELD_detr_state_bits: if (update.has_bits()) peer->set_detr_state_bits(update.bits()); break;
	case Id::FIELD_bene_state_bits: if (update.has_bits()) peer->set_bene_state_bits(update.bits()); break;
	case Id::FIELD_casting_spell_id: if (update.has_i32()) peer->set_casting_spell_id(update.i32()); break;
	case Id::FIELD_combat_state: if (update.has_i32()) peer->set_combat_state(update.i32()); break;
	case Id::FIELD_gem:
		if (update.has_int32_list()) {
			peer->clear_gem();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->add_gem(update.int32_list().value(i));
		}
		break;
	case Id::FIELD_version: if (update.has_f()) peer->set_version(update.f()); break;
	case Id::FIELD_experience: if (update.has_experience()) *peer->mutable_experience() = update.experience(); break;
	case Id::FIELD_make_camp: if (update.has_make_camp()) *peer->mutable_make_camp() = update.make_camp(); break;
	case Id::FIELD_macro: if (update.has_macro()) *peer->mutable_macro() = update.macro(); break;
	case Id::FIELD_free_inventory:
		if (update.has_int32_list()) {
			peer->clear_free_inventory();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->add_free_inventory(update.int32_list().value(i));
		}
		break;
	default: return false;
	}
	return true;
}

bool ApplyFieldUpdate(const mq::proto::charinfo::FieldUpdate& update, CharinfoPeer* peer)
{
	using Id = mq::proto::charinfo::CharinfoFieldId;
	switch (update.field_id()) {
	case Id::FIELD_sender: if (update.has_str()) peer->name = update.str(); break;
	case Id::FIELD_name: if (update.has_str()) peer->name = update.str(); break;
	case Id::FIELD_id: if (update.has_i32()) peer->id = update.i32(); break;
	case Id::FIELD_level: if (update.has_i32()) peer->level = update.i32(); break;
	case Id::FIELD_class_info:
		if (update.has_class_info()) {
			peer->class_info.name = update.class_info().name();
			peer->class_info.short_name = update.class_info().short_name();
			peer->class_info.id = update.class_info().id();
		}
		break;
	case Id::FIELD_pct_hps: if (update.has_i32()) peer->pct_hps = update.i32(); break;
	case Id::FIELD_pct_mana: if (update.has_i32()) peer->pct_mana = update.i32(); break;
	case Id::FIELD_target:
		if (update.has_target()) {
			peer->target.name = update.target().name();
			peer->target.id = update.target().id();
		}
		break;
	case Id::FIELD_target_hp: if (update.has_i32()) peer->target_hp = update.i32(); break;
	case Id::FIELD_zone:
		if (update.has_zone()) {
			peer->zone.name = update.zone().name();
			peer->zone.short_name = update.zone().short_name();
			peer->zone.id = update.zone().id();
			peer->zone.instance_id = update.zone().instance_id();
			peer->zone.x = update.zone().x();
			peer->zone.y = update.zone().y();
			peer->zone.z = update.zone().z();
			peer->zone.heading = update.zone().heading();
			peer->zone.distance = -1.0;
			SetZoneDistanceFromLocal(peer->zone);
		}
		break;
	case Id::FIELD_buff_spells:
		if (update.has_spell_list()) {
			peer->buff.clear();
			const int n = update.spell_list().spell_size();
			for (int i = 0; i < n; i++) {
				const auto& sp = update.spell_list().spell(i);
				PeerBuffEntry e;
				e.spell.name = sp.name();
				e.spell.id = sp.id();
				e.spell.category = sp.category();
				e.spell.level = sp.level();
				e.duration = -1;
				peer->buff.push_back(std::move(e));
			}
		}
		break;
	case Id::FIELD_buff_durations:
		if (update.has_int32_list()) {
			const auto& list = update.int32_list();
			for (int i = 0; i < list.value_size() && i < static_cast<int>(peer->buff.size()); i++)
				peer->buff[static_cast<size_t>(i)].duration = list.value(i);
		}
		break;
	case Id::FIELD_short_buff_spells:
		if (update.has_spell_list()) {
			peer->short_buff.clear();
			const int n = update.spell_list().spell_size();
			for (int i = 0; i < n; i++) {
				const auto& sp = update.spell_list().spell(i);
				PeerBuffEntry e;
				e.spell.name = sp.name();
				e.spell.id = sp.id();
				e.spell.category = sp.category();
				e.spell.level = sp.level();
				e.duration = -1;
				peer->short_buff.push_back(std::move(e));
			}
		}
		break;
	case Id::FIELD_short_buff_durations:
		if (update.has_int32_list()) {
			const auto& list = update.int32_list();
			for (int i = 0; i < list.value_size() && i < static_cast<int>(peer->short_buff.size()); i++)
				peer->short_buff[static_cast<size_t>(i)].duration = list.value(i);
		}
		break;
	case Id::FIELD_pet_buff_spells:
		if (update.has_spell_list()) {
			peer->pet_buff.clear();
			const int n = update.spell_list().spell_size();
			for (int i = 0; i < n; i++) {
				const auto& sp = update.spell_list().spell(i);
				PeerBuffEntry e;
				e.spell.name = sp.name();
				e.spell.id = sp.id();
				e.spell.category = sp.category();
				e.spell.level = sp.level();
				e.duration = -1;
				peer->pet_buff.push_back(std::move(e));
			}
		}
		break;
	case Id::FIELD_pet_buff_durations:
		if (update.has_int32_list()) {
			const auto& list = update.int32_list();
			for (int i = 0; i < list.value_size() && i < static_cast<int>(peer->pet_buff.size()); i++)
				peer->pet_buff[static_cast<size_t>(i)].duration = list.value(i);
		}
		break;
	case Id::FIELD_free_buff_slots: if (update.has_i32()) peer->free_buff_slots = update.i32(); break;
	case Id::FIELD_detrimentals: if (update.has_i32()) peer->detrimentals = update.i32(); break;
	case Id::FIELD_count_poison: if (update.has_i32()) peer->count_poison = update.i32(); break;
	case Id::FIELD_count_disease: if (update.has_i32()) peer->count_disease = update.i32(); break;
	case Id::FIELD_count_curse: if (update.has_i32()) peer->count_curse = update.i32(); break;
	case Id::FIELD_count_corruption: if (update.has_i32()) peer->count_corruption = update.i32(); break;
	case Id::FIELD_pet_hp: if (update.has_i32()) peer->pet_hp = update.i32(); break;
	case Id::FIELD_max_endurance: if (update.has_i32()) peer->max_endurance = update.i32(); break;
	case Id::FIELD_current_hp: if (update.has_i32()) peer->current_hp = update.i32(); break;
	case Id::FIELD_max_hp: if (update.has_i32()) peer->max_hp = update.i32(); break;
	case Id::FIELD_current_mana: if (update.has_i32()) peer->current_mana = update.i32(); break;
	case Id::FIELD_max_mana: if (update.has_i32()) peer->max_mana = update.i32(); break;
	case Id::FIELD_current_endurance: if (update.has_i32()) peer->current_endurance = update.i32(); break;
	case Id::FIELD_pct_endurance: if (update.has_i32()) peer->pct_endurance = update.i32(); break;
	case Id::FIELD_pet_id: if (update.has_i32()) peer->pet_id = update.i32(); break;
	case Id::FIELD_pet_affinity: if (update.has_b()) peer->pet_affinity = update.b(); break;
	case Id::FIELD_no_cure: if (update.has_i64()) peer->no_cure = update.i64(); break;
	case Id::FIELD_life_drain: if (update.has_i64()) peer->life_drain = update.i64(); break;
	case Id::FIELD_mana_drain: if (update.has_i64()) peer->mana_drain = update.i64(); break;
	case Id::FIELD_endu_drain: if (update.has_i64()) peer->endu_drain = update.i64(); break;
	case Id::FIELD_state_bits:
		if (update.has_bits()) {
			peer->state_bits = update.bits();
			peer->state = StateBitsToStrings(update.bits());
		}
		break;
	case Id::FIELD_detr_state_bits: if (update.has_bits()) { peer->detr_state_bits = update.bits(); peer->buff_state = BuffStateBitsToStrings(peer->detr_state_bits, peer->bene_state_bits); } break;
	case Id::FIELD_bene_state_bits: if (update.has_bits()) { peer->bene_state_bits = update.bits(); peer->buff_state = BuffStateBitsToStrings(peer->detr_state_bits, peer->bene_state_bits); } break;
	case Id::FIELD_casting_spell_id: if (update.has_i32()) peer->casting_spell_id = update.i32(); break;
	case Id::FIELD_combat_state: if (update.has_i32()) peer->combat_state = update.i32(); break;
	case Id::FIELD_gem:
		if (update.has_int32_list()) {
			peer->gems.clear();
			PcProfile* profile = GetPcProfile();
			for (int i = 0; i < update.int32_list().value_size(); i++) {
				int32_t spellId = update.int32_list().value(i);
				PeerGemEntry ge;
				ge.id = spellId;
				if (EQ_Spell* spell = GetSpellByID(spellId)) {
					ge.name = spell->Name[0] ? spell->Name : "";
					ge.category = spell->Category;
					ge.level = profile ? static_cast<int32_t>(spell->GetSpellLevelNeeded(profile->Class)) : 0;
				} else {
					ge.name = "";
					ge.category = 0;
					ge.level = 0;
				}
				peer->gems.push_back(std::move(ge));
			}
		}
		break;
	case Id::FIELD_version: if (update.has_f()) peer->version = update.f(); break;
	case Id::FIELD_experience:
		if (update.has_experience()) {
			PeerExperienceInfo ex;
			ex.pct_exp = update.experience().pct_exp();
			ex.pct_aa_exp = update.experience().pct_aa_exp();
			ex.pct_group_leader_exp = update.experience().pct_group_leader_exp();
			ex.total_aa = update.experience().total_aa();
			ex.aa_spent = update.experience().aa_spent();
			ex.aa_unused = update.experience().aa_unused();
			ex.aa_assigned = update.experience().aa_assigned();
			peer->has_experience = true;
			peer->experience = ex;
		}
		break;
	case Id::FIELD_make_camp:
		if (update.has_make_camp()) {
			PeerMakeCampInfo mc;
			mc.status = update.make_camp().status();
			mc.x = update.make_camp().x();
			mc.y = update.make_camp().y();
			mc.radius = update.make_camp().radius();
			mc.distance = update.make_camp().distance();
			peer->has_make_camp = true;
			peer->make_camp = mc;
		}
		break;
	case Id::FIELD_macro:
		if (update.has_macro()) {
			PeerMacroInfo mac;
			mac.macro_state = update.macro().macro_state();
			mac.macro_name = update.macro().macro_name();
			peer->has_macro = true;
			peer->macro = mac;
		}
		break;
	case Id::FIELD_free_inventory:
		if (update.has_int32_list()) {
			peer->free_inventory.clear();
			for (int i = 0; i < update.int32_list().value_size(); i++)
				peer->free_inventory.push_back(update.int32_list().value(i));
		}
		break;
	default: return false;
	}
	return true;
}

bool StacksForPeer(const CharinfoPeer& peer, const char* spellNameOrId)
{
	if (!spellNameOrId || !spellNameOrId[0])
		return false;
	EQ_Spell* testSpell = GetSpellByName(spellNameOrId);
	if (!testSpell)
		return false;

	for (const auto& entry : peer.buff) {
		if (entry.spell.id <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(entry.spell.id);
		if (!buffSpell) continue;
		if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
			return false;
	}
	for (const auto& entry : peer.short_buff) {
		if (entry.spell.id <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(entry.spell.id);
		if (!buffSpell) continue;
		if (!IsBardSong(buffSpell) && !(IsSPAEffect(testSpell, SPA_CHANGE_FORM) && !testSpell->DurationWindow)) {
			if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
				return false;
		}
	}
	return true;
}

bool StacksPetForPeer(const CharinfoPeer& peer, const char* spellNameOrId)
{
	if (!spellNameOrId || !spellNameOrId[0])
		return false;
	EQ_Spell* testSpell = GetSpellByName(spellNameOrId);
	if (!testSpell)
		return false;

	for (const auto& entry : peer.pet_buff) {
		if (entry.spell.id <= 0) continue;
		EQ_Spell* buffSpell = GetSpellByID(entry.spell.id);
		if (!buffSpell) continue;
		if (buffSpell == testSpell || !WillStackWith(testSpell, buffSpell))
			return false;
	}
	return true;
}

} // namespace charinfo
