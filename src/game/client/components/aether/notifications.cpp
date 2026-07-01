#include "notifications.h"

#include <base/color.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#if defined(CONF_AUTOUPDATE)
#include <engine/updater.h>
#endif

#include <game/client/gameclient.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>

void CAetherNotifications::ConNotify(IConsole::IResult *pResult, void *pUserData)
{
	CAetherNotifications *pSelf = static_cast<CAetherNotifications *>(pUserData);
	pSelf->Push("Aether", pResult->NumArguments() > 0 ? pResult->GetString(0) : "Notification test");
}

void CAetherNotifications::ConClear(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAetherNotifications *>(pUserData)->Clear();
}

void CAetherNotifications::ConCheckUpdate(IConsole::IResult *pResult, void *pUserData)
{
	CAetherNotifications *pSelf = static_cast<CAetherNotifications *>(pUserData);
#if defined(CONF_AUTOUPDATE)
	pSelf->Updater()->CheckForUpdate();
	pSelf->m_UpdateCheckRequested = true;
	pSelf->Push("Aether Update", "Checking for updates...");
#else
	pSelf->Push("Aether Update", "Updater is not available in this build.");
#endif
}

void CAetherNotifications::OnConsoleInit()
{
	Console()->Register("ae_notify", "?r[message]", CFGFLAG_CLIENT, ConNotify, this, "Show an Aether notification");
	Console()->Register("ae_notifications_clear", "", CFGFLAG_CLIENT, ConClear, this, "Clear Aether notifications");
	Console()->Register("ae_check_update", "", CFGFLAG_CLIENT, ConCheckUpdate, this, "Check for an Aether update");
}

void CAetherNotifications::Push(const char *pTitle, const char *pMessage, float Seconds)
{
	if(!g_Config.m_AeNotifications)
		return;
	SNotification Notification;
	str_copy(Notification.m_aTitle, pTitle && pTitle[0] ? pTitle : "Aether", sizeof(Notification.m_aTitle));
	str_copy(Notification.m_aMessage, pMessage && pMessage[0] ? pMessage : "-", sizeof(Notification.m_aMessage));
	Notification.m_Start = time_get();
	Notification.m_End = Notification.m_Start + (int64_t)(std::clamp(Seconds, 1.5f, 20.0f) * time_freq());
	m_vNotifications.push_back(Notification);
	if(m_vNotifications.size() > 8)
		m_vNotifications.erase(m_vNotifications.begin(), m_vNotifications.begin() + (m_vNotifications.size() - 8));
}

void CAetherNotifications::Clear()
{
	m_vNotifications.clear();
}

void CAetherNotifications::OnUpdate()
{
	const int64_t Now = time_get();
	m_vNotifications.erase(std::remove_if(m_vNotifications.begin(), m_vNotifications.end(), [&](const SNotification &Notification) {
		return Notification.m_End <= Now;
	}), m_vNotifications.end());

	if(!g_Config.m_AeNotifications || !g_Config.m_AeNotificationsUpdateCheck)
		return;

#if defined(CONF_AUTOUPDATE)
	if(!m_UpdateCheckRequested && Client()->State() == IClient::STATE_OFFLINE)
	{
		if(m_UpdateCheckTime == 0)
			m_UpdateCheckTime = Now + 8 * time_freq();
		if(Now >= m_UpdateCheckTime)
		{
			Updater()->CheckForUpdate();
			m_UpdateCheckRequested = true;
		}
	}

	const int State = Updater()->GetCurrentState();
	if(State == m_LastUpdaterState)
		return;
	m_LastUpdaterState = State;
	if(State == IUpdater::UPDATE_AVAILABLE)
		Push("Aether Update", "New client version is available.", 8.0f);
	else if(State == IUpdater::NEED_RESTART)
		Push("Aether Update", "Update is ready. Restart to apply.", 8.0f);
	else if(State == IUpdater::FAIL)
		Push("Aether Update", "Update check failed.", 6.0f);
#endif
}

void CAetherNotifications::OnRender()
{
	if(!g_Config.m_AeNotifications || m_vNotifications.empty())
		return;
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;

	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	const ColorRGBA Accent = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true)).WithAlpha(0.95f);
	const int64_t Now = time_get();
	float y = 24.0f;
	const int Count = std::min<int>((int)m_vNotifications.size(), 4);
	for(int i = 0; i < Count; ++i)
	{
		const SNotification &Notification = m_vNotifications[m_vNotifications.size() - 1 - i];
		const float Remaining = std::clamp((float)(Notification.m_End - Now) / (float)time_freq(), 0.0f, 20.0f);
		const float Total = std::max(1.0f, (float)(Notification.m_End - Notification.m_Start) / (float)time_freq());
		const float Life = std::clamp(Remaining / Total, 0.0f, 1.0f);
		const float Fade = std::min(1.0f, std::min((float)(Now - Notification.m_Start) / (0.18f * time_freq()), Remaining / 0.22f));
		if(Fade <= 0.01f)
			continue;

		const float TitleW = TextRender()->TextWidth(10.2f, Localize(Notification.m_aTitle));
		const float MsgW = TextRender()->TextWidth(9.1f, Localize(Notification.m_aMessage));
		const float Width = std::clamp(TitleW + MsgW + 55.0f, 220.0f, 430.0f);
		const float Height = 36.0f;
		CUIRect Panel(ScreenW - Width - 20.0f + (1.0f - Fade) * 20.0f, y, Width, Height);
		Panel.Draw(ColorRGBA(0.012f, 0.014f, 0.022f, 0.88f * Fade), IGraphics::CORNER_ALL, Height * 0.5f);

		CUIRect Body = Panel;
		Body.Margin(11.0f, &Body);
		const float DotSize = 7.0f;
		Graphics()->DrawRect(Body.x, Body.y + (Body.h - DotSize) * 0.5f, DotSize, DotSize, Accent.WithAlpha(0.95f * Fade), IGraphics::CORNER_ALL, DotSize * 0.5f);
		Body.x += DotSize + 8.0f;
		Body.w -= DotSize + 8.0f;
		TextRender()->TextColor(1.0f, 0.88f, 1.0f, 0.96f * Fade);
		TextRender()->Text(Body.x, Body.y + 1.8f, 10.2f, Localize(Notification.m_aTitle), Body.w);
		TextRender()->TextColor(0.76f, 0.81f, 0.92f, 0.88f * Fade);
		TextRender()->Text(Body.x + TitleW + 8.0f, Body.y + 2.8f, 9.1f, Localize(Notification.m_aMessage), maximum(0.0f, Body.w - TitleW - 8.0f));
		Graphics()->DrawRect(Panel.x + 12.0f, Panel.y + Panel.h - 4.4f, (Panel.w - 24.0f) * Life, 1.4f, Accent.WithAlpha(0.72f * Fade), IGraphics::CORNER_ALL, 0.8f);

		y += Height + 7.0f;
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}
