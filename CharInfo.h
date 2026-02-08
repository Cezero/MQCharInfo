#pragma once

#include "charinfo.pb.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

namespace charinfo {

// Version constant; bump when making breaking or notable changes.
constexpr float CHARINFO_VERSION = 1.3f;

// In-memory peer state keyed by sender (character) name.
using PeerMap = std::unordered_map<std::string, mq::proto::charinfo::CharinfoPublish>;

PeerMap& GetPeers();
std::mutex& GetPeersMutex();

// Build current character state into a CharinfoPublish. Returns false if not in game.
bool BuildPublishPayload(mq::proto::charinfo::CharinfoPublish* out);

// Build delta update from current vs previous state. Returns true if updates were added.
bool BuildUpdatePayload(const mq::proto::charinfo::CharinfoPublish& current,
	const mq::proto::charinfo::CharinfoPublish& previous,
	mq::proto::charinfo::CharinfoUpdate* out);

// Apply a single FieldUpdate to an existing CharinfoPublish (receiver merge). Returns true if applied.
bool ApplyFieldUpdate(const mq::proto::charinfo::FieldUpdate& update,
	mq::proto::charinfo::CharinfoPublish* peer);

// Stacks: true if the given spell (by name or ID string) stacks with all buffs/short buffs on this peer.
bool StacksForPeer(const mq::proto::charinfo::CharinfoPublish& peer, const char* spellNameOrId);
// StacksPet: true if the given spell stacks with all pet buffs on this peer.
bool StacksPetForPeer(const mq::proto::charinfo::CharinfoPublish& peer, const char* spellNameOrId);

// State/BuffState bits to display strings (same order as Lua State[] and BuffState[]).
std::vector<std::string> StateBitsToStrings(uint32_t state_bits);
std::vector<std::string> BuffStateBitsToStrings(uint32_t detr_bits, uint32_t bene_bits);

} // namespace charinfo
