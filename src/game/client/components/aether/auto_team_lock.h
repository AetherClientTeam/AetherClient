#ifndef GAME_CLIENT_COMPONENTS_AETHER_AUTO_TEAM_LOCK_H
#define GAME_CLIENT_COMPONENTS_AETHER_AUTO_TEAM_LOCK_H

#include <game/client/component.h>

#include <cstdint>

class CAetherAutoTeamLock : public CComponent
{
	int m_LastTeam = 0;
	int m_PendingTeam = 0;
	int64_t m_LockAt = 0;
	bool m_LockSent = false;

	int CurrentDDTeam() const;
	void ResetLockState();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif
