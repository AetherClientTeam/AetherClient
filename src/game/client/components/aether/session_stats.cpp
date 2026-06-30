#include "session_stats.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

float CAetherSessionStats::PanelScale() const
{
	return std::clamp(g_Config.m_AeSessionStatsScale / 100.0f, 0.5f, 2.0f);
}

CUIRect CAetherSessionStats::PanelRect() const
{
	const float Scale = PanelScale();
	const int Seconds = m_SessionStart > 0 ? (int)((time_get() - m_SessionStart) / time_freq()) : 0;
	const float Pad = 3.0f * Scale;
	const float Font = 6.2f * Scale;
	char aDeaths[32];
	str_format(aDeaths, sizeof(aDeaths), "D: %d", m_Deaths);
	const float DeathWidth = TextRender()->TextWidth(Font, aDeaths);
	float ContentWidth = DeathWidth;
	if(g_Config.m_AeSessionStatsShowTime)
	{
		char aTime[32];
		if(Seconds >= 3600)
			str_format(aTime, sizeof(aTime), "%d:%02d:%02d", Seconds / 3600, (Seconds / 60) % 60, Seconds % 60);
		else
			str_format(aTime, sizeof(aTime), "%02d:%02d", (Seconds / 60) % 60, Seconds % 60);
		const float TimeWidth = TextRender()->TextWidth(Font, aTime);
		ContentWidth += TimeWidth + 8.0f * Scale;
	}
	const float Width = std::max(g_Config.m_AeSessionStatsShowTime ? 70.0f * Scale : 0.0f, ContentWidth + Pad * 2.0f);
	const float Height = 16.0f * Scale;
	return CUIRect(8.0f + g_Config.m_AeSessionStatsOffsetX, g_Config.m_AeSessionStatsOffsetY, Width, Height);
}

CUIRect CAetherSessionStats::ResizeHandleRect() const
{
	const float Scale = PanelScale();
	return CUIRect(m_LastRect.x + m_LastRect.w - 6.0f * Scale, m_LastRect.y + m_LastRect.h - 6.0f * Scale, 6.0f * Scale, 6.0f * Scale);
}

vec2 CAetherSessionStats::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherSessionStats::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	CUIRect Rect = PanelRect();
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, ScreenWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, ScreenHeight - Rect.h));
	g_Config.m_AeSessionStatsOffsetX = round_to_int(Rect.x - 8.0f);
	g_Config.m_AeSessionStatsOffsetY = round_to_int(Rect.y);
}

void CAetherSessionStats::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	const CUIRect Rect = PanelRect();
	if(std::abs((Rect.x + PanelWidth * 0.5f) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeSessionStatsOffsetX = round_to_int(ScreenWidth * 0.5f - PanelWidth * 0.5f - 8.0f);
	if(std::abs((Rect.y + PanelHeight * 0.5f) - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeSessionStatsOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f);
	ClampOffsets();
}

void CAetherSessionStats::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeSessionStatsScale = std::clamp(NewScale, 50, 200);
	const CUIRect Rect = PanelRect();
	g_Config.m_AeSessionStatsOffsetX = round_to_int(Center.x - Rect.w * 0.5f - 8.0f);
	g_Config.m_AeSessionStatsOffsetY = round_to_int(Center.y - Rect.h * 0.5f);
	ApplyCenterSnap(300.0f * Graphics()->ScreenAspect(), 300.0f, Rect.w, Rect.h);
}

void CAetherSessionStats::ResetStats()
{
	m_SessionStart = time_get();
	m_Deaths = 0;
}

void CAetherSessionStats::OnReset()
{
	ResetStats();
}

void CAetherSessionStats::RenderPanel(CUIRect Rect)
{
	const float Scale = PanelScale();
	Graphics()->TextureClear();
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.03f, 0.035f, 0.045f, 0.48f), IGraphics::CORNER_ALL, 1.2f * Scale);

	const float Font = 6.2f * Scale;
	const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.20f);
	char aTime[32];
	char aDeaths[32];
	const float Pad = 3.0f * Scale;
	const float y = Rect.y + (Rect.h - Font) * 0.5f - 0.5f * Scale;
	const float x = Rect.x + Pad;
	const float w = Rect.w - Pad * 2.0f;

	const int Seconds = m_SessionStart > 0 ? (int)((time_get() - m_SessionStart) / time_freq()) : 0;
	if(g_Config.m_AeSessionStatsShowTime)
	{
		if(Seconds >= 3600)
			str_format(aTime, sizeof(aTime), "%d:%02d:%02d", Seconds / 3600, (Seconds / 60) % 60, Seconds % 60);
		else
			str_format(aTime, sizeof(aTime), "%02d:%02d", (Seconds / 60) % 60, Seconds % 60);
	}
	else
		aTime[0] = '\0';

	if(aTime[0] != '\0')
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.96f);
		TextRender()->Text(x, y, Font, aTime, -1.0f);
	}
	TextRender()->TextColor(1.0f, 0.45f, 0.45f, 0.96f);
	str_format(aDeaths, sizeof(aDeaths), "D: %d", m_Deaths);
	const float DeathTextWidth = TextRender()->TextWidth(Font, aDeaths);
	if(aTime[0] != '\0')
		TextRender()->Text(x + w - DeathTextWidth, y, Font, aDeaths, -1.0f);
	else
		TextRender()->Text(Rect.x + Rect.w * 0.5f - DeathTextWidth * 0.5f, y, Font, aDeaths, -1.0f);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->TextOutlineColor(OldOutline);

	if(m_EditorOpen)
	{
		const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		const float CenterX = ScreenWidth * 0.5f;
		const float CenterY = 150.0f;
		const float PanelCenterX = Rect.x + Rect.w * 0.5f;
		const float PanelCenterY = Rect.y + Rect.h * 0.5f;
		Graphics()->DrawRect(CenterX - 0.25f, 0.0f, 0.5f, 300.0f, Theme.WithAlpha(std::abs(PanelCenterX - CenterX) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(0.0f, CenterY - 0.25f, ScreenWidth, 0.5f, Theme.WithAlpha(std::abs(PanelCenterY - CenterY) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(Rect.x - 1.0f, Rect.y - 1.0f, Rect.w + 2.0f, Rect.h + 2.0f, Theme.WithAlpha(0.22f), IGraphics::CORNER_ALL, 1.5f * Scale);
		const CUIRect Handle = ResizeHandleRect();
		Graphics()->DrawRect(Handle.x, Handle.y, Handle.w, Handle.h, ColorRGBA(0.02f, 0.025f, 0.035f, 0.88f), IGraphics::CORNER_ALL, 1.0f * Scale);
		Graphics()->DrawRect(Handle.x + Handle.w - 1.3f * Scale, Handle.y + 1.0f * Scale, 1.0f * Scale, Handle.h - 2.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		Graphics()->DrawRect(Handle.x + 1.0f * Scale, Handle.y + Handle.h - 1.3f * Scale, Handle.w - 2.0f * Scale, 1.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		RenderTools()->RenderCursor(HudMousePos(), 12.0f);
	}
}

void CAetherSessionStats::OnRender()
{
	if(!g_Config.m_AeSessionStats && !m_EditorOpen)
		return;
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi && !m_EditorOpen)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(m_SessionStart == 0)
		m_SessionStart = time_get();

	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, 300.0f);
	m_LastRect = PanelRect();
	RenderPanel(m_LastRect);
}

void CAetherSessionStats::OnMessage(int MsgType, void *pRawMsg)
{
	if(!g_Config.m_AeSessionStats || GameClient()->m_SuppressEvents)
		return;

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		const CNetMsg_Sv_KillMsg *pMsg = static_cast<CNetMsg_Sv_KillMsg *>(pRawMsg);
		if(pMsg->m_Victim == LocalId)
			++m_Deaths;
	}
}

bool CAetherSessionStats::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherSessionStats::OnUpdate()
{
	if(!m_EditorOpen || m_EditorInteraction == EEditorInteraction::IDLE)
		return;
	if(!Input()->NativeMousePressed(1))
	{
		m_EditorInteraction = EEditorInteraction::IDLE;
		return;
	}
	if(m_EditorInteraction == EEditorInteraction::DRAGGING)
	{
		const vec2 Mouse = HudMousePos();
		g_Config.m_AeSessionStatsOffsetX = round_to_int(Mouse.x - m_DragOffset.x - 8.0f);
		g_Config.m_AeSessionStatsOffsetY = round_to_int(Mouse.y - m_DragOffset.y);
		ApplyCenterSnap(300.0f * Graphics()->ScreenAspect(), 300.0f, m_LastRect.w, m_LastRect.h);
	}
	else if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float BaseWidth = 78.0f;
		const float BaseHeight = 18.0f;
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
	}
}

void CAetherSessionStats::CloseEditor()
{
	if(!m_EditorOpen)
		return;
	m_EditorOpen = false;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(true);
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		Input()->MouseModeAbsolute();
	else
		Input()->MouseModeRelative();
}

void CAetherSessionStats::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		CloseEditor();
	if(NewState == IClient::STATE_ONLINE)
		ResetStats();
}

bool CAetherSessionStats::OnInput(const IInput::CEvent &Event)
{
	if(!m_EditorOpen)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		CloseEditor();
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_R)
	{
		g_Config.m_AeSessionStatsOffsetX = 0;
		g_Config.m_AeSessionStatsOffsetY = 76;
		g_Config.m_AeSessionStatsScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(m_LastRect.Inside(Mouse))
		{
			SetScaleKeepingCenter(g_Config.m_AeSessionStatsScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f));
			return true;
		}
	}
	if(Event.m_Key == KEY_MOUSE_1)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const vec2 Mouse = HudMousePos();
			if(ResizeHandleRect().Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::RESIZING;
				m_ResizeCenter = vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f);
				return true;
			}
			if(m_LastRect.Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::DRAGGING;
				m_DragOffset = Mouse - vec2(m_LastRect.x, m_LastRect.y);
				return true;
			}
		}
		if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool WasEditing = m_EditorInteraction != EEditorInteraction::IDLE;
			m_EditorInteraction = EEditorInteraction::IDLE;
			return WasEditing;
		}
	}
	return false;
}

bool CAetherSessionStats::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	if(!m_EditorOpen)
		return false;
	if(m_EditorInteraction == EEditorInteraction::DRAGGING)
	{
		const vec2 Mouse = HudMousePos();
		g_Config.m_AeSessionStatsOffsetX = round_to_int(Mouse.x - m_DragOffset.x - 8.0f);
		g_Config.m_AeSessionStatsOffsetY = round_to_int(Mouse.y - m_DragOffset.y);
		ApplyCenterSnap(300.0f * Graphics()->ScreenAspect(), 300.0f, m_LastRect.w, m_LastRect.h);
		return true;
	}
	if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float BaseWidth = 78.0f;
		const float BaseHeight = 18.0f;
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		return true;
	}
	return false;
}
