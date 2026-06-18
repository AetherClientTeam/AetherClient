#ifndef GAME_CLIENT_COMPONENTS_AETHER_SESSION_STATS_H
#define GAME_CLIENT_COMPONENTS_AETHER_SESSION_STATS_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <base/vmath.h>

class CAetherSessionStats : public CComponent
{
	enum class EEditorInteraction
	{
		IDLE,
		DRAGGING,
		RESIZING,
	};

	bool m_EditorOpen = false;
	EEditorInteraction m_EditorInteraction = EEditorInteraction::IDLE;
	vec2 m_DragOffset;
	vec2 m_ResizeCenter;
	CUIRect m_LastRect;

	int64_t m_SessionStart = 0;
	int m_Deaths = 0;

	float PanelScale() const;
	CUIRect PanelRect() const;
	CUIRect ResizeHandleRect() const;
	vec2 HudMousePos() const;
	void ClampOffsets();
	void ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight);
	void SetScaleKeepingCenter(int NewScale, vec2 Center);
	void ResetStats();
	void RenderPanel(CUIRect Rect);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnStateChange(int NewState, int OldState) override;

	bool OpenEditor();
	void CloseEditor();
	bool IsEditorOpen() const { return m_EditorOpen; }
};

#endif
