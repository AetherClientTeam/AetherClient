#ifndef GAME_CLIENT_COMPONENTS_AETHER_PSA_H
#define GAME_CLIENT_COMPONENTS_AETHER_PSA_H

#include <game/client/component.h>

class CAetherPsa : public CComponent
{
public:
	enum EAutoPhase
	{
		AUTO_NONE = 0,
		AUTO_WAIT_START_CLICK,
		AUTO_COUNTDOWN,
		AUTO_LOW,
		AUTO_HIGH,
		AUTO_WAIT_PICK,
	};

private:
	float m_Center = 200.0f;
	int m_Step = 0;
	bool m_Active = false;
	bool m_Completed = false;
	bool m_TimerActive = false;
	double m_TimerEnd = 0.0;
	bool m_TimerDone = false;
	int m_AutoPhase = AUTO_NONE;
	int m_AutoCountdown = 0;
	double m_AutoCountdownNext = 0.0;
	double m_AutoPhaseEnd = 0.0;
	int m_AutoPhaseSeconds = 300;

	void ApplyPointerValue(int Value) const;
	void AutoCancelInternal();
	void AutoBeginLowPhase();
	void AutoBeginHighPhase();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;

	void Start(int BaseValue);
	void Reset(int BaseValue);
	void SelectLow();
	void SelectHigh();
	void StartTimer(int Seconds);
	void ApplyCurrent(bool UseLow) const;
	void ApplyBase() const;
	void AutoStart(int PhaseSeconds);
	void AutoCancel();
	void AutoSkip();

	bool IsActive() const { return m_Active && !m_Completed; }
	bool IsCompleted() const { return m_Completed; }
	bool AutoInProgress() const;
	int AutoPhase() const { return m_AutoPhase; }
	int AutoCountdown() const { return m_AutoCountdown; }
	int StepDisplay() const;
	void GetTriplet(int *pLow, int *pBase, int *pHigh) const;
	int SuggestedValue() const;
};

#endif
