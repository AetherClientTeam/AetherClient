/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "maplayers.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <iterator>

CMapLayers::CMapLayers(ERenderType Type, bool OnlineOnly)
{
	m_Type = Type;
	m_OnlineOnly = OnlineOnly;

	// static parameters for ingame rendering
	m_Params.m_RenderType = m_Type;
	m_Params.m_RenderInvalidTiles = false;
	m_Params.m_TileAndQuadBuffering = true;
	m_Params.m_RenderTileBorder = true;
}

void CMapLayers::OnInit()
{
	m_pLayers = Layers();
	m_pImages = &GameClient()->m_MapImages;
	m_MapRenderer.Clear();
}

CCamera *CMapLayers::GetCurCamera()
{
	return &GameClient()->m_Camera;
}

void CMapLayers::OnMapLoad()
{
	FRenderUploadCallback FRenderCallback = [&](const char *pTitle, const char *pMessage, int IncreaseCounter) { GameClient()->m_Menus.RenderLoading(pTitle, pMessage, IncreaseCounter); };
	auto FRenderCallbackOptional = std::make_optional<FRenderUploadCallback>(FRenderCallback);

	const char *pLoadingTitle = Localize("Loading map");
	const char *pLoadingMessage = Localize("Uploading map data to GPU");
	GameClient()->m_Menus.RenderLoading(pLoadingTitle, pLoadingMessage, 0);

	// can't do that in CMapLayers::OnInit, because some of this interfaces are not available yet
	m_MapRenderer.OnInit(Graphics(), TextRender(), RenderMap());

	m_EnvEvaluator = CEnvelopeState(m_pLayers->Map(), m_OnlineOnly);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
	m_MapRenderer.Load(m_Type, m_pLayers, m_pImages, &m_EnvEvaluator, FRenderCallbackOptional);
}

void CMapLayers::OnRender()
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// dynamic parameters for ingame rendering
	m_Params.m_EntityOverlayVal = m_Type == RENDERTYPE_FULL_DESIGN ? 0 : g_Config.m_ClOverlayEntities;
	m_Params.m_Center = GetCurCamera()->m_Center;
	m_Params.m_Zoom = GetCurCamera()->m_Zoom;
	m_Params.m_RenderText = g_Config.m_ClTextEntities;
	m_Params.m_DebugRenderGroupClips = g_Config.m_DbgRenderGroupClips;
	m_Params.m_DebugRenderQuadClips = g_Config.m_DbgRenderQuadClips;
	m_Params.m_DebugRenderClusterClips = g_Config.m_DbgRenderClusterClips;
	m_Params.m_DebugRenderTileClips = g_Config.m_DbgRenderTileClips;

	m_MapRenderer.Render(m_Params);

	if(m_Type == ERenderType::RENDERTYPE_FOREGROUND &&
		g_Config.m_AeOptimizer && g_Config.m_AeOptimizerFpsFog && g_Config.m_AeOptimizerFpsFogRenderRect)
	{
		float Width, Height;
		Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), m_Params.m_Zoom, &Width, &Height);
		if(!std::isfinite(Width) || !std::isfinite(Height) || Width <= 0.0f || Height <= 0.0f ||
			!std::isfinite(m_Params.m_Center.x) || !std::isfinite(m_Params.m_Center.y) ||
			!std::isfinite(m_Params.m_Zoom) || m_Params.m_Zoom <= 0.0f)
		{
			return;
		}
		Graphics()->MapScreen(m_Params.m_Center.x - Width / 2.0f, m_Params.m_Center.y - Height / 2.0f,
			m_Params.m_Center.x + Width / 2.0f, m_Params.m_Center.y + Height / 2.0f);

		const int RadiusTiles = std::clamp(g_Config.m_AeOptimizerFpsFogRadius, 6, 160);
		const float Radius = RadiusTiles * 32.0f;
		const float Left = m_Params.m_Center.x - Radius;
		const float Right = m_Params.m_Center.x + Radius;
		const float Top = m_Params.m_Center.y - Radius;
		const float Bottom = m_Params.m_Center.y + Radius;
		constexpr float MaxDebugCoord = 1000000.0f;
		if(!std::isfinite(Radius) || Radius <= 0.0f ||
			!std::isfinite(Left) || !std::isfinite(Right) || !std::isfinite(Top) || !std::isfinite(Bottom) ||
			std::abs(Left) > MaxDebugCoord || std::abs(Right) > MaxDebugCoord ||
			std::abs(Top) > MaxDebugCoord || std::abs(Bottom) > MaxDebugCoord ||
			Right <= Left || Bottom <= Top)
		{
			return;
		}
		const IGraphics::CLineItem aLines[] = {
			IGraphics::CLineItem(Left, Top, Right, Top),
			IGraphics::CLineItem(Right, Top, Right, Bottom),
			IGraphics::CLineItem(Right, Bottom, Left, Bottom),
			IGraphics::CLineItem(Left, Bottom, Left, Top),
		};
		Graphics()->LinesBegin();
		Graphics()->SetColor(0.55f, 0.75f, 1.0f, 0.75f);
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
	}
}
