#include "crosshair_layer.h"

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

void CAetherCrosshairLayer::OnRender()
{
	GameClient()->m_Hud.RenderCursorOverlay();
}
