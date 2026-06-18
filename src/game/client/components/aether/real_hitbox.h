#ifndef GAME_CLIENT_COMPONENTS_AETHER_REAL_HITBOX_H
#define GAME_CLIENT_COMPONENTS_AETHER_REAL_HITBOX_H

#include <game/client/component.h>

class CAetherRealHitbox : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
