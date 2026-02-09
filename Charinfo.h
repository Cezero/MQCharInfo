#pragma once

#include "CharinfoPeer.h"
#include "charinfo.pb.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

namespace charinfo {

// Version constant; bump when making breaking or notable changes.
constexpr float CHARINFO_VERSION = 1.5f;

// In-memory peer state keyed by sender (character) name.
using PeerMap = std::unordered_map<std::string, std::shared_ptr<CharinfoPeer>>;

PeerMap& GetPeers();

// Build current character state into a CharinfoPublish. Returns false if not in game.
bool BuildPublishPayload(mq::proto::charinfo::CharinfoPublish* out);

// Build delta update from current vs previous state. Returns true if updates were added.
bool BuildUpdatePayload(const mq::proto::charinfo::CharinfoPublish& current,
	const mq::proto::charinfo::CharinfoPublish& previous,
	mq::proto::charinfo::CharinfoUpdate* out);

// Apply a single FieldUpdate to an existing CharinfoPublish (receiver merge). Returns true if applied.
// Used when building update payloads from current vs previous protobuf state.
bool ApplyFieldUpdate(const mq::proto::charinfo::FieldUpdate& update,
	mq::proto::charinfo::CharinfoPublish* peer);

// State/BuffState bits to display strings (same order as Lua State[] and BuffState[]).
std::vector<std::string> StateBitsToStrings(uint32_t state_bits);
std::vector<std::string> BuffStateBitsToStrings(uint32_t detr_bits, uint32_t bene_bits);

} // namespace charinfo
