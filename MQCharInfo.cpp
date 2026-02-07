/*
 * MQCharInfo - Character info over Actors, Lua module API
 * NetBots-style peer data via post office; require("plugin.charinfo") for GetInfo/GetPeers/GetPeer.
 */

#include "mq/Plugin.h"
#include "CharInfo.h"
#include "charinfo.pb.h"

#include <chrono>
#include <mutex>

PreSetup("MQCharInfo");
PLUGIN_VERSION(1.0);

static postoffice::DropboxAPI s_charinfoDropbox;
static std::chrono::steady_clock::time_point s_nextPublish;
static const std::chrono::milliseconds s_publishInterval(1000);

static void HandleMessage(const std::shared_ptr<postoffice::Message>& message)
{
	if (!message || !message->Payload)
		return;

	mq::proto::charinfo::CharInfoMessage msg;
	if (!msg.ParseFromString(*message->Payload))
		return;

	using Id = mq::proto::charinfo::CharInfoMessageId;

	if (msg.id() == Id::Publish && msg.has_publish())
	{
		const std::string& sender = msg.publish().sender();
		if (!sender.empty())
		{
			std::lock_guard<std::mutex> lock(charinfo::GetPeersMutex());
			charinfo::GetPeers()[sender] = msg.publish();
		}
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
	}
}

static void SendPublish()
{
	mq::proto::charinfo::CharInfoPublish payload;
	if (!charinfo::BuildPublishPayload(&payload))
		return;

	mq::proto::charinfo::CharInfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharInfoMessageId::Publish);
	*msg.mutable_publish() = payload;

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

static void SendRemove()
{
	if (!pLocalPlayer)
		return;

	mq::proto::charinfo::CharInfoMessage msg;
	msg.set_id(mq::proto::charinfo::CharInfoMessageId::Remove);
	msg.mutable_remove()->set_sender(pLocalPlayer->DisplayedName);

	postoffice::Address address;
	address.Server = GetServerShortName();
	address.Mailbox = "charinfo";

	s_charinfoDropbox.Post(address, msg);
}

PLUGIN_API void InitializePlugin()
{
	s_charinfoDropbox = postoffice::AddActor("charinfo", HandleMessage);
	s_nextPublish = std::chrono::steady_clock::now();
}

PLUGIN_API void ShutdownPlugin()
{
	SendRemove();
	s_charinfoDropbox.Remove();
}

PLUGIN_API void OnPulse()
{
	auto now = std::chrono::steady_clock::now();
	if (now >= s_nextPublish)
	{
		s_nextPublish = now + s_publishInterval;
		SendPublish();
	}
}
