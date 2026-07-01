#include "session_markers.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

void CAetherSessionMarkers::ConAddMarker(IConsole::IResult *pResult, void *pUserData)
{
	CAetherSessionMarkers *pSelf = static_cast<CAetherSessionMarkers *>(pUserData);
	pSelf->AddMarker(pResult->NumArguments() > 0 ? pResult->GetString(0) : "manual");
}

void CAetherSessionMarkers::OnConsoleInit()
{
	Console()->Register("ae_session_marker", "?r[reason]", CFGFLAG_CLIENT, ConAddMarker, this, "Add an Aether demo/session marker");
}

void CAetherSessionMarkers::AddMarker(const char *pReason)
{
	if(!g_Config.m_AeSessionMarkers)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(g_Config.m_AeNotificationsSessionMarkers)
			GameClient()->m_AetherNotifications.Push("Session Marker", "Join a server or demo first.");
		return;
	}

	Console()->ExecuteLine("add_demomarker", IConsole::CLIENT_ID_UNSPECIFIED);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Marker added%s%s", pReason && pReason[0] ? ": " : ".", pReason && pReason[0] ? pReason : "");
	if(g_Config.m_AeNotificationsSessionMarkers)
		GameClient()->m_AetherNotifications.Push("Session Marker", aBuf, 3.0f);
}

bool CAetherSessionMarkers::LocalFreezeActive() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		return false;

	const auto &Character = GameClient()->m_Snap.m_aCharacters[LocalId];
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	int FreezeEnd = 0;
	bool DeepFrozen = false;
	if(Character.m_HasExtendedData)
	{
		FreezeEnd = Character.m_ExtendedData.m_FreezeEnd;
		DeepFrozen = FreezeEnd == -1;
	}
	else
	{
		FreezeEnd = GameClient()->m_aClients[LocalId].m_FreezeEnd;
		DeepFrozen = GameClient()->m_aClients[LocalId].m_DeepFrozen;
	}
	return FreezeEnd == -1 || FreezeEnd > Tick || DeepFrozen;
}

void CAetherSessionMarkers::OnUpdate()
{
	if(!g_Config.m_AeSessionMarkers || !g_Config.m_AeSessionMarkerAutoFreeze || GameClient()->m_SuppressEvents)
	{
		m_LastFrozen = false;
		return;
	}

	const bool Frozen = LocalFreezeActive();
	if(!Frozen)
	{
		m_LastFrozen = false;
		return;
	}
	if(m_LastFrozen)
		return;

	const int64_t Now = time_get();
	if(Now - m_LastFreezeMarkerTime < time_freq())
		return;
	m_LastFreezeMarkerTime = Now;
	m_LastFrozen = true;
	AddMarker("freeze");
}
