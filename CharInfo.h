#pragma once

#include "charinfo.pb.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

namespace charinfo {

// Version constant; bump when making breaking or notable changes.
constexpr float CHARINFO_VERSION = 1.1f;

// In-memory peer state keyed by sender (character) name.
using PeerMap = std::unordered_map<std::string, mq::proto::charinfo::CharInfoPublish>;

PeerMap& GetPeers();
std::mutex& GetPeersMutex();

// Build current character state into a CharInfoPublish. Returns false if not in game.
bool BuildPublishPayload(mq::proto::charinfo::CharInfoPublish* out);

// Stacks: true if the given spell (by name or ID string) stacks with all buffs/short buffs on this peer.
bool StacksForPeer(const mq::proto::charinfo::CharInfoPublish& peer, const char* spellNameOrId);
// StacksPet: true if the given spell stacks with all pet buffs on this peer.
bool StacksPetForPeer(const mq::proto::charinfo::CharInfoPublish& peer, const char* spellNameOrId);

// State/BuffState bits to display strings (same order as Lua State[] and BuffState[]).
std::vector<std::string> StateBitsToStrings(uint32_t state_bits);
std::vector<std::string> BuffStateBitsToStrings(uint32_t detr_bits, uint32_t bene_bits);

} // namespace charinfo
