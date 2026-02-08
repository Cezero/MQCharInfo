/*
 * ImGui settings panel for MQCharinfo: plugins/Charinfo.
 * Read-only peer list and Key/Value data (matches botgui.lua Peers tab).
 */

#include "CharinfoPanel.h"
#include "Charinfo.h"
#include "charinfo.pb.h"
#include "mq/Plugin.h"

#include <imgui.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace {

using PeerMap = charinfo::PeerMap;

static void DrawPeerData(const mq::proto::charinfo::CharinfoPublish& peer)
{
	// Scalars (key names match LuaModule.cpp)
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("Name");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%s", peer.name().c_str());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("ID");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.id());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("Level");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.level());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PctHPs");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.pct_hps());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PctMana");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.pct_mana());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("TargetHP");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.target_hp());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("FreeBuffSlots");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.free_buff_slots());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("Detrimentals");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.detrimentals());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CountPoison");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.count_poison());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CountDisease");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.count_disease());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CountCurse");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.count_curse());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CountCorruption");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.count_corruption());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PetHP");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.pet_hp());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("MaxEndurance");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.max_endurance());

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CurrentHP");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.current_hp());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("MaxHP");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.max_hp());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CurrentMana");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.current_mana());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("MaxMana");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.max_mana());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CurrentEndurance");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.current_endurance());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PctEndurance");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.pct_endurance());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PetID");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.pet_id());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("PetAffinity");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%s", peer.pet_affinity() ? "true" : "false");
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("Version");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%.2f", peer.version());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CombatState");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.combat_state());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("CastingSpellID");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", peer.casting_spell_id());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("NoCure");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%lld", (long long)peer.no_cure());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("LifeDrain");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%lld", (long long)peer.life_drain());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("ManaDrain");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%lld", (long long)peer.mana_drain());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text("EnduDrain");
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%lld", (long long)peer.endu_drain());

	// Class
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("Class", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Name");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", peer.class_info().name().c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("ShortName");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", peer.class_info().short_name().c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("ID");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%d", peer.class_info().id());
		ImGui::TreePop();
	}

	// Target
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("Target", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Name");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", peer.target().name().c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("ID");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%d", peer.target().id());
		ImGui::TreePop();
	}

	// Zone
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("Zone", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Name");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", peer.zone().name().c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("ShortName");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", peer.zone().short_name().c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("ID");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%d", peer.zone().id());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("InstanceID");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%d", peer.zone().instance_id());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("X");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%.2f", peer.zone().x());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Y");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%.2f", peer.zone().y());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Z");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%.2f", peer.zone().z());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Heading");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%.2f", peer.zone().heading());
		// Client-side Distance (same zone as local player only)
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Distance");
		ImGui::TableSetColumnIndex(1);
		if (pLocalPlayer && pLocalPC && pLocalPC->zoneId == peer.zone().id() && static_cast<uint16_t>(pLocalPC->instance) == static_cast<uint16_t>(peer.zone().instance_id())) {
			float dist = Get3DDistance(pLocalPlayer->X, pLocalPlayer->Y, pLocalPlayer->Z, peer.zone().x(), peer.zone().y(), peer.zone().z());
			ImGui::Text("%.1f", dist);
		} else {
			ImGui::TextUnformatted("—");
		}
		ImGui::TreePop();
	}

	// State (stringified bit names, same as Lua State[])
	{
		std::vector<std::string> stateStrs = charinfo::StateBitsToStrings(peer.state_bits());
		std::string stateDisplay;
		for (const auto& s : stateStrs) {
			if (!stateDisplay.empty()) stateDisplay += ", ";
			stateDisplay += s;
		}
		if (stateDisplay.empty()) stateDisplay = "(none)";
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("State");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(stateDisplay.c_str());
	}

	// BuffState (stringified detr/bene bit names, same as Lua BuffState[])
	{
		std::vector<std::string> buffStrs = charinfo::BuffStateBitsToStrings(peer.detr_state_bits(), peer.bene_state_bits());
		std::string buffDisplay;
		for (const auto& s : buffStrs) {
			if (!buffDisplay.empty()) buffDisplay += ", ";
			buffDisplay += s;
		}
		if (buffDisplay.empty()) buffDisplay = "(none)";
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("BuffState");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(buffDisplay.c_str());
	}

	// Experience
	if (peer.has_experience()) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::TreeNodeEx("Experience", ImGuiTreeNodeFlags_SpanFullWidth)) {
			const auto& ex = peer.experience();
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("PctExp"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", ex.pct_exp());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("PctAAExp"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", ex.pct_aa_exp());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("PctGroupLeaderExp"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", ex.pct_group_leader_exp());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("TotalAA"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", ex.total_aa());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("AASpent"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", ex.aa_spent());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("AAUnused"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", ex.aa_unused());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("AAAssigned"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", ex.aa_assigned());
			ImGui::TreePop();
		}
	}

	// MakeCamp
	if (peer.has_make_camp()) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::TreeNodeEx("MakeCamp", ImGuiTreeNodeFlags_SpanFullWidth)) {
			const auto& mc = peer.make_camp();
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Status"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", mc.status());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("X"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", mc.x());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Y"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", mc.y());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Radius"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", mc.radius());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Distance"); ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", mc.distance());
			ImGui::TreePop();
		}
	}

	// Macro
	if (peer.has_macro()) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::TreeNodeEx("Macro", ImGuiTreeNodeFlags_SpanFullWidth)) {
			const auto& mac = peer.macro();
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("MacroState"); ImGui::TableSetColumnIndex(1); ImGui::Text("%d", mac.macro_state());
			ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("MacroName"); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", mac.macro_name().c_str());
			ImGui::TreePop();
		}
	}

	// Gems
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("Gems", ImGuiTreeNodeFlags_SpanFullWidth)) {
		for (int i = 0; i < peer.gem_size(); ++i) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d]", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", peer.gem(i));
		}
		ImGui::TreePop();
	}

	// FreeInventory
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("FreeInventory", ImGuiTreeNodeFlags_SpanFullWidth)) {
		for (int i = 0; i < peer.free_inventory_size(); ++i) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Size %d", i);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", peer.free_inventory(i));
		}
		ImGui::TreePop();
	}

	// Buff (buff_spells[i] + buff_durations[i])
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("Buff", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		for (int i = 0; i < peer.buff_spells_size(); ++i)
		{
			int dur = (i < peer.buff_durations_size()) ? peer.buff_durations(i) : -1;
			const auto& sp = peer.buff_spells(i);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Duration", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", dur);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell Name", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%s", sp.name().c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell ID", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", sp.id());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell Category", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", sp.category());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell Level", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", sp.level());
		}
		ImGui::TreePop();
	}

	// ShortBuff (short_buff_spells[i] + short_buff_durations[i])
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("ShortBuff", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		for (int i = 0; i < peer.short_buff_spells_size(); ++i)
		{
			int dur = (i < peer.short_buff_durations_size()) ? peer.short_buff_durations(i) : -1;
			const auto& sp = peer.short_buff_spells(i);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Duration", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", dur);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell Name", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%s", sp.name().c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell ID", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", sp.id());
		}
		ImGui::TreePop();
	}

	// PetBuff (pet_buff_spells[i] + pet_buff_durations[i])
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (ImGui::TreeNodeEx("PetBuff", ImGuiTreeNodeFlags_SpanFullWidth))
	{
		for (int i = 0; i < peer.pet_buff_spells_size(); ++i)
		{
			int dur = (i < peer.pet_buff_durations_size()) ? peer.pet_buff_durations(i) : -1;
			const auto& sp = peer.pet_buff_spells(i);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Duration", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", dur);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell Name", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%s", sp.name().c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("[%d] Spell ID", i + 1);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", sp.id());
		}
		ImGui::TreePop();
	}
}

} // namespace

void DrawCharinfoPanel()
{
	std::vector<std::string> names;
	{
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		const PeerMap& peers = charinfo::GetPeers();
		names.reserve(peers.size());
		for (const auto& p : peers)
			names.push_back(p.first);
	}

	if (names.empty())
	{
		ImGui::Text("No peers (no character data received yet).");
		return;
	}

	std::sort(names.begin(), names.end());

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
		| ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame;

	for (const std::string& name : names)
	{
		mq::proto::charinfo::CharinfoPublish peerCopy;
		{
			std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
			const PeerMap& peers = charinfo::GetPeers();
			auto it = peers.find(name);
			if (it == peers.end())
				continue;
			peerCopy = it->second;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth))
		{
			std::string tableId = "peers_table_" + name;
			if (ImGui::BeginTable(tableId.c_str(), 2, tableFlags, ImVec2(-1, 0)))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_None, 0.4f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_None, 0.6f);
				ImGui::TableHeadersRow();
				DrawPeerData(peerCopy);
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}
}
