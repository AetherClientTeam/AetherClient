#ifndef GAME_CLIENT_COMPONENTS_AETHER_FAIL_SOUND_H
#define GAME_CLIENT_COMPONENTS_AETHER_FAIL_SOUND_H

#include <game/client/component.h>

class CAetherFailSound : public CComponent
{
	int m_LocalSampleId = -1;
	int m_OthersSampleId = -1;
	int m_TeamLastSampleId = -1;
	int m_LastProcessedTick = -1;
	unsigned m_SamplesFingerprint = 0xffffffffu;
	bool m_TeamLastPrev = false;

	static unsigned SamplesFingerprint();
	static bool ValidDDRaceTeam(int Team);
	bool IsLocalClientId(int ClientId) const;
	bool ShouldPlayOthers(int ClientId) const;
	bool TeamLastNow() const;
	void EnsureSamples();
	void UnloadSamples();
	void PlaySample(int SampleId, int Volume);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
};

#endif
