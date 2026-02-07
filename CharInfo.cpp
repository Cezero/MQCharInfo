/*
 * MQCharInfo - Peer state, payload build, and Stacks/StacksPet (NetBots-style).
 */

#include "CharInfo.h"
#include "mq/Plugin.h"

#include <eqlib/game/Constants.h>
#include <eqlib/game/EQData.h>
#include <eqlib/game/Spells.h>

namespace charinfo {

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
	si->set_level(spell->Level);

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
				si->set_level(spell->Level);
			}
		}
		PlayerClient* pet = GetSpawnByID(pLocalPlayer->PetID);
		if (pet && pet->HPMax > 0)
			out->set_pet_hp(static_cast<int>(pet->HPCurrent * 100 / pet->HPMax));
	}

	if (pLocalPlayer->EnduranceMax >= 0)
		out->set_max_endurance(static_cast<int>(pLocalPlayer->EnduranceMax));

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
