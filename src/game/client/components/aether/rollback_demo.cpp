#include "rollback_demo.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

bool CAetherRollbackDemo::NotificationsEnabled() const
{
	return g_Config.m_AeNotifications && g_Config.m_AeNotificationsRollbackDemo;
}

void CAetherRollbackDemo::EmitStatus(const char *pMessage)
{
	if(NotificationsEnabled())
		GameClient()->m_AetherNotifications.Push("Rollback Demo", pMessage);
	else
		GameClient()->m_Chat.Echo(Localize(pMessage));
}

void CAetherRollbackDemo::ExpectReplayEcho()
{
	m_ReplayEchoSuppressUntil = NotificationsEnabled() ? time_get() + 5 * time_freq() : 0;
}

void CAetherRollbackDemo::ConSaveRollbackDemo(IConsole::IResult *pResult, void *pUserData)
{
	CAetherRollbackDemo *pSelf = static_cast<CAetherRollbackDemo *>(pUserData);
	const int Length = pResult->NumArguments() > 0 ? std::clamp(pResult->GetInteger(0), 10, 600) : g_Config.m_AeRollbackDemoSeconds;

	if(pSelf->Client()->State() != IClient::STATE_ONLINE || !pSelf->GameClient()->Map()->IsLoaded())
	{
		pSelf->EmitStatus("Join a loaded map first.");
		return;
	}

	if(!g_Config.m_ClReplays)
	{
		g_Config.m_ClReplays = 1;
		g_Config.m_ClReplayLength = std::max(g_Config.m_ClReplayLength, Length);
		pSelf->Client()->DemoRecorder_UpdateReplayRecorder();
		pSelf->EmitStatus("Replay recording enabled. Try again in a few seconds.");
		return;
	}

	pSelf->ExpectReplayEcho();
	char aCommand[64];
	str_format(aCommand, sizeof(aCommand), "save_replay %d", Length);
	pSelf->Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	pSelf->EmitStatus("Saving rollback clip...");
}

void CAetherRollbackDemo::OnConsoleInit()
{
	Console()->Register("ae_save_rollback_demo", "?i[seconds]", CFGFLAG_CLIENT, ConSaveRollbackDemo, this, "Save the last seconds from the replay recorder");
}

void CAetherRollbackDemo::OnUpdate()
{
	const int Enabled = g_Config.m_AeRollbackDemo;
	const int Seconds = g_Config.m_AeRollbackDemoSeconds;
	if(Enabled)
	{
		if(!g_Config.m_ClReplays)
			g_Config.m_ClReplays = 1;
		if(g_Config.m_ClReplayLength < Seconds)
			g_Config.m_ClReplayLength = Seconds;
	}

	const bool CanUpdateRecorder = Client()->State() == IClient::STATE_ONLINE && GameClient()->Map()->IsLoaded();
	if(CanUpdateRecorder && (Enabled != m_LastEnabled || Seconds != m_LastSeconds))
		Client()->DemoRecorder_UpdateReplayRecorder();
	m_LastEnabled = Enabled;
	m_LastSeconds = Seconds;
}

bool CAetherRollbackDemo::ConsumeReplayEcho(const char *pString)
{
	if(!NotificationsEnabled() || !pString || m_ReplayEchoSuppressUntil <= 0 || time_get() > m_ReplayEchoSuppressUntil)
		return false;
	if(!str_find_nocase(pString, "replay"))
		return false;

	if(str_find_nocase(pString, "success") || str_find_nocase(pString, "saved"))
		GameClient()->m_AetherNotifications.Push("Rollback Demo", "Rollback clip saved.");
	else if(str_find_nocase(pString, "fail"))
		GameClient()->m_AetherNotifications.Push("Rollback Demo", "Rollback clip failed.");
	else if(str_find_nocase(pString, "disabled"))
		GameClient()->m_AetherNotifications.Push("Rollback Demo", "Replay feature is disabled.");
	else
		GameClient()->m_AetherNotifications.Push("Rollback Demo", pString);
	m_ReplayEchoSuppressUntil = 0;
	return true;
}
