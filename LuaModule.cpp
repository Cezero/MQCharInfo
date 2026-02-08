/*
 * Lua module for MQCharinfo: require("plugin.charinfo")
 * Exposes GetInfo, GetPeers, GetPeerCnt, GetPeer (with Stacks/StacksPet).
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

namespace
{

	using PeerPtr = std::shared_ptr<charinfo::CharinfoPeer>;

	// Wrapper so we never push shared_ptr to sol2 (avoids std::less<void> / push overload errors).
	struct CharinfoPeerRef {
		std::shared_ptr<charinfo::CharinfoPeer> ptr;
		CharinfoPeerRef() = default;
		explicit CharinfoPeerRef(std::shared_ptr<charinfo::CharinfoPeer> p) : ptr(std::move(p)) {}
		charinfo::CharinfoPeer* get() const { return ptr.get(); }
		bool operator==(const CharinfoPeerRef& o) const { return ptr.get() == o.ptr.get(); }
		bool operator<(const CharinfoPeerRef& o) const { return std::less<charinfo::CharinfoPeer*>()(ptr.get(), o.ptr.get()); }
		bool operator<=(const CharinfoPeerRef& o) const { return !(o < *this); }
	};

	// If peer is invalidated, return nil; otherwise run fn(peer) and return its result.
	template <typename F>
	sol::object GuardPeer(sol::this_state L, const CharinfoPeerRef& ref, F fn)
	{
		if (!ref.ptr || ref.ptr->invalidated())
			return sol::lua_nil;
		return fn(*ref.ptr);
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
			"Distance", sol::property([](const charinfo::PeerZoneInfo &z, sol::this_state L)
									  {
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

		// CharinfoPeer: usertype for wrapper (never push shared_ptr to sol2).
		L.new_usertype<CharinfoPeerRef>(
			"CharinfoPeer", sol::no_constructor,

			"Name", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													 { return sol::make_object(L, peer.name); }); }),
			"ID", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
												   { return sol::make_object(L, peer.id); }); }),
			"Level", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													  { return sol::make_object(L, peer.level); }); }),
			"PctHPs", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													   { return sol::make_object(L, peer.pct_hps); }); }),
			"PctMana", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														{ return sol::make_object(L, peer.pct_mana); }); }),
			"TargetHP", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														 { return sol::make_object(L, peer.target_hp); }); }),
			"FreeBuffSlots", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															  { return sol::make_object(L, peer.free_buff_slots); }); }),
			"Detrimentals", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															 { return sol::make_object(L, peer.detrimentals); }); }),
			"CountPoison", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															{ return sol::make_object(L, peer.count_poison); }); }),
			"CountDisease", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															 { return sol::make_object(L, peer.count_disease); }); }),
			"CountCurse", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														   { return sol::make_object(L, peer.count_curse); }); }),
			"CountCorruption", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
											 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
																{ return sol::make_object(L, peer.count_corruption); }); }),
			"PetHP", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													  { return sol::make_object(L, peer.pet_hp); }); }),
			"MaxEndurance", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															 { return sol::make_object(L, peer.max_endurance); }); }),
			"CurrentHP", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														  { return sol::make_object(L, peer.current_hp); }); }),
			"MaxHP", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													  { return sol::make_object(L, peer.max_hp); }); }),
			"CurrentMana", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															{ return sol::make_object(L, peer.current_mana); }); }),
			"MaxMana", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														{ return sol::make_object(L, peer.max_mana); }); }),
			"CurrentEndurance", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
											  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
																 { return sol::make_object(L, peer.current_endurance); }); }),
			"PctEndurance", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															 { return sol::make_object(L, peer.pct_endurance); }); }),
			"PetID", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													  { return sol::make_object(L, peer.pet_id); }); }),
			"PetAffinity", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															{ return sol::make_object(L, peer.pet_affinity); }); }),
			"NoCure", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													   { return sol::make_object(L, peer.no_cure); }); }),
			"LifeDrain", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														  { return sol::make_object(L, peer.life_drain); }); }),
			"ManaDrain", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														  { return sol::make_object(L, peer.mana_drain); }); }),
			"EnduDrain", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														  { return sol::make_object(L, peer.endu_drain); }); }),
			"Version", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
														{ return sol::make_object(L, peer.version); }); }),
			"CombatState", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										 { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															{ return sol::make_object(L, peer.combat_state); }); }),
			"CastingSpellID", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
											{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
															   { return sol::make_object(L, peer.casting_spell_id); }); }),

			"Class", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													  { return sol::make_object(L, peer.class_info); }); }),
			"Target", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									{ return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													   { return sol::make_object(L, peer.target); }); }),
			"Zone", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								  { return GuardPeer(L, ref, [L](const charinfo::CharinfoPeer &peer)
													 { return sol::make_object(L, peer.zone); }); }),

			"State", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table t = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->state.size(); i++)
				t[i + 1] = ref.ptr->state[i];
			return sol::make_object(L, t); }),
			"BuffState", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table t = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->buff_state.size(); i++)
				t[i + 1] = ref.ptr->buff_state[i];
			return sol::make_object(L, t); }),

			"Buff", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								  {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table arr = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->buff.size(); i++)
				arr[i + 1] = ref.ptr->buff[i];
			return sol::make_object(L, arr); }),
			"ShortBuff", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									   {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table arr = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->short_buff.size(); i++)
				arr[i + 1] = ref.ptr->short_buff[i];
			return sol::make_object(L, arr); }),
			"PetBuff", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									 {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table arr = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->pet_buff.size(); i++)
				arr[i + 1] = ref.ptr->pet_buff[i];
			return sol::make_object(L, arr); }),

			"Gems", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								  {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table arr = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->gems.size(); i++)
				arr[i + 1] = ref.ptr->gems[i];
			return sol::make_object(L, arr); }),
			"FreeInventory", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										   {
			if (!ref.ptr || ref.ptr->invalidated()) return sol::make_object(L, sol::lua_nil);
			sol::table arr = sol::state_view(L).create_table();
			for (size_t i = 0; i < ref.ptr->free_inventory.size(); i++)
				arr[i + 1] = ref.ptr->free_inventory[i];
			return sol::make_object(L, arr); }),

			"Experience", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
										{
			if (!ref.ptr || ref.ptr->invalidated() || !ref.ptr->has_experience) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, ref.ptr->experience); }),
			"MakeCamp", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
									  {
			if (!ref.ptr || ref.ptr->invalidated() || !ref.ptr->has_make_camp) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, ref.ptr->make_camp); }),
			"Macro", sol::property([](const CharinfoPeerRef &ref, sol::this_state L)
								   {
			if (!ref.ptr || ref.ptr->invalidated() || !ref.ptr->has_macro) return sol::make_object(L, sol::lua_nil);
			return sol::make_object(L, ref.ptr->macro); }),

			"Stacks", sol::overload(
				[](const CharinfoPeerRef &ref, const std::string &spell)
					{ return ref.ptr && !ref.ptr->invalidated() && charinfo::StacksForPeer(*ref.ptr, spell.c_str()); },
				[](const CharinfoPeerRef &ref, const sol::object &spellArg) -> bool {
					if (!ref.ptr || ref.ptr->invalidated()) return false;
					sol::type t = spellArg.get_type();
					if (t == sol::type::string) return charinfo::StacksForPeer(*ref.ptr, spellArg.as<std::string>().c_str());
					if (t == sol::type::number) return charinfo::StacksForPeer(*ref.ptr, std::to_string(static_cast<int>(spellArg.as<double>())).c_str());
					return false;
				}),
			"StacksPet", sol::overload(
				[](const CharinfoPeerRef &ref, const std::string &spell)
					{ return ref.ptr && !ref.ptr->invalidated() && charinfo::StacksPetForPeer(*ref.ptr, spell.c_str()); },
				[](const CharinfoPeerRef &ref, const sol::object &spellArg) -> bool {
					if (!ref.ptr || ref.ptr->invalidated()) return false;
					sol::type t = spellArg.get_type();
					if (t == sol::type::string) return charinfo::StacksPetForPeer(*ref.ptr, spellArg.as<std::string>().c_str());
					if (t == sol::type::number) return charinfo::StacksPetForPeer(*ref.ptr, std::to_string(static_cast<int>(spellArg.as<double>())).c_str());
					return false;
				}),

			sol::meta_function::to_string, [](const CharinfoPeerRef &ref)
			{
			if (!ref.ptr || ref.ptr->invalidated()) return std::string("(invalidated peer)");
			return std::string("CharinfoPeer(") + ref.ptr->name + ")"; },
			sol::meta_function::equal_to, [](const CharinfoPeerRef& a, const CharinfoPeerRef& b) { return a == b; },
			sol::meta_function::less_than, [](const CharinfoPeerRef& a, const CharinfoPeerRef& b) { return a < b; },
			sol::meta_function::less_than_or_equal_to, [](const CharinfoPeerRef& a, const CharinfoPeerRef& b) { return a <= b; });
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
		return sol::make_object(L, CharinfoPeerRef(it->second));
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

	module["GetPeer"] = [](sol::this_state L, const std::string &name) -> sol::object
	{
		auto it = charinfo::GetPeers().find(name);
		if (it == charinfo::GetPeers().end())
			return sol::lua_nil;
		return sol::make_object(L, CharinfoPeerRef(it->second));
	};

	// Callable: Charinfo(name) == GetPeer(name).
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
