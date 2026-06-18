#ifndef GAME_CLIENT_COMPONENTS_AETHER_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_AETHER_MUSIC_PLAYER_H

#include "media_backend.h"

#include <engine/graphics.h>

#include <game/client/component.h>

#include <array>

class CAetherMusicPlayer : public CComponent
{
	enum class EEditorInteraction
	{
		IDLE,
		DRAGGING,
		RESIZING,
	};

	static constexpr float PANEL_WIDTH = 76.0f;
	static constexpr float PANEL_HEIGHT = 22.0f;

	CAetherMediaBackend m_MediaBackend;
	IGraphics::CTextureHandle m_ArtworkTexture;
	IGraphics::CTextureHandle m_FallbackLogoTexture;
	uint64_t m_LoadedArtworkGeneration = 0;
	uint32_t m_ArtworkWidth = 0;
	uint32_t m_ArtworkHeight = 0;
	AetherMusic::SColor m_DynamicColor{0.10f, 0.12f, 0.16f};
	AetherMusic::SColor m_ArtworkAccent{0.35f, 0.68f, 1.0f};
	AetherMusic::SColor m_TargetDynamicColor{0.10f, 0.12f, 0.16f};
	AetherMusic::SColor m_TargetArtworkAccent{0.35f, 0.68f, 1.0f};
	int64_t m_LastColorUpdateMs = 0;
	bool m_HasArtwork = false;
	bool m_EditorOpen = false;
	EEditorInteraction m_EditorInteraction = EEditorInteraction::IDLE;
	vec2 m_DragOffset;
	vec2 m_ResizeAnchor;
	vec2 m_ResizeCenter;
	AetherMusic::SRect m_LastPanelRect;
	std::array<float, 5> m_aSmoothedBands{};
	std::array<float, 5> m_aPreviousRawBands{};
	std::array<float, 5> m_aSlowRawBands{};
	std::array<float, 5> m_aBandPunch{};
	float m_LastRmsEnergy = 0.0f;
	float m_BassPulse = 0.0f;
	int64_t m_LastVisualizerUpdateMs = 0;
	bool m_FallbackLogoTried = false;

	float PanelScale() const;
	float DynamicPanelWidth(float Scale) const;
	AetherMusic::SRect ResizeHandleRect() const;
	void UpdateArtwork(const CAetherMediaBackend::SSnapshot &Snapshot);
	void UpdateColors();
	void ReleaseArtwork();
	void ReleaseFallbackLogo();
	void RenderPanel(const CAetherMediaBackend::SSnapshot &Snapshot);
	void RenderFallbackMonogram(const AetherMusic::SRect &ArtworkRect, float Alpha);
	void RenderVisualizer(const AetherMusic::SRect &PanelRect, const CAetherMediaBackend::SSnapshot &Snapshot, AetherMusic::SColor Accent, float Alpha);
	void ClampOffsets();
	void ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight);
	void SetScaleKeepingCenter(int NewScale, vec2 Center);
	vec2 HudMousePos() const;

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnRender() override;
	void OnUpdate() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool OpenEditor();
	void CloseEditor();
	bool IsEditorOpen() const { return m_EditorOpen; }
};

#endif
