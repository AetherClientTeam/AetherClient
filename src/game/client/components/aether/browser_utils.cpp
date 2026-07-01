#include "browser_utils.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

#include <algorithm>

void CAetherBrowserUtils::OnUpdate()
{
	if(!g_Config.m_AeBrowserUtils || !g_Config.m_AeBrowserAutoRefresh || !GameClient()->m_Menus.IsActive() || Client()->State() == IClient::STATE_CONNECTING || Client()->State() == IClient::STATE_LOADING)
	{
		m_LastRefresh = 0;
		return;
	}
	if(time_get() < m_SkipRefreshUntil)
		return;
	if(g_Config.m_UiPage < CMenus::PAGE_INTERNET || g_Config.m_UiPage > CMenus::PAGE_FAVORITE_COMMUNITY_5)
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
	const int64_t Interval = time_freq() * std::clamp(g_Config.m_AeBrowserRefreshSeconds, 5, 120);
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

void CAetherBrowserUtils::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE && OldState == IClient::STATE_DEMOPLAYBACK)
	{
		m_LastRefresh = 0;
		m_SkipRefreshUntil = time_get() + time_freq();
	}
}
