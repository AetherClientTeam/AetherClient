#include "input_visualizer.h"

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

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float BASE_PANEL_W = 200.0f;
constexpr float BASE_PANEL_H = 72.0f;
constexpr double VIZ_HISTORY_SECONDS = 45.0;

void DrawSegmentH(IGraphics *pGraphics, float Left, float Width, float y, float Height, double Now, double Start, double End, double Flow, const ColorRGBA &Color, float Radius)
{
	const double AgeStart = Now - Start;
	const double AgeEnd = Now - End;
	if(AgeStart > VIZ_HISTORY_SECONDS)
		return;
	const float x0 = Left + Width - (float)(AgeStart * Flow);
	const float x1 = Left + Width - (float)(AgeEnd * Flow);
	const float SegmentLeft = std::clamp(std::min(x0, x1), Left, Left + Width);
	const float SegmentRight = std::clamp(std::max(x0, x1), Left, Left + Width);
	if(SegmentRight <= SegmentLeft)
		return;
	pGraphics->DrawRect(SegmentLeft, y + 1.5f, SegmentRight - SegmentLeft, Height - 3.0f, Color, IGraphics::CORNER_ALL, Radius);
}

void DrawSegmentV(IGraphics *pGraphics, float x, float Width, float Top, float Height, double Now, double Start, double End, double Flow, const ColorRGBA &Color, float Radius)
{
	const double AgeStart = Now - Start;
	const double AgeEnd = Now - End;
	if(AgeStart > VIZ_HISTORY_SECONDS)
		return;
	const float y0 = Top + Height - (float)(AgeStart * Flow);
	const float y1 = Top + Height - (float)(AgeEnd * Flow);
	const float SegmentTop = std::clamp(std::min(y0, y1), Top, Top + Height);
	const float SegmentBottom = std::clamp(std::max(y0, y1), Top, Top + Height);
	if(SegmentBottom <= SegmentTop)
		return;
	pGraphics->DrawRect(x + 1.5f, SegmentTop, Width - 3.0f, SegmentBottom - SegmentTop, Color, IGraphics::CORNER_ALL, Radius);
}

void DrawHookMarker(IGraphics *pGraphics, float x, float Top, float Bottom, float Opacity)
{
	if(Bottom <= Top)
		return;
	const float LineX = std::floor(x + 0.5f) + 0.5f;
	pGraphics->DrawRect(LineX, Top, 1.0f, Bottom - Top, ColorRGBA(1.0f, 0.12f, 0.12f, 0.95f * Opacity), IGraphics::CORNER_NONE, 0.0f);
}
}

float CAetherInputVisualizer::PanelScale() const
{
	return std::clamp(g_Config.m_AeInputVisualizerScale / 100.0f, 0.5f, 2.0f);
}

CUIRect CAetherInputVisualizer::PanelRect() const
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const float Scale = PanelScale();
	const float LenScale = std::clamp(g_Config.m_AeInputVisualizerLength / 100.0f, 0.5f, 2.5f);
	float Width = BASE_PANEL_W * Scale * (g_Config.m_AeInputVisualizerVertical ? 1.0f : LenScale);
	float Height = BASE_PANEL_H * Scale * (g_Config.m_AeInputVisualizerVertical ? LenScale : 1.0f);
	if(g_Config.m_AeInputVisualizerVertical)
		std::swap(Width, Height);
	const float X = ScreenWidth * 0.5f - Width * 0.5f + g_Config.m_AeInputVisualizerOffsetX;
	const float Y = ScreenHeight - Height - g_Config.m_AeInputVisualizerOffsetY;
	return CUIRect(X, Y, Width, Height);
}

CUIRect CAetherInputVisualizer::ResizeHandleRect() const
{
	const float Scale = PanelScale();
	return CUIRect(m_LastRect.x + m_LastRect.w - 6.0f * Scale, m_LastRect.y + m_LastRect.h - 6.0f * Scale, 6.0f * Scale, 6.0f * Scale);
}

vec2 CAetherInputVisualizer::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherInputVisualizer::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	CUIRect Rect = PanelRect();
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, ScreenWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, ScreenHeight - Rect.h));
	g_Config.m_AeInputVisualizerOffsetX = round_to_int(Rect.x - (ScreenWidth - Rect.w) * 0.5f);
	g_Config.m_AeInputVisualizerOffsetY = round_to_int(ScreenHeight - Rect.y - Rect.h);
}

void CAetherInputVisualizer::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	if(std::abs((ScreenWidth * 0.5f + g_Config.m_AeInputVisualizerOffsetX) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeInputVisualizerOffsetX = 0;
	const float PanelCenterY = ScreenHeight - g_Config.m_AeInputVisualizerOffsetY - PanelHeight * 0.5f;
	if(std::abs(PanelCenterY - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeInputVisualizerOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f);
	ClampOffsets();
}

void CAetherInputVisualizer::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeInputVisualizerScale = std::clamp(NewScale, 50, 200);
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const CUIRect Rect = PanelRect();
	g_Config.m_AeInputVisualizerOffsetX = round_to_int(Center.x - ScreenWidth * 0.5f);
	g_Config.m_AeInputVisualizerOffsetY = round_to_int(ScreenHeight - Center.y - Rect.h * 0.5f);
	ApplyCenterSnap(ScreenWidth, ScreenHeight, Rect.w, Rect.h);
}

void CAetherInputVisualizer::OnUpdate()
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
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		const float ScreenHeight = 300.0f;
		g_Config.m_AeInputVisualizerOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeInputVisualizerOffsetY = round_to_int(ScreenHeight - (Mouse.y - m_DragOffset.y) - m_LastRect.h);
		ApplyCenterSnap(ScreenWidth, ScreenHeight, m_LastRect.w, m_LastRect.h);
	}
	else if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float LenScale = std::clamp(g_Config.m_AeInputVisualizerLength / 100.0f, 0.5f, 2.5f);
		float BaseWidth = BASE_PANEL_W * (g_Config.m_AeInputVisualizerVertical ? 1.0f : LenScale);
		float BaseHeight = BASE_PANEL_H * (g_Config.m_AeInputVisualizerVertical ? LenScale : 1.0f);
		if(g_Config.m_AeInputVisualizerVertical)
			std::swap(BaseWidth, BaseHeight);
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
	}
}

void CAetherInputVisualizer::ClearLaneHistory()
{
	m_vSegments.clear();
	m_aWasDown.fill(false);
	m_aLaneActive.fill(false);
}

bool CAetherInputVisualizer::WantsRemoteInput() const
{
	return g_Config.m_AeInputVisualizerSpectatedInput &&
		GameClient()->m_Snap.m_SpecInfo.m_Active &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId > SPEC_FREEVIEW &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < MAX_CLIENTS;
}

bool CAetherInputVisualizer::WantsDemoReplayInput() const
{
	const int ClientId = GameClient()->m_DemoSpecId > SPEC_FREEVIEW ? GameClient()->m_DemoSpecId : GameClient()->m_Snap.m_LocalClientId;
	return Client()->State() == IClient::STATE_DEMOPLAYBACK &&
		!WantsRemoteInput() &&
		ClientId >= 0 &&
		ClientId < MAX_CLIENTS &&
		GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
}

double CAetherInputVisualizer::TimelineNow(bool WantRemote, bool WantDemoReplay) const
{
	const bool UseGameTimeline = Client()->State() == IClient::STATE_DEMOPLAYBACK && (WantRemote || WantDemoReplay);
	if(UseGameTimeline)
	{
		const double TickRate = (double)maximum(1, Client()->GameTickSpeed());
		return (Client()->GameTick(g_Config.m_ClDummy) + Client()->IntraGameTick(g_Config.m_ClDummy)) / TickRate;
	}
	return time_get() / (double)time_freq();
}

void CAetherInputVisualizer::BuildDownFromLocal(std::array<bool, MAX_LANES> &aDown) const
{
	const int Dummy = g_Config.m_ClDummy;
	const CNetObj_PlayerInput &InputData = GameClient()->m_Controls.m_aInputData[Dummy];
	aDown[(int)ELane::LEFT] = InputData.m_Direction < 0 || GameClient()->m_Controls.m_aInputDirectionLeft[Dummy] != 0;
	aDown[(int)ELane::RIGHT] = InputData.m_Direction > 0 || GameClient()->m_Controls.m_aInputDirectionRight[Dummy] != 0;
	aDown[(int)ELane::JUMP] = InputData.m_Jump != 0;
	aDown[(int)ELane::FIRE] = Input()->KeyIsPressed(KEY_MOUSE_1) || (InputData.m_Fire & 1) != 0;
	aDown[(int)ELane::HOOK] = Input()->KeyIsPressed(KEY_MOUSE_2) || InputData.m_Hook != 0;
}

void CAetherInputVisualizer::BuildDownFromCharacter(const CNetObj_Character *pChar, int GameTick, std::array<bool, MAX_LANES> &aDown) const
{
	if(!pChar)
		return;
	aDown[(int)ELane::LEFT] = pChar->m_Direction < 0;
	aDown[(int)ELane::RIGHT] = pChar->m_Direction > 0;
	aDown[(int)ELane::JUMP] = false;
	aDown[(int)ELane::FIRE] = absolute(GameTick - pChar->m_AttackTick) <= 3;
	aDown[(int)ELane::HOOK] = pChar->m_HookState > HOOK_IDLE;
}

void CAetherInputVisualizer::UpdateInputState(double Now, bool WantRemote, bool WantDemoReplay)
{
	std::array<bool, MAX_LANES> aDown = {};

	const int DemoId = GameClient()->m_DemoSpecId > SPEC_FREEVIEW ? GameClient()->m_DemoSpecId : GameClient()->m_Snap.m_LocalClientId;
	const int SourceKey = (WantRemote ? 1 : 0) * 100000 +
		(WantRemote ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : 0) * 16 +
		(WantDemoReplay ? 1 : 0) * 500000 +
		(WantDemoReplay ? DemoId : 0) * 4 +
		(g_Config.m_AeInputVisualizerSpectatedInput & 1) +
		(g_Config.m_AeInputVisualizerMouse & 1) * 2 +
		(g_Config.m_AeInputVisualizerShowFire & 1) * 4 +
		(g_Config.m_AeInputVisualizerShowJump & 1) * 8;

	if(SourceKey != m_SourceKey)
	{
		m_SourceKey = SourceKey;
		ClearLaneHistory();
	}

	if(WantRemote)
	{
		const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			BuildDownFromCharacter(&GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur, Client()->GameTick(g_Config.m_ClDummy), aDown);
	}
	else if(WantDemoReplay)
	{
		if(DemoId >= 0 && DemoId < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[DemoId].m_Active)
			BuildDownFromCharacter(&GameClient()->m_Snap.m_aCharacters[DemoId].m_Cur, Client()->GameTick(g_Config.m_ClDummy), aDown);
	}
	else if(g_Config.m_AeInputVisualizerShowLocal)
		BuildDownFromLocal(aDown);

	if(!g_Config.m_AeInputVisualizerShowJump)
		aDown[(int)ELane::JUMP] = false;
	if(!g_Config.m_AeInputVisualizerMouse)
	{
		aDown[(int)ELane::FIRE] = false;
		aDown[(int)ELane::HOOK] = false;
	}
	if(!g_Config.m_AeInputVisualizerShowFire)
		aDown[(int)ELane::FIRE] = false;

	for(int i = 0; i < MAX_LANES; ++i)
	{
		if(aDown[i] && !m_aWasDown[i])
		{
			m_aLaneActive[i] = true;
			m_aHoldStart[i] = Now;
		}
		else if(!aDown[i] && m_aWasDown[i] && m_aLaneActive[i])
		{
			const double Start = m_aHoldStart[i];
			m_aLaneActive[i] = false;
			if(Now > Start && (Now - Start) > 1e-6)
			{
				m_vSegments.push_back({Start, Now, (ELane)i});
				while(m_vSegments.size() > MAX_SEGMENTS)
					m_vSegments.pop_front();
			}
		}
		m_aWasDown[i] = aDown[i];
	}

	while(!m_vSegments.empty() && Now - m_vSegments.front().m_End > HISTORY_SECONDS + 1.0)
		m_vSegments.pop_front();
}

ColorRGBA CAetherInputVisualizer::LaneColor(ELane Lane) const
{
	unsigned int ColorValue = g_Config.m_AeInputVisualizerColorLeft;
	switch(Lane)
	{
	case ELane::LEFT: ColorValue = g_Config.m_AeInputVisualizerColorLeft; break;
	case ELane::RIGHT: ColorValue = g_Config.m_AeInputVisualizerColorRight; break;
	case ELane::JUMP: ColorValue = g_Config.m_AeInputVisualizerColorJump; break;
	case ELane::FIRE: ColorValue = g_Config.m_AeInputVisualizerColorFire; break;
	case ELane::HOOK: ColorValue = g_Config.m_AeInputVisualizerColorHook; break;
	case ELane::COUNT: break;
	}
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(ColorValue, true));
	if(Color.a <= 0.02f)
		Color.a = 1.0f;
	Color.a *= std::clamp(g_Config.m_AeInputVisualizerOpacity / 100.0f, 0.1f, 1.0f);
	return Color;
}

const char *CAetherInputVisualizer::LaneLabel(ELane Lane) const
{
	switch(Lane)
	{
	case ELane::LEFT: return "A";
	case ELane::RIGHT: return "D";
	case ELane::JUMP: return "J";
	case ELane::FIRE: return "M1";
	case ELane::HOOK: return "M2";
	case ELane::COUNT: return "";
	}
	return "";
}

std::array<CAetherInputVisualizer::ELane, CAetherInputVisualizer::MAX_LANES> CAetherInputVisualizer::VisibleLanes(int &Count) const
{
	std::array<ELane, MAX_LANES> aLanes = {};
	Count = 0;
	aLanes[Count++] = ELane::LEFT;
	aLanes[Count++] = ELane::RIGHT;
	if(g_Config.m_AeInputVisualizerShowJump)
		aLanes[Count++] = ELane::JUMP;
	if(g_Config.m_AeInputVisualizerMouse)
	{
		if(g_Config.m_AeInputVisualizerShowFire)
			aLanes[Count++] = ELane::FIRE;
		aLanes[Count++] = ELane::HOOK;
	}
	return aLanes;
}

void CAetherInputVisualizer::RenderHorizontal(CUIRect Inner, double Now)
{
	int LaneCount = 0;
	const auto aLanes = VisibleLanes(LaneCount);
	if(LaneCount <= 0)
		return;

	const float Scale = PanelScale();
	const float Opacity = std::clamp(g_Config.m_AeInputVisualizerOpacity / 100.0f, 0.1f, 1.0f);
	const float ThickScale = std::clamp(g_Config.m_AeInputVisualizerThickness / 100.0f, 0.4f, 2.0f);
	const float LaneGap = 1.0f * Scale;
	const float BaseLaneHeight = (Inner.h - (LaneCount - 1) * LaneGap) / LaneCount;
	float LaneHeight = maximum(4.0f * Scale, BaseLaneHeight * ThickScale);
	if(LaneCount * LaneHeight + (LaneCount - 1) * LaneGap > Inner.h)
		LaneHeight = maximum(4.0f * Scale, (Inner.h - (LaneCount - 1) * LaneGap) / LaneCount);
	const float LabelWidth = g_Config.m_AeInputVisualizerLabels ? 14.0f * Scale : 0.0f;
	const float GraphLeft = Inner.x + LabelWidth + (g_Config.m_AeInputVisualizerBackground ? 2.0f * Scale : 0.0f);
	const float GraphWidth = maximum(10.0f, Inner.x + Inner.w - GraphLeft - 2.0f * Scale);
	const double Flow = std::clamp(g_Config.m_AeInputVisualizerFlow, 40, 1000) * GraphWidth / 420.0;
	const float LaneRound = g_Config.m_AeInputVisualizerSharpCorners ? 0.75f : 2.0f * Scale;
	const float SegmentRound = g_Config.m_AeInputVisualizerSharpCorners ? 0.5f : 1.5f * Scale;

	for(int k = 0; k < LaneCount; ++k)
	{
		const ELane Lane = aLanes[k];
		const float y = Inner.y + k * (LaneHeight + LaneGap);
		if(g_Config.m_AeInputVisualizerLabels)
			TextRender()->Text(Inner.x, y + 0.5f * Scale, 5.5f * Scale, LaneLabel(Lane));
		if(g_Config.m_AeInputVisualizerBackground)
			Graphics()->DrawRect(GraphLeft, y, GraphWidth, LaneHeight, ColorRGBA(0.10f, 0.11f, 0.13f, 0.82f * Opacity), IGraphics::CORNER_ALL, LaneRound);

		const ColorRGBA Color = LaneColor(Lane);
		for(const SSegment &Segment : m_vSegments)
			if(Segment.m_Lane == Lane)
				DrawSegmentH(Graphics(), GraphLeft, GraphWidth, y, LaneHeight, Now, Segment.m_Start, Segment.m_End, Flow, Color, SegmentRound);
		if(m_aLaneActive[(int)Lane])
			DrawSegmentH(Graphics(), GraphLeft, GraphWidth, y, LaneHeight, Now, m_aHoldStart[(int)Lane], Now, Flow, Color, SegmentRound);
	}

	if(g_Config.m_AeInputVisualizerHookMarkers)
	{
		const float Top = Inner.y;
		const float Bottom = Inner.y + LaneCount * LaneHeight + (LaneCount - 1) * LaneGap;
		auto DrawEdge = [&](double Time) {
			const double Age = Now - Time;
			if(Age > HISTORY_SECONDS)
				return;
			const float x = std::clamp(GraphLeft + GraphWidth - (float)(Age * Flow), GraphLeft, GraphLeft + GraphWidth);
			DrawHookMarker(Graphics(), x, Top, Bottom, Opacity);
		};
		for(const SSegment &Segment : m_vSegments)
			if(Segment.m_Lane == ELane::HOOK)
			{
				DrawEdge(Segment.m_Start);
				DrawEdge(Segment.m_End);
			}
		if(m_aLaneActive[(int)ELane::HOOK])
			DrawEdge(m_aHoldStart[(int)ELane::HOOK]);
	}
}

void CAetherInputVisualizer::RenderVertical(CUIRect Inner, double Now)
{
	int LaneCount = 0;
	const auto aLanes = VisibleLanes(LaneCount);
	if(LaneCount <= 0)
		return;

	const float Scale = PanelScale();
	const float Opacity = std::clamp(g_Config.m_AeInputVisualizerOpacity / 100.0f, 0.1f, 1.0f);
	const float ThickScale = std::clamp(g_Config.m_AeInputVisualizerThickness / 100.0f, 0.4f, 2.0f);
	const float LabelHeight = g_Config.m_AeInputVisualizerLabels ? 10.0f * Scale : 0.0f;
	const float GraphTop = Inner.y + LabelHeight + (g_Config.m_AeInputVisualizerBackground ? 2.0f * Scale : 0.0f);
	const float GraphHeight = maximum(10.0f, Inner.y + Inner.h - GraphTop - 2.0f * Scale);
	const float LaneGap = 1.0f * Scale;
	const float BaseLaneWidth = (Inner.w - (LaneCount - 1) * LaneGap) / LaneCount;
	float LaneWidth = maximum(4.0f * Scale, BaseLaneWidth * ThickScale);
	if(LaneCount * LaneWidth + (LaneCount - 1) * LaneGap > Inner.w)
		LaneWidth = maximum(4.0f * Scale, (Inner.w - (LaneCount - 1) * LaneGap) / LaneCount);
	const double Flow = std::clamp(g_Config.m_AeInputVisualizerFlow, 40, 1000) * GraphHeight / 420.0;
	const float LaneRound = g_Config.m_AeInputVisualizerSharpCorners ? 0.75f : 2.0f * Scale;
	const float SegmentRound = g_Config.m_AeInputVisualizerSharpCorners ? 0.5f : 1.5f * Scale;

	for(int k = 0; k < LaneCount; ++k)
	{
		const ELane Lane = aLanes[k];
		const float x = Inner.x + k * (LaneWidth + LaneGap);
		if(g_Config.m_AeInputVisualizerLabels)
			TextRender()->Text(x + LaneWidth * 0.5f - TextRender()->TextWidth(5.5f * Scale, LaneLabel(Lane)) * 0.5f, Inner.y, 5.5f * Scale, LaneLabel(Lane));
		if(g_Config.m_AeInputVisualizerBackground)
			Graphics()->DrawRect(x, GraphTop, LaneWidth, GraphHeight, ColorRGBA(0.10f, 0.11f, 0.13f, 0.82f * Opacity), IGraphics::CORNER_ALL, LaneRound);

		const ColorRGBA Color = LaneColor(Lane);
		for(const SSegment &Segment : m_vSegments)
			if(Segment.m_Lane == Lane)
				DrawSegmentV(Graphics(), x, LaneWidth, GraphTop, GraphHeight, Now, Segment.m_Start, Segment.m_End, Flow, Color, SegmentRound);
		if(m_aLaneActive[(int)Lane])
			DrawSegmentV(Graphics(), x, LaneWidth, GraphTop, GraphHeight, Now, m_aHoldStart[(int)Lane], Now, Flow, Color, SegmentRound);
	}
}

void CAetherInputVisualizer::RenderInternal(bool ForcePreview)
{
	if(!ForcePreview && !g_Config.m_AeInputVisualizer)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const bool WantRemote = WantsRemoteInput();
	const bool WantDemoReplay = WantsDemoReplayInput();
	const double Now = ForcePreview ? time_get() / (double)time_freq() : TimelineNow(WantRemote, WantDemoReplay);
	if(!ForcePreview)
		UpdateInputState(Now, WantRemote, WantDemoReplay);
	else if(m_vSegments.empty())
	{
		m_vSegments.push_back({Now - 1.1, Now - 0.70, ELane::LEFT});
		m_vSegments.push_back({Now - 0.80, Now - 0.25, ELane::RIGHT});
		m_vSegments.push_back({Now - 0.55, Now - 0.40, ELane::JUMP});
		m_vSegments.push_back({Now - 0.35, Now - 0.18, ELane::FIRE});
		m_vSegments.push_back({Now - 0.65, Now - 0.10, ELane::HOOK});
	}

	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, 300.0f);
	Graphics()->TextureClear();
	m_LastRect = PanelRect();

	const float Scale = PanelScale();
	const float Opacity = std::clamp(g_Config.m_AeInputVisualizerOpacity / 100.0f, 0.1f, 1.0f);
	const float Radius = g_Config.m_AeInputVisualizerSharpCorners ? 0.0f : 4.0f * Scale;
	if(g_Config.m_AeInputVisualizerBackground)
		Graphics()->DrawRect(m_LastRect.x, m_LastRect.y, m_LastRect.w, m_LastRect.h, ColorRGBA(0.045f, 0.048f, 0.060f, 0.72f * Opacity), IGraphics::CORNER_ALL, Radius);

	CUIRect Inner = m_LastRect;
	Inner.Margin(3.0f * Scale, &Inner);
	if(g_Config.m_AeInputVisualizerVertical)
		RenderVertical(Inner, Now);
	else
		RenderHorizontal(Inner, Now);

	if(m_EditorOpen)
	{
		const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const float CenterX = ScreenWidth * 0.5f;
		const float CenterY = 150.0f;
		const float PanelCenterX = m_LastRect.x + m_LastRect.w * 0.5f;
		const float PanelCenterY = m_LastRect.y + m_LastRect.h * 0.5f;
		Graphics()->DrawRect(CenterX - 0.25f, 0.0f, 0.5f, 300.0f, Theme.WithAlpha(std::abs(PanelCenterX - CenterX) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(0.0f, CenterY - 0.25f, ScreenWidth, 0.5f, Theme.WithAlpha(std::abs(PanelCenterY - CenterY) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(m_LastRect.x - 1.0f, m_LastRect.y - 1.0f, m_LastRect.w + 2.0f, m_LastRect.h + 2.0f, Theme.WithAlpha(0.22f), IGraphics::CORNER_ALL, 5.0f * Scale);
		const CUIRect Handle = ResizeHandleRect();
		Graphics()->DrawRect(Handle.x, Handle.y, Handle.w, Handle.h, ColorRGBA(0.02f, 0.025f, 0.035f, 0.88f), IGraphics::CORNER_ALL, 1.2f * Scale);
		Graphics()->DrawRect(Handle.x + Handle.w - 1.3f * Scale, Handle.y + 1.0f * Scale, 1.0f * Scale, Handle.h - 2.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		Graphics()->DrawRect(Handle.x + 1.0f * Scale, Handle.y + Handle.h - 1.3f * Scale, Handle.w - 2.0f * Scale, 1.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		RenderTools()->RenderCursor(HudMousePos(), 12.0f);
	}
}

void CAetherInputVisualizer::OnRender()
{
	if(!g_Config.m_AeInputVisualizer && !m_EditorOpen)
		return;
	if(g_Config.m_AeFocusMode && !m_EditorOpen)
		return;
	if(GameClient()->m_Menus.IsActive() && !m_EditorOpen)
		return;
	RenderInternal(m_EditorOpen);
}

bool CAetherInputVisualizer::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherInputVisualizer::CloseEditor()
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

void CAetherInputVisualizer::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
	{
		CloseEditor();
		ClearLaneHistory();
	}
}

bool CAetherInputVisualizer::OnInput(const IInput::CEvent &Event)
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
		g_Config.m_AeInputVisualizerOffsetX = 0;
		g_Config.m_AeInputVisualizerOffsetY = 132;
		g_Config.m_AeInputVisualizerScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(m_LastRect.Inside(Mouse))
		{
			SetScaleKeepingCenter(g_Config.m_AeInputVisualizerScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f));
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

bool CAetherInputVisualizer::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	if(!m_EditorOpen)
		return false;
	if(m_EditorInteraction == EEditorInteraction::DRAGGING)
	{
		const vec2 Mouse = HudMousePos();
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		const float ScreenHeight = 300.0f;
		g_Config.m_AeInputVisualizerOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeInputVisualizerOffsetY = round_to_int(ScreenHeight - (Mouse.y - m_DragOffset.y) - m_LastRect.h);
		ApplyCenterSnap(ScreenWidth, ScreenHeight, m_LastRect.w, m_LastRect.h);
		return true;
	}
	if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float LenScale = std::clamp(g_Config.m_AeInputVisualizerLength / 100.0f, 0.5f, 2.5f);
		float BaseWidth = BASE_PANEL_W * (g_Config.m_AeInputVisualizerVertical ? 1.0f : LenScale);
		float BaseHeight = BASE_PANEL_H * (g_Config.m_AeInputVisualizerVertical ? LenScale : 1.0f);
		if(g_Config.m_AeInputVisualizerVertical)
			std::swap(BaseWidth, BaseHeight);
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		return true;
	}
	return false;
}
