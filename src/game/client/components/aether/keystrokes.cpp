#include "keystrokes.h"

#include <base/color.h>
#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/client/ui_rect.h>
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

void CAetherKeystrokes::RenderKey(float x, float y, float w, float h, const char *pLabel, bool Active, float Scale)
{
	const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
	const ColorRGBA Background = Active ? Theme.WithAlpha(0.86f) : ColorRGBA(0.05f, 0.055f, 0.065f, 0.72f);

	Graphics()->TextureClear();
	Graphics()->DrawRect(x, y, w, h, Background, 0, 0.0f);

	if(pLabel && pLabel[0] == '-' && pLabel[1] == '-' && pLabel[2] == '\0')
	{
		const float LineW = w * 0.46f;
		const float LineH = std::max(1.2f, 1.35f * Scale);
		Graphics()->DrawRect(x + w * 0.5f - LineW * 0.5f, y + h * 0.5f - LineH * 0.5f, LineW, LineH, ColorRGBA(1.0f, 1.0f, 1.0f, Active ? 0.95f : 0.72f), IGraphics::CORNER_ALL, LineH * 0.5f);
		return;
	}

	float FontSize = 6.0f * Scale;
	const float MaxTextWidth = std::max(2.0f, w - 3.0f * Scale);
	const float TextWidth = TextRender()->TextWidth(FontSize, pLabel);
	if(TextWidth > MaxTextWidth && TextWidth > 0.0f)
		FontSize = std::max(4.2f * Scale, FontSize * MaxTextWidth / TextWidth);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Active ? 1.0f : 0.78f);
	TextRender()->Text(x + w * 0.5f - TextRender()->TextWidth(FontSize, pLabel) * 0.5f, y + h * 0.5f - FontSize * 0.56f, FontSize, pLabel);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

float CAetherKeystrokes::PanelScale() const
{
	return std::clamp(g_Config.m_AeKeystrokesScale / 100.0f, 0.5f, 2.0f);
}

CUIRect CAetherKeystrokes::PanelRect() const
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const float Scale = PanelScale();
	const float Key = 13.0f * Scale;
	const float Gap = 2.0f * Scale;
	float Width = Key * 2.0f + Gap;
	float Height = Key * 3.0f + Gap * 2.0f;
	if(g_Config.m_AeKeystrokesHorizontal)
	{
		int Keys = 2 + (g_Config.m_AeKeystrokesShowJump ? 1 : 0) + (g_Config.m_AeKeystrokesShowFire ? 1 : 0) + 1;
		Width = Keys * Key + maximum(0, Keys - 1) * Gap;
		Height = Key;
	}
	else
	{
		const int Rows = 2 + (g_Config.m_AeKeystrokesShowJump ? 1 : 0);
		Height = Rows * Key + maximum(0, Rows - 1) * Gap;
	}
	const float X = ScreenWidth * 0.5f - Width * 0.5f + g_Config.m_AeKeystrokesOffsetX;
	const float Y = ScreenHeight - Height - g_Config.m_AeKeystrokesOffsetY;
	return CUIRect(X, Y, Width, Height);
}

CUIRect CAetherKeystrokes::ResizeHandleRect() const
{
	const float Scale = PanelScale();
	return CUIRect(m_LastRect.x + m_LastRect.w - 5.0f * Scale, m_LastRect.y + m_LastRect.h - 5.0f * Scale, 5.0f * Scale, 5.0f * Scale);
}

vec2 CAetherKeystrokes::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherKeystrokes::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	CUIRect Rect = PanelRect();
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, ScreenWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, ScreenHeight - Rect.h));
	g_Config.m_AeKeystrokesOffsetX = round_to_int(Rect.x - (ScreenWidth - Rect.w) * 0.5f);
	g_Config.m_AeKeystrokesOffsetY = round_to_int(ScreenHeight - Rect.y - Rect.h);
}

void CAetherKeystrokes::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	if(std::abs((ScreenWidth * 0.5f + g_Config.m_AeKeystrokesOffsetX) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeKeystrokesOffsetX = 0;
	const float PanelCenterY = ScreenHeight - g_Config.m_AeKeystrokesOffsetY - PanelHeight * 0.5f;
	if(std::abs(PanelCenterY - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeKeystrokesOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f);
	ClampOffsets();
}

void CAetherKeystrokes::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeKeystrokesScale = std::clamp(NewScale, 50, 200);
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const CUIRect Rect = PanelRect();
	g_Config.m_AeKeystrokesOffsetX = round_to_int(Center.x - ScreenWidth * 0.5f);
	g_Config.m_AeKeystrokesOffsetY = round_to_int(ScreenHeight - Center.y - Rect.h * 0.5f);
	ApplyCenterSnap(ScreenWidth, ScreenHeight, Rect.w, Rect.h);
}

void CAetherKeystrokes::OnUpdate()
{
	if(m_EditorOpen && m_EditorInteraction != EEditorInteraction::IDLE)
	{
		if(!Input()->NativeMousePressed(1))
			m_EditorInteraction = EEditorInteraction::IDLE;
		else if(m_EditorInteraction == EEditorInteraction::DRAGGING)
		{
			const vec2 Mouse = HudMousePos();
			const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
			const float ScreenHeight = 300.0f;
			g_Config.m_AeKeystrokesOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
			g_Config.m_AeKeystrokesOffsetY = round_to_int(ScreenHeight - (Mouse.y - m_DragOffset.y) - m_LastRect.h);
			ApplyCenterSnap(ScreenWidth, ScreenHeight, m_LastRect.w, m_LastRect.h);
		}
		else if(m_EditorInteraction == EEditorInteraction::RESIZING)
		{
			const vec2 Mouse = HudMousePos();
			const float Key = 13.0f;
			const float Gap = 2.0f;
			const int Keys = 2 + (g_Config.m_AeKeystrokesShowJump ? 1 : 0) + (g_Config.m_AeKeystrokesShowFire ? 1 : 0) + 1;
			const float BaseWidth = g_Config.m_AeKeystrokesHorizontal ? Keys * Key + maximum(0, Keys - 1) * Gap : 28.0f;
			const float BaseHeight = g_Config.m_AeKeystrokesHorizontal ? Key : 43.0f;
			const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
			const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
			SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		}
	}
}

void CAetherKeystrokes::OnRender()
{
	if(!g_Config.m_AeKeystrokes && !m_EditorOpen)
		return;
	if(g_Config.m_AeFocusMode && !m_EditorOpen)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);

	bool Left = false;
	bool Right = false;
	bool Jump = false;
	bool Fire = false;
	bool Hook = false;

	const bool WantRemote = GameClient()->m_Snap.m_SpecInfo.m_Active &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId > SPEC_FREEVIEW &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < MAX_CLIENTS &&
		GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_Active;
	const int DemoId = GameClient()->m_DemoSpecId > SPEC_FREEVIEW ? GameClient()->m_DemoSpecId : GameClient()->m_Snap.m_LocalClientId;
	const bool WantDemoReplay = Client()->State() == IClient::STATE_DEMOPLAYBACK &&
		!WantRemote &&
		DemoId >= 0 &&
		DemoId < MAX_CLIENTS &&
		GameClient()->m_Snap.m_aCharacters[DemoId].m_Active;

	if(WantRemote || WantDemoReplay)
	{
		const int ClientId = WantRemote ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : DemoId;
		const CNetObj_Character &Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const int GameTick = Client()->GameTick(g_Config.m_ClDummy);
		Left = Char.m_Direction < 0;
		Right = Char.m_Direction > 0;
		Fire = absolute(GameTick - Char.m_AttackTick) <= 3;
		Hook = Char.m_HookState > HOOK_IDLE;
	}
	else
	{
		const int Dummy = g_Config.m_ClDummy;
		const CNetObj_PlayerInput &InputData = GameClient()->m_Controls.m_aInputData[Dummy];
		Left = InputData.m_Direction < 0 || GameClient()->m_Controls.m_aInputDirectionLeft[Dummy] != 0;
		Right = InputData.m_Direction > 0 || GameClient()->m_Controls.m_aInputDirectionRight[Dummy] != 0;
		Jump = InputData.m_Jump != 0;
		Fire = Input()->KeyIsPressed(KEY_MOUSE_1) || (InputData.m_Fire & 1) != 0;
		Hook = Input()->KeyIsPressed(KEY_MOUSE_2) || InputData.m_Hook != 0;
	}

	const float Scale = std::clamp(g_Config.m_AeKeystrokesScale / 100.0f, 0.5f, 2.0f);
	const float Key = 13.0f * Scale;
	const float Gap = 2.0f * Scale;
	m_LastRect = PanelRect();

	const char *pJumpLabel = g_Config.m_AeKeystrokesJumpLabel == 0 ? "Jump" : (g_Config.m_AeKeystrokesJumpLabel == 2 ? "Space" : "--");
	if(g_Config.m_AeKeystrokesHorizontal)
	{
		float x = m_LastRect.x;
		RenderKey(x, m_LastRect.y, Key, Key, "<", Left, Scale);
		x += Key + Gap;
		RenderKey(x, m_LastRect.y, Key, Key, ">", Right, Scale);
		x += Key + Gap;
		if(g_Config.m_AeKeystrokesShowJump)
		{
			RenderKey(x, m_LastRect.y, Key, Key, pJumpLabel, Jump, Scale);
			x += Key + Gap;
		}
		if(g_Config.m_AeKeystrokesShowFire)
		{
			RenderKey(x, m_LastRect.y, Key, Key, "M1", Fire, Scale);
			x += Key + Gap;
		}
		RenderKey(x, m_LastRect.y, Key, Key, "M2", Hook, Scale);
	}
	else
	{
		float y = m_LastRect.y;
		RenderKey(m_LastRect.x, y, Key, Key, "A", Left, Scale);
		RenderKey(m_LastRect.x + Key + Gap, y, Key, Key, "D", Right, Scale);
		y += Key + Gap;
		if(g_Config.m_AeKeystrokesShowJump)
		{
			RenderKey(m_LastRect.x, y, m_LastRect.w, Key, pJumpLabel, Jump, Scale);
			y += Key + Gap;
		}
		if(g_Config.m_AeKeystrokesShowFire)
			RenderKey(m_LastRect.x, y, Key, Key, "M1", Fire, Scale);
		RenderKey(m_LastRect.x + Key + Gap, y, Key, Key, "M2", Hook, Scale);
	}

	if(m_EditorOpen)
	{
		const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const float CenterX = ScreenWidth * 0.5f;
		const float CenterY = ScreenHeight * 0.5f;
		const float PanelCenterX = m_LastRect.x + m_LastRect.w * 0.5f;
		const float PanelCenterY = m_LastRect.y + m_LastRect.h * 0.5f;
		Graphics()->DrawRect(CenterX - 0.25f, 0.0f, 0.5f, ScreenHeight, Theme.WithAlpha(std::abs(PanelCenterX - CenterX) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(0.0f, CenterY - 0.25f, ScreenWidth, 0.5f, Theme.WithAlpha(std::abs(PanelCenterY - CenterY) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(m_LastRect.x - 1.0f, m_LastRect.y - 1.0f, m_LastRect.w + 2.0f, m_LastRect.h + 2.0f, Theme.WithAlpha(0.22f), 0, 0.0f);
		const CUIRect Handle = ResizeHandleRect();
		Graphics()->DrawRect(Handle.x, Handle.y, Handle.w, Handle.h, ColorRGBA(0.02f, 0.025f, 0.035f, 0.88f), IGraphics::CORNER_ALL, 1.0f * Scale);
		Graphics()->DrawRect(Handle.x + Handle.w - 1.2f * Scale, Handle.y + 1.0f * Scale, 0.9f * Scale, Handle.h - 2.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		Graphics()->DrawRect(Handle.x + 1.0f * Scale, Handle.y + Handle.h - 1.2f * Scale, Handle.w - 2.0f * Scale, 0.9f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		RenderTools()->RenderCursor(HudMousePos(), 12.0f);
	}
}

bool CAetherKeystrokes::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherKeystrokes::CloseEditor()
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

void CAetherKeystrokes::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		CloseEditor();
}

bool CAetherKeystrokes::OnInput(const IInput::CEvent &Event)
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
		g_Config.m_AeKeystrokesOffsetX = 0;
		g_Config.m_AeKeystrokesOffsetY = 86;
		g_Config.m_AeKeystrokesScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(m_LastRect.Inside(Mouse))
		{
			const vec2 Center(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f);
			SetScaleKeepingCenter(g_Config.m_AeKeystrokesScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), Center);
			return true;
		}
		return false;
	}
	if(Event.m_Key == KEY_MOUSE_1)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const vec2 Mouse = HudMousePos();
			const CUIRect Handle = ResizeHandleRect();
			if(Handle.Inside(Mouse))
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

bool CAetherKeystrokes::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	return m_EditorOpen && m_EditorInteraction != EEditorInteraction::IDLE;
}
