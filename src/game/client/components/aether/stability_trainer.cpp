#include "stability_trainer.h"

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
#include <game/client/prediction/entities/character.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int AETHER_STABILITY_AVERAGE_TICKS = 12;
constexpr int AETHER_STABILITY_BAR_GLIDE = 8;
constexpr int AETHER_STABILITY_MIN_SPEED = 600;
constexpr int AETHER_STABILITY_VELOCITY_SCALE = 800;
} // namespace

bool CAetherStabilityTrainer::IsLocalClientId(int ClientId) const
{
	if(ClientId == GameClient()->m_aLocalIds[0])
		return true;
	return Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[1];
}

int CAetherStabilityTrainer::ResolveTrackId() const
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId > SPEC_FREEVIEW && GameClient()->m_DemoSpecId < MAX_CLIENTS)
		return GameClient()->m_DemoSpecId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
		if(SpectatorId > SPEC_FREEVIEW && SpectatorId < MAX_CLIENTS)
			return SpectatorId;
	}
	return GameClient()->m_aLocalIds[g_Config.m_ClDummy];
}

vec2 CAetherStabilityTrainer::ResolveVelocity(int TrackId) const
{
	CCharacter *pChar = GameClient()->m_GameWorld.GetCharacterById(TrackId);
	if(pChar)
		return pChar->Core()->m_Vel;
	const auto &SnapChar = GameClient()->m_Snap.m_aCharacters[TrackId];
	return vec2(SnapChar.m_Cur.m_VelX / 256.0f, SnapChar.m_Cur.m_VelY / 256.0f);
}

void CAetherStabilityTrainer::ResetState()
{
	m_LastTrackId = -1;
	m_RollingQuality = 0.0f;
	m_SmoothBar = 0.5f;
	m_WarnAmbiguous = false;
}

float CAetherStabilityTrainer::BarTarget(vec2 Vel, int TrackId, bool *pWarnOut) const
{
	const float Speed = length(Vel);
	const float MinSpeed = AETHER_STABILITY_MIN_SPEED / 100.0f;
	float ActiveWeight = 1.0f;
	if(MinSpeed > 0.001f && Speed < MinSpeed)
		ActiveWeight = Speed / MinSpeed;

	bool Ambiguous = false;
	if(IsLocalClientId(TrackId))
	{
		const int Dummy = g_Config.m_ClDummy;
		Ambiguous = GameClient()->m_Controls.m_aInputDirectionLeft[Dummy] != 0 && GameClient()->m_Controls.m_aInputDirectionRight[Dummy] != 0;
	}
	if(pWarnOut)
		*pWarnOut = Ambiguous;

	const float Reference = maximum(0.05f, AETHER_STABILITY_VELOCITY_SCALE / 100.0f);
	const float Instant = 0.5f + 0.5f * std::clamp(Vel.x / Reference, -1.0f, 1.0f);
	return Instant * ActiveWeight + 0.5f * (1.0f - ActiveWeight);
}

float CAetherStabilityTrainer::Quality(vec2 Vel, int TrackId, bool *pWarnOut) const
{
	const float Speed = length(Vel);
	const float MinSpeed = AETHER_STABILITY_MIN_SPEED / 100.0f;
	float ActiveWeight = 1.0f;
	if(MinSpeed > 0.001f && Speed < MinSpeed)
		ActiveWeight = Speed / MinSpeed;

	bool Ambiguous = false;
	if(IsLocalClientId(TrackId))
	{
		const int Dummy = g_Config.m_ClDummy;
		Ambiguous = GameClient()->m_Controls.m_aInputDirectionLeft[Dummy] != 0 && GameClient()->m_Controls.m_aInputDirectionRight[Dummy] != 0;
	}
	if(pWarnOut)
		*pWarnOut = Ambiguous;

	const float Reference = maximum(0.05f, AETHER_STABILITY_VELOCITY_SCALE / 100.0f);
	const float Instant = 1.0f - std::clamp(std::abs(Vel.x) / Reference, 0.0f, 1.0f);
	const float Blended = std::clamp(Instant * ActiveWeight + 1.0f * (1.0f - ActiveWeight), 0.0f, 1.0f);
	return Ambiguous ? Blended * 0.25f : Blended;
}

ColorRGBA CAetherStabilityTrainer::QualityColor(float Quality) const
{
	Quality = std::clamp(Quality, 0.0f, 1.0f);
	auto Lerp = [](ColorRGBA a, ColorRGBA b, float u) {
		return ColorRGBA(
			a.r + (b.r - a.r) * u,
			a.g + (b.g - a.g) * u,
			a.b + (b.b - a.b) * u,
			a.a + (b.a - a.a) * u);
	};
	const ColorRGBA Cyan(0.22f, 0.94f, 1.0f, 0.98f);
	const ColorRGBA Green(0.22f, 0.86f, 0.38f, 0.98f);
	const ColorRGBA Orange(1.0f, 0.5f, 0.12f, 0.98f);
	const ColorRGBA Red(0.92f, 0.16f, 0.14f, 0.98f);
	if(Quality >= 0.66f)
	{
		const float u = (Quality - 0.66f) / 0.34f;
		return Lerp(Green, Cyan, u);
	}
	if(Quality >= 0.33f)
	{
		const float u = (Quality - 0.33f) / 0.33f;
		return Lerp(Orange, Green, u);
	}
	return Lerp(Red, Orange, Quality / 0.33f);
}

float CAetherStabilityTrainer::PanelScale() const
{
	return std::clamp(g_Config.m_AeStabilityTrainerScale / 100.0f, 0.5f, 2.0f);
}

CUIRect CAetherStabilityTrainer::PanelRect() const
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const float Scale = PanelScale();
	const float TrackMul = std::clamp(g_Config.m_AeStabilityTrainerTrackWidth / 100.0f, 0.5f, 2.0f);
	const float ThickMul = std::clamp(g_Config.m_AeStabilityTrainerBarThickness / 100.0f, 0.5f, 2.0f);
	const float Width = maximum(86.0f * TrackMul, 42.0f) * Scale;
	const float Height = ((g_Config.m_AeStabilityTrainerShowBar ? 11.0f * ThickMul : 0.0f) + 4.0f) * Scale;
	const float X = ScreenWidth * 0.5f - Width * 0.5f + g_Config.m_AeStabilityTrainerOffsetX;
	const float Y = ScreenHeight - Height - g_Config.m_AeStabilityTrainerOffsetY;
	return CUIRect(X, Y, Width, Height);
}

CUIRect CAetherStabilityTrainer::ResizeHandleRect() const
{
	const float Scale = PanelScale();
	return CUIRect(m_LastRect.x + m_LastRect.w - 6.0f * Scale, m_LastRect.y + m_LastRect.h - 6.0f * Scale, 6.0f * Scale, 6.0f * Scale);
}

vec2 CAetherStabilityTrainer::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherStabilityTrainer::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	CUIRect Rect = PanelRect();
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, ScreenWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, ScreenHeight - Rect.h));
	g_Config.m_AeStabilityTrainerOffsetX = round_to_int(Rect.x - (ScreenWidth - Rect.w) * 0.5f);
	g_Config.m_AeStabilityTrainerOffsetY = round_to_int(ScreenHeight - Rect.y - Rect.h);
}

void CAetherStabilityTrainer::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	if(std::abs((ScreenWidth * 0.5f + g_Config.m_AeStabilityTrainerOffsetX) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeStabilityTrainerOffsetX = 0;
	const float PanelCenterY = ScreenHeight - g_Config.m_AeStabilityTrainerOffsetY - PanelHeight * 0.5f;
	if(std::abs(PanelCenterY - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeStabilityTrainerOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f);
	ClampOffsets();
}

void CAetherStabilityTrainer::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeStabilityTrainerScale = std::clamp(NewScale, 50, 200);
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const CUIRect Rect = PanelRect();
	g_Config.m_AeStabilityTrainerOffsetX = round_to_int(Center.x - ScreenWidth * 0.5f);
	g_Config.m_AeStabilityTrainerOffsetY = round_to_int(ScreenHeight - Center.y - Rect.h * 0.5f);
	ApplyCenterSnap(ScreenWidth, ScreenHeight, Rect.w, Rect.h);
}

void CAetherStabilityTrainer::UpdateState()
{
	if(!g_Config.m_AeStabilityTrainer)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int TrackId = ResolveTrackId();
	if(TrackId < 0 || TrackId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[TrackId].m_Active)
	{
		ResetState();
		return;
	}
	if(!IsLocalClientId(TrackId) && !g_Config.m_AeStabilityTrainerSpectate)
	{
		ResetState();
		return;
	}
	if(IsLocalClientId(TrackId) && !GameClient()->m_NewPredictedTick)
		return;

	const vec2 Vel = ResolveVelocity(TrackId);
	if(TrackId != m_LastTrackId)
	{
		m_LastTrackId = TrackId;
		m_RollingQuality = 0.0f;
		m_SmoothBar = BarTarget(Vel, TrackId, &m_WarnAmbiguous);
	}

	bool Warn = false;
	const float Instant = Quality(Vel, TrackId, &Warn);
	m_WarnAmbiguous = Warn;
	const float Alpha = minimum(1.0f, 1.0f / (float)maximum(1, AETHER_STABILITY_AVERAGE_TICKS));
	m_RollingQuality = m_RollingQuality * (1.0f - Alpha) + Instant * Alpha;
}

void CAetherStabilityTrainer::RenderPanel(bool ForcePreview)
{
	if(!ForcePreview && (!g_Config.m_AeStabilityTrainer || !g_Config.m_AeStabilityTrainerShowBar))
		return;
	if(!ForcePreview && (GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive()))
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	float Rolling = m_RollingQuality;
	float QualityNow = Rolling;
	bool Warn = m_WarnAmbiguous;
	float TargetBar = 0.5f;
	if(ForcePreview)
	{
		Rolling = 0.78f;
		QualityNow = 0.78f;
		Warn = false;
		TargetBar = 0.58f;
		m_SmoothBar = TargetBar;
	}
	else
	{
		const int TrackId = ResolveTrackId();
		if(TrackId < 0 || TrackId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[TrackId].m_Active)
			return;
		if(!IsLocalClientId(TrackId) && !g_Config.m_AeStabilityTrainerSpectate)
			return;
		const vec2 Vel = ResolveVelocity(TrackId);
		TargetBar = BarTarget(Vel, TrackId, &Warn);
		QualityNow = Quality(Vel, TrackId, nullptr);
		const float Rate = 5.0f + std::clamp(AETHER_STABILITY_BAR_GLIDE / 100.0f, 0.08f, 1.0f) * 58.0f;
		const float Alpha = minimum(1.0f, maximum(Client()->RenderFrameTime(), 0.00005f) * Rate);
		m_SmoothBar += (TargetBar - m_SmoothBar) * Alpha;
		if(std::fabs(TargetBar - m_SmoothBar) < 0.0005f)
			m_SmoothBar = TargetBar;
	}

	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, 300.0f);
	Graphics()->TextureClear();
	m_LastRect = PanelRect();

	const float Scale = PanelScale();
	const float ThickMul = std::clamp(g_Config.m_AeStabilityTrainerBarThickness / 100.0f, 0.5f, 2.0f);
	const bool Sharp = g_Config.m_AeStabilityTrainerSharpCorners != 0;
	ColorRGBA Solid = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AeStabilityTrainerColor, true));
	if(Solid.a <= 0.02f)
		Solid.a = 1.0f;
	const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
	float y = m_LastRect.y + 2.0f * Scale;

	if(g_Config.m_AeStabilityTrainerShowBar || ForcePreview)
	{
		const float BarHeight = 9.0f * Scale * ThickMul;
		const float Radius = Sharp ? 0.0f : BarHeight * 0.45f;
		Graphics()->DrawRect(m_LastRect.x, y, m_LastRect.w, BarHeight, ColorRGBA(0.12f, 0.12f, 0.14f, 0.92f), Sharp ? IGraphics::CORNER_NONE : IGraphics::CORNER_ALL, Radius);
		const float Pad = maximum(1.0f, BarHeight * 0.11f);
		const float MidX = m_LastRect.x + m_LastRect.w * 0.5f;
		Graphics()->DrawRect(MidX - 0.5f, y + Pad, 1.0f, BarHeight - 2.0f * Pad, ColorRGBA(0.05f, 0.05f, 0.06f, 0.9f), IGraphics::CORNER_NONE, 0.0f);

		const float BlockRel = std::clamp(g_Config.m_AeStabilityTrainerBlockWidth / 100.0f, 0.35f, 1.5f);
		const float BlockWidth = maximum(5.0f * Scale, m_LastRect.w * 0.2f * BlockRel);
		const float Travel = maximum(1.0f, m_LastRect.w - BlockWidth - 4.0f);
		const float x = m_LastRect.x + 2.0f + std::clamp(Warn ? 0.5f : m_SmoothBar, 0.0f, 1.0f) * Travel;
		ColorRGBA BlockColor = Warn ? ColorRGBA(0.9f, 0.2f, 0.18f, 0.95f) : (g_Config.m_AeStabilityTrainerColorize ? QualityColor(QualityNow) : Solid);
		const float BlockY = y + Pad * 1.15f;
		const float BlockHeight = maximum(2.0f, BarHeight - Pad * 2.3f);
		Graphics()->DrawRect(x, BlockY, BlockWidth, BlockHeight, BlockColor, Sharp ? IGraphics::CORNER_NONE : IGraphics::CORNER_ALL, Sharp ? 0.0f : minimum(2.0f * Scale, BlockHeight * 0.35f));
	}

	if(m_EditorOpen)
	{
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
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

void CAetherStabilityTrainer::OnRender()
{
	if(!g_Config.m_AeStabilityTrainer && !m_EditorOpen)
		return;
	if(g_Config.m_AeFocusMode && !m_EditorOpen)
		return;
	UpdateState();
	RenderPanel(m_EditorOpen);
}

bool CAetherStabilityTrainer::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherStabilityTrainer::OnUpdate()
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
		g_Config.m_AeStabilityTrainerOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeStabilityTrainerOffsetY = round_to_int(ScreenHeight - (Mouse.y - m_DragOffset.y) - m_LastRect.h);
		ApplyCenterSnap(ScreenWidth, ScreenHeight, m_LastRect.w, m_LastRect.h);
	}
	else if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float TrackMul = std::clamp(g_Config.m_AeStabilityTrainerTrackWidth / 100.0f, 0.5f, 2.0f);
		const float ThickMul = std::clamp(g_Config.m_AeStabilityTrainerBarThickness / 100.0f, 0.5f, 2.0f);
		const float BaseWidth = maximum(86.0f * TrackMul, 42.0f);
		const float BaseHeight = ((g_Config.m_AeStabilityTrainerShowBar ? 11.0f * ThickMul : 0.0f) + 4.0f);
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
	}
}

void CAetherStabilityTrainer::CloseEditor()
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

void CAetherStabilityTrainer::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
	{
		CloseEditor();
		ResetState();
	}
}

bool CAetherStabilityTrainer::OnInput(const IInput::CEvent &Event)
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
		g_Config.m_AeStabilityTrainerOffsetX = 0;
		g_Config.m_AeStabilityTrainerOffsetY = 74;
		g_Config.m_AeStabilityTrainerScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(m_LastRect.Inside(Mouse))
		{
			SetScaleKeepingCenter(g_Config.m_AeStabilityTrainerScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f));
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

bool CAetherStabilityTrainer::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
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
		g_Config.m_AeStabilityTrainerOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeStabilityTrainerOffsetY = round_to_int(ScreenHeight - (Mouse.y - m_DragOffset.y) - m_LastRect.h);
		ApplyCenterSnap(ScreenWidth, ScreenHeight, m_LastRect.w, m_LastRect.h);
		return true;
	}
	if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float TrackMul = std::clamp(g_Config.m_AeStabilityTrainerTrackWidth / 100.0f, 0.5f, 2.0f);
		const float ThickMul = std::clamp(g_Config.m_AeStabilityTrainerBarThickness / 100.0f, 0.5f, 2.0f);
		const float BaseWidth = maximum(86.0f * TrackMul, 42.0f);
		const float BaseHeight = ((g_Config.m_AeStabilityTrainerShowBar ? 11.0f * ThickMul : 0.0f) + 4.0f);
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (BaseHeight * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		return true;
	}
	return false;
}
