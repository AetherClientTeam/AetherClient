#ifndef GAME_CLIENT_COMPONENTS_AETHER_AIM_TRAINING_H
#define GAME_CLIENT_COMPONENTS_AETHER_AIM_TRAINING_H

#include <game/client/component.h>

#include <base/vmath.h>

#include <vector>

class CAetherAimTraining : public CComponent
{
	enum class EState
	{
		IDLE,
		WAITING_CLICK,
		COUNTDOWN,
		RUNNING,
	};

	struct STarget
	{
		vec2 m_Offset;
		float m_Radius = 0.0f;
		float m_StartRadius = 0.0f;
		int64_t m_SpawnTime = 0;
	};

	std::vector<STarget> m_vTargets;
	int m_Hits = 0;
	int m_Misses = 0;
	int m_Combo = 0;
	int m_BestCombo = 0;
	EState m_State = EState::IDLE;
	int64_t m_CountdownStart = 0;

	vec2 CenterWorld() const;
	vec2 CurrentAimWorld() const;
	vec2 NativeMouseWorld() const;
	float BaseRadius() const;
	float MaxDistance() const;
	float DespawnSeconds() const;
	bool IsConfiguredAndPlayable() const;
	bool IsOverlayActive() const;
	bool IsRunning() const;
	STarget NewTarget() const;
	void EnsureTargets();
	void TryHit();
	void BlockGameAttackInput();
	void ClearGameAttackInput();
	void Stop();
	void RenderDim(float *pPoints) const;
	void RenderTarget(const STarget &Target, vec2 Center, int64_t Now) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnStateChange(int NewState, int OldState) override;

	void Restart();
};

#endif
