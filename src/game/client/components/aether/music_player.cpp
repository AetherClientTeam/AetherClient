#include "music_player.h"

#include <base/color.h>

#include <engine/client.h>
#include <engine/image.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/hud.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
int64_t SteadyMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

AetherMusic::SColor MixColor(const AetherMusic::SColor &Current, const AetherMusic::SColor &Target, float Amount)
{
	return {
		Current.m_R + (Target.m_R - Current.m_R) * Amount,
		Current.m_G + (Target.m_G - Current.m_G) * Amount,
		Current.m_B + (Target.m_B - Current.m_B) * Amount};
}

constexpr AetherMusic::SColor VERA_IDLE_BACKGROUND{0.075f, 0.052f, 0.088f};
constexpr AetherMusic::SColor VERA_IDLE_ACCENT{0.93f, 0.62f, 0.96f};
}

float CAetherMusicPlayer::PanelScale() const
{
	return std::clamp(g_Config.m_AeMusicScale / 100.0f, 0.5f, 2.0f);
}

float CAetherMusicPlayer::DynamicPanelWidth(float Scale) const
{
	constexpr float Padding = 2.0f;
	constexpr float Artwork = 20.0f;
	constexpr float Gap = 5.0f;
	constexpr float Visualizer = 22.0f;
	const AetherMusic::STimerModel Timer = GameClient()->m_Hud.GameTimerModel();
	const float TimerFontSize = 8.0f * Scale;
	const float TimerWidth = Timer.m_Visible ? TextRender()->TextWidth(TimerFontSize, Timer.m_Text.c_str(), -1, -1.0f) / Scale : 0.0f;
	return std::max(PANEL_WIDTH, Padding * 2.0f + Artwork + Gap + std::max(28.0f, TimerWidth) + Gap + Visualizer);
}

AetherMusic::SRect CAetherMusicPlayer::ResizeHandleRect() const
{
	const float HandleSize = 6.0f;
	return {
		m_LastPanelRect.m_X + m_LastPanelRect.m_W - HandleSize * 0.5f,
		m_LastPanelRect.m_Y + m_LastPanelRect.m_H - HandleSize * 0.5f,
		HandleSize,
		HandleSize};
}

void CAetherMusicPlayer::ReleaseArtwork()
{
	if(m_ArtworkTexture.IsValid())
		Graphics()->UnloadTexture(&m_ArtworkTexture);
	m_LoadedArtworkGeneration = 0;
	m_ArtworkWidth = 0;
	m_ArtworkHeight = 0;
	m_HasArtwork = false;
}

void CAetherMusicPlayer::ReleaseFallbackLogo()
{
	if(m_FallbackLogoTexture.IsValid())
		Graphics()->UnloadTexture(&m_FallbackLogoTexture);
	m_FallbackLogoTried = false;
}

void CAetherMusicPlayer::UpdateArtwork(const CAetherMediaBackend::SSnapshot &Snapshot)
{
	if(Snapshot.m_ArtworkGeneration == 0 || Snapshot.m_ArtworkGeneration == m_LoadedArtworkGeneration)
		return;
	if(!Snapshot.m_pArtworkRgba || Snapshot.m_pArtworkRgba->empty() || Snapshot.m_ArtworkWidth == 0 || Snapshot.m_ArtworkHeight == 0)
	{
		if(m_ArtworkTexture.IsValid())
			Graphics()->UnloadTexture(&m_ArtworkTexture);
		m_HasArtwork = false;
		m_ArtworkWidth = 0;
		m_ArtworkHeight = 0;
		m_LoadedArtworkGeneration = Snapshot.m_ArtworkGeneration;
		return;
	}

	CImageInfo Image;
	Image.m_Width = Snapshot.m_ArtworkWidth;
	Image.m_Height = Snapshot.m_ArtworkHeight;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = static_cast<uint8_t *>(std::malloc(Snapshot.m_pArtworkRgba->size()));
	if(!Image.m_pData)
	{
		m_LoadedArtworkGeneration = Snapshot.m_ArtworkGeneration;
		return;
	}
	std::memcpy(Image.m_pData, Snapshot.m_pArtworkRgba->data(), Snapshot.m_pArtworkRgba->size());
	m_TargetArtworkAccent = AetherMusic::DominantColor(*Snapshot.m_pArtworkRgba);
	m_TargetDynamicColor = AetherMusic::DarkenForPanel(m_TargetArtworkAccent);
	if(m_LastColorUpdateMs == 0)
	{
		m_ArtworkAccent = m_TargetArtworkAccent;
		m_DynamicColor = m_TargetDynamicColor;
		m_LastColorUpdateMs = SteadyMilliseconds();
	}
	if(m_ArtworkTexture.IsValid())
		Graphics()->UnloadTexture(&m_ArtworkTexture);
	m_ArtworkTexture = Graphics()->LoadTextureRawMove(Image, 0, "Aether media thumbnail");
	m_HasArtwork = m_ArtworkTexture.IsValid();
	m_ArtworkWidth = Snapshot.m_ArtworkWidth;
	m_ArtworkHeight = Snapshot.m_ArtworkHeight;
	m_LoadedArtworkGeneration = Snapshot.m_ArtworkGeneration;
}

void CAetherMusicPlayer::UpdateColors()
{
	const int64_t Now = SteadyMilliseconds();
	if(m_LastColorUpdateMs == 0)
	{
		m_LastColorUpdateMs = Now;
		return;
	}
	const float DeltaSeconds = std::clamp((Now - m_LastColorUpdateMs) / 1000.0f, 0.0f, 0.1f);
	m_LastColorUpdateMs = Now;
	const float Blend = 1.0f - std::exp(-DeltaSeconds / 0.42f);
	m_ArtworkAccent = MixColor(m_ArtworkAccent, m_TargetArtworkAccent, Blend);
	m_DynamicColor = MixColor(m_DynamicColor, m_TargetDynamicColor, Blend);
}

void CAetherMusicPlayer::RenderFallbackMonogram(const AetherMusic::SRect &ArtworkRect, float Alpha)
{
	const float Scale = ArtworkRect.m_H / 14.0f;
	Graphics()->DrawRect(ArtworkRect.m_X, ArtworkRect.m_Y, ArtworkRect.m_W, ArtworkRect.m_H, ColorRGBA(0.16f, 0.18f, 0.22f, Alpha), IGraphics::CORNER_ALL, 3.0f * Scale);
	if(!m_FallbackLogoTried)
	{
		m_FallbackLogoTexture = Graphics()->LoadTexture("core/logos/aether_vera_logo.png", IStorage::TYPE_ALL);
		if(!m_FallbackLogoTexture.IsValid())
			m_FallbackLogoTexture = Graphics()->LoadTexture("core/logos/vera_512.png", IStorage::TYPE_ALL);
		m_FallbackLogoTried = true;
	}
	if(m_FallbackLogoTexture.IsValid())
	{
		const float Inset = 1.5f * Scale;
		Graphics()->WrapClamp();
		Graphics()->TextureSet(m_FallbackLogoTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem Quad(ArtworkRect.m_X + Inset, ArtworkRect.m_Y + Inset, ArtworkRect.m_W - Inset * 2.0f, ArtworkRect.m_H - Inset * 2.0f);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
		Graphics()->TextureClear();
		Graphics()->WrapNormal();
		return;
	}
	TextRender()->TextColor(0.82f, 0.86f, 0.92f, Alpha);
	const float FontSize = 11.0f * Scale;
	const float Width = TextRender()->TextWidth(FontSize, "A");
	TextRender()->Text(ArtworkRect.m_X + (ArtworkRect.m_W - Width) / 2.0f, ArtworkRect.m_Y + 2.4f * Scale, FontSize, "A");
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAetherMusicPlayer::RenderVisualizer(const AetherMusic::SRect &PanelRect, const CAetherMediaBackend::SSnapshot &Snapshot, AetherMusic::SColor Accent, float Alpha)
{
	if(!g_Config.m_AeMusicVisualizer)
		return;
	if(g_Config.m_AeMusicVisualizerStyle > 1)
		g_Config.m_AeMusicVisualizerStyle = 0;
	const int VisualizerStyle = g_Config.m_AeMusicVisualizerStyle;
	const float Brightest = std::max({Accent.m_R, Accent.m_G, Accent.m_B});
	if(Brightest < 0.72f)
	{
		const float Lift = (0.72f - Brightest) / std::max(1.0f - Brightest, 0.001f);
		Accent.m_R += (1.0f - Accent.m_R) * Lift;
		Accent.m_G += (1.0f - Accent.m_G) * Lift;
		Accent.m_B += (1.0f - Accent.m_B) * Lift;
	}
	const float Scale = PanelRect.m_H / PANEL_HEIGHT;
	const float VisualizerWidth = (VisualizerStyle == 1 ? 20.0f : 17.0f) * Scale;
	const float VisualizerX = PanelRect.m_X + PanelRect.m_W - (3.0f * Scale) - VisualizerWidth;
	const float CenterY = PanelRect.m_Y + PanelRect.m_H * 0.5f;
	const float ClipPad = 3.0f * Scale;
	const float ClipTop = PanelRect.m_Y + ClipPad;
	const float ClipBottom = PanelRect.m_Y + PanelRect.m_H - ClipPad;
	const float HalfAvailableHeight = std::max(1.0f * Scale, (ClipBottom - ClipTop) * 0.5f);
	const float GlowAmount = std::clamp(g_Config.m_AeMusicVisualizerGlow / 100.0f, 0.0f, 1.0f);
	const bool AudioActive = Snapshot.m_PlaybackState == AetherMusic::EPlaybackState::PLAYING || Snapshot.m_AudioActive || Snapshot.m_RootMeanSquare > 0.0005f;
	constexpr std::array<float, 5> aIdleBands = {0.06f, 0.07f, 0.065f, 0.07f, 0.06f};
	const int64_t NowMs = SteadyMilliseconds();
	if(m_LastVisualizerUpdateMs == 0)
		m_LastVisualizerUpdateMs = NowMs;
	const float DeltaSeconds = std::clamp((NowMs - m_LastVisualizerUpdateMs) / 1000.0f, 0.0f, 0.1f);
	m_LastVisualizerUpdateMs = NowMs;
	const float RawRmsEnergy = AudioActive ? std::clamp(std::sqrt(std::max(0.0f, Snapshot.m_RootMeanSquare)) * 7.5f, 0.0f, 1.35f) : 0.0f;
	const float RmsAttack = RawRmsEnergy > m_LastRmsEnergy ? 0.030f : 0.140f;
	const float RmsBlend = 1.0f - std::exp(-DeltaSeconds / RmsAttack);
	const float PreviousRmsEnergy = m_LastRmsEnergy;
	m_LastRmsEnergy += (RawRmsEnergy - m_LastRmsEnergy) * RmsBlend;
	const float RmsRise = std::max(0.0f, m_LastRmsEnergy - PreviousRmsEnergy);
	m_BassPulse = std::max(m_BassPulse * std::exp(-DeltaSeconds / 0.16f), RmsRise * 3.2f);
	for(size_t i = 0; i < m_aSmoothedBands.size(); ++i)
	{
		const float RawBand = AudioActive && Snapshot.m_VisualizerAvailable ? std::clamp(Snapshot.m_aBands[i], 0.0f, 1.6f) : 0.0f;
		const float SlowBlend = 1.0f - std::exp(-DeltaSeconds / (i < 2 ? 0.58f : 0.42f));
		m_aSlowRawBands[i] += (RawBand - m_aSlowRawBands[i]) * SlowBlend;
		const float RawRise = std::max(0.0f, RawBand - m_aPreviousRawBands[i]);
		const float RawFollow = RawBand > m_aPreviousRawBands[i] ? 0.70f : 0.18f;
		m_aPreviousRawBands[i] += (RawBand - m_aPreviousRawBands[i]) * RawFollow;
		const float RelativeRise = std::max(0.0f, RawBand - m_aSlowRawBands[i] * (i < 2 ? 0.84f : 0.94f));
		const float PunchGain = i == 0 ? 4.55f : (i == 1 ? 3.85f : 2.15f);
		m_aBandPunch[i] = std::max(m_aBandPunch[i] * std::exp(-DeltaSeconds / (i < 2 ? 0.078f : 0.125f)), std::max(RawRise, RelativeRise) * PunchGain);

		float SourceBand = std::pow(RawBand, i < 2 ? 0.66f : 0.58f) + m_aBandPunch[i];
		if(i == 0)
			SourceBand = std::max(SourceBand * 1.24f, m_LastRmsEnergy * 0.26f + m_BassPulse * 1.08f);
		else if(i == 1)
			SourceBand = std::max(SourceBand * 1.34f, m_LastRmsEnergy * 0.22f + m_BassPulse * 0.82f);
		else
			SourceBand *= 1.24f;
		const float Target = AudioActive ? std::max(aIdleBands[i] * 0.16f, SourceBand) : aIdleBands[i];
		const float TimeConstant = Target > m_aSmoothedBands[i] ? 0.016f : (i < 2 ? 0.072f : 0.105f);
		const float Blend = 1.0f - std::exp(-DeltaSeconds / TimeConstant);
		m_aSmoothedBands[i] += (Target - m_aSmoothedBands[i]) * Blend;
	}
	auto BandAt = [&](float Position) {
		const float ScaledPosition = std::clamp(Position, 0.0f, 1.0f) * 4.0f;
		const int Left = std::min((int)ScaledPosition, 3);
		const float Fraction = ScaledPosition - Left;
		const float SmoothFraction = Fraction * Fraction * (3.0f - 2.0f * Fraction);
		return m_aSmoothedBands[Left] + (m_aSmoothedBands[Left + 1] - m_aSmoothedBands[Left]) * SmoothFraction;
	};
	auto EdgeFade = [](float Position) {
		const float FadeIn = std::clamp(Position / 0.16f, 0.0f, 1.0f);
		const float FadeOut = std::clamp((1.0f - Position) / 0.16f, 0.0f, 1.0f);
		const float Fade = std::min(FadeIn, FadeOut);
		return Fade * Fade * (3.0f - 2.0f * Fade);
	};

	if(VisualizerStyle == 1)
	{
		constexpr int Segments = 24;
		std::vector<IGraphics::CFreeformItem> vFill;
		vFill.reserve(Segments);
		auto BuildMountain = [&](float RadiusBonus, std::vector<IGraphics::CFreeformItem> &vItems) {
			vItems.clear();
			for(int Segment = 0; Segment < Segments; ++Segment)
			{
				const float T0 = Segment / (float)Segments;
				const float T1 = (Segment + 1) / (float)Segments;
				const float X0 = VisualizerX + T0 * VisualizerWidth;
				const float X1 = VisualizerX + T1 * VisualizerWidth;
				const float Amp0 = std::min((1.0f + BandAt(T0) * 7.0f + RadiusBonus) * EdgeFade(T0) * Scale, HalfAvailableHeight);
				const float Amp1 = std::min((1.0f + BandAt(T1) * 7.0f + RadiusBonus) * EdgeFade(T1) * Scale, HalfAvailableHeight);
				vItems.emplace_back(X0, CenterY - Amp0, X1, CenterY - Amp1, X0, CenterY + Amp0, X1, CenterY + Amp1);
			}
		};
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		if(GlowAmount > 0.0f)
		{
			for(int Pass = 2; Pass >= 1; --Pass)
			{
				BuildMountain(Pass * 1.9f * GlowAmount, vFill);
				Graphics()->SetColor(Accent.m_R, Accent.m_G, Accent.m_B, Alpha * GlowAmount * 0.13f * (3 - Pass));
				Graphics()->QuadsDrawFreeform(vFill.data(), vFill.size());
			}
		}
		BuildMountain(0.0f, vFill);
		Graphics()->SetColor(Accent.m_R, Accent.m_G, Accent.m_B, Alpha * 0.9f);
		Graphics()->QuadsDrawFreeform(vFill.data(), vFill.size());
		Graphics()->QuadsEnd();
		return;
	}

	const float BarWidth = 1.5f * Scale;
	const float Gap = 1.0f * Scale;
	const float TotalWidth = 5.0f * BarWidth + 4.0f * Gap;
	const float StartX = VisualizerX + (VisualizerWidth - TotalWidth) * 0.5f;
	for(size_t i = 0; i < 5; ++i)
	{
		static constexpr std::array<float, 5> s_aVisualGain = {0.92f, 1.08f, 1.25f, 1.18f, 1.10f};
		const float Height = std::min(std::max(1.4f * Scale, m_aSmoothedBands[i] * 13.4f * s_aVisualGain[i] * Scale), HalfAvailableHeight * 2.0f);
		const float X = StartX + i * (BarWidth + Gap);
		const float Y = CenterY - Height * 0.5f;
		for(int Pass = 3; Pass >= 1; --Pass)
		{
			const float Glow = std::min(Pass * (0.18f + 0.78f * GlowAmount) * Scale, std::max(0.0f, std::min(Y - ClipTop, ClipBottom - (Y + Height))));
			Graphics()->DrawRect(X - Glow, Y - Glow, BarWidth + Glow * 2.0f, Height + Glow * 2.0f,
				ColorRGBA(Accent.m_R, Accent.m_G, Accent.m_B, Alpha * GlowAmount * 0.04f * (4 - Pass)), IGraphics::CORNER_ALL, BarWidth + Glow);
		}
		Graphics()->DrawRect(X, Y, BarWidth, Height, ColorRGBA(Accent.m_R, Accent.m_G, Accent.m_B, Alpha), IGraphics::CORNER_ALL, BarWidth);
	}
}

void CAetherMusicPlayer::RenderPanel(const CAetherMediaBackend::SSnapshot &Snapshot)
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const float Scale = PanelScale();
	if(g_Config.m_AeMusicVisualizerStyle > 1)
		g_Config.m_AeMusicVisualizerStyle = 0;
	const float PanelWidth = DynamicPanelWidth(Scale) * Scale;
	const float PanelHeight = PANEL_HEIGHT * Scale;
	const float Opacity = std::clamp(g_Config.m_AeMusicOpacity / 100.0f, 0.1f, 1.0f);
	m_LastPanelRect = AetherMusic::ClampTopCenter(ScreenWidth, ScreenHeight, PanelWidth, PanelHeight, g_Config.m_AeMusicOffsetX, g_Config.m_AeMusicOffsetY);
	UpdateColors();

	const int64_t ArtworkActivityMs = std::max(Snapshot.m_LastPlayingMs, Snapshot.m_ArtworkReceivedMs);
	const auto ArtworkState = AetherMusic::ArtworkState(Snapshot.m_PlaybackState, m_HasArtwork, ArtworkActivityMs, SteadyMilliseconds());
	AetherMusic::SColor Background = m_DynamicColor;
	AetherMusic::SColor Accent = m_ArtworkAccent;
	if(!g_Config.m_AeMusicDynamicColor)
	{
		const ColorRGBA StaticColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AeMusicBackgroundColor));
		Background = {StaticColor.r, StaticColor.g, StaticColor.b};
		Accent = Background;
	}
	else if(ArtworkState == AetherMusic::EArtworkState::FALLBACK)
	{
		Background = VERA_IDLE_BACKGROUND;
		Accent = VERA_IDLE_ACCENT;
	}
	const float ContentAlpha = Opacity * (ArtworkState == AetherMusic::EArtworkState::DIMMED ? 0.42f : 1.0f);
	Graphics()->DrawRect(m_LastPanelRect.m_X, m_LastPanelRect.m_Y, m_LastPanelRect.m_W, m_LastPanelRect.m_H,
		ColorRGBA(Background.m_R, Background.m_G, Background.m_B, Opacity), IGraphics::CORNER_ALL, 4.0f * Scale);

	auto DrawArtworkToRect = [&](const AetherMusic::SRect &Rect, float DrawAlpha) {
		float U0 = 0.0f;
		float V0 = 0.0f;
		float U1 = 1.0f;
		float V1 = 1.0f;
		if(m_ArtworkWidth > 0 && m_ArtworkHeight > 0 && m_ArtworkWidth != m_ArtworkHeight)
		{
			if(m_ArtworkWidth > m_ArtworkHeight)
			{
				const float Crop = (1.0f - (float)m_ArtworkHeight / (float)m_ArtworkWidth) * 0.5f;
				U0 = Crop;
				U1 = 1.0f - Crop;
			}
			else
			{
				const float Crop = (1.0f - (float)m_ArtworkWidth / (float)m_ArtworkHeight) * 0.5f;
				V0 = Crop;
				V1 = 1.0f - Crop;
			}
		}

		Graphics()->TextureSet(m_ArtworkTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, DrawAlpha);
		Graphics()->QuadsSetSubset(U0, V0, U1, V1);
		IGraphics::CQuadItem ArtworkQuad(Rect.m_X, Rect.m_Y, Rect.m_W, Rect.m_H);
		Graphics()->QuadsDrawTL(&ArtworkQuad, 1);
		Graphics()->QuadsEnd();
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		Graphics()->TextureClear();

		const float Radius = std::min({4.0f * Scale, Rect.m_W * 0.45f, Rect.m_H * 0.45f});
		const int Steps = 10;
		auto MaskCorner = [&](bool Right, bool Bottom) {
			for(int Step = 0; Step < Steps; ++Step)
			{
				const float Strip0 = Radius * Step / Steps;
				const float Strip1 = Radius * (Step + 1) / Steps;
				const float StripMid = (Strip0 + Strip1) * 0.5f;
				const float DistanceFromOuterEdge = Bottom ? Radius - StripMid : StripMid;
				const float CenterDistance = DistanceFromOuterEdge - Radius;
				const float VisibleWidth = std::sqrt(std::max(0.0f, Radius * Radius - CenterDistance * CenterDistance));
				const float CoverWidth = std::max(0.0f, Radius - VisibleWidth);
				if(CoverWidth <= 0.01f)
					continue;
				const float X = Right ? Rect.m_X + Rect.m_W - CoverWidth : Rect.m_X;
				const float Y = Bottom ? Rect.m_Y + Rect.m_H - Radius + Strip0 : Rect.m_Y + Strip0;
				Graphics()->DrawRect(X, Y, CoverWidth, Strip1 - Strip0,
					ColorRGBA(Background.m_R, Background.m_G, Background.m_B, 1.0f), 0, 0.0f);
			}
		};
		MaskCorner(false, false);
		MaskCorner(true, false);
		MaskCorner(false, true);
		MaskCorner(true, true);
	};

	const float ArtworkSize = std::min(20.0f * Scale, m_LastPanelRect.m_H - 2.0f * Scale);
	const AetherMusic::SRect ArtworkRect{m_LastPanelRect.m_X + 1.0f * Scale, m_LastPanelRect.m_Y + (m_LastPanelRect.m_H - ArtworkSize) * 0.5f, ArtworkSize, ArtworkSize};
	Graphics()->DrawRect(ArtworkRect.m_X, ArtworkRect.m_Y, ArtworkRect.m_W, ArtworkRect.m_H, ColorRGBA(0.04f, 0.05f, 0.07f, Opacity * 0.42f), IGraphics::CORNER_ALL, 4.0f * Scale);
	if(ArtworkState != AetherMusic::EArtworkState::FALLBACK && m_ArtworkTexture.IsValid())
	{
		DrawArtworkToRect(ArtworkRect, ContentAlpha);
	}
	else
		RenderFallbackMonogram(ArtworkRect, Opacity);

	if(g_Config.m_ClShowhudTimer)
	{
		const float TimerFontSize = 8.0f * Scale;
		const float TimerY = m_LastPanelRect.m_Y + m_LastPanelRect.m_H * 0.5f - TimerFontSize * 0.50f;
		const float TimerLeft = ArtworkRect.m_X + ArtworkRect.m_W + 5.0f * Scale;
		const float TimerRight = m_LastPanelRect.m_X + m_LastPanelRect.m_W - (g_Config.m_AeMusicVisualizerStyle == 1 ? 33.0f : 27.0f) * Scale;
		GameClient()->m_Hud.RenderGameTimerAt((TimerLeft + TimerRight) * 0.5f, TimerY, TimerFontSize, Opacity);
	}

	RenderVisualizer(m_LastPanelRect, Snapshot, Accent, Opacity);

	if(m_EditorOpen)
	{
		const ColorRGBA ThemeColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const float CenterX = ScreenWidth * 0.5f;
		const float CenterY = ScreenHeight * 0.5f;
		const float PanelCenterX = m_LastPanelRect.m_X + m_LastPanelRect.m_W * 0.5f;
		const float PanelCenterY = m_LastPanelRect.m_Y + m_LastPanelRect.m_H * 0.5f;
		const bool SnapX = std::abs(PanelCenterX - CenterX) <= 4.0f;
		const bool SnapY = std::abs(PanelCenterY - CenterY) <= 4.0f;
		Graphics()->DrawRect(CenterX - 0.25f, 0.0f, 0.5f, ScreenHeight, ThemeColor.WithAlpha(SnapX ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(0.0f, CenterY - 0.25f, ScreenWidth, 0.5f, ThemeColor.WithAlpha(SnapY ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(m_LastPanelRect.m_X - 1.0f, m_LastPanelRect.m_Y - 1.0f, m_LastPanelRect.m_W + 2.0f, m_LastPanelRect.m_H + 2.0f,
			ThemeColor.WithAlpha(0.22f), IGraphics::CORNER_ALL, 5.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
		const AetherMusic::SRect Handle = ResizeHandleRect();
		Graphics()->DrawRect(Handle.m_X, Handle.m_Y, Handle.m_W, Handle.m_H, ColorRGBA(0.10f, 0.15f, 0.22f, 0.95f), IGraphics::CORNER_ALL, 1.5f);
		Graphics()->DrawRect(Handle.m_X + 1.2f, Handle.m_Y + 1.2f, Handle.m_W - 2.4f, Handle.m_H - 2.4f, ThemeColor.WithAlpha(1.0f), IGraphics::CORNER_ALL, 1.0f);

		const char *pHelp = "Drag panel | Drag corner: resize | R reset | Esc close";
		const float Width = TextRender()->TextWidth(7.0f, pHelp);
		TextRender()->Text(ScreenWidth / 2.0f - Width / 2.0f, 25.0f, 7.0f, pHelp);
		char aScale[32];
		str_format(aScale, sizeof(aScale), "Size: %d%%", g_Config.m_AeMusicScale);
		const float ScaleWidth = TextRender()->TextWidth(7.0f, aScale);
		TextRender()->Text(ScreenWidth / 2.0f - ScaleWidth / 2.0f, 33.0f, 7.0f, aScale);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CAetherMusicPlayer::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float PanelWidth = DynamicPanelWidth(PanelScale()) * PanelScale();
	const float PanelHeight = PANEL_HEIGHT * PanelScale();
	const AetherMusic::SRect Rect = AetherMusic::ClampTopCenter(ScreenWidth, 300.0f, PanelWidth, PanelHeight, g_Config.m_AeMusicOffsetX, g_Config.m_AeMusicOffsetY);
	g_Config.m_AeMusicOffsetX = round_to_int(Rect.m_X - (ScreenWidth - PanelWidth) / 2.0f);
	g_Config.m_AeMusicOffsetY = round_to_int(Rect.m_Y - 2.0f);
}

void CAetherMusicPlayer::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	if(std::abs((ScreenWidth * 0.5f + g_Config.m_AeMusicOffsetX) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeMusicOffsetX = 0;
	const float PanelCenterY = 2.0f + g_Config.m_AeMusicOffsetY + PanelHeight * 0.5f;
	if(std::abs(PanelCenterY - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeMusicOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f - 2.0f);
	ClampOffsets();
}

void CAetherMusicPlayer::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeMusicScale = std::clamp(NewScale, 50, 200);
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	const float PanelWidth = DynamicPanelWidth(PanelScale()) * PanelScale();
	const float PanelHeight = PANEL_HEIGHT * PanelScale();
	g_Config.m_AeMusicOffsetX = round_to_int(Center.x - ScreenWidth * 0.5f);
	g_Config.m_AeMusicOffsetY = round_to_int(Center.y - PanelHeight * 0.5f - 2.0f);
	ApplyCenterSnap(ScreenWidth, ScreenHeight, PanelWidth, PanelHeight);
}

vec2 CAetherMusicPlayer::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherMusicPlayer::OnUpdate()
{
	if(g_Config.m_AeMusicPlayer)
	{
		if(!m_MediaBackend.Running())
			m_MediaBackend.Start();
		m_MediaBackend.SetVisualizer(g_Config.m_AeMusicVisualizer != 0, g_Config.m_AeMusicVisualizerSensitivity);
	}
	else if(m_MediaBackend.Running())
		m_MediaBackend.Stop();

	if(m_EditorOpen && m_EditorInteraction != EEditorInteraction::IDLE)
	{
		if(!Input()->NativeMousePressed(1))
			m_EditorInteraction = EEditorInteraction::IDLE;
		else if(m_EditorInteraction == EEditorInteraction::DRAGGING)
		{
			const vec2 Mouse = HudMousePos();
			const float PanelWidth = m_LastPanelRect.m_W > 0.0f ? m_LastPanelRect.m_W : DynamicPanelWidth(PanelScale()) * PanelScale();
			const float PanelHeight = PANEL_HEIGHT * PanelScale();
			const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
			const float ScreenHeight = 300.0f;
			g_Config.m_AeMusicOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - PanelWidth) / 2.0f);
			g_Config.m_AeMusicOffsetY = round_to_int(Mouse.y - m_DragOffset.y - 2.0f);
			ApplyCenterSnap(ScreenWidth, ScreenHeight, PanelWidth, PanelHeight);
		}
		else if(m_EditorInteraction == EEditorInteraction::RESIZING)
		{
			const vec2 Mouse = HudMousePos();
			const float BaseWidth = DynamicPanelWidth(1.0f);
			const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (BaseWidth * 0.5f);
			const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (PANEL_HEIGHT * 0.5f);
			SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		}
	}
}

void CAetherMusicPlayer::OnRender()
{
	if(!g_Config.m_AeMusicPlayer && !m_EditorOpen)
	{
		if(m_ArtworkTexture.IsValid())
			ReleaseArtwork();
		return;
	}
	if(g_Config.m_AeFocusMode && !g_Config.m_AeFocusModeKeepMusicPlayer && !m_EditorOpen)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	Graphics()->MapScreen(0.0f, 0.0f, 300.0f * Graphics()->ScreenAspect(), 300.0f);
	const auto Snapshot = m_MediaBackend.Snapshot();
	UpdateArtwork(Snapshot);
	RenderPanel(Snapshot);
	if(m_EditorOpen)
		RenderTools()->RenderCursor(HudMousePos(), 12.0f);
}

void CAetherMusicPlayer::OnShutdown()
{
	CloseEditor();
	m_MediaBackend.Stop();
	ReleaseArtwork();
	ReleaseFallbackLogo();
}

void CAetherMusicPlayer::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		CloseEditor();
}

bool CAetherMusicPlayer::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherMusicPlayer::CloseEditor()
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

bool CAetherMusicPlayer::OnInput(const IInput::CEvent &Event)
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
		g_Config.m_AeMusicOffsetX = 0;
		g_Config.m_AeMusicOffsetY = 0;
		g_Config.m_AeMusicScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(CUIRect(m_LastPanelRect.m_X, m_LastPanelRect.m_Y, m_LastPanelRect.m_W, m_LastPanelRect.m_H).Inside(Mouse))
		{
			const vec2 Center(m_LastPanelRect.m_X + m_LastPanelRect.m_W * 0.5f, m_LastPanelRect.m_Y + m_LastPanelRect.m_H * 0.5f);
			SetScaleKeepingCenter(g_Config.m_AeMusicScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), Center);
			return true;
		}
		return false;
	}
	if(Event.m_Key == KEY_MOUSE_1)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const vec2 Mouse = HudMousePos();
			const AetherMusic::SRect Handle = ResizeHandleRect();
			if(CUIRect(Handle.m_X, Handle.m_Y, Handle.m_W, Handle.m_H).Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::RESIZING;
				m_ResizeAnchor = vec2(m_LastPanelRect.m_X, m_LastPanelRect.m_Y);
				m_ResizeCenter = vec2(m_LastPanelRect.m_X + m_LastPanelRect.m_W * 0.5f, m_LastPanelRect.m_Y + m_LastPanelRect.m_H * 0.5f);
				return true;
			}
			else if(CUIRect(m_LastPanelRect.m_X, m_LastPanelRect.m_Y, m_LastPanelRect.m_W, m_LastPanelRect.m_H).Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::DRAGGING;
				m_DragOffset = Mouse - vec2(m_LastPanelRect.m_X, m_LastPanelRect.m_Y);
				return true;
			}
		}
		if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool WasEditing = m_EditorInteraction != EEditorInteraction::IDLE;
			m_EditorInteraction = EEditorInteraction::IDLE;
			return WasEditing;
		}
		return false;
	}
	return false;
}

bool CAetherMusicPlayer::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	if(!m_EditorOpen)
		return false;
	return m_EditorInteraction != EEditorInteraction::IDLE;
}
