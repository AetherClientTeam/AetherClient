#ifndef GAME_CLIENT_COMPONENTS_AETHER_INPUT_VISUALIZER_H
#define GAME_CLIENT_COMPONENTS_AETHER_INPUT_VISUALIZER_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <base/vmath.h>

#include <array>
#include <deque>

struct CNetObj_Character;

class CAetherInputVisualizer : public CComponent
{
	enum class EEditorInteraction
	{
		IDLE,
		DRAGGING,
		RESIZING,
	};

	enum class ELane
	{
		LEFT,
		RIGHT,
		JUMP,
		FIRE,
		HOOK,
		COUNT,
	};

	struct SSegment
	{
		double m_Start = 0.0;
		double m_End = 0.0;
		ELane m_Lane = ELane::LEFT;
	};

	static constexpr int MAX_LANES = (int)ELane::COUNT;
	static constexpr double HISTORY_SECONDS = 45.0;
	static constexpr size_t MAX_SEGMENTS = 4000;

	bool m_EditorOpen = false;
	EEditorInteraction m_EditorInteraction = EEditorInteraction::IDLE;
	vec2 m_DragOffset;
	vec2 m_ResizeCenter;
	CUIRect m_LastRect;
	std::array<bool, MAX_LANES> m_aWasDown = {};
	std::array<bool, MAX_LANES> m_aLaneActive = {};
	std::array<double, MAX_LANES> m_aHoldStart = {};
	std::deque<SSegment> m_vSegments;
	int m_SourceKey = -1;
	double m_FrozenClock = 0.0;

	float PanelScale() const;
	CUIRect PanelRect() const;
	CUIRect ResizeHandleRect() const;
	vec2 HudMousePos() const;
	void ClampOffsets();
	void ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight);
	void SetScaleKeepingCenter(int NewScale, vec2 Center);
	void ClearLaneHistory();
	double TimelineNow(bool WantRemote, bool WantDemoReplay) const;
	bool WantsRemoteInput() const;
	bool WantsDemoReplayInput() const;
	void BuildDownFromLocal(std::array<bool, MAX_LANES> &aDown) const;
	void BuildDownFromCharacter(const CNetObj_Character *pChar, int GameTick, std::array<bool, MAX_LANES> &aDown) const;
	void UpdateInputState(double Now, bool WantRemote, bool WantDemoReplay);
	ColorRGBA LaneColor(ELane Lane) const;
	const char *LaneLabel(ELane Lane) const;
	std::array<ELane, MAX_LANES> VisibleLanes(int &Count) const;
	void RenderInternal(bool ForcePreview);
	void RenderHorizontal(CUIRect Inner, double Now);
	void RenderVertical(CUIRect Inner, double Now);

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
