/*
 * Lua module for MQCharInfo: require("plugin.charinfo")
 * Exposes GetInfo, GetPeers, GetPeerCnt, GetPeer (with Stacks/StacksPet).
 *
 * IMPORTANT (do not change without testing require("plugin.charinfo") and the loader):
 * - CreateLuaModule must be exported as "CreateLuaModule" so MQ2Lua's GetPluginProc(..., "CreateLuaModule") finds it.
 * - PLUGIN_API (extern "C" __declspec(dllexport)) is used on purpose: it exports the unmangled name. VC++ warns
 *   C4190 (C linkage with C++ return type) but it works. Do not replace with __declspec(dllexport) alone or add
 *   a .def unless the export actually fails.
 */

#include "CharInfo.h"
#include "charinfo.pb.h"
#include "mq/Plugin.h"

#include <eqlib/game/Spells.h>

#include <sol/sol.hpp>
#include <algorithm>
#include <mutex>
#include <vector>

using namespace mq::proto::charinfo;

namespace {

// Push charinfo::StateBitsToStrings / BuffStateBitsToStrings into a Lua table (1-based array).
static sol::table StringsToLuaTable(sol::state_view L, const std::vector<std::string>& strs)
{
	sol::table arr = L.create_table();
	for (size_t i = 0; i < strs.size(); i++)
		arr[i + 1] = strs[i];
	return arr;
}

} // namespace

static sol::table PeerToLuaTable(sol::state_view L, const CharInfoPublish &peer)
{
	sol::table t = L.create_table();

	t["Name"] = peer.name();
	t["ID"] = peer.id();
	t["Level"] = peer.level();
	t["PctHPs"] = peer.pct_hps();
	t["PctMana"] = peer.pct_mana();
	t["TargetHP"] = peer.target_hp();
	t["FreeBuffSlots"] = peer.free_buff_slots();
	t["Detrimentals"] = peer.detrimentals();
	t["CountPoison"] = peer.count_poison();
	t["CountDisease"] = peer.count_disease();
	t["CountCurse"] = peer.count_curse();
	t["CountCorruption"] = peer.count_corruption();
	t["PetHP"] = peer.pet_hp();
	t["MaxEndurance"] = peer.max_endurance();

	// New top-level vitals
	t["CurrentHP"] = peer.current_hp();
	t["MaxHP"] = peer.max_hp();
	t["CurrentMana"] = peer.current_mana();
	t["MaxMana"] = peer.max_mana();
	t["CurrentEndurance"] = peer.current_endurance();
	t["PctEndurance"] = peer.pct_endurance();
	t["PetID"] = peer.pet_id();
	t["PetAffinity"] = peer.pet_affinity();
	t["NoCure"] = static_cast<int64_t>(peer.no_cure());
	t["LifeDrain"] = static_cast<int64_t>(peer.life_drain());
	t["ManaDrain"] = static_cast<int64_t>(peer.mana_drain());
	t["EnduDrain"] = static_cast<int64_t>(peer.endu_drain());
	t["Version"] = peer.version();
	t["CombatState"] = peer.combat_state();
	t["CastingSpellID"] = peer.casting_spell_id();

	// State[] and BuffState[] from bits
	t["State"] = StringsToLuaTable(L, charinfo::StateBitsToStrings(peer.state_bits()));
	t["BuffState"] = StringsToLuaTable(L, charinfo::BuffStateBitsToStrings(peer.detr_state_bits(), peer.bene_state_bits()));

	sol::table classT = L.create_table();
	classT["Name"] = peer.class_info().name();
	classT["ShortName"] = peer.class_info().short_name();
	classT["ID"] = peer.class_info().id();
	t["Class"] = classT;

	sol::table targetT = L.create_table();
	targetT["Name"] = peer.target().name();
	targetT["ID"] = peer.target().id();
	t["Target"] = targetT;

	sol::table zoneT = L.create_table();
	zoneT["Name"] = peer.zone().name();
	zoneT["ShortName"] = peer.zone().short_name();
	zoneT["ID"] = peer.zone().id();
	zoneT["InstanceID"] = peer.zone().instance_id();
	zoneT["X"] = peer.zone().x();
	zoneT["Y"] = peer.zone().y();
	zoneT["Z"] = peer.zone().z();
	zoneT["Heading"] = peer.zone().heading();
	// Client-side Distance: only set when peer is in same zone as local player; nil otherwise.
	if (pLocalPlayer && pLocalPC && pLocalPC->zoneId == peer.zone().id() && static_cast<uint16_t>(pLocalPC->instance) == static_cast<uint16_t>(peer.zone().instance_id())) {
		float dist = Get3DDistance(pLocalPlayer->X, pLocalPlayer->Y, pLocalPlayer->Z, peer.zone().x(), peer.zone().y(), peer.zone().z());
		zoneT["Distance"] = static_cast<double>(dist);
	} else {
		zoneT["Distance"] = sol::lua_nil;
	}
	t["Zone"] = zoneT;

	auto addBuffList = [&](int size, const auto &access)
	{
		sol::table arr = L.create_table();
		for (int i = 0; i < size; i++)
		{
			const auto &e = access(i);
			sol::table entry = L.create_table();
			entry["Duration"] = e.duration();
			if (e.has_spell())
			{
				sol::table sp = L.create_table();
				sp["Name"] = e.spell().name();
				sp["ID"] = e.spell().id();
				sp["Category"] = e.spell().category();
				sp["Level"] = e.spell().level();
				entry["Spell"] = sp;
			}
			arr[i + 1] = entry;
		}
		return arr;
	};

	t["Buff"] = addBuffList(peer.buff_size(), [&](int i) -> const BuffEntry &
							{ return peer.buff(i); });
	t["ShortBuff"] = addBuffList(peer.short_buff_size(), [&](int i) -> const BuffEntry &
								 { return peer.short_buff(i); });
	t["PetBuff"] = addBuffList(peer.pet_buff_size(), [&](int i) -> const BuffEntry &
							   { return peer.pet_buff(i); });

	// Gems: ordered array of spell tables (Gems[1].ID, Gems[1].Name, ...)
	{
		sol::table gemsArr = L.create_table();
		PcProfile* profile = GetPcProfile();
		for (int i = 0; i < peer.gem_size(); i++) {
			int spellId = peer.gem(i);
			sol::table sp = L.create_table();
			sp["ID"] = spellId;
			if (EQ_Spell* spell = GetSpellByID(spellId)) {
				sp["Name"] = std::string(spell->Name[0] ? spell->Name : "");
				sp["Category"] = spell->Category;
				sp["Level"] = (profile ? static_cast<int>(spell->GetSpellLevelNeeded(profile->Class)) : 0);
			} else {
				sp["Name"] = "";
				sp["Category"] = 0;
				sp["Level"] = 0;
			}
			gemsArr[i + 1] = sp;
		}
		t["Gems"] = gemsArr;
	}

	// FreeInventory (ordered by size 0..4)
	{
		sol::table inv = L.create_table();
		for (int i = 0; i < peer.free_inventory_size(); i++)
			inv[i + 1] = peer.free_inventory(i);
		t["FreeInventory"] = inv;
	}

	// Experience subtable
	if (peer.has_experience()) {
		const auto& ex = peer.experience();
		sol::table expT = L.create_table();
		expT["PctExp"] = ex.pct_exp();
		expT["PctAAExp"] = ex.pct_aa_exp();
		expT["PctGroupLeaderExp"] = ex.pct_group_leader_exp();
		expT["TotalAA"] = ex.total_aa();
		expT["AASpent"] = ex.aa_spent();
		expT["AAUnused"] = ex.aa_unused();
		expT["AAAssigned"] = ex.aa_assigned();
		t["Experience"] = expT;
	}

	// MakeCamp subtable
	if (peer.has_make_camp()) {
		const auto& mc = peer.make_camp();
		sol::table campT = L.create_table();
		campT["Status"] = mc.status();
		campT["X"] = mc.x();
		campT["Y"] = mc.y();
		campT["Radius"] = mc.radius();
		campT["Distance"] = mc.distance();
		t["MakeCamp"] = campT;
	}

	// Macro subtable
	if (peer.has_macro()) {
		const auto& mac = peer.macro();
		sol::table macroT = L.create_table();
		macroT["MacroState"] = mac.macro_state();
		macroT["MacroName"] = mac.macro_name();
		t["Macro"] = macroT;
	}

	return t;
}

// Keep PLUGIN_API: exports unmangled "CreateLuaModule" for GetPluginProc. C4190 warning is acceptable.
PLUGIN_API sol::object CreateLuaModule(sol::this_state s)
{
	sol::state_view L(s);

	sol::table module = L.create_table();

	module["GetInfo"] = [](sol::this_state L, const std::string &name) -> sol::object
	{
		sol::state_view sv(L);
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		auto it = charinfo::GetPeers().find(name);
		if (it == charinfo::GetPeers().end())
			return sol::lua_nil;
		return sol::make_object(L, PeerToLuaTable(sv, it->second));
	};

	module["GetPeers"] = [](sol::this_state L)
	{
		sol::state_view sv(L);
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		std::vector<std::string> names;
		for (const auto &p : charinfo::GetPeers())
			names.push_back(p.first);
		std::sort(names.begin(), names.end());
		sol::table arr = sv.create_table();
		for (size_t i = 0; i < names.size(); i++)
			arr[i + 1] = names[i];
		return sol::make_object(L, arr);
	};

	module["GetPeerCnt"] = []()
	{
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		return static_cast<int>(charinfo::GetPeers().size());
	};

	module["GetPeer"] = [](sol::this_state L, const std::string &name) -> sol::object
	{
		sol::state_view sv(L);
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		auto it = charinfo::GetPeers().find(name);
		if (it == charinfo::GetPeers().end())
			return sol::lua_nil;
		const CharInfoPublish &peer = it->second;
		sol::table proxy = PeerToLuaTable(sv, peer);
		proxy["Stacks"] = [peer](sol::object spellObj)
		{
			std::string spell;
			if (spellObj.is<std::string>())
				spell = spellObj.as<std::string>();
			else if (spellObj.is<int>())
				spell = std::to_string(spellObj.as<int>());
			else
				return false;
			return charinfo::StacksForPeer(peer, spell.c_str());
		};
		proxy["StacksPet"] = [peer](sol::object spellObj)
		{
			std::string spell;
			if (spellObj.is<std::string>())
				spell = spellObj.as<std::string>();
			else if (spellObj.is<int>())
				spell = std::to_string(spellObj.as<int>());
			else
				return false;
			return charinfo::StacksPetForPeer(peer, spell.c_str());
		};
		return sol::make_object(L, proxy);
	};

	// Callable: Charinfo(name) == GetPeer(name). First arg is self (module), second is name.
	module[sol::metatable_key] = L.create_table_with(
		sol::meta_function::call, [](sol::this_state L, sol::variadic_args args) -> sol::object
		{
			if (args.size() < 2)
				return sol::lua_nil;
			sol::table self = args.get<sol::table>(0);
			std::string name = args.get<std::string>(1);
			return self["GetPeer"].get<sol::function>()(name); });

	return sol::make_object(L, module);
}
