/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/textrender.h>

#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/aether/client_variant.h>
#include <game/client/components/motd.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace
{
constexpr int KOG_POINTS_REFRESH_SECONDS = 600;
constexpr int KOG_POINTS_REQUEST_SECONDS = 20;
constexpr int KOG_POINTS_ENDPOINT_BACKOFF_SECONDS = 300;
constexpr float SCOREBOARD_PLAYER_POPUP_WIDTH = 178.0f;

ColorRGBA AetherGradientNicknameColor(int ClientId, float Alpha, const CAetherBadges::SGradientNicknameStyle &Style)
{
	ColorRGBA Start = color_cast<ColorRGBA>(ColorHSLA(Style.m_StartColor));
	ColorRGBA End = color_cast<ColorRGBA>(ColorHSLA(Style.m_EndColor));
	float T = (ClientId % 7) / 6.0f;
	const float Seconds = time_get() / (float)time_freq();
	if(Style.m_Animated)
	{
		T = 0.5f + 0.5f * std::sin(Seconds * (0.25f + Style.m_Speed / 45.0f) + ClientId * 0.73f);
	}
	float Glow = 0.0f;
	if(Style.m_Style == 1)
	{
		const float Shine = std::pow(0.5f + 0.5f * std::sin(Seconds * (1.0f + Style.m_Speed / 30.0f) + ClientId * 0.37f), 3.0f);
		Glow = minimum(1.0f, Glow * 1.9f + 0.24f + Shine * 0.28f);
		Start.r = mix(Start.r, 1.0f, 0.14f + Shine * 0.34f);
		Start.g = mix(Start.g, 0.92f, 0.12f + Shine * 0.28f);
		Start.b = mix(Start.b, 0.48f, Shine * 0.18f);
		End.r = mix(End.r, 1.0f, 0.18f + Shine * 0.38f);
		End.g = mix(End.g, 0.96f, 0.16f + Shine * 0.32f);
		End.b = mix(End.b, 0.58f, Shine * 0.20f);
	}
	else if(Style.m_Style == 2)
	{
		const float Pulse = 0.5f + 0.5f * std::sin(Seconds * (0.85f + Style.m_Speed / 55.0f) + ClientId * 1.11f);
		Glow = minimum(1.0f, Glow * 1.65f + 0.20f + Pulse * 0.18f);
		Start.r = mix(Start.r, 1.0f, 0.10f + Pulse * 0.12f);
		Start.g = mix(Start.g, 1.0f, 0.08f + Pulse * 0.10f);
		Start.b = mix(Start.b, 1.0f, 0.10f + Pulse * 0.12f);
		End.r = mix(End.r, 1.0f, 0.10f + (1.0f - Pulse) * 0.12f);
		End.g = mix(End.g, 1.0f, 0.08f + (1.0f - Pulse) * 0.10f);
		End.b = mix(End.b, 1.0f, 0.10f + (1.0f - Pulse) * 0.12f);
	}
	ColorRGBA Out;
	Out.r = mix(Start.r, End.r, T);
	Out.g = mix(Start.g, End.g, T);
	Out.b = mix(Start.b, End.b, T);
	Out.a = Alpha;
	Out.r = mix(Out.r, 1.0f, Glow * 0.18f);
	Out.g = mix(Out.g, 1.0f, Glow * 0.18f);
	Out.b = mix(Out.b, 1.0f, Glow * 0.18f);
	if(Style.m_Style == 1)
	{
		Out.r = minimum(1.0f, Out.r * 1.22f + 0.08f);
		Out.g = minimum(1.0f, Out.g * 1.22f + 0.08f);
		Out.b = minimum(1.0f, Out.b * 1.22f + 0.08f);
	}
	return Out;
}

bool AetherGradientNicknameGlow(const CAetherBadges::SGradientNicknameStyle &Style, const ColorRGBA &TextColor, ColorRGBA &GlowColor, float &GlowRadius)
{
	(void)Style;
	GlowColor = TextColor;
	GlowColor.a = 0.0f;
	GlowRadius = 0.0f;
	return false;
}

bool AetherScoreboardIsKogServer(IClient *pClient)
{
	CServerInfo ServerInfo = {};
	pClient->GetServerInfo(&ServerInfo);
	return str_find_nocase(ServerInfo.m_aName, "kog") || str_find_nocase(ServerInfo.m_aGameType, "kog") || str_find_nocase(ServerInfo.m_aAddress, "kog");
}

void AetherBuildApiUrl(char *pOut, int OutSize, const char *pPath)
{
	char aBase[256];
	str_copy(aBase, g_Config.m_AeBadgesApiUrl, sizeof(aBase));
	int Len = str_length(aBase);
	while(Len > 0 && aBase[Len - 1] == '/')
		aBase[--Len] = '\0';
	str_format(pOut, OutSize, "%s%s", aBase, pPath);
}

bool AetherScoreboardCanLockLocalTeam(CGameClient *pGameClient)
{
	const int Team = pGameClient->AetherLocalDDTeam();
	return Team > TEAM_FLOCK && Team != TEAM_SUPER;
}

float AetherScoreboardPlayerPopupHeight(bool IsLocal, bool IsSpectating, bool HasTeamLockButton)
{
	float Height = IsLocal ? 118.0f : (AetherVariant::WarlistEnabled() ? 257.0f : 185.0f);
	Height += HasTeamLockButton && IsLocal ? 22.0f : 0.0f;
	if(!IsSpectating)
		Height += 22.0f;
	return Height;
}

ColorRGBA AetherScoreboardAccentColor(CGameClient *pGameClient, int ClientId, float Alpha)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		const int DDTeam = pGameClient->m_Teams.Team(ClientId);
		if(DDTeam != TEAM_FLOCK)
			return pGameClient->GetDDTeamColor(DDTeam).WithAlpha(Alpha);
	}
	return ColorRGBA(0.98f, 0.68f, 1.0f, Alpha);
}

void AetherScoreboardTeamLabel(CGameClient *pGameClient, int ClientId, char *pBuffer, int BufferSize)
{
	const int DDTeam = pGameClient->m_Teams.Team(ClientId);
	if(DDTeam == TEAM_SUPER)
		str_copy(pBuffer, Localize("Super"), BufferSize);
	else if(DDTeam == TEAM_FLOCK)
		str_copy(pBuffer, Localize("No team"), BufferSize);
	else
		str_format(pBuffer, BufferSize, Localize("Team %d"), DDTeam);
}

const json_value *JsonObjectGetNoCase(const json_value *pObject, const char *pIndex)
{
	if(!pObject || pObject->type != json_object || !pIndex)
		return nullptr;
	if(const json_value *pExact = json_object_get(pObject, pIndex))
		return pExact;
	for(unsigned i = 0; i < pObject->u.object.length; ++i)
	{
		if(str_comp_nocase(pObject->u.object.values[i].name, pIndex) == 0)
			return pObject->u.object.values[i].value;
	}
	return nullptr;
}

const char *JsonStringValue(const json_value *pValue, const char *pFallback = "")
{
	const char *pString = json_string_get(pValue);
	return pString ? pString : pFallback;
}

bool JsonIntFlexible(const json_value *pValue, int &Out)
{
	if(!pValue)
		return false;
	if(pValue->type == json_integer)
	{
		Out = (int)pValue->u.integer;
		return true;
	}
	if(pValue->type == json_double)
	{
		Out = (int)pValue->u.dbl;
		return true;
	}
	if(pValue->type == json_string && pValue->u.string.ptr && pValue->u.string.ptr[0])
	{
		char *pEnd = nullptr;
		const long Value = std::strtol(pValue->u.string.ptr, &pEnd, 10);
		if(pEnd && pEnd != pValue->u.string.ptr)
		{
			Out = (int)Value;
			return true;
		}
	}
	return false;
}

bool JsonKogPointsValue(const json_value *pValue, int &Out)
{
	if(JsonIntFlexible(pValue, Out))
		return true;
	if(!pValue || pValue->type != json_object)
		return false;
	const char *apPointKeys[] = {"points", "Points", "total_points", "totalPoints", "rank_points", "rankPoints"};
	for(const char *pKey : apPointKeys)
	{
		if(JsonIntFlexible(JsonObjectGetNoCase(pValue, pKey), Out))
			return true;
	}
	const json_value *pData = JsonObjectGetNoCase(pValue, "data");
	if(pData && pData != pValue && JsonKogPointsValue(pData, Out))
		return true;
	const json_value *pPlayer = JsonObjectGetNoCase(pValue, "player");
	return pPlayer && pPlayer != pValue && JsonKogPointsValue(pPlayer, Out);
}
}

CScoreboard::CScoreboard()
{
	OnReset();
}

void CScoreboard::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CScoreboard::LockMouse()
{
	Ui()->ClosePopupMenus();
	m_MouseUnlocked = false;
	SetUiMousePos(m_LastMousePos.value());
	m_LastMousePos = Ui()->MousePos();
}

void CScoreboard::ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	pSelf->GameClient()->m_Spectator.OnRelease();
	pSelf->GameClient()->m_Emoticon.OnRelease();

	pSelf->m_Active = pResult->GetInteger(0) != 0;

	if(!pSelf->IsActive() && pSelf->m_MouseUnlocked)
	{
		pSelf->LockMouse();
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	if(!pSelf->IsActive() ||
		pSelf->GameClient()->m_Menus.IsActive() ||
		pSelf->GameClient()->m_Chat.IsActive() ||
		pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	pSelf->m_MouseUnlocked = !pSelf->m_MouseUnlocked;

	if(!pSelf->m_MouseUnlocked)
	{
		pSelf->Ui()->ClosePopupMenus();
	}

	vec2 OldMousePos = pSelf->Ui()->MousePos();

	if(pSelf->m_LastMousePos == std::nullopt)
	{
		pSelf->SetUiMousePos(pSelf->Ui()->Screen()->Center());
	}
	else
	{
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
	}

	// save pos, so moving the mouse in esc menu doesn't change the position
	pSelf->m_LastMousePos = OldMousePos;
}

void CScoreboard::OnConsoleInit()
{
	Console()->Register("+scoreboard", "", CFGFLAG_CLIENT, ConKeyScoreboard, this, "Show scoreboard");
	Console()->Register("toggle_scoreboard_cursor", "", CFGFLAG_CLIENT, ConToggleScoreboardCursor, this, "Toggle scoreboard cursor");
}

void CScoreboard::OnInit()
{
	m_DeadTeeTexture = Graphics()->LoadTexture("deadtee.png", IStorage::TYPE_ALL);
}

void CScoreboard::OnReset()
{
	m_Active = false;
	m_MouseUnlocked = false;
	m_LastMousePos = std::nullopt;
	ResetKogPoints();
}

void CScoreboard::OnRelease()
{
	m_Active = false;

	if(m_MouseUnlocked)
	{
		LockMouse();
	}
}

void CScoreboard::ResetKogPoints()
{
	m_pKogPointsRequest = nullptr;
	m_KogPointsPendingNameCount = 0;
	m_LastKogPointsRequestTime = 0;
	m_KogPointsUnavailableUntil = 0;
	str_copy(m_aKogPointsApiUrl, g_Config.m_AeBadgesApiUrl, sizeof(m_aKogPointsApiUrl));
	for(auto &Entry : m_aKogPoints)
	{
		Entry.m_aName[0] = '\0';
		Entry.m_Points = 0;
		Entry.m_HasPoints = false;
		Entry.m_LastUpdateTime = 0;
	}
	for(auto &aName : m_aaKogPointsPendingNames)
		aName[0] = '\0';
}

CScoreboard::SKogPointsEntry *CScoreboard::KogPointsEntryForName(const char *pName, bool Create)
{
	if(!pName || !pName[0])
		return nullptr;
	for(auto &Entry : m_aKogPoints)
	{
		if(Entry.m_aName[0] && str_comp_nocase(Entry.m_aName, pName) == 0)
			return &Entry;
	}
	if(!Create)
		return nullptr;
	for(auto &Entry : m_aKogPoints)
	{
		if(!Entry.m_aName[0])
		{
			str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));
			return &Entry;
		}
	}
	return nullptr;
}

void CScoreboard::ApplyKogPointsForName(const char *pName, int Points)
{
	if(!pName || !pName[0])
		return;

	const int64_t Now = time_get();
	SKogPointsEntry *pEntry = KogPointsEntryForName(pName, true);
	if(!pEntry)
		return;
	pEntry->m_Points = Points;
	pEntry->m_HasPoints = true;
	pEntry->m_LastUpdateTime = Now;
}

bool CScoreboard::KogPointsForClient(int ClientId, int &Points) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(!pName[0])
		return false;
	for(const auto &Entry : m_aKogPoints)
	{
		if(Entry.m_HasPoints && str_comp_nocase(Entry.m_aName, pName) == 0)
		{
			Points = Entry.m_Points;
			return true;
		}
	}
	return false;
}

void CScoreboard::PumpKogPointsRequest()
{
	if(!m_pKogPointsRequest || !m_pKogPointsRequest->Done())
		return;

	const EHttpState HttpState = m_pKogPointsRequest->State();
	const int StatusCode = HttpState == EHttpState::DONE ? m_pKogPointsRequest->StatusCode() : 0;
	if(HttpState == EHttpState::DONE && StatusCode >= 200 && StatusCode < 400)
	{
		json_value *pJson = m_pKogPointsRequest->ResultJson();
		const json_value *pPlayers = pJson && pJson->type == json_object ? JsonObjectGetNoCase(pJson, "players") : pJson;
		if(pPlayers && pPlayers->type == json_object)
		{
			for(int i = 0; i < m_KogPointsPendingNameCount; ++i)
			{
				int Points = 0;
				const json_value *pEntry = JsonObjectGetNoCase(pPlayers, m_aaKogPointsPendingNames[i]);
				if(JsonKogPointsValue(pEntry, Points))
					ApplyKogPointsForName(m_aaKogPointsPendingNames[i], Points);
			}
		}
		else if(pPlayers && pPlayers->type == json_array)
		{
			for(unsigned i = 0; i < pPlayers->u.array.length; ++i)
			{
				const json_value *pEntry = pPlayers->u.array.values[i];
				if(!pEntry || pEntry->type != json_object)
					continue;
				const char *pName = JsonStringValue(JsonObjectGetNoCase(pEntry, "player_name"));
				if(!pName[0])
					pName = JsonStringValue(JsonObjectGetNoCase(pEntry, "name"));
				if(!pName[0])
					pName = JsonStringValue(JsonObjectGetNoCase(pEntry, "player"));
				int Points = 0;
				if(pName[0] && JsonKogPointsValue(pEntry, Points))
					ApplyKogPointsForName(pName, Points);
			}
		}
		if(pJson)
			json_value_free(pJson);
	}
	else if(StatusCode == 403 || StatusCode == 404 || StatusCode == 503)
	{
		m_KogPointsUnavailableUntil = time_get() + (int64_t)KOG_POINTS_ENDPOINT_BACKOFF_SECONDS * time_freq();
	}

	m_pKogPointsRequest = nullptr;
	m_KogPointsPendingNameCount = 0;
}

void CScoreboard::RequestKogPoints(bool KogServer)
{
	if(!g_Config.m_AeKogPointsScoreboard || !KogServer || g_Config.m_AeBadgesApiUrl[0] == '\0')
		return;
	if(str_comp(m_aKogPointsApiUrl, g_Config.m_AeBadgesApiUrl) != 0)
		ResetKogPoints();
	if(m_pKogPointsRequest)
		return;

	const int64_t Now = time_get();
	if(m_KogPointsUnavailableUntil > Now)
		return;
	if(m_LastKogPointsRequestTime != 0 && Now - m_LastKogPointsRequestTime < (int64_t)KOG_POINTS_REQUEST_SECONDS * time_freq())
		return;

	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("client");
	Json.WriteStrValue(AetherVariant::Key());
	Json.WriteAttribute("version");
	Json.WriteStrValue(CLIENT_RELEASE_VERSION);
	Json.WriteAttribute("players");
	Json.BeginArray();

	int Count = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(!pInfo || pInfo->m_ClientId < 0)
			continue;
		const char *pName = GameClient()->m_aClients[pInfo->m_ClientId].m_aName;
		if(!pName[0])
			continue;
		KogPointsEntryForName(pName, true);

		bool Fresh = false;
		for(const auto &Entry : m_aKogPoints)
		{
			if(Entry.m_HasPoints && str_comp_nocase(Entry.m_aName, pName) == 0 &&
				Now - Entry.m_LastUpdateTime < (int64_t)KOG_POINTS_REFRESH_SECONDS * time_freq())
			{
				Fresh = true;
				break;
			}
		}
		if(Fresh)
			continue;

		bool Duplicate = false;
		for(int i = 0; i < Count; ++i)
		{
			if(str_comp_nocase(m_aaKogPointsPendingNames[i], pName) == 0)
			{
				Duplicate = true;
				break;
			}
		}
		if(Duplicate)
			continue;

		str_copy(m_aaKogPointsPendingNames[Count], pName, sizeof(m_aaKogPointsPendingNames[Count]));
		Json.WriteStrValue(pName);
		++Count;
		if(Count == MAX_CLIENTS)
			break;
	}

	Json.EndArray();
	Json.EndObject();
	if(Count == 0)
		return;

	char aUrl[256];
	AetherBuildApiUrl(aUrl, sizeof(aUrl), "/v1/kog/points");
	const std::string Payload = Json.GetOutputString();
	m_pKogPointsRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pKogPointsRequest->PostJson(Payload.c_str());
	m_pKogPointsRequest->MaxResponseSize(128 * 1024);
	m_pKogPointsRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pKogPointsRequest);
	m_KogPointsPendingNameCount = Count;
	m_LastKogPointsRequestTime = Now;
}

bool CScoreboard::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CScoreboard::OnInput(const IInput::CEvent &Event)
{
	if(m_MouseUnlocked && Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		LockMouse();
		return true;
	}

	return IsActive() && m_MouseUnlocked;
}

void CScoreboard::RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize)
{
	const bool IsMapTitle = !GameClient()->IsTeamPlay();
	if(IsMapTitle && m_MouseUnlocked && GameClient()->m_aMapDescription[0] != '\0')
	{
		const int ButtonResult = Ui()->DoButtonLogic(&m_MapTitleButtonId, 0, &TitleLabel, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(ButtonResult != 0)
		{
			m_MapTitlePopupContext.m_pScoreboard = this;

			m_MapTitlePopupContext.m_FontSize = 12.0f;
			const float MaxWidth = 300.0f;
			const float Margin = 5.0f;
			const char *pDescription = GameClient()->m_aMapDescription;
			const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription) + 0.5f), MaxWidth);
			float TextHeight = 0.0f;
			STextSizeProperties TextSizeProps{};
			TextSizeProps.m_pHeight = &TextHeight;
			TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription, -1, TextWidth, 0, TextSizeProps);

			Ui()->DoPopupMenu(&m_MapTitlePopupContext, Ui()->MouseX(), Ui()->MouseY(), TextWidth + Margin * 2, TextHeight + Margin * 2, &m_MapTitlePopupContext, CMapTitlePopupContext::Render);
		}
		if(Ui()->HotItem() == &m_MapTitleButtonId)
		{
			TitleLabel.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.3f), IGraphics::CORNER_ALL, 5.0f);
		}
	}

	SLabelProperties Props;
	Props.m_MaxWidth = TitleLabel.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleLabel, pTitle, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
}

void CScoreboard::RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize)
{
	// map best
	char aScore[128] = "";
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes || TimeScore || Race7)
	{
		if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET)
		{
			Ui()->RenderTime(ScoreLabel,
				TitleFontSize,
				GameClient()->m_MapBestTimeSeconds,
				GameClient()->m_MapBestTimeSeconds == FinishTime::NOT_FINISHED_MILLIS,
				GameClient()->m_MapBestTimeMillis,
				GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis);
			return;
		}
	}
	else if(GameClient()->IsTeamPlay()) // normal score
	{
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if(pGameDataObj)
		{
			str_format(aScore, sizeof(aScore), "%d", Team == TEAM_RED ? pGameDataObj->m_TeamscoreRed : pGameDataObj->m_TeamscoreBlue);
		}
	}
	else
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW &&
			GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId]->m_Score);
		}
		else if(GameClient()->m_Snap.m_pLocalInfo)
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Score);
		}
	}

	const float ScoreTextWidth = aScore[0] != '\0' ? TextRender()->TextWidth(TitleFontSize, aScore, -1, -1.0f, 0) : 0.0f;
	if(ScoreTextWidth != 0.0f)
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, "00:00:00");
	const float TitleTextWidth = TextRender()->TextWidth(TitleFontSize, pTitle);

	TitleBar.VMargin(10.0f, &TitleBar);
	CUIRect TitleLabel, ScoreLabel;
	if(Team == TEAM_RED)
	{
		TitleBar.VSplitRight(ScoreTextWidth, &TitleLabel, &ScoreLabel);
		TitleLabel.VSplitRight(5.0f, &TitleLabel, nullptr);
		TitleLabel.VSplitLeft(minimum(TitleTextWidth + 2.0f, TitleLabel.w), &TitleLabel, nullptr);
	}
	else
	{
		TitleBar.VSplitLeft(ScoreTextWidth, &ScoreLabel, &TitleLabel);
		TitleLabel.VSplitLeft(5.0f, nullptr, &TitleLabel);
		TitleLabel.VSplitRight(minimum(TitleTextWidth + 2.0f, TitleLabel.w), nullptr, &TitleLabel);
	}

	RenderTitle(TitleLabel, Team, pTitle, TitleFontSize);
	RenderTitleScore(ScoreLabel, Team, TitleFontSize);
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	Goals.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	Goals.VMargin(5.0f, &Goals);

	const float FontSize = 10.0f;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	char aBuf[64];

	if(pGameInfoObj->m_ScoreLimit)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_ML);
	}

	if(pGameInfoObj->m_TimeLimit)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MC);
	}

	if(pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MR);
	}
}

void CScoreboard::RenderSpectators(CUIRect Spectators)
{
	Spectators.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	constexpr float SpectatorCut = 5.0f;
	Spectators.Margin(SpectatorCut, &Spectators);

	CTextCursor Cursor;
	Cursor.SetPosition(Spectators.TopLeft());
	Cursor.m_FontSize = 11.0f;
	Cursor.m_LineWidth = Spectators.w;
	Cursor.m_MaxLines = round_truncate(Spectators.h / Cursor.m_FontSize);

	int RemainingSpectators = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;
		++RemainingSpectators;
	}

	TextRender()->TextEx(&Cursor, Localize("Spectators"));

	if(RemainingSpectators > 0)
	{
		TextRender()->TextEx(&Cursor, ": ");
	}

	bool CommaNeeded = false;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;

		if(CommaNeeded)
		{
			TextRender()->TextEx(&Cursor, ", ");
		}

		if(Cursor.m_LineCount == Cursor.m_MaxLines && RemainingSpectators >= 2)
		{
			// This is less expensive than checking with a separate invisible
			// text cursor though we waste some space at the end of the line.
			char aRemaining[64];
			str_format(aRemaining, sizeof(aRemaining), Localize("%d others…", "Spectators"), RemainingSpectators);
			TextRender()->TextEx(&Cursor, aRemaining);
			break;
		}

		CUIRect SpectatorRect, SpectatorRectLineBreak;
		float Margin = 1.0f;
		SpectatorRect.x = Cursor.m_X - Margin;
		SpectatorRect.y = Cursor.m_Y;

		if(g_Config.m_ClShowIds)
		{
			char aClientId[16];
			GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::NO_INDENT);
			TextRender()->TextEx(&Cursor, aClientId);
		}

		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];
		{
			char aAetherClan[64];
			const char *pClanName = ClientData.m_aClan;
			if(GameClient()->m_AetherBadges.ScoreboardClanForClient(pInfo->m_ClientId, AetherScoreboardIsKogServer(Client()), aAetherClan, sizeof(aAetherClan)))
				pClanName = aAetherClan;
			if(pClanName[0] != '\0')
			{
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)));
				}
				else
				{
					TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f));
				}

				TextRender()->TextEx(&Cursor, pClanName);
				TextRender()->TextEx(&Cursor, " ");

				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}

		if(GameClient()->m_aClients[pInfo->m_ClientId].m_AuthLevel)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)));
		}

		TextRender()->TextEx(&Cursor, GameClient()->m_aClients[pInfo->m_ClientId].m_aName);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CommaNeeded = true;
		--RemainingSpectators;

		bool LineBreakDetected = false;
		SpectatorRect.h = Cursor.m_FontSize;

		// detect line breaks
		if(Cursor.m_Y != SpectatorRect.y)
		{
			LineBreakDetected = true;
			SpectatorRectLineBreak.x = Spectators.x - SpectatorCut;
			SpectatorRectLineBreak.y = Cursor.m_Y;
			SpectatorRectLineBreak.h = Cursor.m_FontSize;
			SpectatorRectLineBreak.w = Cursor.m_X - Spectators.x + SpectatorCut + 2 * Margin;

			SpectatorRect.w = Spectators.x + Spectators.w + SpectatorCut - SpectatorRect.x;
		}
		else
		{
			SpectatorRect.w = Cursor.m_X - SpectatorRect.x + 2 * Margin;
		}

		if(m_MouseUnlocked)
		{
			int ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, 0, &SpectatorRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);

			if(LineBreakDetected && ButtonResult == 0)
			{
				ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId, 0, &SpectatorRectLineBreak, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
			}
			if(ButtonResult != 0)
			{
				m_ScoreboardPopupContext.m_pScoreboard = this;
				m_ScoreboardPopupContext.m_ClientId = pInfo->m_ClientId;
				m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId ||
								     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
				m_ScoreboardPopupContext.m_IsSpectating = true;

				const float PopupHeight = AetherScoreboardPlayerPopupHeight(m_ScoreboardPopupContext.m_IsLocal, m_ScoreboardPopupContext.m_IsSpectating, AetherScoreboardCanLockLocalTeam(GameClient()));
				Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), SCOREBOARD_PLAYER_POPUP_WIDTH,
					PopupHeight, &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
			}

			if(Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId ||
				Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId ||
				(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
			{
				m_HoveredClientId = pInfo->m_ClientId;
				if(!LineBreakDetected)
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_ALL, 2.5f);
				}
				else
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_L, 2.5f);
					SpectatorRectLineBreak.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_R, 2.5f);
				}
			}
		}
	}
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool MillisecondScore = GameClient()->m_ReceivedDDNetPlayerFinishTimes;
	const bool TrueMilliseconds = GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis;
	const int NumPlayers = CountEnd - CountStart;
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;

	const bool UseTime = Race7 || TimeScore || MillisecondScore;
	const bool KogServer = AetherScoreboardIsKogServer(Client());
	const bool ShowKogPoints = false;

	// calculate measurements
	float LineHeight;
	float TeeSizeMod;
	float Spacing;
	float RoundRadius;
	float FontSize;
	if(NumPlayers <= 8)
	{
		LineHeight = 30.0f;
		TeeSizeMod = 0.5f;
		Spacing = 8.0f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 12)
	{
		LineHeight = 25.0f;
		TeeSizeMod = 0.45f;
		Spacing = 2.5f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 16)
	{
		LineHeight = 20.0f;
		TeeSizeMod = 0.4f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 24)
	{
		LineHeight = 13.5f;
		TeeSizeMod = 0.3f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 10.0f;
	}
	else if(NumPlayers <= 32)
	{
		LineHeight = 10.0f;
		TeeSizeMod = 0.2f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 8.0f;
	}
	else if(LowScoreboardWidth)
	{
		LineHeight = 7.5f;
		TeeSizeMod = 0.125f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 7.0f;
	}
	else
	{
		LineHeight = 5.0f;
		TeeSizeMod = 0.1f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 5.0f;
	}

	const float ScoreOffset = Scoreboard.x + 20.0f;
	const float ScoreLength = TextRender()->TextWidth(FontSize, UseTime ? "00:00:00" : "99999");
	const float TeeOffset = ScoreOffset + ScoreLength + 20.0f;
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float NameLength = (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength;
	const float CountryLength = (LineHeight - Spacing - TeeSizeMod * 5.0f) * 2.0f;
	const float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - 10.0f;
	const float CountryOffset = PingOffset - CountryLength;
	const float PointsLength = ShowKogPoints ? TextRender()->TextWidth(FontSize, "9999999") : 0.0f;
	const float PointsOffset = ShowKogPoints ? CountryOffset - PointsLength - 8.0f : CountryOffset;
	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = (ShowKogPoints ? PointsOffset : CountryOffset) - ClanOffset - 2.5f;

	// render headlines
	const float HeadlineFontsize = 11.0f;
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, &Scoreboard);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const char *pScore = UseTime ? Localize("Time") : Localize("Score");
	TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(HeadlineFontsize, pScore), HeadlineY, HeadlineFontsize, pScore);
	TextRender()->Text(NameOffset, HeadlineY, HeadlineFontsize, Localize("Name"));
	const char *pClanLabel = Localize("Clan");
	TextRender()->Text(ClanOffset + (ClanLength - TextRender()->TextWidth(HeadlineFontsize, pClanLabel)) / 2.0f, HeadlineY, HeadlineFontsize, pClanLabel);
	if(ShowKogPoints)
	{
		const char *pPointsLabel = Localize("Points");
		TextRender()->Text(PointsOffset + PointsLength - TextRender()->TextWidth(HeadlineFontsize, pPointsLabel), HeadlineY, HeadlineFontsize, pPointsLabel);
	}
	const char *pPingLabel = Localize("Ping");
	TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(HeadlineFontsize, pPingLabel), HeadlineY, HeadlineFontsize, pPingLabel);

	// render player entries
	int CountRendered = 0;
	int CurrentDDTeamSize = 0;

	char aBuf[64];
	int MaxTeamSize = Config()->m_SvMaxTeamSize;
	auto CountPlayersInDDTeam = [&](int DDTeam, int RenderDead) {
		int Count = 0;
		for(const CNetObj_PlayerInfo *pTeamInfo : GameClient()->m_Snap.m_apInfoByDDTeamScore)
		{
			if(!pTeamInfo || pTeamInfo->m_Team != Team || GameClient()->m_Teams.Team(pTeamInfo->m_ClientId) != DDTeam)
				continue;
			const bool Dead = Client()->m_TranslationContext.m_aClients[pTeamInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
			if((!RenderDead && Dead) || (RenderDead && !Dead))
				continue;
			++Count;
		}
		return Count;
	};

	for(int RenderDead = 0; RenderDead < 2; RenderDead++)
	{
		int PrevDDTeam = -1;
		CurrentDDTeamSize = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			// make sure that we render the correct team
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!pInfo || pInfo->m_Team != Team)
				continue;
			bool IsDead = Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
			if(!RenderDead && IsDead)
				continue;
			if(RenderDead && !IsDead)
				continue;
			if(CountRendered++ < CountStart)
				continue;

			int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			int NextDDTeam = 0;

			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			TextColor.a = RenderDead ? 0.5f : 1.0f;
			TextRender()->TextColor(TextColor);

			for(int j = i + 1; j < MAX_CLIENTS; j++)
			{
				const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
				if(!pInfoNext || pInfoNext->m_Team != Team)
					continue;

				NextDDTeam = GameClient()->m_Teams.Team(pInfoNext->m_ClientId);
				break;
			}

			CUIRect RowAndSpacing, Row;
			Scoreboard.HSplitTop(LineHeight + Spacing, &RowAndSpacing, &Scoreboard);
			RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);

			// team background
			if(DDTeam != TEAM_FLOCK)
			{
				const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f);
				if(PrevDDTeam != DDTeam)
				{
					State.m_TeamStartX = Row.x;
					State.m_TeamStartY = Row.y;

					int RowsInTeam = 1;
					for(int j = i + 1; j < MAX_CLIENTS; j++)
					{
						const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
						if(!pInfoNext || pInfoNext->m_Team != Team)
							continue;

						const bool NextDead = Client()->m_TranslationContext.m_aClients[pInfoNext->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
						if(!RenderDead && NextDead)
							continue;
						if(RenderDead && !NextDead)
							continue;

						if(GameClient()->m_Teams.Team(pInfoNext->m_ClientId) != DDTeam)
							break;
						RowsInTeam++;
					}

					CUIRect TeamRect = RowAndSpacing;
					TeamRect.h = std::min(RowsInTeam * (LineHeight + Spacing), RowAndSpacing.h + Scoreboard.h);
					if(g_Config.m_AeGradientTeamColors)
					{
						const ColorRGBA Base = Color.WithAlpha(0.62f);
						const ColorRGBA Black(0.0f, 0.0f, 0.0f, Base.a);
						TeamRect.Draw4(Base, Black, Base, Black, IGraphics::CORNER_ALL, RoundRadius);
					}
					else
					{
						TeamRect.Draw(Color, IGraphics::CORNER_ALL, RoundRadius);
					}
				}

				CurrentDDTeamSize++;

				if(NextDDTeam != DDTeam)
				{
					const float TeamFontSize = FontSize / 1.5f;
					const int DisplayDDTeamSize = std::max(CurrentDDTeamSize, CountPlayersInDDTeam(DDTeam, RenderDead));

					if(NumPlayers > 8)
					{
						if(DDTeam == TEAM_SUPER)
							str_copy(aBuf, Localize("Super"));
						else if(DisplayDDTeamSize <= 1)
							str_format(aBuf, sizeof(aBuf), "%d", DDTeam);
						else
							str_format(aBuf, sizeof(aBuf), Localize("%d\n(%d/%d)", "Team and size"), DDTeam, DisplayDDTeamSize, MaxTeamSize);
						TextRender()->Text(State.m_TeamStartX, maximum(State.m_TeamStartY + Row.h / 2.0f - TeamFontSize, State.m_TeamStartY + 1.5f /* padding top */), TeamFontSize, aBuf);
					}
					else
					{
						if(DDTeam == TEAM_SUPER)
							str_copy(aBuf, Localize("Super"));
						else if(DisplayDDTeamSize > 1)
							str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, DisplayDDTeamSize, MaxTeamSize);
						else
							str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
						TextRender()->Text(Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f, Row.y + Row.h, TeamFontSize, aBuf);
					}

					CurrentDDTeamSize = 0;
				}
			}
			PrevDDTeam = DDTeam;

			// background so it's easy to find the local player or the followed one in spectator mode
			if((!GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId))
			{
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, RoundRadius);
			}

			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];

			if(m_MouseUnlocked)
			{
				const int ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, 0, &Row, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(ButtonResult != 0)
				{
					OpenPlayerPopup(pInfo->m_ClientId, false);
				}

				if(Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId ||
					(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
				{
					CUIRect HoverRect = RowAndSpacing;
					HoverRect.h = Row.h + Spacing;
					HoverRect.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.32f), IGraphics::CORNER_ALL, RoundRadius);
				}
			}

			// score
			CUIRect ScorePosition;
			ScorePosition.x = ScoreOffset;
			ScorePosition.w = ScoreLength;
			ScorePosition.y = Row.y;
			ScorePosition.h = Row.h;

			if(Race7)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score / 1000, pInfo->m_Score == protocol7::FinishTime::NOT_FINISHED, pInfo->m_Score % 1000, true);
			}
			else if(MillisecondScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, ClientData.m_FinishTimeSeconds, ClientData.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS, ClientData.m_FinishTimeMillis, TrueMilliseconds);
			}
			else if(TimeScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score, pInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE, -1, false);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Score, -999, 99999));
				TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(FontSize, aBuf), ScorePosition.y + (Row.h - FontSize) / 2.0f, FontSize, aBuf);
			}

			// CTF flag
			if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
				pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
			{
				Graphics()->BlendNormal();
				Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
				Graphics()->QuadsBegin();
				Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y - 2.5f - Spacing / 2.0f, Row.h / 2.0f, Row.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}

			// skin
			if(RenderDead)
			{
				Graphics()->BlendNormal();
				Graphics()->TextureSet(m_DeadTeeTexture);
				Graphics()->QuadsBegin();
				if(GameClient()->IsTeamPlay())
				{
					Graphics()->SetColor(GameClient()->m_Skins7.GetTeamColor(true, 0, GameClient()->m_aClients[pInfo->m_ClientId].m_Team, protocol7::SKINPART_BODY));
				}
				CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y, TeeInfo.m_Size, TeeInfo.m_Size);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}
			else
			{
				CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				if(pInfo->m_ClientId >= 0 && GameClient()->m_AetherBlockAwareness.ShouldColorPlayer(pInfo->m_ClientId))
				{
					if(GameClient()->m_AetherBlockAwareness.ShouldUseDefaultColorSkin(pInfo->m_ClientId) && GameClient()->m_Players.EnsureAetherBlockTeeRenderInfoReady())
					{
						const float Size = TeeInfo.m_Size;
						const bool GotAirJump = TeeInfo.m_GotAirJump;
						TeeInfo = GameClient()->m_Players.AetherBlockTeeRenderInfo()->TeeRenderInfo();
						TeeInfo.m_Size = Size;
						TeeInfo.m_GotAirJump = GotAirJump;
					}
					GameClient()->m_AetherBlockAwareness.ApplyRenderInfo(pInfo->m_ClientId, TeeInfo);
				}
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
				const vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
			}

			// name
			{
				CTextCursor Cursor;
				Cursor.SetPosition(vec2(NameOffset, Row.y + (Row.h - FontSize) / 2.0f));
				Cursor.m_FontSize = FontSize;
				Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.m_LineWidth = NameLength;
				if(ClientData.m_AuthLevel)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)));
				}
				if(g_Config.m_ClShowIds)
				{
					char aClientId[16];
					GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
					TextRender()->TextEx(&Cursor, aClientId);
				}

				if(pInfo->m_ClientId >= 0 && (GameClient()->m_aClients[pInfo->m_ClientId].m_Foe || GameClient()->m_aClients[pInfo->m_ClientId].m_ChatIgnore))
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->TextEx(&Cursor, FontIcon::COMMENT_SLASH);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}

				if(pInfo->m_ClientId >= 0 && GameClient()->m_AetherBlockAwareness.ShouldColorName(pInfo->m_ClientId))
					TextRender()->TextColor(GameClient()->m_AetherBlockAwareness.NameColorForClient(pInfo->m_ClientId, TextColor.a));
				// TClient
				else if(AetherVariant::WarlistEnabled() && pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
					TextRender()->TextColor(GameClient()->m_WarList.GetNameplateColor(pInfo->m_ClientId));

				const int BadgeIconCount = pInfo->m_ClientId >= 0 && g_Config.m_AeBadges && g_Config.m_AeBadgesScoreboard ? GameClient()->m_AetherBadges.BadgeIconCount(pInfo->m_ClientId, 3) : 0;
				bool BadgeIconsRendered = false;
				if(BadgeIconCount > 0)
				{
					const float BadgeIconSize = FontSize * 1.56f;
					const float BadgeIconWidth = GameClient()->m_AetherBadges.BadgeIconsWidth(pInfo->m_ClientId, BadgeIconSize, 3);
					const float IconX = std::max(NameOffset, std::min(Cursor.m_X, NameOffset + NameLength - BadgeIconWidth));
					const float IconY = Row.y + (Row.h - BadgeIconSize) / 2.0f;
					BadgeIconsRendered = GameClient()->m_AetherBadges.RenderBadgeIcons(pInfo->m_ClientId, IconX, IconY, BadgeIconSize, TextColor.a, 3);
					if(BadgeIconsRendered)
						Cursor.m_X = IconX + BadgeIconWidth + 1.0f;
				}
				char aBadgeText[96];
				if(!BadgeIconsRendered && pInfo->m_ClientId >= 0 && g_Config.m_AeBadges && g_Config.m_AeBadgesScoreboard && GameClient()->m_AetherBadges.FormatBadgeText(pInfo->m_ClientId, aBadgeText, sizeof(aBadgeText), 3))
				{
					TextRender()->TextColor(ColorRGBA(0.55f, 0.82f, 1.0f, TextColor.a));
					TextRender()->TextEx(&Cursor, aBadgeText[0] == ' ' ? aBadgeText + 1 : aBadgeText);
					TextRender()->TextEx(&Cursor, " ");
				}
				if(pInfo->m_ClientId >= 0 && ClientData.m_Friend && g_Config.m_ClNamePlatesFriendMark)
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->TextColor(ColorRGBA(0.95f, 0.25f, 0.25f, TextColor.a));
					TextRender()->TextEx(&Cursor, FontIcon::HEART);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->TextEx(&Cursor, " ");
				}
				ColorRGBA NameTextColor = TextColor;
				ColorRGBA NameGlowColor;
				float NameGlowRadius = 0.0f;
				int NameGradientStyle = 0;
				int NameGradientSpeed = 35;
				bool NameGradientGlow = false;
				if(ClientData.m_AuthLevel)
					NameTextColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor));
				if(pInfo->m_ClientId >= 0 && GameClient()->m_AetherBlockAwareness.ShouldColorName(pInfo->m_ClientId))
					NameTextColor = GameClient()->m_AetherBlockAwareness.NameColorForClient(pInfo->m_ClientId, TextColor.a);
				else if(AetherVariant::WarlistEnabled() && pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
					NameTextColor = GameClient()->m_WarList.GetNameplateColor(pInfo->m_ClientId);
				else if(pInfo->m_ClientId >= 0)
				{
					CAetherBadges::SGradientNicknameStyle Style;
					if(GameClient()->m_AetherBadges.GradientNicknameStyleForClient(pInfo->m_ClientId, Style))
					{
						NameTextColor = AetherGradientNicknameColor(pInfo->m_ClientId, TextColor.a, Style);
						NameGradientGlow = AetherGradientNicknameGlow(Style, NameTextColor, NameGlowColor, NameGlowRadius);
						NameGradientStyle = Style.m_Style;
						NameGradientSpeed = Style.m_Speed;
					}
				}
				if(NameGradientGlow)
				{
					const vec2 aOffsets[] = {
						vec2(-NameGlowRadius, 0.0f),
						vec2(NameGlowRadius, 0.0f),
						vec2(0.0f, -NameGlowRadius),
						vec2(0.0f, NameGlowRadius),
						vec2(-NameGlowRadius * 0.72f, -NameGlowRadius * 0.72f),
						vec2(NameGlowRadius * 0.72f, -NameGlowRadius * 0.72f),
						vec2(-NameGlowRadius * 0.72f, NameGlowRadius * 0.72f),
						vec2(NameGlowRadius * 0.72f, NameGlowRadius * 0.72f),
					};
					TextRender()->TextColor(NameGlowColor);
					for(const vec2 &Offset : aOffsets)
					{
						CTextCursor GlowCursor = Cursor;
						GlowCursor.m_X += Offset.x;
						GlowCursor.m_Y += Offset.y;
						TextRender()->TextEx(&GlowCursor, ClientData.m_aName);
					}
				}
				if(NameGradientGlow && NameGradientStyle == 1)
				{
					const float Seconds = time_get() / (float)time_freq();
					const float Pulse = std::pow(0.5f + 0.5f * std::sin(Seconds * (1.2f + NameGradientSpeed / 45.0f)), 4.0f);
					CTextCursor ShineCursor = Cursor;
					ShineCursor.m_X += 0.7f;
					ShineCursor.m_Y -= 0.7f;
					TextRender()->TextColor(ColorRGBA(1.0f, 0.92f, 0.46f, TextColor.a * (0.08f + Pulse * 0.22f)));
					TextRender()->TextEx(&ShineCursor, ClientData.m_aName);
				}
				TextRender()->TextColor(NameTextColor);
				TextRender()->TextEx(&Cursor, ClientData.m_aName);
				char aDummyOwner[64];
				if(pInfo->m_ClientId >= 0 && GameClient()->m_AetherBlockAwareness.DummyOwnerLabel(pInfo->m_ClientId, aDummyOwner, sizeof(aDummyOwner)))
				{
					TextRender()->TextColor(ColorRGBA(0.58f, 0.78f, 1.0f, TextColor.a * 0.82f));
					TextRender()->TextEx(&Cursor, " ");
					TextRender()->TextEx(&Cursor, aDummyOwner);
				}

				// ready / watching
				if(Client()->IsSixup() && Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_READY)
				{
					TextRender()->TextColor(0.1f, 1.0f, 0.1f, TextColor.a);
					TextRender()->TextEx(&Cursor, "✓");
				}
			}

			// clan
			{
				char aAetherClan[64];
				const char *pClanName = ClientData.m_aClan;
				bool AetherClan = false;
				if(GameClient()->m_AetherBadges.ScoreboardClanForClient(pInfo->m_ClientId, KogServer, aAetherClan, sizeof(aAetherClan)))
				{
					pClanName = aAetherClan;
					AetherClan = true;
				}

				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)));
				}
				else
				{
					TextRender()->TextColor(TextColor);
				}

				if(pInfo->m_ClientId >= 0 && GameClient()->m_AetherBlockAwareness.ShouldColorName(pInfo->m_ClientId))
					TextRender()->TextColor(GameClient()->m_AetherBlockAwareness.NameColorForClient(pInfo->m_ClientId, TextColor.a));
				// TClient
				else if(AetherVariant::WarlistEnabled() && pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
					TextRender()->TextColor(GameClient()->m_WarList.GetClanColor(pInfo->m_ClientId));

				float ClanFontSize = FontSize;
				if(AetherClan)
				{
					const float TextWidth = TextRender()->TextWidth(ClanFontSize, pClanName);
					if(TextWidth > ClanLength && TextWidth > 0.0f)
						ClanFontSize = std::clamp(ClanFontSize * ClanLength / TextWidth, FontSize * 0.50f, FontSize);
				}
				CTextCursor Cursor;
				Cursor.SetPosition(vec2(ClanOffset + (ClanLength - minimum(TextRender()->TextWidth(ClanFontSize, pClanName), ClanLength)) / 2.0f, Row.y + (Row.h - ClanFontSize) / 2.0f));
				Cursor.m_FontSize = ClanFontSize;
				Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.m_LineWidth = ClanLength;
				TextRender()->TextEx(&Cursor, pClanName);
			}

			// country flag
			GameClient()->m_CountryFlags.Render(ClientData.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f),
				CountryOffset, Row.y + (Spacing + TeeSizeMod * 5.0f) / 2.0f, CountryLength, Row.h - Spacing - TeeSizeMod * 5.0f);

			// KoG points
			if(ShowKogPoints)
			{
				int Points = 0;
				if(KogPointsForClient(pInfo->m_ClientId, Points))
				{
					TextRender()->TextColor(ColorRGBA(0.98f, 0.73f, 1.0f, TextColor.a));
					str_format(aBuf, sizeof(aBuf), "%d", std::clamp(Points, 0, 99999999));
					TextRender()->Text(PointsOffset + PointsLength - TextRender()->TextWidth(FontSize, aBuf), Row.y + (Row.h - FontSize) / 2.0f, FontSize, aBuf);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}

			// ping
			if(g_Config.m_ClEnablePingColor)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(pInfo->m_Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f)));
			}
			else
			{
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
			TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(FontSize, aBuf), Row.y + (Row.h - FontSize) / 2.0f, FontSize, aBuf);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(CountRendered == CountEnd)
				break;
		}
		if(CountRendered == CountEnd)
			break;
	}
}

void CScoreboard::RenderRecordingNotification(float x)
{
	char aBuf[512] = "";

	const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
		if(GameClient()->DemoRecorder(Recorder)->IsRecording())
		{
			char aTime[32];
			str_time((int64_t)GameClient()->DemoRecorder(Recorder)->Length() * 100, ETimeFormat::HOURS, aTime, sizeof(aTime));
			str_append(aBuf, pName);
			str_append(aBuf, " ");
			str_append(aBuf, aTime);
			str_append(aBuf, "  ");
		}
	};

	AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
	AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
	AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
	AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));

	if(aBuf[0] == '\0')
		return;

	const float FontSize = 10.0f;

	CUIRect Rect = {x, 0.0f, TextRender()->TextWidth(FontSize, aBuf) + 30.0f, 25.0f};
	Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 7.5f);
	Rect.VSplitLeft(10.0f, nullptr, &Rect);
	Rect.VSplitRight(5.0f, &Rect, nullptr);

	CUIRect Circle;
	Rect.VSplitLeft(10.0f, &Circle, &Rect);
	Circle.HMargin((Circle.h - Circle.w) / 2.0f, &Circle);
	Circle.Draw(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Rect.VSplitLeft(5.0f, nullptr, &Rect);
	Ui()->DoLabel(&Rect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::RenderPlayerHoverCard(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return;

	const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	if(!pInfo)
		return;

	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	const CUIRect Screen = *Ui()->Screen();
	const float CardWidth = 220.0f;
	const float CardHeight = 74.0f;
	CUIRect Card = {Ui()->MouseX() + 14.0f, Ui()->MouseY() + 14.0f, CardWidth, CardHeight};

	if(Card.x + Card.w > Screen.w - 6.0f)
		Card.x = Ui()->MouseX() - Card.w - 14.0f;
	if(Card.y + Card.h > Screen.h - 6.0f)
		Card.y = Ui()->MouseY() - Card.h - 14.0f;

	const float MaxX = maximum(6.0f, Screen.w - Card.w - 6.0f);
	const float MaxY = maximum(6.0f, Screen.h - Card.h - 6.0f);
	Card.x = std::clamp(Card.x, 6.0f, MaxX);
	Card.y = std::clamp(Card.y, 6.0f, MaxY);

	const ColorRGBA Accent = AetherScoreboardAccentColor(GameClient(), ClientId, 1.0f);
	Card.Draw(ColorRGBA(0.025f, 0.022f, 0.035f, 0.93f), IGraphics::CORNER_ALL, 7.0f);

	CUIRect AccentLine = Card;
	AccentLine.h = 2.5f;
	AccentLine.Draw(Accent.WithAlpha(0.95f), IGraphics::CORNER_T, 7.0f);

	CUIRect Body;
	Card.Margin(6.0f, &Body);

	CUIRect TeeColumn, TeePanel, Info;
	Body.VSplitLeft(56.0f, &TeeColumn, &Info);
	Info.VSplitLeft(7.0f, nullptr, &Info);

	const float TeePanelSize = minimum(TeeColumn.w, TeeColumn.h);
	TeePanel = TeeColumn;
	TeePanel.w = TeePanelSize;
	TeePanel.h = TeePanelSize;
	TeePanel.x += (TeeColumn.w - TeePanelSize) * 0.5f;
	TeePanel.y += (TeeColumn.h - TeePanelSize) * 0.5f;

	TeePanel.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 6.0f);
	{
		CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
		TeeInfo.m_Size = 30.0f;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeePanel.Center() + vec2(0.0f, OffsetToMid.y * 0.45f));
	}

	CUIRect NameLine, MetaLine, ClanLine, DetailLine;
	Info.HSplitTop(17.0f, &NameLine, &Info);
	Info.HSplitTop(15.0f, &MetaLine, &Info);
	Info.HSplitTop(15.0f, &ClanLine, &Info);
	Info.HSplitTop(15.0f, &DetailLine, &Info);

	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(NameLine.x, NameLine.y + 1.0f));
		Cursor.m_FontSize = 12.5f;
		Cursor.m_LineWidth = NameLine.w;
		Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.97f));
		TextRender()->TextEx(&Cursor, ClientData.m_aName);
	}

	char aTeam[32];
	AetherScoreboardTeamLabel(GameClient(), ClientId, aTeam, sizeof(aTeam));
	char aMeta[96];
	str_format(aMeta, sizeof(aMeta), "#%d  %s  %d ms", ClientId, aTeam, std::clamp(pInfo->m_Latency, 0, 999));
	TextRender()->TextColor(ColorRGBA(0.76f, 0.80f, 0.90f, 0.86f));
	TextRender()->Text(MetaLine.x, MetaLine.y + 1.0f, 9.5f, aMeta, MetaLine.w);

	char aAetherClan[64];
	const char *pClanName = ClientData.m_aClan;
	if(GameClient()->m_AetherBadges.ScoreboardClanForClient(ClientId, AetherScoreboardIsKogServer(Client()), aAetherClan, sizeof(aAetherClan)))
		pClanName = aAetherClan;
	if(pClanName[0])
	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(ClanLine.x, ClanLine.y + 1.0f));
		Cursor.m_FontSize = 9.5f;
		Cursor.m_LineWidth = ClanLine.w;
		Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		TextRender()->TextColor(ColorRGBA(0.70f, 0.76f, 0.86f, 0.86f));
		TextRender()->TextEx(&Cursor, pClanName);
	}

	DetailLine.Draw(Accent.WithAlpha(0.13f), IGraphics::CORNER_ALL, 4.0f);
	DetailLine.Margin(3.0f, &DetailLine);
	float DetailX = DetailLine.x;
	const int BadgeIconCount = g_Config.m_AeBadges && g_Config.m_AeBadgesScoreboard ? GameClient()->m_AetherBadges.BadgeIconCount(ClientId, 4) : 0;
	if(BadgeIconCount > 0)
	{
		const float BadgeIconSize = 10.5f;
		const float BadgeWidth = GameClient()->m_AetherBadges.BadgeIconsWidth(ClientId, BadgeIconSize, 4);
		GameClient()->m_AetherBadges.RenderBadgeIcons(ClientId, DetailX, DetailLine.y + 0.2f, BadgeIconSize, 0.95f, 4);
		DetailX += BadgeWidth + 4.0f;
	}

	char aDetail[128];
	aDetail[0] = '\0';
	char aBadgeText[96];
	if(g_Config.m_AeBadges && g_Config.m_AeBadgesScoreboard && GameClient()->m_AetherBadges.FormatBadgeText(ClientId, aBadgeText, sizeof(aBadgeText), 4))
	{
		const char *pBadgeText = aBadgeText[0] == ' ' ? aBadgeText + 1 : aBadgeText;
		str_copy(aDetail, pBadgeText, sizeof(aDetail));
	}
	else if(ClientData.m_Friend)
		str_copy(aDetail, Localize("Friend"), sizeof(aDetail));
	else
		str_format(aDetail, sizeof(aDetail), "%s: %s", Localize("Skin"), ClientData.m_aSkinName);

	TextRender()->TextColor(ColorRGBA(0.95f, 0.80f, 1.0f, 0.92f));
	TextRender()->Text(DetailX, DetailLine.y + 0.8f, 8.8f, aDetail, maximum(0.0f, DetailLine.x + DetailLine.w - DetailX));

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CScoreboard::OnRender()
{
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!IsActive())
	{
		// lock mouse if scoreboard was opened by being dead or game pause
		if(m_MouseUnlocked)
		{
			LockMouse();
		}
		return;
	}

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// if the score board is active, then we should clear the motd message as well
	if(GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();
	m_HoveredClientId = -1;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	const int NumPlayers = Teams ? maximum(aTeamSize[TEAM_RED], aTeamSize[TEAM_BLUE]) : aTeamSize[TEAM_RED];

	const float ScoreboardSmallWidth = 375.0f + 10.0f;
	const float ScoreboardWidth = !Teams && NumPlayers <= 16 ? ScoreboardSmallWidth : 750.0f;
	const float TitleHeight = 30.0f;

	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, 75.0f, ScoreboardWidth, 355.0f + TitleHeight};
	CScoreboardRenderState RenderState{};

	if(Teams)
	{
		const char *pRedTeamName = GetTeamName(TEAM_RED);
		const char *pBlueTeamName = GetTeamName(TEAM_BLUE);

		// Game over title
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if((pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && pGameDataObj)
		{
			char aTitle[256];
			if(pGameDataObj->m_TeamscoreRed > pGameDataObj->m_TeamscoreBlue)
			{
				TextRender()->TextColor(ColorRGBA(0.975f, 0.17f, 0.17f, 1.0f));
				if(pRedTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Red team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pRedTeamName);
				}
			}
			else if(pGameDataObj->m_TeamscoreBlue > pGameDataObj->m_TeamscoreRed)
			{
				TextRender()->TextColor(ColorRGBA(0.17f, 0.46f, 0.975f, 1.0f));
				if(pBlueTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Blue team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pBlueTeamName);
				}
			}
			else
			{
				TextRender()->TextColor(ColorRGBA(0.91f, 0.78f, 0.33f, 1.0f));
				str_copy(aTitle, Localize("Draw!"));
			}

			const float TitleFontSize = 36.0f;
			CUIRect GameOverTitle = {Scoreboard.x, Scoreboard.y - TitleFontSize - 6.0f, Scoreboard.w, TitleFontSize};
			Ui()->DoLabel(&GameOverTitle, aTitle, TitleFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		CUIRect RedScoreboard, BlueScoreboard, RedTitle, BlueTitle;
		Scoreboard.VSplitMid(&RedScoreboard, &BlueScoreboard, 7.5f);
		RedScoreboard.HSplitTop(TitleHeight, &RedTitle, &RedScoreboard);
		BlueScoreboard.HSplitTop(TitleHeight, &BlueTitle, &BlueScoreboard);

		RedTitle.Draw(ColorRGBA(0.975f, 0.17f, 0.17f, 0.5f), IGraphics::CORNER_T, 7.5f);
		BlueTitle.Draw(ColorRGBA(0.17f, 0.46f, 0.975f, 0.5f), IGraphics::CORNER_T, 7.5f);
		RedScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);
		BlueScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);

		RenderTitleBar(RedTitle, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName);
		RenderTitleBar(BlueTitle, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName);
		RenderScoreboard(RedScoreboard, TEAM_RED, 0, NumPlayers, RenderState);
		RenderScoreboard(BlueScoreboard, TEAM_BLUE, 0, NumPlayers, RenderState);
	}
	else
	{
		Scoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);

		const char *pTitle;
		if(pGameInfoObj && (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			pTitle = Localize("Game over");
		}
		else
		{
			pTitle = GameClient()->Map()->BaseName();
		}

		CUIRect Title;
		Scoreboard.HSplitTop(TitleHeight, &Title, &Scoreboard);
		RenderTitleBar(Title, TEAM_GAME, pTitle);

		if(NumPlayers <= 16)
		{
			RenderScoreboard(Scoreboard, TEAM_GAME, 0, NumPlayers, RenderState);
		}
		else if(NumPlayers <= 64)
		{
			int PlayersPerSide;
			if(NumPlayers <= 24)
				PlayersPerSide = 12;
			else if(NumPlayers <= 32)
				PlayersPerSide = 16;
			else if(NumPlayers <= 48)
				PlayersPerSide = 24;
			else
				PlayersPerSide = 32;

			CUIRect LeftScoreboard, RightScoreboard;
			Scoreboard.VSplitMid(&LeftScoreboard, &RightScoreboard);
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RenderState);
		}
		else
		{
			const int NumColumns = 3;
			const int PlayersPerColumn = std::ceil(128.0f / NumColumns);
			CUIRect RemainingScoreboard = Scoreboard;
			for(int i = 0; i < NumColumns; ++i)
			{
				CUIRect Column;
				RemainingScoreboard.VSplitLeft(Scoreboard.w / NumColumns, &Column, &RemainingScoreboard);
				RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RenderState);
			}
		}
	}

	CUIRect Spectators = {(Screen.w - ScoreboardSmallWidth) / 2.0f, Scoreboard.y + Scoreboard.h + 5.0f, ScoreboardSmallWidth, 100.0f};
	if(pGameInfoObj && (pGameInfoObj->m_ScoreLimit || pGameInfoObj->m_TimeLimit || (pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)))
	{
		CUIRect Goals;
		Spectators.HSplitTop(25.0f, &Goals, &Spectators);
		Spectators.HSplitTop(5.0f, nullptr, &Spectators);
		RenderGoals(Goals);
	}
	RenderSpectators(Spectators);

	RenderRecordingNotification((Screen.w / 7) * 4 + 10);

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->RenderPopupMenus();

		if(m_MouseUnlocked)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

		Ui()->FinishCheck();
	}
}

bool CScoreboard::IsActive() const
{
	// if statboard is active don't show scoreboard
	if(GameClient()->m_Statboard.IsActive())
		return false;

	if(m_Active)
		return true;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(GameClient()->m_Snap.m_pLocalInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// we are not a spectator, check if we are dead and the game isn't paused
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_ClScoreboardOnDeath &&
			!(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
			return true;
	}

	// if the game is over
	if(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		return true;

	return false;
}

void CScoreboard::OpenPlayerPopup(int ClientId, bool IsSpectating)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return;

	m_ScoreboardPopupContext.m_pScoreboard = this;
	m_ScoreboardPopupContext.m_ClientId = ClientId;
	m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == ClientId ||
					      (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == ClientId);
	m_ScoreboardPopupContext.m_IsSpectating = IsSpectating;

	Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), SCOREBOARD_PLAYER_POPUP_WIDTH,
		AetherScoreboardPlayerPopupHeight(m_ScoreboardPopupContext.m_IsLocal, m_ScoreboardPopupContext.m_IsSpectating, AetherScoreboardCanLockLocalTeam(GameClient())), &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
}

const char *CScoreboard::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

CUi::EPopupMenuFunctionResult CScoreboard::CScoreboardPopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CScoreboardPopupContext *pPopupContext = static_cast<CScoreboardPopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;
	CUi *pUi = pPopupContext->m_pScoreboard->Ui();

	CGameClient::CClientData &Client = pScoreboard->GameClient()->m_aClients[pPopupContext->m_ClientId];

	if(!Client.m_Active)
		return CUi::POPUP_CLOSE_CURRENT;

	const float Margin = 6.0f;
	View.Margin(Margin, &View);

	CUIRect Header, HeaderBody, TeePanel, PlayerText, NameLine, MetaLine, Container, Action;
	const float ItemSpacing = 3.0f;
	const float FontSize = 11.0f;

	View.HSplitTop(42.0f, &Header, &View);
	Header.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 6.0f);
	Header.Margin(5.0f, &HeaderBody);
	HeaderBody.VSplitLeft(31.0f, &TeePanel, &PlayerText);
	PlayerText.VSplitLeft(7.0f, nullptr, &PlayerText);
	PlayerText.HSplitTop(16.0f, &NameLine, &MetaLine);
	{
		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size = 24.0f;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
		pScoreboard->RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeePanel.Center() + vec2(0.0f, OffsetToMid.y * 0.35f));
	}
	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(NameLine.x, NameLine.y + 1.0f));
		Cursor.m_FontSize = 11.0f;
		Cursor.m_LineWidth = NameLine.w;
		Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		pScoreboard->TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.96f));
		pScoreboard->TextRender()->TextEx(&Cursor, Client.m_aName);
	}
	{
		char aTeam[32];
		AetherScoreboardTeamLabel(pScoreboard->GameClient(), pPopupContext->m_ClientId, aTeam, sizeof(aTeam));
		char aMeta[96];
		const CNetObj_PlayerInfo *pInfo = pScoreboard->GameClient()->m_Snap.m_apPlayerInfos[pPopupContext->m_ClientId];
		str_format(aMeta, sizeof(aMeta), "%s  /  %d ms", aTeam, pInfo ? std::clamp(pInfo->m_Latency, 0, 999) : 0);
		pScoreboard->TextRender()->TextColor(ColorRGBA(0.76f, 0.80f, 0.90f, 0.82f));
		pScoreboard->TextRender()->Text(MetaLine.x, MetaLine.y + 1.0f, 9.0f, aMeta, MetaLine.w);
		pScoreboard->TextRender()->TextColor(pScoreboard->TextRender()->DefaultTextColor());
	}

	if(!pPopupContext->m_IsLocal)
	{
		const int ActionsNum = 3;
		const float ActionSize = 27.0f;
		const float ActionSpacing = 6.0f;
		int ActionCorners = IGraphics::CORNER_ALL;

		View.HSplitTop(ItemSpacing * 2, nullptr, &View);
		View.HSplitTop(ActionSize, &Container, &View);
		const float ActionsWidth = ActionsNum * ActionSize + (ActionsNum - 1) * ActionSpacing;
		if(Container.w > ActionsWidth)
			Container.VMargin((Container.w - ActionsWidth) / 2.0f, &Container);

		Container.VSplitLeft(ActionSize, &Action, &Container);

		ColorRGBA FriendActionColor = Client.m_Friend ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction)) :
								ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction));
		const char *pFriendActionIcon = pUi->HotItem() == &pPopupContext->m_FriendAction && Client.m_Friend ? FontIcon::HEART_CRACK : FontIcon::HEART;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_FriendAction, pFriendActionIcon, Client.m_Friend, &Action, BUTTONFLAG_LEFT, ActionCorners, true, FriendActionColor))
		{
			if(Client.m_Friend)
			{
				pScoreboard->GameClient()->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
			}
			else
			{
				pScoreboard->GameClient()->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
			}
		}

		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_FriendAction, &Action, Client.m_Friend ? Localize("Remove friend") : Localize("Add friend"));

		Container.VSplitLeft(ActionSpacing, nullptr, &Container);
		Container.VSplitLeft(ActionSize, &Action, &Container);

		if(pUi->DoButton_FontIcon(&pPopupContext->m_MuteAction, FontIcon::BAN, Client.m_ChatIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_ChatIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_MuteAction, &Action, Client.m_ChatIgnore ? Localize("Unmute") : Localize("Mute"));

		Container.VSplitLeft(ActionSpacing, nullptr, &Container);
		Container.VSplitLeft(ActionSize, &Action, &Container);

		const char *EmoticonActionIcon = Client.m_EmoticonIgnore ? FontIcon::COMMENT_SLASH : FontIcon::COMMENT;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_EmoticonAction, EmoticonActionIcon, Client.m_EmoticonIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_EmoticonIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_EmoticonAction, &Action, Client.m_EmoticonIgnore ? Localize("Unmute emoticons") : Localize("Mute emoticons"));
	}

	const float ButtonSize = 17.5f;
	View.HSplitTop(ItemSpacing * 2, nullptr, &View);
	View.HSplitTop(ButtonSize, &Container, &View);
	CUIRect LeftAction, RightAction;
	Container.VSplitMid(&LeftAction, &RightAction, 4.0f);

	const bool ActiveDummy = g_Config.m_ClDummy && pScoreboard->Client()->DummyConnected();
	if(pUi->DoButton_PopupMenu(&pPopupContext->m_CopySkinButton, Localize("Skin"), &LeftAction, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * pUi->ButtonColorMul(&pPopupContext->m_CopySkinButton))))
	{
		if(ActiveDummy)
		{
			str_copy(g_Config.m_ClDummySkin, Client.m_aSkinName);
			pScoreboard->GameClient()->SendDummyInfo(false);
		}
		else
		{
			str_copy(g_Config.m_ClPlayerSkin, Client.m_aSkinName);
			pScoreboard->GameClient()->SendInfo(false);
		}
		pScoreboard->GameClient()->m_Chat.Echo(Localize("Copied player skin."));
		return CUi::POPUP_CLOSE_CURRENT;
	}
	pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_CopySkinButton, &LeftAction, Localize("Copy skin"));

	if(pUi->DoButton_PopupMenu(&pPopupContext->m_CopyColorButton, Localize("Colors"), &RightAction, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * pUi->ButtonColorMul(&pPopupContext->m_CopyColorButton))))
	{
		if(ActiveDummy)
		{
			g_Config.m_ClDummyUseCustomColor = Client.m_UseCustomColor;
			g_Config.m_ClDummyColorBody = Client.m_ColorBody;
			g_Config.m_ClDummyColorFeet = Client.m_ColorFeet;
			pScoreboard->GameClient()->SendDummyInfo(false);
		}
		else
		{
			g_Config.m_ClPlayerUseCustomColor = Client.m_UseCustomColor;
			g_Config.m_ClPlayerColorBody = Client.m_ColorBody;
			g_Config.m_ClPlayerColorFeet = Client.m_ColorFeet;
			pScoreboard->GameClient()->SendInfo(false);
		}
		pScoreboard->GameClient()->m_Chat.Echo(Localize("Copied player colors."));
		return CUi::POPUP_CLOSE_CURRENT;
	}
	pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_CopyColorButton, &RightAction, Localize("Copy colors"));

	const bool ShowInviteAction = !pPopupContext->m_IsLocal;
	const bool ShowLockAction = AetherScoreboardCanLockLocalTeam(pScoreboard->GameClient());
	if(ShowInviteAction || ShowLockAction)
	{
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		if(ShowInviteAction && ShowLockAction)
			Container.VSplitMid(&LeftAction, &RightAction, 4.0f);
		else
		{
			LeftAction = Container;
			RightAction = Container;
		}
		if(ShowInviteAction)
		{
			const ColorRGBA InviteColor = AetherScoreboardAccentColor(pScoreboard->GameClient(), pPopupContext->m_ClientId, 0.35f * pUi->ButtonColorMul(&pPopupContext->m_InviteButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_InviteButton, Localize("Invite"), &LeftAction, FontSize, TEXTALIGN_MC, 0.0f, false, true, InviteColor))
			{
				char aCommand[128 + MAX_NAME_LENGTH];
				str_format(aCommand, sizeof(aCommand), "/invite %s", Client.m_aName);
				pScoreboard->GameClient()->m_Chat.SendChat(0, aCommand);
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
		if(ShowLockAction)
		{
			CUIRect &LockRect = ShowInviteAction ? RightAction : LeftAction;
			const bool Locked = pScoreboard->GameClient()->AetherLocalTeamLocked();
			const ColorRGBA LockColor = Locked ?
							    ColorRGBA(0.58f, 0.86f, 1.0f, 0.30f * pUi->ButtonColorMul(&pPopupContext->m_TeamLockButton)) :
							    ColorRGBA(0.98f, 0.68f, 1.0f, 0.30f * pUi->ButtonColorMul(&pPopupContext->m_TeamLockButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_TeamLockButton, Locked ? Localize("Unlock") : Localize("Lock"), &LockRect, FontSize, TEXTALIGN_MC, 0.0f, false, true, LockColor))
			{
				pScoreboard->GameClient()->m_Chat.SendChat(0, Locked ? "/lock 0" : "/lock 1");
				pScoreboard->GameClient()->AetherSetLocalTeamLocked(!Locked);
				return CUi::POPUP_CLOSE_CURRENT;
			}
			pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_TeamLockButton, &LockRect, Locked ? Localize("Unlock team") : Localize("Lock team"));
		}
	}

	if(!pPopupContext->m_IsLocal && AetherVariant::WarlistEnabled())
	{
		auto RemoveBlockEntries = [&]() {
			while(pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "enemy"))
				pScoreboard->GameClient()->m_WarList.RemoveWarEntry(Client.m_aName, "", "enemy");
			while(pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "team"))
				pScoreboard->GameClient()->m_WarList.RemoveWarEntry(Client.m_aName, "", "team");
			while(pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "helper"))
				pScoreboard->GameClient()->m_WarList.RemoveWarEntry(Client.m_aName, "", "helper");
		};

		auto AddBlockEntry = [&](const char *pType, const char *pLabel) {
			const bool HadEnemy = pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "enemy");
			const bool HadAlly = pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "team");
			const bool HadHelper = pScoreboard->GameClient()->m_WarList.FindWarEntry(Client.m_aName, "", "helper");
			const bool HadAny = HadEnemy || HadAlly || HadHelper;
			const bool SameType =
				(str_comp(pType, "enemy") == 0 && HadEnemy) ||
				(str_comp(pType, "team") == 0 && HadAlly) ||
				(str_comp(pType, "helper") == 0 && HadHelper);
			RemoveBlockEntries();
			pScoreboard->GameClient()->m_WarList.AddWarEntry(Client.m_aName, "", "", pType);
			pScoreboard->GameClient()->m_WarList.UpdateWarPlayers();
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "%s %s as %s.", HadAny && !SameType ? "Changed" : "Added", Client.m_aName, pLabel);
			pScoreboard->GameClient()->m_Chat.Echo(aMsg);
		};

		View.HSplitTop(ItemSpacing * 2, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_BlockEnemyButton, "Enemy", &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(1.0f, 0.25f, 0.22f, 0.58f * pUi->ButtonColorMul(&pPopupContext->m_BlockEnemyButton))))
		{
			AddBlockEntry("enemy", "Enemy");
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_BlockAllyButton, "Ally", &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(0.28f, 0.95f, 0.40f, 0.52f * pUi->ButtonColorMul(&pPopupContext->m_BlockAllyButton))))
		{
			AddBlockEntry("team", "Ally");
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_BlockHelperButton, "Helper", &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(1.0f, 0.86f, 0.25f, 0.54f * pUi->ButtonColorMul(&pPopupContext->m_BlockHelperButton))))
		{
			AddBlockEntry("helper", "Helper");
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_BlockNeutralButton, "Neutral", &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, ColorRGBA(0.72f, 0.74f, 0.78f, 0.46f * pUi->ButtonColorMul(&pPopupContext->m_BlockNeutralButton))))
		{
			RemoveBlockEntries();
			pScoreboard->GameClient()->m_WarList.UpdateWarPlayers();
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "Removed %s from Block Awareness.", Client.m_aName);
			pScoreboard->GameClient()->m_Chat.Echo(aMsg);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(!pPopupContext->m_IsSpectating)
	{
		View.HSplitTop(ItemSpacing * 2, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		bool IsSpectating = pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active && pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == pPopupContext->m_ClientId;
		ColorRGBA SpectateButtonColor = IsSpectating ?
							ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f * pUi->ButtonColorMul(&pPopupContext->m_SpectateButton)) :
							ColorRGBA(0.42f, 0.66f, 1.0f, 0.34f * pUi->ButtonColorMul(&pPopupContext->m_SpectateButton));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_SpectateButton, Localize("Spectate"), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, SpectateButtonColor))
		{
			if(IsSpectating)
			{
				pScoreboard->GameClient()->m_TClient.RequestFastSpecReturn();
			}
			else
			{
				pScoreboard->GameClient()->m_TClient.RequestFastSpecSpectate(pPopupContext->m_ClientId);
			}
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CScoreboard::CMapTitlePopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CMapTitlePopupContext *pPopupContext = static_cast<CMapTitlePopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;

	pScoreboard->TextRender()->Text(View.x, View.y, pPopupContext->m_FontSize, pScoreboard->GameClient()->m_aMapDescription, View.w);

	return CUi::POPUP_KEEP_OPEN;
}
