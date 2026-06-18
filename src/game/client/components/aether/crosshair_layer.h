#ifndef GAME_CLIENT_COMPONENTS_AETHER_CROSSHAIR_LAYER_H
#define GAME_CLIENT_COMPONENTS_AETHER_CROSSHAIR_LAYER_H

#include <game/client/component.h>

class CAetherCrosshairLayer : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
