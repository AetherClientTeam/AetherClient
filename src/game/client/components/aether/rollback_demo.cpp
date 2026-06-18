#include "rollback_demo.h"

#include <engine/client.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

void CAetherRollbackDemo::ConSaveRollbackDemo(IConsole::IResult *pResult, void *pUserData)
{
	CAetherRollbackDemo *pSelf = static_cast<CAetherRollbackDemo *>(pUserData);
	const int Length = pResult->NumArguments() > 0 ? std::clamp(pResult->GetInteger(0), 10, 600) : g_Config.m_AeRollbackDemoSeconds;

	if(pSelf->Client()->State() != IClient::STATE_ONLINE || !pSelf->GameClient()->Map()->IsLoaded())
	{
		pSelf->GameClient()->m_Chat.Echo(Localize("Rollback demo is only available while playing on a loaded map."));
		return;
	}

	if(!g_Config.m_ClReplays)
	{
		g_Config.m_ClReplays = 1;
		g_Config.m_ClReplayLength = std::max(g_Config.m_ClReplayLength, Length);
		pSelf->Client()->DemoRecorder_UpdateReplayRecorder();
		pSelf->GameClient()->m_Chat.Echo(Localize("Rollback demo recording enabled. Try again after a few seconds."));
		return;
	}

	char aCommand[64];
	str_format(aCommand, sizeof(aCommand), "save_replay %d", Length);
	pSelf->Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
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
