#include "crosshair_layer.h"

#include <game/client/gameclient.h>

void CAetherCrosshairLayer::OnRender()
{
	GameClient()->m_Hud.RenderCursorOverlay();
}
