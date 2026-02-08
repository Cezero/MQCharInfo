/*
 * MQCharinfo - Character info over Actors, Lua module API
 * NetBots-style peer data via post office; require("plugin.charinfo") for GetInfo/GetPeers/GetPeer.
 */

#include "mq/Plugin.h"
#include "Charinfo.h"
#include "CharinfoPanel.h"
#include "charinfo.pb.h"

#include <eqlib/game/Constants.h>

#include <chrono>
#include <mutex>

PreSetup("MQCharinfo");
PLUGIN_VERSION(charinfo::CHARINFO_VERSION);

static postoffice::DropboxAPI s_charinfoDropbox;
static bool s_actorRegistered = false;
static std::chrono::steady_clock::time_point s_nextPublish;
static const std::chrono::milliseconds s_publishInterval(1000);

static mq::proto::charinfo::CharinfoPublish s_lastPublished;
static bool Initialized = false;
static bool s_initialized = false;
static bool s_justZoned = false;

static void HandleMessage(const std::shared_ptr<postoffice::Message>& message)
{
	if (!message || !message->Payload)
		return;

	mq::proto::charinfo::CharinfoMessage msg;
	if (!msg.ParseFromString(*message->Payload))
		return;

	using Id = mq::proto::charinfo::CharinfoMessageId;

	if (msg.id() == Id::Publish && msg.has_publish())
	{
		const std::string& sender = msg.publish().sender();
		if (!sender.empty())
		{
			std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
			charinfo::GetPeers()[sender] = msg.publish();
			WriteChatf("[MQCharinfo] Received Publish from '%s' (peer count %zu)", sender.c_str(), charinfo::GetPeers().size());
		}
		return;
	}

	if (msg.id() == Id::Update && msg.has_update())
	{
		const auto& update = msg.update();
		const std::string& sender = update.sender();
		if (sender.empty())
			return;
		std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
		auto it = charinfo::GetPeers().find(sender);
		if (it == charinfo::GetPeers().end())
			return;
		for (int i = 0; i < update.updates_size(); i++)
			charinfo::ApplyFieldUpdate(update.updates(i), &it->second);
		return;
	}

	if (msg.id() == Id::Remove && msg.has_remove())
	{
		const std::string& sender = msg.remove().sender();
		if (!sender.empty())
		{
			std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
			charinfo::GetPeers().erase(sender);
		}
		return;
	}

	if (msg.id() == Id::Joined && msg.has_joined())
	{
		const std::string& joinedSender = msg.joined().sender();
		if (pLocalPlayer && joinedSender == pLocalPlayer->DisplayedName)
			return;
		WriteChatf("[MQCharinfo] Received Joined from '%s'", joinedSender.empty() ? "(empty)" : joinedSender.c_str());

		mq::proto::charinfo::CharinfoPublish payload;
		if (!charinfo::BuildPublishPayload(&payload))
		{
			WriteChatf("[MQCharinfo] Joined: not in game, skipping reply");
			return;
		}
		mq::proto::charinfo::CharinfoMessage replyMsg;
		replyMsg.set_id(mq::proto::charinfo::CharinfoMessageId::Publish);
		*replyMsg.mutable_publish() = payload;
		postoffice::Address address;
		address.Server = GetServerShortName();
		address.Mailbox = "charinfo";
		s_charinfoDropbox.Post(address, replyMsg);
	}
}

static void SendFullPublish(const mq::proto::charinfo::CharinfoPublish& payload)
{
	mq::proto::charinfo::CharinfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharinfoMessageId::Publish);
	*msg.mutable_publish() = payload;

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

static void SendJoined(const std::string& sender)
{
	WriteChatf("[MQCharinfo] Sending Joined as '%s'", sender.empty() ? "(empty)" : sender.c_str());
	mq::proto::charinfo::CharinfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharinfoMessageId::Joined);
	msg.mutable_joined()->set_sender(sender);

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

static void SendUpdate(const mq::proto::charinfo::CharinfoUpdate& update)
{
	mq::proto::charinfo::CharinfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharinfoMessageId::Update);
	*msg.mutable_update() = update;

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

static void SendRemove()
{
	if (!pLocalPlayer)
		return;

	mq::proto::charinfo::CharinfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharinfoMessageId::Remove);
	msg.mutable_remove()->set_sender(pLocalPlayer->DisplayedName);

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

PLUGIN_API void InitializePlugin()
{
	s_nextPublish = std::chrono::steady_clock::now();
	AddSettingsPanel("plugins/charinfo", DrawCharinfoPanel);
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveSettingsPanel("plugins/Charinfo");
	if (s_actorRegistered)
	{
		SendRemove();
		s_charinfoDropbox.Remove();
		s_actorRegistered = false;
	}
}

PLUGIN_API void SetGameState(int GameState)
{
	if (GameState == GAMESTATE_INGAME)
	{
		if (!s_actorRegistered)
		{
			s_charinfoDropbox = postoffice::AddActor("charinfo", HandleMessage);
			s_actorRegistered = true;
		}
	}
	if (GameState < GAMESTATE_INGAME)
	{
		if (s_actorRegistered)
		{
			SendRemove();
			s_charinfoDropbox.Remove();
			s_actorRegistered = false;
		}
		Initialized = false;
		s_initialized = false;
	}
}

PLUGIN_API void OnZoned()
{
	s_justZoned = true;
}

PLUGIN_API void OnPulse()
{
	if (GetGameState() != GAMESTATE_INGAME || !pLocalPC)
		return;
	if (!s_actorRegistered)
		return;

	if (!Initialized)
	{
		WriteChatf("[MQCharinfo]: Initialized. version %.2f", charinfo::CHARINFO_VERSION);
		Initialized = true;
		return;
	}

	auto now = std::chrono::steady_clock::now();
	if (now < s_nextPublish)
		return;

	s_nextPublish = now + s_publishInterval;

	mq::proto::charinfo::CharinfoPublish payload;
	if (!charinfo::BuildPublishPayload(&payload))
		return;

	if (!s_initialized || s_justZoned)
	{
		SendFullPublish(payload);
		SendJoined(payload.sender());
		s_lastPublished = payload;
		s_initialized = true;
		s_justZoned = false;
	}
	else
	{
		mq::proto::charinfo::CharinfoUpdate update;
		update.set_sender(payload.sender());
		if (charinfo::BuildUpdatePayload(payload, s_lastPublished, &update) && update.updates_size() > 0)
		{
			SendUpdate(update);
			s_lastPublished = payload;
		}
	}
}
