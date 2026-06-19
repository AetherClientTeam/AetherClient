#include "real_hitbox.h"

#include <base/color.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/gamecore.h>

#include <generated/protocol.h>

#include <algorithm>

void CAetherRealHitbox::OnRender()
{
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;
	if(!g_Config.m_AeShowRealHitbox)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_apPlayerInfos[ClientId])
		return;
	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(!ClientData.m_Active)
		return;

	Graphics()->TextureClear();
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AeRealHitboxColor)).WithAlpha(1.0f);
	const vec2 Pos = ClientData.m_RenderPos;
	const float DotSize = std::max(3.0f, CCharacterCore::PhysicalSize() / 7.0f);
	const float Half = DotSize * 0.5f;
	Graphics()->DrawRect(Pos.x - Half, Pos.y - Half, DotSize, DotSize, Color, IGraphics::CORNER_ALL, Half);
}
