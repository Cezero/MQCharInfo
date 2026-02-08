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

#include <sol/sol.hpp>
#include <algorithm>
#include <mutex>
#include <vector>

using namespace mq::proto::charinfo;

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
