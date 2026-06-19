#include "crosshair_layer.h"

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

void CAetherCrosshairLayer::OnRender()
{
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;
	GameClient()->m_Hud.RenderCursorOverlay();
}
