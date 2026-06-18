#ifndef GAME_CLIENT_COMPONENTS_AETHER_OPTIMIZER_H
#define GAME_CLIENT_COMPONENTS_AETHER_OPTIMIZER_H

#include <game/client/component.h>

#include <cstdint>

class CAetherOptimizer : public CComponent
{
	int m_LastHighPriority = -1;
	int m_LastDiscordBelowNormal = -1;
	int64_t m_LastDiscordScanTime = 0;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
