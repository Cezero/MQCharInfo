/*
 * Lua module for MQCharinfo: require("plugin.charinfo")
 * Exposes GetInfo, GetPeers, GetPeerCnt. Peer table from GetInfo includes Stacks/StacksPet.
 * Peer data is bound as usertypes (CharinfoPeer) so Lua reads from C++ without table copies.
 *
 * IMPORTANT (do not change without testing require("plugin.charinfo") and the loader):
 * - CreateLuaModule must be exported as "CreateLuaModule" so MQ2Lua's GetPluginProc(..., "CreateLuaModule") finds it.
 * - PLUGIN_API (extern "C" __declspec(dllexport)) is used on purpose: it exports the unmangled name. VC++ warns
 *   C4190 (C linkage with C++ return type) but it works. Do not replace with __declspec(dllexport) alone or add
 *   a .def unless the export actually fails.
 */

#include "Charinfo.h"
#include "CharinfoPeer.h"
#include "mq/Plugin.h"

#include <eqlib/game/Spells.h>

#include <sol/sol.hpp>
#include <algorithm>
#include <memory>
#include <tuple>
#include <vector>

using namespace mq::proto::charinfo;

namespace {

// Helper for CharinfoPeer field properties: nil if invalidated, otherwise returns the field value.
template <typename MemberT>
auto MakePeerFieldProperty(MemberT charinfo::CharinfoPeer::* member)
{
	return sol::property([member](const charinfo::CharinfoPeer& peer, sol::this_state L) {
		if (peer.invalidated())
			return sol::make_object(L, sol::lua_nil);
		return sol::make_object(L, peer.*member);
	});
}

// Helper for CharinfoPeer container properties exposed as Lua arrays (1-based indices).
template <typename ContainerMember>
auto MakePeerTableProperty(ContainerMember charinfo::CharinfoPeer::* member)
{
	return sol::property([member](const charinfo::CharinfoPeer& peer, sol::this_state L) {
		if (peer.invalidated())
			return sol::make_object(L, sol::lua_nil);

		sol::state_view sv(L);
		sol::table arr = sv.create_table();

		const auto& container = peer.*member;
		for (size_t i = 0; i < container.size(); ++i)
			arr[i + 1] = container[i];

		return sol::make_object(L, arr);
	});
}

static void RegisterCharInfoUsertypes(sol::state_view L)
{
	// Nested types (no constructor; used as members of CharinfoPeer).
	L.new_usertype<charinfo::PeerSpellInfo>(
		"PeerSpellInfo", sol::no_constructor,
		"Name", &charinfo::PeerSpellInfo::name,
		"ID", &charinfo::PeerSpellInfo::id,
		"Category", &charinfo::PeerSpellInfo::category,
		"Level", &charinfo::PeerSpellInfo::level,
		sol::meta_function::equal_to, [](const charinfo::PeerSpellInfo& a, const charinfo::PeerSpellInfo& b) { return std::tie(a.name, a.id, a.category, a.level) == std::tie(b.name, b.id, b.category, b.level); },
		sol::meta_function::less_than, [](const charinfo::PeerSpellInfo& a, const charinfo::PeerSpellInfo& b) { return std::tie(a.name, a.id, a.category, a.level) < std::tie(b.name, b.id, b.category, b.level); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerSpellInfo& a, const charinfo::PeerSpellInfo& b) { return std::tie(a.name, a.id, a.category, a.level) <= std::tie(b.name, b.id, b.category, b.level); });

	L.new_usertype<charinfo::PeerBuffEntry>(
		"PeerBuffEntry", sol::no_constructor,
		"Spell", &charinfo::PeerBuffEntry::spell,
		"Duration", &charinfo::PeerBuffEntry::duration,
		sol::meta_function::equal_to, [](const charinfo::PeerBuffEntry& a, const charinfo::PeerBuffEntry& b) { return a.duration == b.duration && std::tie(a.spell.name, a.spell.id, a.spell.category, a.spell.level) == std::tie(b.spell.name, b.spell.id, b.spell.category, b.spell.level); },
		sol::meta_function::less_than, [](const charinfo::PeerBuffEntry& a, const charinfo::PeerBuffEntry& b) { return std::tie(a.duration, a.spell.name, a.spell.id) < std::tie(b.duration, b.spell.name, b.spell.id); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerBuffEntry& a, const charinfo::PeerBuffEntry& b) { return std::tie(a.duration, a.spell.name, a.spell.id) <= std::tie(b.duration, b.spell.name, b.spell.id); });

	L.new_usertype<charinfo::PeerClassInfo>(
		"PeerClassInfo", sol::no_constructor,
		"Name", &charinfo::PeerClassInfo::name,
		"ShortName", &charinfo::PeerClassInfo::short_name,
		"ID", &charinfo::PeerClassInfo::id,
		sol::meta_function::equal_to, [](const charinfo::PeerClassInfo& a, const charinfo::PeerClassInfo& b) { return std::tie(a.name, a.short_name, a.id) == std::tie(b.name, b.short_name, b.id); },
		sol::meta_function::less_than, [](const charinfo::PeerClassInfo& a, const charinfo::PeerClassInfo& b) { return std::tie(a.name, a.short_name, a.id) < std::tie(b.name, b.short_name, b.id); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerClassInfo& a, const charinfo::PeerClassInfo& b) { return std::tie(a.name, a.short_name, a.id) <= std::tie(b.name, b.short_name, b.id); });

	L.new_usertype<charinfo::PeerTargetInfo>(
		"PeerTargetInfo", sol::no_constructor,
		"Name", &charinfo::PeerTargetInfo::name,
		"ID", &charinfo::PeerTargetInfo::id,
		sol::meta_function::equal_to, [](const charinfo::PeerTargetInfo& a, const charinfo::PeerTargetInfo& b) { return std::tie(a.name, a.id) == std::tie(b.name, b.id); },
		sol::meta_function::less_than, [](const charinfo::PeerTargetInfo& a, const charinfo::PeerTargetInfo& b) { return std::tie(a.name, a.id) < std::tie(b.name, b.id); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerTargetInfo& a, const charinfo::PeerTargetInfo& b) { return std::tie(a.name, a.id) <= std::tie(b.name, b.id); });

	L.new_usertype<charinfo::PeerZoneInfo>(
		"PeerZoneInfo", sol::no_constructor,
		"Name", &charinfo::PeerZoneInfo::name,
		"ShortName", &charinfo::PeerZoneInfo::short_name,
		"ID", &charinfo::PeerZoneInfo::id,
		"InstanceID", &charinfo::PeerZoneInfo::instance_id,
		"X", &charinfo::PeerZoneInfo::x,
		"Y", &charinfo::PeerZoneInfo::y,
		"Z", &charinfo::PeerZoneInfo::z,
		"Heading", &charinfo::PeerZoneInfo::heading,
		"Distance", sol::property([](const charinfo::PeerZoneInfo &z, sol::this_state L) {
			if (z.distance < 0)
				return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, z.distance); }),
		sol::meta_function::equal_to, [](const charinfo::PeerZoneInfo& a, const charinfo::PeerZoneInfo& b) { return std::tie(a.name, a.short_name, a.id, a.instance_id, a.x, a.y, a.z, a.heading, a.distance) == std::tie(b.name, b.short_name, b.id, b.instance_id, b.x, b.y, b.z, b.heading, b.distance); },
		sol::meta_function::less_than, [](const charinfo::PeerZoneInfo& a, const charinfo::PeerZoneInfo& b) { return std::tie(a.name, a.short_name, a.id, a.instance_id, a.x, a.y, a.z, a.heading, a.distance) < std::tie(b.name, b.short_name, b.id, b.instance_id, b.x, b.y, b.z, b.heading, b.distance); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerZoneInfo& a, const charinfo::PeerZoneInfo& b) { return std::tie(a.name, a.short_name, a.id, a.instance_id, a.x, a.y, a.z, a.heading, a.distance) <= std::tie(b.name, b.short_name, b.id, b.instance_id, b.x, b.y, b.z, b.heading, b.distance); });

	L.new_usertype<charinfo::PeerExperienceInfo>(
		"PeerExperienceInfo", sol::no_constructor,
		"PctExp", &charinfo::PeerExperienceInfo::pct_exp,
		"PctAAExp", &charinfo::PeerExperienceInfo::pct_aa_exp,
		"PctGroupLeaderExp", &charinfo::PeerExperienceInfo::pct_group_leader_exp,
		"TotalAA", &charinfo::PeerExperienceInfo::total_aa,
		"AASpent", &charinfo::PeerExperienceInfo::aa_spent,
		"AAUnused", &charinfo::PeerExperienceInfo::aa_unused,
		"AAAssigned", &charinfo::PeerExperienceInfo::aa_assigned,
		sol::meta_function::equal_to, [](const charinfo::PeerExperienceInfo& a, const charinfo::PeerExperienceInfo& b) { return std::tie(a.pct_exp, a.pct_aa_exp, a.pct_group_leader_exp, a.total_aa, a.aa_spent, a.aa_unused, a.aa_assigned) == std::tie(b.pct_exp, b.pct_aa_exp, b.pct_group_leader_exp, b.total_aa, b.aa_spent, b.aa_unused, b.aa_assigned); },
		sol::meta_function::less_than, [](const charinfo::PeerExperienceInfo& a, const charinfo::PeerExperienceInfo& b) { return std::tie(a.pct_exp, a.pct_aa_exp, a.pct_group_leader_exp, a.total_aa, a.aa_spent, a.aa_unused, a.aa_assigned) < std::tie(b.pct_exp, b.pct_aa_exp, b.pct_group_leader_exp, b.total_aa, b.aa_spent, b.aa_unused, b.aa_assigned); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerExperienceInfo& a, const charinfo::PeerExperienceInfo& b) { return std::tie(a.pct_exp, a.pct_aa_exp, a.pct_group_leader_exp, a.total_aa, a.aa_spent, a.aa_unused, a.aa_assigned) <= std::tie(b.pct_exp, b.pct_aa_exp, b.pct_group_leader_exp, b.total_aa, b.aa_spent, b.aa_unused, b.aa_assigned); });

	L.new_usertype<charinfo::PeerMakeCampInfo>(
		"PeerMakeCampInfo", sol::no_constructor,
		"Status", &charinfo::PeerMakeCampInfo::status,
		"X", &charinfo::PeerMakeCampInfo::x,
		"Y", &charinfo::PeerMakeCampInfo::y,
		"Radius", &charinfo::PeerMakeCampInfo::radius,
		"Distance", &charinfo::PeerMakeCampInfo::distance,
		sol::meta_function::equal_to, [](const charinfo::PeerMakeCampInfo& a, const charinfo::PeerMakeCampInfo& b) { return std::tie(a.status, a.x, a.y, a.radius, a.distance) == std::tie(b.status, b.x, b.y, b.radius, b.distance); },
		sol::meta_function::less_than, [](const charinfo::PeerMakeCampInfo& a, const charinfo::PeerMakeCampInfo& b) { return std::tie(a.status, a.x, a.y, a.radius, a.distance) < std::tie(b.status, b.x, b.y, b.radius, b.distance); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerMakeCampInfo& a, const charinfo::PeerMakeCampInfo& b) { return std::tie(a.status, a.x, a.y, a.radius, a.distance) <= std::tie(b.status, b.x, b.y, b.radius, b.distance); });

	L.new_usertype<charinfo::PeerMacroInfo>(
		"PeerMacroInfo", sol::no_constructor,
		"MacroState", &charinfo::PeerMacroInfo::macro_state,
		"MacroName", &charinfo::PeerMacroInfo::macro_name,
		sol::meta_function::equal_to, [](const charinfo::PeerMacroInfo& a, const charinfo::PeerMacroInfo& b) { return std::tie(a.macro_state, a.macro_name) == std::tie(b.macro_state, b.macro_name); },
		sol::meta_function::less_than, [](const charinfo::PeerMacroInfo& a, const charinfo::PeerMacroInfo& b) { return std::tie(a.macro_state, a.macro_name) < std::tie(b.macro_state, b.macro_name); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerMacroInfo& a, const charinfo::PeerMacroInfo& b) { return std::tie(a.macro_state, a.macro_name) <= std::tie(b.macro_state, b.macro_name); });

	L.new_usertype<charinfo::PeerGemEntry>(
		"PeerGemEntry", sol::no_constructor,
		"ID", &charinfo::PeerGemEntry::id,
		"Name", &charinfo::PeerGemEntry::name,
		"Category", &charinfo::PeerGemEntry::category,
		"Level", &charinfo::PeerGemEntry::level,
		sol::meta_function::equal_to, [](const charinfo::PeerGemEntry& a, const charinfo::PeerGemEntry& b) { return std::tie(a.id, a.name, a.category, a.level) == std::tie(b.id, b.name, b.category, b.level); },
		sol::meta_function::less_than, [](const charinfo::PeerGemEntry& a, const charinfo::PeerGemEntry& b) { return std::tie(a.id, a.name, a.category, a.level) < std::tie(b.id, b.name, b.category, b.level); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::PeerGemEntry& a, const charinfo::PeerGemEntry& b) { return std::tie(a.id, a.name, a.category, a.level) <= std::tie(b.id, b.name, b.category, b.level); });

	// CharinfoPeer: usertype bound directly to the underlying peer object.
	L.new_usertype<charinfo::CharinfoPeer>(
		"CharinfoPeer", sol::no_constructor,
		"Name", MakePeerFieldProperty(&charinfo::CharinfoPeer::name),
		"ID", MakePeerFieldProperty(&charinfo::CharinfoPeer::id),
		"Level", MakePeerFieldProperty(&charinfo::CharinfoPeer::level),
		"PctHPs", MakePeerFieldProperty(&charinfo::CharinfoPeer::pct_hps),
		"PctMana", MakePeerFieldProperty(&charinfo::CharinfoPeer::pct_mana),
		"TargetHP", MakePeerFieldProperty(&charinfo::CharinfoPeer::target_hp),
		"FreeBuffSlots", MakePeerFieldProperty(&charinfo::CharinfoPeer::free_buff_slots),
		"Detrimentals", MakePeerFieldProperty(&charinfo::CharinfoPeer::detrimentals),
		"CountPoison", MakePeerFieldProperty(&charinfo::CharinfoPeer::count_poison),
		"CountDisease", MakePeerFieldProperty(&charinfo::CharinfoPeer::count_disease),
		"CountCurse", MakePeerFieldProperty(&charinfo::CharinfoPeer::count_curse),
		"CountCorruption", MakePeerFieldProperty(&charinfo::CharinfoPeer::count_corruption),
		"PetHP", MakePeerFieldProperty(&charinfo::CharinfoPeer::pet_hp),
		"MaxEndurance", MakePeerFieldProperty(&charinfo::CharinfoPeer::max_endurance),
		"CurrentHP", MakePeerFieldProperty(&charinfo::CharinfoPeer::current_hp),
		"MaxHP", MakePeerFieldProperty(&charinfo::CharinfoPeer::max_hp),
		"CurrentMana", MakePeerFieldProperty(&charinfo::CharinfoPeer::current_mana),
		"MaxMana", MakePeerFieldProperty(&charinfo::CharinfoPeer::max_mana),
		"CurrentEndurance", MakePeerFieldProperty(&charinfo::CharinfoPeer::current_endurance),
		"PctEndurance", MakePeerFieldProperty(&charinfo::CharinfoPeer::pct_endurance),
		"PetID", MakePeerFieldProperty(&charinfo::CharinfoPeer::pet_id),
		"PetAffinity", MakePeerFieldProperty(&charinfo::CharinfoPeer::pet_affinity),
		"NoCure", MakePeerFieldProperty(&charinfo::CharinfoPeer::no_cure),
		"LifeDrain", MakePeerFieldProperty(&charinfo::CharinfoPeer::life_drain),
		"ManaDrain", MakePeerFieldProperty(&charinfo::CharinfoPeer::mana_drain),
		"EnduDrain", MakePeerFieldProperty(&charinfo::CharinfoPeer::endu_drain),
		"Version", MakePeerFieldProperty(&charinfo::CharinfoPeer::version),
		"CombatState", MakePeerFieldProperty(&charinfo::CharinfoPeer::combat_state),
		"CastingSpellID", MakePeerFieldProperty(&charinfo::CharinfoPeer::casting_spell_id),
		"Class", MakePeerFieldProperty(&charinfo::CharinfoPeer::class_info),
		"Target", MakePeerFieldProperty(&charinfo::CharinfoPeer::target),
		"Zone", MakePeerFieldProperty(&charinfo::CharinfoPeer::zone),
		"State", MakePeerTableProperty(&charinfo::CharinfoPeer::state),
		"BuffState", MakePeerTableProperty(&charinfo::CharinfoPeer::buff_state),
		"Buff", MakePeerTableProperty(&charinfo::CharinfoPeer::buff),
		"ShortBuff", MakePeerTableProperty(&charinfo::CharinfoPeer::short_buff),
		"PetBuff", MakePeerTableProperty(&charinfo::CharinfoPeer::pet_buff),
		"Gems", MakePeerTableProperty(&charinfo::CharinfoPeer::gems),
		"FreeInventory", MakePeerTableProperty(&charinfo::CharinfoPeer::free_inventory),
		"Experience", sol::property([](const charinfo::CharinfoPeer &peer, sol::this_state L) {
			if (peer.invalidated() || !peer.has_experience) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, peer.experience); }),
		"MakeCamp", sol::property([](const charinfo::CharinfoPeer &peer, sol::this_state L) {
			if (peer.invalidated() || !peer.has_make_camp) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, peer.make_camp); }),
		"Macro", sol::property([](const charinfo::CharinfoPeer &peer, sol::this_state L) {
			if (peer.invalidated() || !peer.has_macro) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, peer.macro); }),
		"Stacks", sol::overload(
			[](const charinfo::CharinfoPeer &peer, const std::string &spell) {
				return !peer.invalidated() && charinfo::StacksForPeer(peer, spell.c_str()); },
			[](const charinfo::CharinfoPeer &peer, const sol::object &spellArg) -> bool {
				if (peer.invalidated()) return false;
				sol::type t = spellArg.get_type();
				if (t == sol::type::string) return charinfo::StacksForPeer(peer, spellArg.as<std::string>().c_str());
				if (t == sol::type::number) return charinfo::StacksForPeer(peer, std::to_string(static_cast<int>(spellArg.as<double>())).c_str());
				return false; }),
		"StacksPet", sol::overload(
			[](const charinfo::CharinfoPeer &peer, const std::string &spell) {
				return !peer.invalidated() && charinfo::StacksPetForPeer(peer, spell.c_str()); },
			[](const charinfo::CharinfoPeer &peer, const sol::object &spellArg) -> bool {
				if (peer.invalidated()) return false;
				sol::type t = spellArg.get_type();
				if (t == sol::type::string) return charinfo::StacksPetForPeer(peer, spellArg.as<std::string>().c_str());
				if (t == sol::type::number) return charinfo::StacksPetForPeer(peer, std::to_string(static_cast<int>(spellArg.as<double>())).c_str());
				return false; }),
		sol::meta_function::to_string, [](const charinfo::CharinfoPeer &peer) {
			if (peer.invalidated()) return std::string("(invalidated peer)");
			return std::string("CharinfoPeer(") + peer.name + ")"; },
		sol::meta_function::equal_to, [](const charinfo::CharinfoPeer& a, const charinfo::CharinfoPeer& b) { return &a == &b; },
		sol::meta_function::less_than, [](const charinfo::CharinfoPeer& a, const charinfo::CharinfoPeer& b) {
			return std::less<const charinfo::CharinfoPeer*>()(&a, &b); },
		sol::meta_function::less_than_or_equal_to, [](const charinfo::CharinfoPeer& a, const charinfo::CharinfoPeer& b) {
			return !std::less<const charinfo::CharinfoPeer*>()(&b, &a); });
}

} // namespace

PLUGIN_API sol::object CreateLuaModule(sol::this_state s)
{
	sol::state_view L(s);
	RegisterCharInfoUsertypes(L);

	sol::table module = L.create_table();

	module["GetInfo"] = [](sol::this_state L, const std::string &name) -> sol::object
	{
		auto it = charinfo::GetPeers().find(name);
		if (it == charinfo::GetPeers().end())
			return sol::lua_nil;
		return sol::make_object(L, it->second);
	};

	module["GetPeers"] = [](sol::this_state L)
	{
		sol::state_view sv(L);
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
		return static_cast<int>(charinfo::GetPeers().size());
	};

	// Callable: charinfo(name) == GetInfo(name).
	module[sol::metatable_key] = L.create_table_with(
		sol::meta_function::call, [](sol::this_state L, sol::variadic_args args) -> sol::object
		{
			if (args.size() < 2)
				return sol::lua_nil;
			sol::table self = args.get<sol::table>(0);
			std::string name = args.get<std::string>(1);
			return self["GetInfo"].get<sol::function>()(name); });

	return sol::make_object(L, module);
}
