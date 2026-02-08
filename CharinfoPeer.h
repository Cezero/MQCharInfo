#pragma once

#include "charinfo.pb.h"
#include <memory>
#include <string>
#include <vector>

namespace charinfo {

// Lua-shaped types: match the exact structure exposed to Lua (peer.Buff[i].Spell, peer.Zone.Distance, etc.).

struct PeerSpellInfo {
	std::string name;
	int32_t id = 0;
	int32_t category = 0;
	int32_t level = 0;
};

struct PeerBuffEntry {
	PeerSpellInfo spell;
	int32_t duration = -1;
};

struct PeerClassInfo {
	std::string name;
	std::string short_name;
	int32_t id = 0;
};

struct PeerTargetInfo {
	std::string name;
	int32_t id = 0;
};

struct PeerZoneInfo {
	std::string name;
	std::string short_name;
	int32_t id = 0;
	int32_t instance_id = 0;
	float x = 0, y = 0, z = 0, heading = 0;
	// Client-side distance when in same zone; negative = nil in Lua.
	double distance = -1.0;
};

struct PeerExperienceInfo {
	float pct_exp = 0;
	float pct_aa_exp = 0;
	float pct_group_leader_exp = 0;
	int32_t total_aa = 0;
	int32_t aa_spent = 0;
	int32_t aa_unused = 0;
	int32_t aa_assigned = 0;
};

struct PeerMakeCampInfo {
	int32_t status = 0;
	float x = 0, y = 0, radius = 0, distance = 0;
};

struct PeerMacroInfo {
	int32_t macro_state = 0;
	std::string macro_name;
};

// Gems: each entry is ID + resolved Name, Category, Level (for Lua Gems[i].Name etc.).
struct PeerGemEntry {
	int32_t id = 0;
	std::string name;
	int32_t category = 0;
	int32_t level = 0;
};

class CharinfoPeer {
public:
	bool invalidated() const { return m_invalidated; }
	void set_invalidated(bool v) { m_invalidated = v; }

	// Scalars
	std::string name;
	int32_t id = 0;
	int32_t level = 0;
	int32_t pct_hps = 0;
	int32_t pct_mana = 0;
	int32_t target_hp = 0;
	int32_t free_buff_slots = 0;
	int32_t detrimentals = 0;
	int32_t count_poison = 0, count_disease = 0, count_curse = 0, count_corruption = 0;
	int32_t pet_hp = 0;
	int32_t max_endurance = 0;
	int32_t current_hp = 0, max_hp = 0;
	int32_t current_mana = 0, max_mana = 0;
	int32_t current_endurance = 0, pct_endurance = 0;
	int32_t pet_id = 0;
	bool pet_affinity = false;
	int64_t no_cure = 0, life_drain = 0, mana_drain = 0, endu_drain = 0;
	uint32_t state_bits = 0, detr_state_bits = 0, bene_state_bits = 0;
	int32_t casting_spell_id = 0;
	int32_t combat_state = 0;
	float version = 0;

	// Nested
	PeerClassInfo class_info;
	PeerTargetInfo target;
	PeerZoneInfo zone;
	std::vector<PeerBuffEntry> buff;
	std::vector<PeerBuffEntry> short_buff;
	std::vector<PeerBuffEntry> pet_buff;
	std::vector<std::string> state;
	std::vector<std::string> buff_state;
	std::vector<PeerGemEntry> gems;
	std::vector<int32_t> free_inventory;
	bool has_experience = false;
	PeerExperienceInfo experience;
	bool has_make_camp = false;
	PeerMakeCampInfo make_camp;
	bool has_macro = false;
	PeerMacroInfo macro;

private:
	bool m_invalidated = false;
};

// Build CharinfoPeer from a full Publish (includes Zone.Distance when in same zone).
CharinfoPeer FromPublish(const mq::proto::charinfo::CharinfoPublish& pub);

// Apply a single FieldUpdate to an existing CharinfoPeer. Recomputes Zone.Distance when zone is updated.
bool ApplyFieldUpdate(const mq::proto::charinfo::FieldUpdate& update, CharinfoPeer* peer);

// Stacks / StacksPet using CharinfoPeer data.
bool StacksForPeer(const CharinfoPeer& peer, const char* spellNameOrId);
bool StacksPetForPeer(const CharinfoPeer& peer, const char* spellNameOrId);

} // namespace charinfo
