#pragma once

#include "charinfo.pb.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace charinfo {

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

} // namespace charinfo
