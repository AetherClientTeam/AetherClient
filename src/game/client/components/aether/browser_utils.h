#ifndef GAME_CLIENT_COMPONENTS_AETHER_BROWSER_UTILS_H
#define GAME_CLIENT_COMPONENTS_AETHER_BROWSER_UTILS_H

#include <game/client/component.h>

#include <cstdint>

class CAetherBrowserUtils : public CComponent
{
	int64_t m_LastRefresh = 0;
	int64_t m_SkipRefreshUntil = 0;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif
