#ifndef GAME_CLIENT_COMPONENTS_AETHER_STABILITY_TRAINER_H
#define GAME_CLIENT_COMPONENTS_AETHER_STABILITY_TRAINER_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <base/color.h>
#include <base/vmath.h>

class CAetherStabilityTrainer : public CComponent
{
	enum class EEditorInteraction
	{
		IDLE,
		DRAGGING,
		RESIZING,
	};

	int m_LastTrackId = -1;
	float m_RollingQuality = 0.0f;
	float m_SmoothBar = 0.5f;
	bool m_WarnAmbiguous = false;
	bool m_EditorOpen = false;
	EEditorInteraction m_EditorInteraction = EEditorInteraction::IDLE;
	vec2 m_DragOffset;
	vec2 m_ResizeCenter;
	CUIRect m_LastRect;

	bool IsLocalClientId(int ClientId) const;
	int ResolveTrackId() const;
	vec2 ResolveVelocity(int TrackId) const;
	void ResetState();
	void UpdateState();
	float BarTarget(vec2 Vel, int TrackId, bool *pWarnOut) const;
	float Quality(vec2 Vel, int TrackId, bool *pWarnOut) const;
	ColorRGBA QualityColor(float Quality) const;
	float PanelScale() const;
	CUIRect PanelRect() const;
	CUIRect ResizeHandleRect() const;
	vec2 HudMousePos() const;
	void ClampOffsets();
	void ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight);
	void SetScaleKeepingCenter(int NewScale, vec2 Center);
	void RenderPanel(bool ForcePreview);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnStateChange(int NewState, int OldState) override;

	bool OpenEditor();
	void CloseEditor();
	bool IsEditorOpen() const { return m_EditorOpen; }
};

#endif
