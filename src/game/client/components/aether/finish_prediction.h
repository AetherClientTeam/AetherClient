#ifndef GAME_CLIENT_COMPONENTS_AETHER_FINISH_PREDICTION_H
#define GAME_CLIENT_COMPONENTS_AETHER_FINISH_PREDICTION_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <base/vmath.h>

#include <cstdint>
#include <vector>

class CAetherFinishPrediction : public CComponent
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

	struct SEstimateState
	{
		bool m_Valid = false;
		bool m_HasPredictedTime = false;
		float m_Progress = 0.0f;
		int64_t m_CurrentTimeMs = 0;
		int64_t m_PredictedFinishTimeMs = 0;
		int64_t m_RemainingTimeMs = 0;
	};

	std::vector<int> m_vDistances;
	std::vector<unsigned char> m_vPassable;
	std::vector<ivec2> m_vStartTiles;
	std::vector<ivec2> m_vFinishTiles;
	int m_PathMapWidth = 0;
	int m_PathMapHeight = 0;
	int m_RaceStartTick = -1;
	float m_RaceStartDistance = -1.0f;
	float m_LastProgress = 0.0f;
	int64_t m_SmoothedFinishTimeMs = -1;
	int m_LastPredictTick = -1;

	float PanelScale() const;
	CUIRect PanelRect() const;
	CUIRect ResizeHandleRect() const;
	vec2 HudMousePos() const;
	void ClampOffsets();
	void ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight);
	void SetScaleKeepingCenter(int NewScale, vec2 Center);
	void ResetRunState();
	void ClearPathData();
	bool CurrentLocalPos(vec2 *pPos) const;
	bool RebuildPathData();
	bool EnsurePathData();
	float DistanceAtPos(vec2 Pos) const;
	float StartDistance() const;
	int64_t ScoreboardTimeMs(int ClientId) const;
	int64_t BestTimeMs() const;
	int64_t PersonalBestTimeMs() const;
	int64_t AverageTimeMs() const;
	int64_t ReferenceTimeMs() const;
	bool TimeFallback(SEstimateState *pState, int64_t CurrentTimeMs);
	bool EstimateState(SEstimateState *pState);
	bool Estimate(float *pProgress, int *pElapsedMs, int *pLeftMs);
	void RenderPanel(CUIRect Rect);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
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
