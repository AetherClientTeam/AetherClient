#include "browser_utils.h"

#include <base/system.h>

#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>

void CAetherBrowserUtils::OnUpdate()
{
	if(!g_Config.m_AeBrowserUtils || !g_Config.m_AeBrowserAutoRefresh || !GameClient()->m_Menus.IsActive())
	{
		m_LastRefresh = 0;
		return;
	}

	IServerBrowser *pServerBrowser = ServerBrowser();
	if(!pServerBrowser || pServerBrowser->IsRefreshing() || pServerBrowser->IsGettingServerlist())
		return;

	const int Type = pServerBrowser->GetCurrentType();
	if(Type < IServerBrowser::TYPE_INTERNET || Type >= IServerBrowser::NUM_TYPES)
		return;

	const int64_t Now = time_get();
	const int64_t Interval = time_freq() * std::clamp(g_Config.m_AeBrowserRefreshSeconds, 15, 120);
	if(m_LastRefresh == 0)
	{
		m_LastRefresh = Now;
		return;
	}
	if(Now - m_LastRefresh < Interval)
		return;

	pServerBrowser->Refresh(Type, true);
	m_LastRefresh = Now;
}

void CAetherBrowserUtils::OnReset()
{
	m_LastRefresh = 0;
}
