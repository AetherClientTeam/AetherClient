#include "badges.h"

#include "client_variant.h"

#include <base/math.h>
#include <base/log.h>
#include <base/net.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace
{
constexpr int CLIENT_HEARTBEAT_SECONDS = 10;
constexpr int CLIENT_REALTIME_HELLO_SECONDS = 10;
constexpr int PING_POLL_MILLISECONDS = 650;
constexpr int PING_LOCAL_TTL_SECONDS = 8;

struct SBadgeIconDef
{
	const char *m_pKey;
	const char *m_pPath;
};

constexpr std::array<SBadgeIconDef, 7> BADGE_ICON_DEFS = {{
	{"client_aether", "core/logos/aether_icon_small_256.png"},
	{"client_vera", "core/logos/vera_icon_small_256.png"},
	{"client_via", "core/logos/via_icon_small_256.png"},
	{"client_vex", "core/logos/vex_icon_small_256.png"},
	{"founder", "core/badges/founder.png"},
	{"tester", "core/badges/tester.png"},
	{"chess_winner", "core/badges/chess_winner.png"},
}};

const char *BadgeShortLabel(const char *pKey, const char *pName)
{
	if(str_comp(pKey, "client_aether") == 0)
		return "A";
	if(str_comp(pKey, "client_vera") == 0)
		return "Vera";
	if(str_comp(pKey, "client_via") == 0)
		return "Via";
	if(str_comp(pKey, "client_vex") == 0)
		return "Vex";
	if(str_comp(pKey, "founder") == 0)
		return "F";
	if(str_comp(pKey, "tester") == 0)
		return "T";
	if(str_comp(pKey, "donor") == 0)
		return "D";
	if(str_comp(pKey, "chess_winner") == 0)
		return "Chess";
	return pName && pName[0] ? pName : pKey;
}

bool IsClientBadgeKey(const char *pKey)
{
	return pKey && str_startswith(pKey, "client_");
}

const char *JsonStringValue(const json_value *pValue, const char *pFallback = "")
{
	const char *pString = json_string_get(pValue);
	return pString ? pString : pFallback;
}

bool JsonBoolValue(const json_value *pValue, bool Fallback = false)
{
	return pValue && pValue->type == json_boolean ? pValue->u.boolean : Fallback;
}

int JsonIntValue(const json_value *pValue, int Fallback = 0)
{
	return pValue && pValue->type == json_integer ? (int)pValue->u.integer : Fallback;
}

float JsonFloatValue(const json_value *pValue, float Fallback = 0.0f)
{
	if(!pValue)
		return Fallback;
	if(pValue->type == json_integer)
		return (float)pValue->u.integer;
	if(pValue->type == json_double)
		return (float)pValue->u.dbl;
	return Fallback;
}

std::string JsonStringLiteral(const char *pValue)
{
	CJsonStringWriter Json;
	Json.WriteStrValue(pValue ? pValue : "");
	return Json.GetOutputString();
}

IGraphics::CTextureHandle LoadCoreTextureWithFallback(IGraphics *pGraphics, const char *pPath)
{
	IGraphics::CTextureHandle Texture = pGraphics->LoadTexture(pPath, IStorage::TYPE_ALL);
	if(Texture.IsValid() || !str_startswith(pPath, "core/"))
		return Texture;

	char aFallback[IO_MAX_PATH_LENGTH];
	str_format(aFallback, sizeof(aFallback), "aether/%s", pPath + str_length("core/"));
	return pGraphics->LoadTexture(aFallback, IStorage::TYPE_ALL);
}
} // namespace

int CAetherBadges::BadgeRenderRank(const SBadge &Badge)
{
	return IsClientBadgeKey(Badge.m_aKey) ? 0 : 1;
}

int CAetherBadges::BadgeIconIndex(const char *pKey)
{
	if(!pKey)
		return -1;
	for(size_t i = 0; i < BADGE_ICON_DEFS.size(); ++i)
	{
		if(str_comp(pKey, BADGE_ICON_DEFS[i].m_pKey) == 0)
			return (int)i;
	}
	return -1;
}

const char *CAetherBadges::PingTypeName(EPingType Type)
{
	switch(Type)
	{
	case EPingType::PLACE: return "place";
	case EPingType::HELP: return "help";
	case EPingType::DANGER: return "danger";
	case EPingType::COME: return "come";
	case EPingType::WAIT: return "wait";
	}
	return "place";
}

const char *CAetherBadges::PingTypeDisplayName(EPingType Type)
{
	switch(Type)
	{
	case EPingType::PLACE: return "Place";
	case EPingType::HELP: return "Help";
	case EPingType::DANGER: return "Danger";
	case EPingType::COME: return "Come";
	case EPingType::WAIT: return "Wait";
	}
	return "Place";
}

const char *CAetherBadges::PingTypeGlyph(EPingType Type)
{
	switch(Type)
	{
	case EPingType::PLACE: return "x";
	case EPingType::HELP: return "+";
	case EPingType::DANGER: return "!";
	case EPingType::COME: return ">";
	case EPingType::WAIT: return "...";
	}
	return "x";
}

CAetherBadges::EPingType CAetherBadges::PingTypeFromName(const char *pName)
{
	if(pName && str_comp_nocase(pName, "place") == 0)
		return EPingType::PLACE;
	if(pName && str_comp_nocase(pName, "danger") == 0)
		return EPingType::DANGER;
	if(pName && str_comp_nocase(pName, "come") == 0)
		return EPingType::COME;
	if(pName && str_comp_nocase(pName, "wait") == 0)
		return EPingType::WAIT;
	return EPingType::HELP;
}

CAetherBadges::EPingType CAetherBadges::PingTypeFromWheelVector(vec2 Mouse)
{
	if(length(Mouse) <= 40.0f)
		return EPingType::PLACE;
	float Angle = std::atan2(Mouse.y, Mouse.x);
	if(Angle < 0.0f)
		Angle += 2.0f * pi;
	const int Segment = (int)std::floor((Angle + pi / 5.0f) / (2.0f * pi / 5.0f)) % 5;
	const std::array<EPingType, 5> aTypes = {EPingType::COME, EPingType::HELP, EPingType::PLACE, EPingType::WAIT, EPingType::DANGER};
	return aTypes[Segment];
}

ColorRGBA CAetherBadges::PingTypeColor(EPingType Type, float Alpha)
{
	switch(Type)
	{
	case EPingType::PLACE: return ColorRGBA(0.78f, 0.84f, 1.0f, Alpha);
	case EPingType::HELP: return ColorRGBA(0.20f, 0.85f, 1.0f, Alpha);
	case EPingType::DANGER: return ColorRGBA(1.0f, 0.25f, 0.22f, Alpha);
	case EPingType::COME: return ColorRGBA(0.35f, 0.95f, 0.45f, Alpha);
	case EPingType::WAIT: return ColorRGBA(1.0f, 0.80f, 0.25f, Alpha);
	}
	return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
}

void CAetherBadges::LoadIconTextures()
{
	if(m_IconTexturesLoaded)
		return;
	bool AllLoaded = true;
	for(size_t i = 0; i < BADGE_ICON_DEFS.size(); ++i)
	{
		if(!m_aIconTextures[i].IsValid())
			m_aIconTextures[i] = LoadCoreTextureWithFallback(Graphics(), BADGE_ICON_DEFS[i].m_pPath);
		if(!m_aIconTextures[i].IsValid())
		{
			AllLoaded = false;
			log_debug("core/badges", "badge texture not ready: %s", BADGE_ICON_DEFS[i].m_pPath);
		}
	}
	m_IconTexturesLoaded = AllLoaded;
	m_LastIconRetryTime = time_get();
}

void CAetherBadges::UnloadIconTextures()
{
	for(IGraphics::CTextureHandle &Texture : m_aIconTextures)
	{
		if(Texture.IsValid())
			Graphics()->UnloadTexture(&Texture);
	}
	m_IconTexturesLoaded = false;
}

void CAetherBadges::Clear()
{
	for(SClientBadges &Client : m_aClientBadges)
	{
		Client.m_aName[0] = '\0';
		Client.m_Valid = false;
		Client.m_vBadges.clear();
	}
	m_pResolveRequest = nullptr;
	m_pHeartbeatRequest = nullptr;
	m_pChessOnlineRequest = nullptr;
	m_pAetherOnlineRequest = nullptr;
	m_pChessActionRequest = nullptr;
	m_pPingSendRequest = nullptr;
	m_pPingPollRequest = nullptr;
	m_pClanRequest = nullptr;
	m_ChessAction = EChessHttpAction::NONE;
	m_ClanAction = EClanHttpAction::NONE;
	m_ClanRecoveryAction = EClanHttpAction::NONE;
	m_LastResolveTime = 0;
	m_LastHeartbeatTime = 0;
	m_LastRealtimeHelloTime = 0;
	m_LastIconRetryTime = 0;
	m_LastChessOnlineRequestTime = 0;
	m_LastChessOnlineResponseTime = 0;
	m_LastAetherOnlineRequestTime = 0;
	m_LastAetherOnlineResponseTime = 0;
	m_LastChessRoomPollTime = 0;
	m_LastPingPollTime = 0;
	m_LastClanMineTime = 0;
	m_LastClanDirectoryTime = 0;
	m_ChessInviteExpireTime = 0;
	m_ChessOnlineCount = 0;
	m_AetherOnlineCount = 0;
	m_LastPingSeq = 0;
	m_ChessRoom = SChessRoomState();
	m_Clan = SClanState();
	m_GeneralClan = SClanState();
	m_KogClan = SClanState();
	m_aClanRequestType[0] = '\0';
	m_aClanRequestId[0] = '\0';
	m_aClanRecoveryType[0] = '\0';
	m_LastRealtimePayload.clear();
	m_vChessMessages.clear();
	m_vPings.clear();
	m_vPublicClanMemberships.clear();
	m_aHelpPingStates.fill(EAutoHelpPingState::IDLE);
	m_aLastHeartbeatName[0] = '\0';
	m_aChessInviteId[0] = '\0';
	m_aChessInviteFrom[0] = '\0';
	m_aChessLastError[0] = '\0';
	m_aClanLastError[0] = '\0';
	m_Realtime.SetHelloPayload("");
	str_copy(m_aStatus, "Idle", sizeof(m_aStatus));
	str_copy(m_aAetherOnlineStatus, "Aether online idle.", sizeof(m_aAetherOnlineStatus));
	str_copy(m_aChessStatus, "Online chess ready.", sizeof(m_aChessStatus));
	str_copy(m_aClanStatus, "Clan ready.", sizeof(m_aClanStatus));
	m_ChessInviteActive = false;
	m_PingWheelActive = false;
	m_PingWheelWasActive = false;
	m_PingWheelHasSelection = false;
	m_ClanManagementAvailable = true;
	m_PingWheelMouse = vec2(0.0f, 0.0f);
	m_PingWheelSelected = EPingType::HELP;
}

void CAetherBadges::OnInit()
{
	LoadIconTextures();
	m_Realtime.Start();
	m_Realtime.SetEndpointFromHttpBase(g_Config.m_AeBadgesApiUrl);
}

void CAetherBadges::OnConsoleInit()
{
	Console()->Register("+ae_ping_wheel", "", CFGFLAG_CLIENT, ConPingWheel, this, "Open Aether ping wheel");
	Console()->Register("ae_ping", "?s[type]", CFGFLAG_CLIENT, ConPing, this, "Send Aether ping: place/help/danger/come/wait");
	Console()->Register("ae_ddrace_config", "s[name] ?s[on|off|toggle]", CFGFLAG_CLIENT, ConDdraceConfig, this, "Toggle Aether DDRace config pack");
}

void CAetherBadges::BuildUrl(char *pOut, int OutSize, const char *pPath) const
{
	char aBase[256];
	str_copy(aBase, g_Config.m_AeBadgesApiUrl, sizeof(aBase));
	int Len = str_length(aBase);
	while(Len > 0 && aBase[Len - 1] == '/')
		aBase[--Len] = '\0';
	str_format(pOut, OutSize, "%s%s", aBase, pPath);
}

bool CAetherBadges::HasActivePlayers() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(pInfo && pInfo->m_ClientId >= 0 && GameClient()->m_aClients[pInfo->m_ClientId].m_aName[0] != '\0')
			return true;
	}
	return false;
}

bool CAetherBadges::LocalPlayerName(char *pOut, int OutSize) const
{
	if(!pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const char *pName = nullptr;
	if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
		pName = GameClient()->m_aClients[LocalId].m_aName;
	else if(g_Config.m_PlayerName[0] != '\0')
		pName = g_Config.m_PlayerName;
	if(!pName)
		return false;
	if(pName[0] == '\0')
		return false;
	str_copy(pOut, pName, OutSize);
	return true;
}

bool CAetherBadges::CurrentServerKey(char *pOut, int OutSize) const
{
	if(!pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	net_addr_str(&Client()->ServerAddress(), pOut, OutSize, true);
	if(pOut[0] == '\0' && Client()->ConnectAddressString())
		str_copy(pOut, Client()->ConnectAddressString(), OutSize);
	return pOut[0] != '\0';
}

bool CAetherBadges::BuildPresencePayload(std::string &Payload) const
{
	char aName[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aName, sizeof(aName)))
		return false;

	CServerInfo ServerInfo;
	mem_zero(&ServerInfo, sizeof(ServerInfo));
	const bool InServer = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(InServer)
		Client()->GetServerInfo(&ServerInfo);

	char aServerAddress[NETADDR_MAXSTRSIZE];
	aServerAddress[0] = '\0';
	if(InServer)
		net_addr_str(&Client()->ServerAddress(), aServerAddress, sizeof(aServerAddress), true);
	if(InServer && aServerAddress[0] == '\0' && Client()->ConnectAddressString())
		str_copy(aServerAddress, Client()->ConnectAddressString(), sizeof(aServerAddress));

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	int Team = -1;
	if(InServer && LocalId >= 0 && LocalId < MAX_CLIENTS)
		Team = GameClient()->m_Teams.Team(LocalId);
	const bool Spectator = InServer && (GameClient()->m_Snap.m_SpecInfo.m_Active ||
			       (LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_Team == TEAM_SPECTATORS));
	const int SpectateTarget = Spectator ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : -1;

	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("type");
	Json.WriteStrValue("hello");
	Json.WriteAttribute("player_name");
	Json.WriteStrValue(aName);
	Json.WriteAttribute("client");
	Json.WriteStrValue(AetherVariant::Key());
	Json.WriteAttribute("version");
	Json.WriteStrValue(CLIENT_RELEASE_VERSION);
	Json.WriteAttribute("server_key");
	Json.WriteStrValue(aServerAddress);
	Json.WriteAttribute("server_address");
	Json.WriteStrValue(aServerAddress);
	Json.WriteAttribute("server_name");
	Json.WriteStrValue(ServerInfo.m_aName);
	Json.WriteAttribute("map");
	Json.WriteStrValue(ServerInfo.m_aMap);
	Json.WriteAttribute("team");
	Json.WriteIntValue(Team);
	Json.WriteAttribute("spectator");
	Json.WriteBoolValue(Spectator);
	Json.WriteAttribute("spectate_target");
	Json.WriteIntValue(SpectateTarget);
	Json.EndObject();

	Payload = Json.GetOutputString();
	return true;
}

void CAetherBadges::RestartRealtime()
{
	m_Realtime.Stop();
	m_Realtime.Start();
	m_Realtime.SetEndpointFromHttpBase(g_Config.m_AeBadgesApiUrl);
	m_LastRealtimeHelloTime = 0;
	m_LastRealtimePayload.clear();
}

void CAetherBadges::RequestHeartbeat(bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pHeartbeatRequest)
		return;

	char aName[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aName, sizeof(aName)))
		return;

	const int64_t Now = time_get();
	const bool NameChanged = str_comp(m_aLastHeartbeatName, aName) != 0;
	if(!Force && !NameChanged && m_LastHeartbeatTime != 0 && Now - m_LastHeartbeatTime < (int64_t)CLIENT_HEARTBEAT_SECONDS * time_freq())
		return;

	std::string Payload;
	if(!BuildPresencePayload(Payload))
		return;

	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), "/v1/clients/heartbeat");
	m_pHeartbeatRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pHeartbeatRequest->PostJson(Payload.c_str());
	m_pHeartbeatRequest->MaxResponseSize(16 * 1024);
	m_pHeartbeatRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pHeartbeatRequest);
	m_LastHeartbeatTime = Now;
	str_copy(m_aLastHeartbeatName, aName, sizeof(m_aLastHeartbeatName));
}

void CAetherBadges::PumpHeartbeatRequest()
{
	if(!m_pHeartbeatRequest)
		return;

	const EHttpState HttpState = m_pHeartbeatRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	if(HttpState == EHttpState::DONE && m_pHeartbeatRequest->StatusCode() >= 200 && m_pHeartbeatRequest->StatusCode() < 400)
		m_LastResolveTime = 0;
	else
		str_copy(m_aStatus, "Client badge heartbeat failed.", sizeof(m_aStatus));

	m_pHeartbeatRequest = nullptr;
}

void CAetherBadges::RequestResolve(bool Force)
{
	if(!g_Config.m_AeBadges || g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pResolveRequest || !HasActivePlayers())
		return;

	const int RefreshSeconds = std::clamp(g_Config.m_AeBadgesRefreshSeconds, 30, 3600);
	const int64_t Now = time_get();
	if(!Force && m_LastResolveTime != 0 && Now - m_LastResolveTime < (int64_t)RefreshSeconds * time_freq())
		return;

	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("players");
	Json.BeginArray();
	int Count = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(!pInfo || pInfo->m_ClientId < 0)
			continue;
		const char *pName = GameClient()->m_aClients[pInfo->m_ClientId].m_aName;
		if(pName[0] == '\0')
			continue;
		Json.WriteStrValue(pName);
		++Count;
	}
	Json.EndArray();
	Json.EndObject();
	if(Count == 0)
		return;

	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), "/v1/badges/resolve");
	std::string Payload = Json.GetOutputString();
	m_pResolveRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pResolveRequest->PostJson(Payload.c_str());
	m_pResolveRequest->MaxResponseSize(128 * 1024);
	m_pResolveRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pResolveRequest);
	m_LastResolveTime = Now;
	str_format(m_aStatus, sizeof(m_aStatus), "Resolving %d player(s)...", Count);
}

void CAetherBadges::SendChessOnlineRequestPayload()
{
	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("type");
	Json.WriteStrValue("chess:online");
	Json.EndObject();
	SendRealtimePayload(Json.GetOutputString());
}

void CAetherBadges::RequestChessOnline(bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastChessOnlineRequestTime != 0 && Now - m_LastChessOnlineRequestTime < 12 * time_freq())
		return;

	SendChessOnlineRequestPayload();
	if(!m_pChessOnlineRequest)
	{
		char aUrl[256];
		BuildUrl(aUrl, sizeof(aUrl), "/v1/chess/online");
		m_pChessOnlineRequest = std::make_shared<CHttpRequest>(aUrl);
		m_pChessOnlineRequest->MaxResponseSize(256 * 1024);
		m_pChessOnlineRequest->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(m_pChessOnlineRequest);
	}
	m_LastChessOnlineRequestTime = Now;
	if(!m_pChessActionRequest && m_ChessRoom.m_aCode[0] == '\0')
		str_copy(m_aChessStatus, "Refreshing global chess lobby...", sizeof(m_aChessStatus));
}

void CAetherBadges::RequestAetherOnline(bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastAetherOnlineRequestTime != 0 && Now - m_LastAetherOnlineRequestTime < 12 * time_freq())
		return;
	if(!m_pAetherOnlineRequest)
	{
		char aUrl[256];
		BuildUrl(aUrl, sizeof(aUrl), "/v1/clients/online");
		m_pAetherOnlineRequest = std::make_shared<CHttpRequest>(aUrl);
		m_pAetherOnlineRequest->MaxResponseSize(256 * 1024);
		m_pAetherOnlineRequest->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(m_pAetherOnlineRequest);
		str_copy(m_aAetherOnlineStatus, "Refreshing Aether players...", sizeof(m_aAetherOnlineStatus));
	}
	m_LastAetherOnlineRequestTime = Now;
}

void CAetherBadges::RequestChessLeaderboard(const char *pPeriod, bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
		return;
	const bool Monthly = pPeriod && str_comp(pPeriod, "monthly") == 0;
	std::shared_ptr<CHttpRequest> *ppRequest = Monthly ? &m_pChessLeaderboardMonthlyRequest : &m_pChessLeaderboardAllRequest;
	if(*ppRequest && !Force)
		return;
	if(*ppRequest)
		(*ppRequest)->Abort();

	char aPath[128];
	str_format(aPath, sizeof(aPath), "/v1/chess/leaderboard?period=%s&limit=5", Monthly ? "monthly" : "all");
	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), aPath);
	*ppRequest = std::make_shared<CHttpRequest>(aUrl);
	(*ppRequest)->MaxResponseSize(64 * 1024);
	(*ppRequest)->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(*ppRequest);
	str_copy(m_aChessStatus, "Refreshing chess leaderboards...", sizeof(m_aChessStatus));
}

bool CAetherBadges::WriteChessPlayerName(CJsonStringWriter &Json)
{
	char aName[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aName, sizeof(aName)))
	{
		str_copy(m_aChessStatus, "Set a player name before using online chess.", sizeof(m_aChessStatus));
		str_copy(m_aChessLastError, m_aChessStatus, sizeof(m_aChessLastError));
		return false;
	}
	Json.WriteAttribute("player_name");
	Json.WriteStrValue(aName);
	return true;
}

bool CAetherBadges::StartChessHttpPost(const char *pPath, const std::string &Payload, EChessHttpAction Action, const char *pStatus)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
	{
		str_copy(m_aChessStatus, "Chess API URL is empty.", sizeof(m_aChessStatus));
		str_copy(m_aChessLastError, m_aChessStatus, sizeof(m_aChessLastError));
		return false;
	}
	if(m_pChessActionRequest)
		m_pChessActionRequest->Abort();
	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), pPath);
	m_pChessActionRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pChessActionRequest->PostJson(Payload.c_str());
	m_pChessActionRequest->MaxResponseSize(256 * 1024);
	m_pChessActionRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pChessActionRequest);
	m_ChessAction = Action;
	if(pStatus && pStatus[0])
		str_copy(m_aChessStatus, pStatus, sizeof(m_aChessStatus));
	return true;
}

bool CAetherBadges::StartChessHttpGet(const char *pPath, EChessHttpAction Action, const char *pStatus, bool Quiet)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pChessActionRequest)
		return false;
	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), pPath);
	m_pChessActionRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pChessActionRequest->MaxResponseSize(256 * 1024);
	m_pChessActionRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pChessActionRequest);
	m_ChessAction = Action;
	if(!Quiet && pStatus && pStatus[0])
		str_copy(m_aChessStatus, pStatus, sizeof(m_aChessStatus));
	return true;
}

void CAetherBadges::RequestChessRoomSnapshot(bool Force)
{
	if(m_ChessRoom.m_aCode[0] == '\0' || m_pChessActionRequest)
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastChessRoomPollTime != 0 && Now - m_LastChessRoomPollTime < time_freq())
		return;
	char aName[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aName, sizeof(aName)))
		return;
	char aEscapedName[MAX_NAME_LENGTH * 3];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), aName);
	char aPath[128 + MAX_NAME_LENGTH * 3];
	str_format(aPath, sizeof(aPath), "/v1/chess/rooms/%s?player=%s", m_ChessRoom.m_aCode, aEscapedName);
	if(StartChessHttpGet(aPath, EChessHttpAction::ROOM_SNAPSHOT, "", true))
		m_LastChessRoomPollTime = Now;
}

void CAetherBadges::SendPing(EPingType Type, vec2 Pos, bool Auto)
{
	if(!g_Config.m_AePings || g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pPingSendRequest)
		return;
	char aName[MAX_NAME_LENGTH];
	char aServerKey[NETADDR_MAXSTRSIZE];
	if(!LocalPlayerName(aName, sizeof(aName)) || !CurrentServerKey(aServerKey, sizeof(aServerKey)))
		return;

	CServerInfo ServerInfo;
	mem_zero(&ServerInfo, sizeof(ServerInfo));
	Client()->GetServerInfo(&ServerInfo);

	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("player_name");
	Json.WriteStrValue(aName);
	Json.WriteAttribute("server_key");
	Json.WriteStrValue(aServerKey);
	Json.WriteAttribute("server_address");
	Json.WriteStrValue(aServerKey);
	Json.WriteAttribute("map");
	Json.WriteStrValue(ServerInfo.m_aMap);
	Json.WriteAttribute("type");
	Json.WriteStrValue(PingTypeName(Type));
	Json.WriteAttribute("x");
	Json.WriteIntValue((int)Pos.x);
	Json.WriteAttribute("y");
	Json.WriteIntValue((int)Pos.y);
	Json.WriteAttribute("auto");
	Json.WriteBoolValue(Auto);
	Json.EndObject();
	std::string Payload = Json.GetOutputString();

	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), "/v1/pings/send");
	m_pPingSendRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pPingSendRequest->PostJson(Payload.c_str());
	m_pPingSendRequest->MaxResponseSize(32 * 1024);
	m_pPingSendRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pPingSendRequest);

	SPingEvent Event;
	Event.m_Seq = -1;
	Event.m_Type = Type;
	str_copy(Event.m_aPlayer, aName, sizeof(Event.m_aPlayer));
	Event.m_Pos = Pos;
	Event.m_ExpireTime = time_get() + (int64_t)PING_LOCAL_TTL_SECONDS * time_freq();
	Event.m_Auto = Auto;
	AddOrMergePing(Event);
}

void CAetherBadges::RequestPingPoll(bool Force)
{
	if(!g_Config.m_AePings || g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pPingPollRequest)
		return;
	char aServerKey[NETADDR_MAXSTRSIZE];
	if(!CurrentServerKey(aServerKey, sizeof(aServerKey)))
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastPingPollTime != 0 && Now - m_LastPingPollTime < (int64_t)PING_POLL_MILLISECONDS * time_freq() / 1000)
		return;
	char aEscapedServer[NETADDR_MAXSTRSIZE * 3];
	EscapeUrl(aEscapedServer, sizeof(aEscapedServer), aServerKey);
	char aPath[256];
	str_format(aPath, sizeof(aPath), "/v1/pings/poll?server_key=%s&since=%d", aEscapedServer, m_LastPingSeq);
	char aUrl[320];
	BuildUrl(aUrl, sizeof(aUrl), aPath);
	m_pPingPollRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pPingPollRequest->MaxResponseSize(64 * 1024);
	m_pPingPollRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pPingPollRequest);
	m_LastPingPollTime = Now;
}

void CAetherBadges::PumpPingRequests()
{
	if(m_pPingSendRequest)
	{
		const EHttpState State = m_pPingSendRequest->State();
		if(State == EHttpState::DONE || State == EHttpState::ERROR || State == EHttpState::ABORTED)
			m_pPingSendRequest = nullptr;
	}

	if(!m_pPingPollRequest)
		return;
	const EHttpState State = m_pPingPollRequest->State();
	if(State != EHttpState::DONE && State != EHttpState::ERROR && State != EHttpState::ABORTED)
		return;
	if(State == EHttpState::DONE && m_pPingPollRequest->StatusCode() >= 200 && m_pPingPollRequest->StatusCode() < 400)
	{
		json_value *pJson = m_pPingPollRequest->ResultJson();
		const json_value *pPings = pJson && pJson->type == json_object ? json_object_get(pJson, "pings") : nullptr;
		if(pPings && pPings->type == json_array)
		{
			for(int i = 0; i < json_array_length(pPings); ++i)
			{
				const json_value *pPing = json_array_get(pPings, i);
				if(!pPing || pPing->type != json_object)
					continue;
				SPingEvent Event;
				Event.m_Seq = JsonIntValue(json_object_get(pPing, "seq"));
				Event.m_Type = PingTypeFromName(JsonStringValue(json_object_get(pPing, "type"), "help"));
				str_copy(Event.m_aPlayer, JsonStringValue(json_object_get(pPing, "player_name"), "-"), sizeof(Event.m_aPlayer));
				Event.m_Pos = vec2(JsonFloatValue(json_object_get(pPing, "x")), JsonFloatValue(json_object_get(pPing, "y")));
				Event.m_ExpireTime = time_get() + (int64_t)PING_LOCAL_TTL_SECONDS * time_freq();
				Event.m_Auto = JsonBoolValue(json_object_get(pPing, "auto"));
				m_LastPingSeq = std::max(m_LastPingSeq, Event.m_Seq);
				AddOrMergePing(Event);
			}
		}
		m_LastPingSeq = std::max(m_LastPingSeq, JsonIntValue(json_object_get(pJson, "next_since"), m_LastPingSeq));
		if(pJson)
			json_value_free(pJson);
	}
	m_pPingPollRequest = nullptr;
}

bool CAetherBadges::StartClanHttpPost(const char *pPath, const std::string &Payload, EClanHttpAction Action, const char *pStatus)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
		return false;
	if(m_pClanRequest)
	{
		m_pClanRequest->Abort();
		m_pClanRequest = nullptr;
		m_ClanAction = EClanHttpAction::NONE;
		m_aClanRequestType[0] = '\0';
		m_aClanRequestId[0] = '\0';
	}
	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), pPath);
	m_pClanRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pClanRequest->PostJson(Payload.c_str());
	m_pClanRequest->MaxResponseSize(512 * 1024);
	m_pClanRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pClanRequest);
	m_ClanAction = Action;
	m_aClanLastError[0] = '\0';
	if(pStatus && pStatus[0])
		str_copy(m_aClanStatus, pStatus, sizeof(m_aClanStatus));
	return true;
}

bool CAetherBadges::StartClanHttpGet(const char *pPath, EClanHttpAction Action, const char *pStatus, bool Quiet)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0')
		return false;
	if(m_pClanRequest)
	{
		m_pClanRequest->Abort();
		m_pClanRequest = nullptr;
		m_ClanAction = EClanHttpAction::NONE;
		m_aClanRequestType[0] = '\0';
		m_aClanRequestId[0] = '\0';
	}
	char aUrl[256];
	BuildUrl(aUrl, sizeof(aUrl), pPath);
	m_pClanRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pClanRequest->MaxResponseSize(768 * 1024);
	m_pClanRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pClanRequest);
	m_ClanAction = Action;
	m_aClanLastError[0] = '\0';
	if(!Quiet && pStatus && pStatus[0])
		str_copy(m_aClanStatus, pStatus, sizeof(m_aClanStatus));
	return true;
}

bool CAetherBadges::StartClanRecovery(const SClanState &Clan, EClanHttpAction Action)
{
	if(!Clan.m_Valid || Clan.m_aInviteCode[0] == '\0')
		return false;
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return false;
	std::string Payload = std::string("{\"invite_code\":") + JsonStringLiteral(Clan.m_aInviteCode) +
			      ",\"player_name\":" + JsonStringLiteral(aPlayer) + "}";
	m_ClanRecoveryAction = Action;
	str_copy(m_aClanRecoveryType, str_comp_nocase(Clan.m_aType, "kog") == 0 ? "kog" : "general", sizeof(m_aClanRecoveryType));
	return StartClanHttpPost("/v1/clans/join", Payload, EClanHttpAction::RECOVER_MEMBERSHIP, "Recovering clan membership...");
}

void CAetherBadges::RetryClanRecoveryAction()
{
	const EClanHttpAction Action = m_ClanRecoveryAction;
	const bool KogClan = str_comp_nocase(m_aClanRecoveryType, "kog") == 0;
	SClanState Clan = KogClan ? m_KogClan : m_GeneralClan;
	m_ClanRecoveryAction = EClanHttpAction::NONE;
	m_aClanRecoveryType[0] = '\0';
	if(!Clan.m_Valid)
		return;
	switch(Action)
	{
	case EClanHttpAction::MEMBERS:
		SendClanMembers(Clan);
		break;
	case EClanHttpAction::LEAVE:
		SendClanLeave(Clan);
		break;
	case EClanHttpAction::DISBAND:
		SendClanDisband(Clan);
		break;
	case EClanHttpAction::ROTATE_INVITE:
		SendClanRotateInvite(Clan);
		break;
	default:
		str_copy(m_aClanStatus, "Clan membership recovered.", sizeof(m_aClanStatus));
		break;
	}
}

void CAetherBadges::ClearClanByType(const char *pType)
{
	const bool KogClan = pType && str_comp_nocase(pType, "kog") == 0;
	if(KogClan)
	{
		m_KogClan = SClanState();
		g_Config.m_AeClanKogId[0] = '\0';
		g_Config.m_AeClanKogSecret[0] = '\0';
	}
	else
	{
		m_GeneralClan = SClanState();
		g_Config.m_AeClanGeneralId[0] = '\0';
		g_Config.m_AeClanGeneralSecret[0] = '\0';
	}
	if((KogClan && str_comp(m_Clan.m_aType, "kog") == 0) || (!KogClan && str_comp(m_Clan.m_aType, "general") == 0))
		m_Clan = SClanState();
	if(str_comp(g_Config.m_AeClanId, m_aClanRequestId) == 0)
	{
		g_Config.m_AeClanId[0] = '\0';
		g_Config.m_AeClanSecret[0] = '\0';
	}
}

void CAetherBadges::ApplyClanObject(const json_value *pClan, const char *pSecret)
{
	if(!pClan || pClan->type != json_object)
		return;
	const char *pId = JsonStringValue(json_object_get(pClan, "id"));
	if(pId[0] == '\0')
		return;
	SClanState Clan;
	str_copy(Clan.m_aId, pId, sizeof(Clan.m_aId));
	str_copy(Clan.m_aName, JsonStringValue(json_object_get(pClan, "name")), sizeof(Clan.m_aName));
	str_copy(Clan.m_aType, JsonStringValue(json_object_get(pClan, "type"), "general"), sizeof(Clan.m_aType));
	str_copy(Clan.m_aInviteCode, JsonStringValue(json_object_get(pClan, "invite_code")), sizeof(Clan.m_aInviteCode));
	str_copy(Clan.m_aRole, JsonStringValue(json_object_get(pClan, "role"), "member"), sizeof(Clan.m_aRole));
	Clan.m_MemberCount = JsonIntValue(json_object_get(pClan, "member_count"));
	Clan.m_Valid = true;

	const bool KogClan = str_comp_nocase(Clan.m_aType, "kog") == 0;
	SClanState &TypedClan = KogClan ? m_KogClan : m_GeneralClan;
	char *pTypedId = KogClan ? g_Config.m_AeClanKogId : g_Config.m_AeClanGeneralId;
	char *pTypedSecret = KogClan ? g_Config.m_AeClanKogSecret : g_Config.m_AeClanGeneralSecret;
	const int TypedIdSize = KogClan ? sizeof(g_Config.m_AeClanKogId) : sizeof(g_Config.m_AeClanGeneralId);
	const int TypedSecretSize = KogClan ? sizeof(g_Config.m_AeClanKogSecret) : sizeof(g_Config.m_AeClanGeneralSecret);
	const bool HasNewSecret = pSecret && pSecret[0];
	const bool LegacySecretMatchesClan = !HasNewSecret &&
					     pTypedSecret[0] == '\0' &&
					     g_Config.m_AeClanSecret[0] != '\0' &&
					     g_Config.m_AeClanId[0] != '\0' &&
					     str_comp(g_Config.m_AeClanId, Clan.m_aId) == 0;
	TypedClan = Clan;
	str_copy(pTypedId, Clan.m_aId, TypedIdSize);
	if(HasNewSecret)
		str_copy(pTypedSecret, pSecret, TypedSecretSize);
	else if(LegacySecretMatchesClan)
		str_copy(pTypedSecret, g_Config.m_AeClanSecret, TypedSecretSize);

	m_Clan = Clan;
	if(HasNewSecret)
	{
		str_copy(g_Config.m_AeClanId, Clan.m_aId, sizeof(g_Config.m_AeClanId));
		str_copy(g_Config.m_AeClanSecret, pSecret, sizeof(g_Config.m_AeClanSecret));
	}
}

void CAetherBadges::ApplyClanMembers(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const json_value *pClan = json_object_get(pRoot, "clan");
	if(pClan)
		ApplyClanObject(pClan, nullptr);
	const char *pType = pClan ? JsonStringValue(json_object_get(pClan, "type"), m_aClanRequestType) : m_aClanRequestType;
	SClanState &Clan = str_comp_nocase(pType, "kog") == 0 ? m_KogClan : m_GeneralClan;
	const json_value *pMembers = json_object_get(pRoot, "members");
	Clan.m_MembersLoaded = true;
	Clan.m_MemberListCount = 0;
	if(pMembers && pMembers->type == json_array)
	{
		const int Count = std::min(json_array_length(pMembers), (int)Clan.m_aMembers.size());
		for(int i = 0; i < Count; ++i)
		{
			const json_value *pMember = json_array_get(pMembers, i);
			if(!pMember || pMember->type != json_object)
				continue;
			SClanState::SMember &Member = Clan.m_aMembers[Clan.m_MemberListCount++];
			str_copy(Member.m_aName, JsonStringValue(json_object_get(pMember, "player_name")), sizeof(Member.m_aName));
			str_copy(Member.m_aRole, JsonStringValue(json_object_get(pMember, "role"), "member"), sizeof(Member.m_aRole));
			str_copy(Member.m_aJoinedAt, JsonStringValue(json_object_get(pMember, "joined_at")), sizeof(Member.m_aJoinedAt));
		}
		Clan.m_MemberCount = json_array_length(pMembers);
	}
	if(Clan.m_MemberListCount == 0 && Clan.m_Valid)
	{
		char aPlayer[MAX_NAME_LENGTH];
		if(LocalPlayerName(aPlayer, sizeof(aPlayer)))
		{
			SClanState::SMember &Member = Clan.m_aMembers[Clan.m_MemberListCount++];
			str_copy(Member.m_aName, aPlayer, sizeof(Member.m_aName));
			str_copy(Member.m_aRole, Clan.m_aRole[0] ? Clan.m_aRole : "member", sizeof(Member.m_aRole));
			Member.m_aJoinedAt[0] = '\0';
			Clan.m_MemberCount = std::max(Clan.m_MemberCount, 1);
		}
	}
	if(str_comp_nocase(Clan.m_aType, "kog") == 0)
		m_KogClan = Clan;
	else
		m_GeneralClan = Clan;
	if(str_comp(m_Clan.m_aId, Clan.m_aId) == 0)
		m_Clan = Clan;
}

void CAetherBadges::ApplyClanDirectory(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const json_value *pClans = json_object_get(pRoot, "clans");
	if(!pClans || pClans->type != json_array)
		return;

	m_vPublicClanMemberships.clear();
	for(int ClanIndex = 0; ClanIndex < json_array_length(pClans); ++ClanIndex)
	{
		const json_value *pClan = json_array_get(pClans, ClanIndex);
		if(!pClan || pClan->type != json_object)
			continue;
		const char *pClanName = JsonStringValue(json_object_get(pClan, "name"));
		const char *pClanType = JsonStringValue(json_object_get(pClan, "type"), "general");
		if(pClanName[0] == '\0')
			continue;
		const json_value *pMembers = json_object_get(pClan, "members");
		if(!pMembers || pMembers->type != json_array)
			continue;
		for(int MemberIndex = 0; MemberIndex < json_array_length(pMembers); ++MemberIndex)
		{
			const json_value *pMember = json_array_get(pMembers, MemberIndex);
			if(!pMember || pMember->type != json_object)
				continue;
			const char *pPlayer = JsonStringValue(json_object_get(pMember, "player_name"));
			if(pPlayer[0] == '\0')
				continue;
			SPublicClanMembership Entry;
			str_copy(Entry.m_aPlayer, pPlayer, sizeof(Entry.m_aPlayer));
			str_copy(Entry.m_aClan, pClanName, sizeof(Entry.m_aClan));
			str_copy(Entry.m_aType, pClanType, sizeof(Entry.m_aType));
			m_vPublicClanMemberships.push_back(Entry);
		}
	}
}

void CAetherBadges::ApplyClanResponse(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const char *pSecret = JsonStringValue(json_object_get(pRoot, "membership_secret"));
	if(const json_value *pClan = json_object_get(pRoot, "clan"))
		ApplyClanObject(pClan, pSecret);

	if(m_ClanAction == EClanHttpAction::MINE)
	{
		const json_value *pClans = json_object_get(pRoot, "clans");
		m_Clan = SClanState();
		m_GeneralClan = SClanState();
		m_KogClan = SClanState();
		if(pClans && pClans->type == json_array)
		{
			const json_value *pSelected = nullptr;
			for(int i = 0; i < json_array_length(pClans); ++i)
			{
				const json_value *pClan = json_array_get(pClans, i);
				if(!pClan || pClan->type != json_object)
					continue;
				ApplyClanObject(pClan, nullptr);
				if(!pSelected && (g_Config.m_AeClanId[0] == '\0' || str_comp(JsonStringValue(json_object_get(pClan, "id")), g_Config.m_AeClanId) == 0))
					pSelected = pClan;
			}
			if(!pSelected && json_array_length(pClans) > 0)
				pSelected = json_array_get(pClans, 0);
			ApplyClanObject(pSelected, nullptr);
		}
		if(m_Clan.m_Valid)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Clan: %s", m_Clan.m_aName);
		else
			str_copy(m_aClanStatus, "No clan joined.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::CREATE || m_ClanAction == EClanHttpAction::JOIN)
	{
		if(m_Clan.m_Valid)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Joined clan %s.", m_Clan.m_aName);
	}
	else if(m_ClanAction == EClanHttpAction::RECOVER_MEMBERSHIP)
	{
		if(m_Clan.m_Valid)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Recovered clan membership for %s.", m_Clan.m_aName);
		else
			str_copy(m_aClanStatus, "Recovered clan membership.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::MEMBERS)
	{
		ApplyClanMembers(pRoot);
		const SClanState &Clan = str_comp_nocase(m_aClanRequestType, "kog") == 0 ? m_KogClan : m_GeneralClan;
		if(Clan.m_Valid)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "%s members loaded.", Clan.m_aName);
	}
	else if(m_ClanAction == EClanHttpAction::PUBLIC_DIRECTORY)
	{
		ApplyClanDirectory(pRoot);
		if(!m_vPublicClanMemberships.empty())
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Cloud clan directory loaded: %d members.", (int)m_vPublicClanMemberships.size());
	}
	else if(m_ClanAction == EClanHttpAction::MEMBER_REMOVE)
	{
		ApplyClanMembers(pRoot);
		const char *pRemoved = JsonStringValue(json_object_get(pRoot, "removed_player_name"));
		if(pRemoved[0])
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Removed %s from clan.", pRemoved);
		else
			str_copy(m_aClanStatus, "Clan member removed.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::LEAVE)
	{
		ClearClanByType(m_aClanRequestType);
		str_copy(m_aClanStatus, "Left clan.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::DISBAND)
	{
		ClearClanByType(m_aClanRequestType);
		str_copy(m_aClanStatus, "Clan disbanded.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::ROTATE_INVITE)
	{
		if(m_Clan.m_Valid)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "%s invite code updated.", m_Clan.m_aName);
	}
	else if(m_ClanAction == EClanHttpAction::WARLIST_PUSH)
	{
		const json_value *pSnapshot = json_object_get(pRoot, "snapshot");
		const int Count = JsonIntValue(pSnapshot && pSnapshot->type == json_object ? json_object_get(pSnapshot, "entries_count") : nullptr);
		if(Count > 0)
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Clan warlist pushed: %d entries.", Count);
		else
			str_copy(m_aClanStatus, "Clan warlist pushed.", sizeof(m_aClanStatus));
	}
	else if(m_ClanAction == EClanHttpAction::WARLIST_PULL)
	{
		char aError[160] = "";
		const json_value *pEntries = json_object_get(pRoot, "entries");
		if(GameClient()->m_WarList.ImportJsonEntries(pEntries, aError, sizeof(aError)))
			str_format(m_aClanStatus, sizeof(m_aClanStatus), "Clan %s", aError);
		else
			str_copy(m_aClanStatus, aError[0] ? aError : "No clan warlist entries.", sizeof(m_aClanStatus));
	}
}

void CAetherBadges::PumpClanRequest()
{
	if(!m_pClanRequest)
		return;
	const EHttpState HttpState = m_pClanRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	const EClanHttpAction CompletedAction = m_ClanAction;
	SClanState RecoveryClan;
	EClanHttpAction RecoveryAction = EClanHttpAction::NONE;
	if(HttpState == EHttpState::DONE && m_pClanRequest->StatusCode() >= 200 && m_pClanRequest->StatusCode() < 400)
	{
		json_value *pJson = m_pClanRequest->ResultJson();
		ApplyClanResponse(pJson);
		if(pJson)
			json_value_free(pJson);
		m_aClanLastError[0] = '\0';
	}
	else
	{
		int StatusCode = 0;
		json_value *pJson = nullptr;
		if(HttpState == EHttpState::DONE)
		{
			StatusCode = m_pClanRequest->StatusCode();
			pJson = m_pClanRequest->ResultJson();
		}
		const bool ManagementAction = CompletedAction == EClanHttpAction::MEMBERS || CompletedAction == EClanHttpAction::LEAVE || CompletedAction == EClanHttpAction::DISBAND || CompletedAction == EClanHttpAction::ROTATE_INVITE || CompletedAction == EClanHttpAction::MEMBER_REMOVE;
		const char *pError = pJson && pJson->type == json_object ? JsonStringValue(json_object_get(pJson, "error"), "request failed") : (HttpState == EHttpState::ABORTED ? "request aborted" : "request failed");
		const bool EndpointMissing404 = StatusCode == 404 && (!pJson || str_comp(pError, "Not Found") == 0 || str_find_nocase(pError, "route") != nullptr);
		if(ManagementAction && EndpointMissing404)
		{
			m_ClanManagementAvailable = false;
			str_copy(m_aClanLastError, "Clan management endpoint is not deployed. Deploy latest AetherClient-api, then refresh.", sizeof(m_aClanLastError));
		}
		else if(ManagementAction && StatusCode == 401)
		{
			const bool KogClan = str_comp_nocase(m_aClanRequestType, "kog") == 0;
			const SClanState &Clan = KogClan ? m_KogClan : m_GeneralClan;
			if(Clan.m_Valid && Clan.m_aInviteCode[0] != '\0' && CompletedAction != EClanHttpAction::MEMBER_REMOVE)
			{
				RecoveryClan = Clan;
				RecoveryAction = CompletedAction;
				str_copy(m_aClanLastError, "Clan membership expired. Recovering...", sizeof(m_aClanLastError));
			}
			else
			{
				str_copy(m_aClanLastError, "Clan membership expired. Rejoin with invite code to recover.", sizeof(m_aClanLastError));
				if(Clan.m_Valid)
				{
					SClanState &MutableClan = KogClan ? m_KogClan : m_GeneralClan;
					MutableClan.m_MembersLoaded = true;
					MutableClan.m_MemberListCount = 0;
				}
			}
		}
		else if(CompletedAction == EClanHttpAction::RECOVER_MEMBERSHIP)
		{
			m_ClanRecoveryAction = EClanHttpAction::NONE;
			m_aClanRecoveryType[0] = '\0';
			str_format(m_aClanLastError, sizeof(m_aClanLastError), "Clan membership recovery failed: %s", pError);
		}
		else if(StatusCode > 0)
			str_format(m_aClanLastError, sizeof(m_aClanLastError), "Clan HTTP %d: %s", StatusCode, pError);
		else if(HttpState == EHttpState::ERROR)
			str_copy(m_aClanLastError, "Clan request failed. Check API URL/deploy and try refresh.", sizeof(m_aClanLastError));
		else
			str_copy(m_aClanLastError, "Clan request aborted.", sizeof(m_aClanLastError));
		str_copy(m_aClanStatus, m_aClanLastError, sizeof(m_aClanStatus));
		if(pJson)
			json_value_free(pJson);
	}
	m_pClanRequest = nullptr;
	m_ClanAction = EClanHttpAction::NONE;
	m_aClanRequestType[0] = '\0';
	m_aClanRequestId[0] = '\0';
	if(RecoveryAction != EClanHttpAction::NONE)
		StartClanRecovery(RecoveryClan, RecoveryAction);
	else if(CompletedAction == EClanHttpAction::RECOVER_MEMBERSHIP && m_ClanRecoveryAction != EClanHttpAction::NONE)
		RetryClanRecoveryAction();
}

void CAetherBadges::RequestClanMine(bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pClanRequest)
		return;
	if(Force)
		m_ClanManagementAvailable = true;
	const int64_t Now = time_get();
	if(!Force && m_LastClanMineTime != 0 && Now - m_LastClanMineTime < 60 * time_freq())
		return;
	char aName[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aName, sizeof(aName)))
		return;
	std::string Payload = std::string("{\"player_name\":") + JsonStringLiteral(aName) + "}";
	if(StartClanHttpPost("/v1/clans/mine", Payload, EClanHttpAction::MINE, Force ? "Refreshing clan..." : ""))
		m_LastClanMineTime = Now;
}

void CAetherBadges::RequestClanDirectory(bool Force)
{
	if(g_Config.m_AeBadgesApiUrl[0] == '\0' || m_pClanRequest)
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastClanDirectoryTime != 0 && Now - m_LastClanDirectoryTime < 90 * time_freq())
		return;
	if(StartClanHttpGet("/v1/clans/public", EClanHttpAction::PUBLIC_DIRECTORY, Force ? "Refreshing cloud clan directory..." : "", !Force))
		m_LastClanDirectoryTime = Now;
}

void CAetherBadges::RefreshClan()
{
	m_LastClanMineTime = 0;
	m_LastClanDirectoryTime = 0;
	RequestClanMine(true);
}

bool CAetherBadges::BuildClanAuthPayload(const SClanState &Clan, std::string &Payload)
{
	if(!Clan.m_Valid || Clan.m_aId[0] == '\0')
	{
		str_copy(m_aClanStatus, "Select a joined clan first.", sizeof(m_aClanStatus));
		return false;
	}
	const bool KogClan = str_comp_nocase(Clan.m_aType, "kog") == 0;
	const char *pSecret = KogClan ? g_Config.m_AeClanKogSecret : g_Config.m_AeClanGeneralSecret;
	if((!pSecret || pSecret[0] == '\0') && g_Config.m_AeClanId[0] != '\0' && str_comp(g_Config.m_AeClanId, Clan.m_aId) == 0)
	{
		if(g_Config.m_AeClanSecret[0] != '\0')
		{
			str_copy(KogClan ? g_Config.m_AeClanKogSecret : g_Config.m_AeClanGeneralSecret, g_Config.m_AeClanSecret, KogClan ? sizeof(g_Config.m_AeClanKogSecret) : sizeof(g_Config.m_AeClanGeneralSecret));
			pSecret = KogClan ? g_Config.m_AeClanKogSecret : g_Config.m_AeClanGeneralSecret;
		}
	}
	if((!pSecret || pSecret[0] == '\0') && g_Config.m_AeClanSecret[0] != '\0' && g_Config.m_AeClanId[0] != '\0' && str_comp(g_Config.m_AeClanId, Clan.m_aId) == 0)
		pSecret = g_Config.m_AeClanSecret;
	if(!pSecret || pSecret[0] == '\0')
	{
		str_copy(m_aClanStatus, "Clan membership secret is missing. Rejoin or refresh clan.", sizeof(m_aClanStatus));
		return false;
	}
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return false;
	Payload = std::string("{\"player_name\":") + JsonStringLiteral(aPlayer) +
		  ",\"clan_id\":" + JsonStringLiteral(Clan.m_aId) +
		  ",\"membership_secret\":" + JsonStringLiteral(pSecret) + "}";
	str_copy(m_aClanRequestType, KogClan ? "kog" : "general", sizeof(m_aClanRequestType));
	str_copy(m_aClanRequestId, Clan.m_aId, sizeof(m_aClanRequestId));
	return true;
}

bool CAetherBadges::AddOrMergePing(const SPingEvent &Event)
{
	const int RoundedX = round_to_int(Event.m_Pos.x / 8.0f);
	const int RoundedY = round_to_int(Event.m_Pos.y / 8.0f);
	for(SPingEvent &Existing : m_vPings)
	{
		const bool SameSource = str_comp_nocase(Existing.m_aPlayer, Event.m_aPlayer) == 0 &&
					Existing.m_Type == Event.m_Type &&
					Existing.m_Auto == Event.m_Auto &&
					round_to_int(Existing.m_Pos.x / 8.0f) == RoundedX &&
					round_to_int(Existing.m_Pos.y / 8.0f) == RoundedY;
		if(!SameSource)
			continue;
		if(Event.m_Seq > Existing.m_Seq)
			Existing.m_Seq = Event.m_Seq;
		Existing.m_Pos = Event.m_Pos;
		Existing.m_ExpireTime = std::max(Existing.m_ExpireTime, Event.m_ExpireTime);
		return false;
	}
	m_vPings.push_back(Event);
	return true;
}

bool CAetherBadges::LocalCharacterGrounded() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return false;
	if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalId))
		return pChar->IsGrounded();
	const auto &Char = GameClient()->m_Snap.m_aCharacters[LocalId];
	if(!Char.m_Active)
		return false;
	const vec2 Pos(Char.m_Cur.m_X, Char.m_Cur.m_Y);
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	return Collision()->CheckPoint(Pos.x + HalfSize, Pos.y + HalfSize + 5.0f) ||
	       Collision()->CheckPoint(Pos.x - HalfSize, Pos.y + HalfSize + 5.0f) ||
	       (Collision()->GetMoveRestrictions(Pos + vec2(0.0f, HalfSize + 4.0f), 0.0f) & CANTMOVE_DOWN) != 0;
}

void CAetherBadges::ScanAutoHelpPings()
{
	if(!g_Config.m_AePings || !g_Config.m_AePingAutoHelp || Client()->State() != IClient::STATE_ONLINE)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;
	const int LocalTeam = GameClient()->m_Teams.Team(LocalId);
	const bool LocalGrounded = LocalCharacterGrounded();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(ClientId == LocalId || ClientId == GameClient()->m_aLocalIds[!g_Config.m_ClDummy])
			continue;
		if(!GameClient()->m_aClients[ClientId].m_Active)
		{
			m_aHelpPingStates[ClientId] = EAutoHelpPingState::IDLE;
			continue;
		}
		const bool Frozen = GameClient()->m_AetherBlockAwareness.IsFrozen(ClientId);
		if(!Frozen)
		{
			m_aHelpPingStates[ClientId] = EAutoHelpPingState::IDLE;
			continue;
		}
		if(m_aHelpPingStates[ClientId] == EAutoHelpPingState::SENT)
			continue;

		bool Allowed = g_Config.m_AePingHelpVisibility >= 2;
		const int OtherTeam = GameClient()->m_Teams.Team(ClientId);
		if(LocalTeam >= 0 && OtherTeam == LocalTeam)
			Allowed = true;
		if(g_Config.m_AePingHelpVisibility >= 1)
		{
			const auto Group = GameClient()->m_AetherBlockAwareness.GroupForClient(ClientId);
			if(Group == CAetherBlockAwareness::EGroup::ALLY || Group == CAetherBlockAwareness::EGroup::HELPER)
				Allowed = true;
		}
		if(!Allowed)
			continue;
		if(!LocalGrounded)
		{
			m_aHelpPingStates[ClientId] = EAutoHelpPingState::PENDING_GROUND;
			continue;
		}
		if(m_pPingSendRequest)
		{
			m_aHelpPingStates[ClientId] = EAutoHelpPingState::PENDING_GROUND;
			continue;
		}
		SendPing(EPingType::HELP, GameClient()->m_AetherBlockAwareness.CharacterPos(ClientId), true);
		m_aHelpPingStates[ClientId] = EAutoHelpPingState::SENT;
	}
}

vec2 CAetherBadges::ManualPingPosition() const
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return GameClient()->m_Camera.m_Center;
	const int Dummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = GameClient()->m_aLocalIds[Dummy];
	if(LocalId >= 0 && LocalId < MAX_CLIENTS)
		return GameClient()->m_LocalCharacterPos + GameClient()->m_Controls.m_aMousePos[Dummy];
	return GameClient()->m_Camera.m_Center;
}

void CAetherBadges::RenderPings()
{
	if(!g_Config.m_AePings || m_vPings.empty())
		return;
	const int64_t Now = time_get();
	m_vPings.erase(std::remove_if(m_vPings.begin(), m_vPings.end(), [&](const SPingEvent &Ping) {
		return Ping.m_ExpireTime <= Now;
	}), m_vPings.end());
	if(m_vPings.empty())
		return;

	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	float aPoints[4];
	const vec2 Center = GameClient()->m_Camera.m_Center;
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	const float WorldX0 = aPoints[0];
	const float WorldY0 = aPoints[1];
	const float WorldX1 = aPoints[2];
	const float WorldY1 = aPoints[3];
	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();

	for(const SPingEvent &Ping : m_vPings)
	{
		const float Life = std::clamp((float)(Ping.m_ExpireTime - Now) / ((float)PING_LOCAL_TTL_SECONDS * time_freq()), 0.0f, 1.0f);
		const ColorRGBA Color = PingTypeColor(Ping.m_Type, 0.35f + 0.55f * Life);
		const bool OnScreen = Ping.m_Pos.x >= WorldX0 && Ping.m_Pos.x <= WorldX1 && Ping.m_Pos.y >= WorldY0 && Ping.m_Pos.y <= WorldY1;
		if(OnScreen)
		{
			Graphics()->MapScreen(WorldX0, WorldY0, WorldX1, WorldY1);
			const float Size = 32.0f + (1.0f - Life) * 16.0f;
			CUIRect Marker(Ping.m_Pos.x - Size * 0.5f, Ping.m_Pos.y - Size * 0.5f, Size, Size);
			Marker.Draw(Color, IGraphics::CORNER_ALL, Size * 0.5f);
			CUIRect Label(Ping.m_Pos.x - 70.0f, Ping.m_Pos.y - Size * 0.5f - 18.0f, 140.0f, 18.0f);
			if(!Ping.m_Auto)
			{
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, Color.a);
				Ui()->DoLabel(&Label, PingTypeDisplayName(Ping.m_Type), 11.0f, TEXTALIGN_MC);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		else
		{
			Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);
			const float X = (Ping.m_Pos.x - WorldX0) / maximum(1.0f, WorldX1 - WorldX0) * ScreenW;
			const float Y = (Ping.m_Pos.y - WorldY0) / maximum(1.0f, WorldY1 - WorldY0) * ScreenH;
			const float Margin = 22.0f;
			const float ClampedX = std::clamp(X, Margin, ScreenW - Margin);
			const float ClampedY = std::clamp(Y, Margin, ScreenH - Margin);
			CUIRect Edge(ClampedX - 14.0f, ClampedY - 14.0f, 28.0f, 28.0f);
			Edge.Draw(Color, IGraphics::CORNER_ALL, 14.0f);
			Ui()->DoLabel(&Edge, PingTypeGlyph(Ping.m_Type), 13.0f, TEXTALIGN_MC);
		}
	}
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBadges::RenderPingWheel()
{
	if(!m_PingWheelActive)
		return;
	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);
	const vec2 Center(ScreenW * 0.5f, ScreenH * 0.5f);
	const std::array<EPingType, 5> aTypes = {EPingType::COME, EPingType::HELP, EPingType::PLACE, EPingType::WAIT, EPingType::DANGER};
	const float OuterRadius = 150.0f;
	const float InnerRadius = 40.0f;
	const EPingType Selected = m_PingWheelSelected;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.34f);
	Graphics()->DrawCircle(Center.x, Center.y, 190.0f, 64);
	Graphics()->SetColor(0.08f, 0.10f, 0.14f, 0.84f);
	Graphics()->DrawCircle(Center.x, Center.y, InnerRadius, 32);
	Graphics()->QuadsEnd();

	for(size_t i = 0; i < aTypes.size(); ++i)
	{
		const EPingType Type = aTypes[i];
		const float Angle = 2.0f * pi * (float)i / (float)aTypes.size();
		const vec2 Pos = Center + direction(Angle) * OuterRadius;
		const bool Active = m_PingWheelHasSelection && Type == Selected;
		const float Size = Active ? 74.0f : 58.0f;
		CUIRect Item(Pos.x - Size * 0.5f, Pos.y - Size * 0.5f, Size, Size);
		Item.Draw(ColorRGBA(0.04f, 0.055f, 0.085f, Active ? 0.96f : 0.78f), IGraphics::CORNER_ALL, Size * 0.5f);
		CUIRect ColorDot(Pos.x - Size * 0.22f, Pos.y - Size * 0.34f, Size * 0.44f, Size * 0.44f);
		ColorDot.Draw(PingTypeColor(Type, Active ? 0.95f : 0.65f), IGraphics::CORNER_ALL, Size * 0.22f);
		Ui()->DoLabel(&ColorDot, PingTypeGlyph(Type), Active ? 14.0f : 12.0f, TEXTALIGN_MC);
		CUIRect Text(Pos.x - 46.0f, Pos.y + Size * 0.12f, 92.0f, 18.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Active ? 1.0f : 0.72f);
		Ui()->DoLabel(&Text, PingTypeDisplayName(Type), Active ? 12.0f : 10.5f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(m_PingWheelHasSelection)
	{
		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		IGraphics::CLineItem Line(Center.x, Center.y, Center.x + m_PingWheelMouse.x, Center.y + m_PingWheelMouse.y);
		Graphics()->SetColor(PingTypeColor(Selected, 0.55f));
		Graphics()->LinesDraw(&Line, 1);
		Graphics()->LinesEnd();
	}

	CUIRect CoreLabel(Center.x - 42.0f, Center.y - 11.0f, 84.0f, 22.0f);
	TextRender()->TextColor(0.78f, 0.84f, 0.92f, 1.0f);
	Ui()->DoLabel(&CoreLabel, m_PingWheelHasSelection ? PingTypeDisplayName(Selected) : "Cancel", 11.0f, TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	RenderTools()->RenderCursor(Center + m_PingWheelMouse, 24.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBadges::ConPingWheel(IConsole::IResult *pResult, void *pUserData)
{
	CAetherBadges *pSelf = static_cast<CAetherBadges *>(pUserData);
	const bool Active = pResult->NumArguments() == 0 || pResult->GetInteger(0) != 0;
	if(Active)
	{
		pSelf->m_PingWheelActive = true;
		pSelf->m_PingWheelWasActive = true;
		pSelf->m_PingWheelHasSelection = false;
		pSelf->m_PingWheelMouse = vec2(0.0f, 0.0f);
		pSelf->m_PingWheelSelected = EPingType::PLACE;
	}
	else
	{
		pSelf->m_PingWheelActive = false;
		pSelf->ExecutePingWheelSelection();
	}
}

void CAetherBadges::ConPing(IConsole::IResult *pResult, void *pUserData)
{
	CAetherBadges *pSelf = static_cast<CAetherBadges *>(pUserData);
	const EPingType Type = PingTypeFromName(pResult->NumArguments() > 0 ? pResult->GetString(0) : "help");
	pSelf->SendPing(Type, pSelf->ManualPingPosition(), false);
}

void CAetherBadges::ConDdraceConfig(IConsole::IResult *pResult, void *pUserData)
{
	CAetherBadges *pSelf = static_cast<CAetherBadges *>(pUserData);
	pSelf->ExecuteDdraceConfig(pResult->GetString(0), pResult->NumArguments() > 1 ? pResult->GetString(1) : nullptr);
}

void CAetherBadges::ExecuteDdraceConfig(const char *pName, const char *pMode)
{
	bool *pState = nullptr;
	const char *pOnFile = nullptr;
	const char *pOffFile = nullptr;
	if(str_comp_nocase(pName, "show_others") == 0 || str_comp_nocase(pName, "showothers") == 0)
	{
		pState = &m_DdraceShowOthersOn;
		pOnFile = "core/ddrace_configs/show_others_on.cfg";
		pOffFile = "core/ddrace_configs/show_others_off.cfg";
	}
	else if(str_comp_nocase(pName, "super") == 0)
	{
		pState = &m_DdraceSuperOn;
		pOnFile = "core/ddrace_configs/super_on.cfg";
		pOffFile = "core/ddrace_configs/super_off.cfg";
	}
	else if(str_comp_nocase(pName, "deep_fly") == 0 || str_comp_nocase(pName, "deepfly") == 0)
	{
		pState = &m_DdraceDeepFlyOn;
		pOnFile = "core/ddrace_configs/deep_fly_on.cfg";
		pOffFile = "core/ddrace_configs/deep_fly_off.cfg";
	}
	else if(str_comp_nocase(pName, "edge2edge") == 0 || str_comp_nocase(pName, "edge") == 0)
	{
		pState = &m_DdraceEdge2EdgeOn;
		pOnFile = "core/ddrace_configs/edge2edge_on.cfg";
		pOffFile = "core/ddrace_configs/edge2edge_off.cfg";
	}

	if(!pState || !pOnFile || !pOffFile)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aether/ddrace", "Unknown DDRace config. Use show_others, super, deep_fly or edge2edge.");
		return;
	}

	bool Enable = !*pState;
	if(pMode && pMode[0])
	{
		if(str_comp_nocase(pMode, "on") == 0 || str_comp_nocase(pMode, "apply") == 0 || str_comp(pMode, "1") == 0)
			Enable = true;
		else if(str_comp_nocase(pMode, "off") == 0 || str_comp_nocase(pMode, "reset") == 0 || str_comp(pMode, "0") == 0)
			Enable = false;
	}

	char aExec[160];
	str_format(aExec, sizeof(aExec), "exec \"%s\"", Enable ? pOnFile : pOffFile);
	Console()->ExecuteLine(aExec, IConsole::CLIENT_ID_UNSPECIFIED);
	*pState = Enable;
}

void CAetherBadges::ExecutePingWheelSelection()
{
	if(!m_PingWheelWasActive)
		return;
	m_PingWheelWasActive = false;
	if(!m_PingWheelHasSelection)
		return;
	SendPing(m_PingWheelSelected, ManualPingPosition(), false);
}

void CAetherBadges::SendChessInvite(const char *pTarget)
{
	if(!pTarget || !pTarget[0])
		return;
	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("type");
	Json.WriteStrValue("chess:invite");
	Json.WriteAttribute("to_player");
	Json.WriteStrValue(pTarget);
	Json.WriteAttribute("rated");
	Json.WriteBoolValue(true);
	Json.EndObject();
	SendRealtimePayload(Json.GetOutputString());
	str_format(m_aChessStatus, sizeof(m_aChessStatus), "Challenge sent to %s.", pTarget);
}

void CAetherBadges::SendChessInviteReplyPayload(const char *pType)
{
	if(!m_ChessInviteActive || !pType || !pType[0] || m_aChessInviteId[0] == '\0')
		return;
	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("type");
	Json.WriteStrValue(pType);
	Json.WriteAttribute("invite_id");
	Json.WriteStrValue(m_aChessInviteId);
	Json.EndObject();
	SendRealtimePayload(Json.GetOutputString());
}

void CAetherBadges::SendChessInviteReply(const char *pType)
{
	SendChessInviteReplyPayload(pType);
	if(pType && str_comp(pType, "chess:decline") == 0)
	{
		m_ChessInviteActive = false;
		m_aChessInviteId[0] = '\0';
		m_aChessInviteFrom[0] = '\0';
		str_copy(m_aChessStatus, "Chess invite refused.", sizeof(m_aChessStatus));
	}
	else if(pType && str_comp(pType, "chess:accept") == 0)
	{
		m_ChessInviteActive = false;
		m_aChessInviteId[0] = '\0';
		m_aChessInviteFrom[0] = '\0';
		str_copy(m_aChessStatus, "Accepting chess invite...", sizeof(m_aChessStatus));
		OpenChessOnlineMenu();
	}
}

void CAetherBadges::SendChessRoomCreate(bool Rated)
{
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.WriteAttribute("rated");
	Json.WriteBoolValue(Rated);
	Json.EndObject();
	StartChessHttpPost("/v1/chess/rooms/create", Json.GetOutputString(), EChessHttpAction::ROOM_CREATE, "Creating chess room...");
}

void CAetherBadges::SendChessRoomJoin(const char *pCode)
{
	if(!pCode || pCode[0] == '\0')
		return;
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.WriteAttribute("code");
	Json.WriteStrValue(pCode);
	Json.EndObject();
	StartChessHttpPost("/v1/chess/rooms/join", Json.GetOutputString(), EChessHttpAction::ROOM_JOIN, "Joining chess room...");
}

void CAetherBadges::SendChessRoomReady(bool Ready)
{
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.WriteAttribute("ready");
	Json.WriteBoolValue(Ready);
	Json.EndObject();
	StartChessHttpPost("/v1/chess/rooms/ready", Json.GetOutputString(), EChessHttpAction::ROOM_READY, Ready ? "Ready in chess room..." : "Leaving ready state...");
}

void CAetherBadges::SendChessRoomLeave()
{
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.EndObject();
	StartChessHttpPost("/v1/chess/rooms/leave", Json.GetOutputString(), EChessHttpAction::ROOM_LEAVE, "Leaving chess room...");
	m_ChessRoom = SChessRoomState();
}

void CAetherBadges::SendChessMove(const char *pMatchId, const char *pUci)
{
	if(!pMatchId || !pMatchId[0] || !pUci || !pUci[0])
		return;
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.WriteAttribute("match_id");
	Json.WriteStrValue(pMatchId);
	Json.WriteAttribute("uci");
	Json.WriteStrValue(pUci);
	Json.EndObject();
	StartChessHttpPost("/v1/chess/matches/move", Json.GetOutputString(), EChessHttpAction::MATCH_MOVE, "Sending chess move...");
}

void CAetherBadges::SendChessResign(const char *pMatchId)
{
	if(!pMatchId || !pMatchId[0])
		return;
	CJsonStringWriter Json;
	Json.BeginObject();
	if(!WriteChessPlayerName(Json))
		return;
	Json.WriteAttribute("match_id");
	Json.WriteStrValue(pMatchId);
	Json.EndObject();
	StartChessHttpPost("/v1/chess/matches/resign", Json.GetOutputString(), EChessHttpAction::MATCH_RESIGN, "Resigning chess match...");
}

void CAetherBadges::SendClanCreate(const char *pName, const char *pType)
{
	if(!pName || pName[0] == '\0')
		return;
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return;
	const char *pSafeType = pType && str_comp_nocase(pType, "kog") == 0 ? "kog" : "general";
	std::string Payload = std::string("{\"name\":") + JsonStringLiteral(pName) +
			      ",\"type\":" + JsonStringLiteral(pSafeType) +
			      ",\"player_name\":" + JsonStringLiteral(aPlayer) + "}";
	StartClanHttpPost("/v1/clans/create", Payload, EClanHttpAction::CREATE, "Creating clan...");
}

void CAetherBadges::SendClanJoin(const char *pInviteCode)
{
	if(!pInviteCode || pInviteCode[0] == '\0')
		return;
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return;
	std::string Payload = std::string("{\"invite_code\":") + JsonStringLiteral(pInviteCode) +
			      ",\"player_name\":" + JsonStringLiteral(aPlayer) + "}";
	StartClanHttpPost("/v1/clans/join", Payload, EClanHttpAction::JOIN, "Joining clan...");
}

void CAetherBadges::SendClanMembers(const SClanState &Clan)
{
	std::string Payload;
	if(!BuildClanAuthPayload(Clan, Payload))
	{
		if(Clan.m_Valid && Clan.m_aInviteCode[0] != '\0' && StartClanRecovery(Clan, EClanHttpAction::MEMBERS))
			return;
		return;
	}
	StartClanHttpPost("/v1/clans/members", Payload, EClanHttpAction::MEMBERS, "Loading clan members...");
}

void CAetherBadges::SendClanLeave(const SClanState &Clan)
{
	std::string Payload;
	if(!BuildClanAuthPayload(Clan, Payload))
	{
		if(Clan.m_Valid && Clan.m_aInviteCode[0] != '\0' && StartClanRecovery(Clan, EClanHttpAction::LEAVE))
			return;
		return;
	}
	StartClanHttpPost("/v1/clans/leave", Payload, EClanHttpAction::LEAVE, "Leaving clan...");
}

void CAetherBadges::SendClanDisband(const SClanState &Clan)
{
	std::string Payload;
	if(!BuildClanAuthPayload(Clan, Payload))
	{
		if(Clan.m_Valid && Clan.m_aInviteCode[0] != '\0' && StartClanRecovery(Clan, EClanHttpAction::DISBAND))
			return;
		return;
	}
	StartClanHttpPost("/v1/clans/disband", Payload, EClanHttpAction::DISBAND, "Disbanding clan...");
}

void CAetherBadges::SendClanRotateInvite(const SClanState &Clan)
{
	std::string Payload;
	if(!BuildClanAuthPayload(Clan, Payload))
	{
		if(Clan.m_Valid && Clan.m_aInviteCode[0] != '\0' && StartClanRecovery(Clan, EClanHttpAction::ROTATE_INVITE))
			return;
		return;
	}
	StartClanHttpPost("/v1/clans/invite/rotate", Payload, EClanHttpAction::ROTATE_INVITE, "Rotating invite code...");
}

void CAetherBadges::SendClanMemberRemove(const SClanState &Clan, const char *pTargetPlayerName)
{
	if(!pTargetPlayerName || !pTargetPlayerName[0])
		return;
	std::string Payload;
	if(!BuildClanAuthPayload(Clan, Payload))
		return;
	if(!Payload.empty() && Payload.back() == '}')
		Payload.pop_back();
	Payload += ",\"target_player_name\":";
	Payload += JsonStringLiteral(pTargetPlayerName);
	Payload += "}";
	StartClanHttpPost("/v1/clans/members/remove", Payload, EClanHttpAction::MEMBER_REMOVE, "Removing clan member...");
}

void CAetherBadges::SendClanWarlistPush(const std::string &EntriesJson)
{
	const char *pClanId = g_Config.m_AeClanGeneralId[0] ? g_Config.m_AeClanGeneralId : (m_GeneralClan.m_Valid ? m_GeneralClan.m_aId : "");
	const char *pSecret = g_Config.m_AeClanGeneralSecret;
	if(pClanId[0] == '\0' || pSecret[0] == '\0')
	{
		str_copy(m_aClanStatus, "Join a general clan before pushing warlist.", sizeof(m_aClanStatus));
		return;
	}
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return;
	std::string Payload = std::string("{\"player_name\":") + JsonStringLiteral(aPlayer) +
			      ",\"clan_id\":" + JsonStringLiteral(pClanId) +
			      ",\"membership_secret\":" + JsonStringLiteral(pSecret) +
			      ",\"entries\":" + EntriesJson + "}";
	StartClanHttpPost("/v1/clans/warlist/push", Payload, EClanHttpAction::WARLIST_PUSH, "Pushing clan warlist...");
}

void CAetherBadges::SendClanWarlistPull()
{
	const char *pClanId = g_Config.m_AeClanGeneralId[0] ? g_Config.m_AeClanGeneralId : (m_GeneralClan.m_Valid ? m_GeneralClan.m_aId : "");
	const char *pSecret = g_Config.m_AeClanGeneralSecret;
	if(pClanId[0] == '\0' || pSecret[0] == '\0')
	{
		str_copy(m_aClanStatus, "Join a general clan before pulling warlist.", sizeof(m_aClanStatus));
		return;
	}
	char aPlayer[MAX_NAME_LENGTH];
	if(!LocalPlayerName(aPlayer, sizeof(aPlayer)))
		return;
	std::string Payload = std::string("{\"player_name\":") + JsonStringLiteral(aPlayer) +
			      ",\"clan_id\":" + JsonStringLiteral(pClanId) +
			      ",\"membership_secret\":" + JsonStringLiteral(pSecret) + "}";
	StartClanHttpPost("/v1/clans/warlist/pull", Payload, EClanHttpAction::WARLIST_PULL, "Pulling clan warlist...");
}

bool CAetherBadges::ScoreboardClanForClient(int ClientId, bool KogServer, char *pOut, int OutSize) const
{
	if(!pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(pName[0] == '\0')
		return false;

	const char *pPreferredType = KogServer ? "kog" : "general";
	const SPublicClanMembership *pFallback = nullptr;
	for(const SPublicClanMembership &Entry : m_vPublicClanMemberships)
	{
		if(str_comp_nocase(Entry.m_aPlayer, pName) != 0 || Entry.m_aClan[0] == '\0')
			continue;
		if(str_comp_nocase(Entry.m_aType, pPreferredType) == 0)
		{
			str_copy(pOut, Entry.m_aClan, OutSize);
			return true;
		}
		if(!pFallback)
			pFallback = &Entry;
	}
	if(pFallback)
	{
		str_copy(pOut, pFallback->m_aClan, OutSize);
		return true;
	}

	const bool LocalOrDummy = ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
	if(LocalOrDummy)
	{
		const SClanState &Clan = KogServer ? m_KogClan : m_GeneralClan;
		if(Clan.m_Valid && Clan.m_aName[0] != '\0')
		{
			str_copy(pOut, Clan.m_aName, OutSize);
			return true;
		}
	}
	return false;
}

void CAetherBadges::ApplyBadgeArrayForClient(int ClientId, const json_value *pBadgeArray)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(pName[0] == '\0')
		return;

	SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	str_copy(ClientBadges.m_aName, pName, sizeof(ClientBadges.m_aName));
	ClientBadges.m_Valid = true;
	ClientBadges.m_vBadges.clear();

	if(!pBadgeArray || pBadgeArray->type != json_array)
		return;

	for(int i = 0; i < json_array_length(pBadgeArray); ++i)
	{
		const json_value *pBadge = json_array_get(pBadgeArray, i);
		if(!pBadge || pBadge->type != json_object)
			continue;
		const char *pKey = json_string_get(json_object_get(pBadge, "key"));
		const char *pBadgeName = json_string_get(json_object_get(pBadge, "name"));
		if(!pKey || pKey[0] == '\0')
			continue;
		SBadge Entry;
		str_copy(Entry.m_aKey, pKey, sizeof(Entry.m_aKey));
		str_copy(Entry.m_aName, pBadgeName ? pBadgeName : pKey, sizeof(Entry.m_aName));
		Entry.m_Priority = json_int_get(json_object_get(pBadge, "priority"));
		ClientBadges.m_vBadges.push_back(Entry);
	}

	std::sort(ClientBadges.m_vBadges.begin(), ClientBadges.m_vBadges.end(), [](const SBadge &A, const SBadge &B) {
		const int RankA = BadgeRenderRank(A);
		const int RankB = BadgeRenderRank(B);
		if(RankA != RankB)
			return RankA < RankB;
		if(A.m_Priority != B.m_Priority)
			return A.m_Priority > B.m_Priority;
		return str_comp(A.m_aName, B.m_aName) < 0;
	});
}

void CAetherBadges::ApplyBadgeArrayForName(const char *pName, const json_value *pBadgeArray)
{
	if(!pName || pName[0] == '\0')
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(str_comp(GameClient()->m_aClients[ClientId].m_aName, pName) == 0)
			ApplyBadgeArrayForClient(ClientId, pBadgeArray);
	}
}

void CAetherBadges::ApplyPlayersBadgeObject(const json_value *pPlayers)
{
	if(!pPlayers || pPlayers->type != json_object)
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(!pInfo || pInfo->m_ClientId < 0)
			continue;
		const char *pName = GameClient()->m_aClients[pInfo->m_ClientId].m_aName;
		if(pName[0] == '\0')
			continue;
		ApplyBadgeArrayForClient(pInfo->m_ClientId, json_object_get(pPlayers, pName));
	}
}

void CAetherBadges::ApplyPresenceBadgeObject(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const char *pType = json_string_get(json_object_get(pRoot, "type"));
	if(!pType)
		return;

	if(str_comp(pType, "badge:update") == 0)
	{
		ApplyPlayersBadgeObject(json_object_get(pRoot, "players"));
		return;
	}
	if(str_comp(pType, "presence:snapshot") == 0)
	{
		const json_value *pPlayers = json_object_get(pRoot, "players");
		if(!pPlayers || pPlayers->type != json_array)
			return;
		ApplyAetherOnlinePlayers(pPlayers);
		m_LastAetherOnlineResponseTime = time_get();
		for(int i = 0; i < json_array_length(pPlayers); ++i)
		{
			const json_value *pPresence = json_array_get(pPlayers, i);
			if(!pPresence || pPresence->type != json_object)
				continue;
			ApplyBadgeArrayForName(json_string_get(json_object_get(pPresence, "player_name")), json_object_get(pPresence, "badges"));
		}
		str_copy(m_aStatus, "Realtime snapshot loaded.", sizeof(m_aStatus));
		return;
	}
	if(str_comp(pType, "presence:update") == 0)
	{
		const json_value *pPresence = json_object_get(pRoot, "presence");
		if(!pPresence || pPresence->type != json_object)
			return;
		ApplyAetherOnlineUpdate(pPresence);
		m_LastAetherOnlineResponseTime = time_get();
		ApplyBadgeArrayForName(json_string_get(json_object_get(pPresence, "player_name")), json_object_get(pRoot, "badges"));
		str_copy(m_aStatus, "Realtime badge update.", sizeof(m_aStatus));
		return;
	}
	if(str_comp(pType, "presence:left") == 0)
	{
		const char *pName = json_string_get(json_object_get(pRoot, "player_name"));
		if(!pName)
			return;
		ApplyAetherOnlineLeft(pName);
		for(SClientBadges &ClientBadges : m_aClientBadges)
		{
			if(ClientBadges.m_Valid && str_comp(ClientBadges.m_aName, pName) == 0)
				ClientBadges.m_Valid = false;
		}
	}
}

void CAetherBadges::ApplyChessOnlinePlayers(const json_value *pPlayers)
{
	m_ChessOnlineCount = 0;
	if(!pPlayers || pPlayers->type != json_array)
		return;
	for(int i = 0; i < json_array_length(pPlayers) && m_ChessOnlineCount < (int)m_aChessOnline.size(); ++i)
	{
		ApplyChessOnlineUpdate(json_array_get(pPlayers, i));
	}
}

void CAetherBadges::ApplyChessOnlineUpdate(const json_value *pPlayer)
{
	if(!pPlayer || pPlayer->type != json_object)
		return;
	const char *pName = JsonStringValue(json_object_get(pPlayer, "player_name"));
	if(!pName[0])
		return;
	int Target = -1;
	for(int i = 0; i < m_ChessOnlineCount; ++i)
	{
		if(str_comp_nocase(m_aChessOnline[i].m_aName, pName) == 0)
		{
			Target = i;
			break;
		}
	}
	if(Target < 0 && m_ChessOnlineCount < (int)m_aChessOnline.size())
		Target = m_ChessOnlineCount++;
	if(Target < 0)
		return;
	SChessOnlinePlayer &Entry = m_aChessOnline[Target];
	str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));
	str_copy(Entry.m_aClient, JsonStringValue(json_object_get(pPlayer, "client"), "-"), sizeof(Entry.m_aClient));
	str_copy(Entry.m_aServer, JsonStringValue(json_object_get(pPlayer, "server_name")), sizeof(Entry.m_aServer));
	str_copy(Entry.m_aServerAddress, JsonStringValue(json_object_get(pPlayer, "server_address"), JsonStringValue(json_object_get(pPlayer, "server_key"))), sizeof(Entry.m_aServerAddress));
	str_copy(Entry.m_aMap, JsonStringValue(json_object_get(pPlayer, "map")), sizeof(Entry.m_aMap));
	Entry.m_Rating = json_int_get(json_object_get(pPlayer, "rating"));
	if(Entry.m_Rating <= 0)
		Entry.m_Rating = 1200;
	Entry.m_Spectator = JsonBoolValue(json_object_get(pPlayer, "spectator"));
	Entry.m_InServer = JsonStringValue(json_object_get(pPlayer, "server_key"))[0] != '\0' ||
			   JsonStringValue(json_object_get(pPlayer, "server_address"))[0] != '\0';
}

void CAetherBadges::ApplyChessOnlineLeft(const char *pName)
{
	if(!pName || !pName[0])
		return;
	for(int i = 0; i < m_ChessOnlineCount; ++i)
	{
		if(str_comp_nocase(m_aChessOnline[i].m_aName, pName) != 0)
			continue;
		for(int j = i + 1; j < m_ChessOnlineCount; ++j)
			m_aChessOnline[j - 1] = m_aChessOnline[j];
		--m_ChessOnlineCount;
		return;
	}
}

void CAetherBadges::ApplyAetherOnlinePlayers(const json_value *pPlayers)
{
	m_AetherOnlineCount = 0;
	if(!pPlayers || pPlayers->type != json_array)
		return;
	for(int i = 0; i < json_array_length(pPlayers) && m_AetherOnlineCount < (int)m_aAetherOnline.size(); ++i)
	{
		ApplyAetherOnlineUpdate(json_array_get(pPlayers, i));
	}
}

void CAetherBadges::ApplyAetherOnlineUpdate(const json_value *pPlayer)
{
	if(!pPlayer || pPlayer->type != json_object)
		return;
	const char *pName = JsonStringValue(json_object_get(pPlayer, "player_name"));
	if(!pName[0])
		return;
	int Target = -1;
	for(int i = 0; i < m_AetherOnlineCount; ++i)
	{
		if(str_comp_nocase(m_aAetherOnline[i].m_aName, pName) == 0)
		{
			Target = i;
			break;
		}
	}
	if(Target < 0 && m_AetherOnlineCount < (int)m_aAetherOnline.size())
		Target = m_AetherOnlineCount++;
	if(Target < 0)
		return;

	SChessOnlinePlayer &Entry = m_aAetherOnline[Target];
	str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));
	str_copy(Entry.m_aClient, JsonStringValue(json_object_get(pPlayer, "client"), "-"), sizeof(Entry.m_aClient));
	str_copy(Entry.m_aServer, JsonStringValue(json_object_get(pPlayer, "server_name")), sizeof(Entry.m_aServer));
	str_copy(Entry.m_aServerAddress, JsonStringValue(json_object_get(pPlayer, "server_address"), JsonStringValue(json_object_get(pPlayer, "server_key"))), sizeof(Entry.m_aServerAddress));
	str_copy(Entry.m_aMap, JsonStringValue(json_object_get(pPlayer, "map")), sizeof(Entry.m_aMap));
	Entry.m_Rating = json_int_get(json_object_get(pPlayer, "rating"));
	if(Entry.m_Rating <= 0)
		Entry.m_Rating = 1200;
	Entry.m_Spectator = JsonBoolValue(json_object_get(pPlayer, "spectator"));
	Entry.m_InServer = JsonStringValue(json_object_get(pPlayer, "server_key"))[0] != '\0' ||
			   JsonStringValue(json_object_get(pPlayer, "server_address"))[0] != '\0';
}

void CAetherBadges::ApplyAetherOnlineLeft(const char *pName)
{
	if(!pName || !pName[0])
		return;
	for(int i = 0; i < m_AetherOnlineCount; ++i)
	{
		if(str_comp_nocase(m_aAetherOnline[i].m_aName, pName) != 0)
			continue;
		for(int j = i + 1; j < m_AetherOnlineCount; ++j)
			m_aAetherOnline[j - 1] = m_aAetherOnline[j];
		--m_AetherOnlineCount;
		break;
	}
}

void CAetherBadges::ApplyChessRoomSnapshot(const json_value *pRoom)
{
	m_ChessRoom = SChessRoomState();
	if(!pRoom || pRoom->type != json_object)
		return;
	str_copy(m_ChessRoom.m_aCode, JsonStringValue(json_object_get(pRoom, "code")), sizeof(m_ChessRoom.m_aCode));
	str_copy(m_ChessRoom.m_aStatus, JsonStringValue(json_object_get(pRoom, "status"), "waiting"), sizeof(m_ChessRoom.m_aStatus));
	str_copy(m_ChessRoom.m_aMatchId, JsonStringValue(json_object_get(pRoom, "match_id")), sizeof(m_ChessRoom.m_aMatchId));
	m_ChessRoom.m_Rated = JsonBoolValue(json_object_get(pRoom, "rated"), true);
	const json_value *pPlayers = json_object_get(pRoom, "players");
	if(pPlayers && pPlayers->type == json_array)
	{
		for(int i = 0; i < json_array_length(pPlayers) && m_ChessRoom.m_PlayerCount < (int)m_ChessRoom.m_aPlayers.size(); ++i)
		{
			const json_value *pPlayer = json_array_get(pPlayers, i);
			if(!pPlayer || pPlayer->type != json_object)
				continue;
			SChessRoomPlayer &Entry = m_ChessRoom.m_aPlayers[m_ChessRoom.m_PlayerCount++];
			str_copy(Entry.m_aName, JsonStringValue(json_object_get(pPlayer, "player_name")), sizeof(Entry.m_aName));
			Entry.m_Ready = JsonBoolValue(json_object_get(pPlayer, "ready"), false);
			Entry.m_Owner = JsonBoolValue(json_object_get(pPlayer, "owner"), false);
		}
	}
	if(m_ChessRoom.m_aCode[0])
		str_format(m_aChessStatus, sizeof(m_aChessStatus), "Chess room %s.", m_ChessRoom.m_aCode);
}

void CAetherBadges::ApplyChessMessage(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const char *pType = JsonStringValue(json_object_get(pRoot, "type"));
	if(str_comp(pType, "chess:online_snapshot") == 0)
	{
		ApplyChessOnlinePlayers(json_object_get(pRoot, "players"));
		m_LastChessOnlineResponseTime = time_get();
		if(!m_pChessActionRequest && m_ChessRoom.m_aCode[0] == '\0')
			str_copy(m_aChessStatus, "Global chess lobby refreshed.", sizeof(m_aChessStatus));
	}
	else if(str_comp(pType, "chess:online_update") == 0)
	{
		ApplyChessOnlineUpdate(json_object_get(pRoot, "player"));
		m_LastChessOnlineResponseTime = time_get();
	}
	else if(str_comp(pType, "chess:online_left") == 0)
	{
		ApplyChessOnlineLeft(JsonStringValue(json_object_get(pRoot, "player_name")));
	}
	else if(str_comp(pType, "chess:room_snapshot") == 0)
	{
		ApplyChessRoomSnapshot(json_object_get(pRoot, "room"));
	}
	else if(str_comp(pType, "chess:room_left") == 0)
	{
		m_ChessRoom = SChessRoomState();
		str_copy(m_aChessStatus, "Left chess room.", sizeof(m_aChessStatus));
	}
	else if(str_comp(pType, "chess:room_error") == 0)
	{
		str_format(m_aChessStatus, sizeof(m_aChessStatus), "Chess room: %s", JsonStringValue(json_object_get(pRoot, "error"), "error"));
	}
	else if(str_comp(pType, "chess:invite_received") == 0)
	{
		str_copy(m_aChessInviteId, JsonStringValue(json_object_get(pRoot, "invite_id")), sizeof(m_aChessInviteId));
		str_copy(m_aChessInviteFrom, JsonStringValue(json_object_get(pRoot, "from_player")), sizeof(m_aChessInviteFrom));
		if(m_aChessInviteId[0] && m_aChessInviteFrom[0])
		{
			m_ChessInviteActive = true;
			m_ChessInviteExpireTime = time_get() + 60 * time_freq();
			str_format(m_aChessStatus, sizeof(m_aChessStatus), "%s challenged you.", m_aChessInviteFrom);
		}
	}
	else if(str_comp(pType, "chess:invite_expired") == 0 || str_comp(pType, "chess:invite_declined") == 0)
	{
		m_ChessInviteActive = false;
		m_aChessInviteId[0] = '\0';
		m_aChessInviteFrom[0] = '\0';
		str_copy(m_aChessStatus, "Chess invite closed.", sizeof(m_aChessStatus));
	}
	else if(str_comp(pType, "chess:invite_sent") == 0)
	{
		str_copy(m_aChessStatus, "Challenge sent. Waiting for response.", sizeof(m_aChessStatus));
	}
	else if(str_comp(pType, "chess:match_snapshot") == 0)
	{
		m_ChessInviteActive = false;
		m_aChessInviteId[0] = '\0';
		m_aChessInviteFrom[0] = '\0';
		str_copy(m_aChessStatus, "Chess match started.", sizeof(m_aChessStatus));
	}
	else if(str_comp(pType, "chess:error") == 0)
	{
		const char *pError = JsonStringValue(json_object_get(pRoot, "error"), "unknown");
		str_format(m_aChessStatus, sizeof(m_aChessStatus), "Chess error: %s", pError);
	}
}

void CAetherBadges::PumpRealtimeMessages()
{
	std::vector<std::string> vMessages;
	m_Realtime.PumpMessages(vMessages);
	for(const std::string &Message : vMessages)
	{
		json_settings JsonSettings;
		mem_zero(&JsonSettings, sizeof(JsonSettings));
		char aError[256];
		json_value *pJson = json_parse_ex(&JsonSettings, static_cast<const json_char *>(Message.c_str()), Message.size(), aError);
		if(!pJson)
		{
			log_debug("aether/badges", "realtime json parse failed: %s", aError);
			continue;
		}
		const char *pType = pJson->type == json_object ? json_string_get(json_object_get(pJson, "type")) : nullptr;
		if(pType && str_startswith(pType, "chess:"))
		{
			ApplyChessMessage(pJson);
			if(m_vChessMessages.size() >= 64)
				m_vChessMessages.erase(m_vChessMessages.begin());
			m_vChessMessages.push_back(Message);
			json_value_free(pJson);
			continue;
		}
		if(pType && str_comp(pType, "assets:update") == 0)
		{
			const char *pCategory = JsonStringValue(json_object_get(pJson, "category"), "");
			str_copy(m_aPendingAssetsUpdateCategory, pCategory, sizeof(m_aPendingAssetsUpdateCategory));
			m_AssetsUpdatePending = true;
			json_value_free(pJson);
			continue;
		}
		if(pType && str_comp(pType, "error") == 0)
		{
			const char *pError = JsonStringValue(json_object_get(pJson, "error"), "unknown");
			str_format(m_aStatus, sizeof(m_aStatus), "Realtime error: %s", pError);
			str_format(m_aChessStatus, sizeof(m_aChessStatus), "Realtime error: %s", pError);
			json_value_free(pJson);
			continue;
		}
		ApplyPresenceBadgeObject(pJson);
		json_value_free(pJson);
	}
}

bool CAetherBadges::ConsumeAssetsCloudUpdate(const char *pCategory)
{
	if(!m_AssetsUpdatePending)
		return false;
	if(m_aPendingAssetsUpdateCategory[0] != '\0' && pCategory && pCategory[0] != '\0' && str_comp(m_aPendingAssetsUpdateCategory, pCategory) != 0)
		return false;
	m_AssetsUpdatePending = false;
	m_aPendingAssetsUpdateCategory[0] = '\0';
	return true;
}

void CAetherBadges::PumpResolveRequest()
{
	if(!m_pResolveRequest)
		return;

	const EHttpState HttpState = m_pResolveRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	if(HttpState == EHttpState::DONE && m_pResolveRequest->StatusCode() >= 200 && m_pResolveRequest->StatusCode() < 400)
	{
		json_value *pJson = m_pResolveRequest->ResultJson();
		const json_value *pPlayers = pJson && pJson->type == json_object ? json_object_get(pJson, "players") : nullptr;
		if(pPlayers && pPlayers->type == json_object)
		{
			int BadgeClients = 0;
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
				if(!pInfo || pInfo->m_ClientId < 0)
					continue;
				const char *pName = GameClient()->m_aClients[pInfo->m_ClientId].m_aName;
				ApplyBadgeArrayForClient(pInfo->m_ClientId, json_object_get(pPlayers, pName));
				if(!m_aClientBadges[pInfo->m_ClientId].m_vBadges.empty())
					++BadgeClients;
			}
			str_format(m_aStatus, sizeof(m_aStatus), "Loaded badges for %d player(s).", BadgeClients);
		}
		else
			str_copy(m_aStatus, "Badge response parse failed.", sizeof(m_aStatus));
		if(pJson)
			json_value_free(pJson);
	}
	else
		str_copy(m_aStatus, "Badge API request failed.", sizeof(m_aStatus));

	m_pResolveRequest = nullptr;
}

void CAetherBadges::PumpChessOnlineRequest()
{
	if(!m_pChessOnlineRequest)
		return;

	const EHttpState HttpState = m_pChessOnlineRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	if(HttpState == EHttpState::DONE && m_pChessOnlineRequest->StatusCode() >= 200 && m_pChessOnlineRequest->StatusCode() < 400)
	{
		json_value *pJson = m_pChessOnlineRequest->ResultJson();
		const json_value *pPlayers = pJson && pJson->type == json_object ? json_object_get(pJson, "players") : nullptr;
		ApplyChessOnlinePlayers(pPlayers);
		m_LastChessOnlineResponseTime = time_get();
		if(!m_pChessActionRequest && m_ChessRoom.m_aCode[0] == '\0')
			str_copy(m_aChessStatus, "Global chess lobby refreshed.", sizeof(m_aChessStatus));
		if(pJson)
			json_value_free(pJson);
	}
	else
	{
		if(!m_pChessActionRequest && m_ChessRoom.m_aCode[0] == '\0')
			str_copy(m_aChessStatus, "Chess online refresh failed.", sizeof(m_aChessStatus));
	}
	m_pChessOnlineRequest = nullptr;
}

void CAetherBadges::PumpAetherOnlineRequest()
{
	if(!m_pAetherOnlineRequest)
		return;

	const EHttpState HttpState = m_pAetherOnlineRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	if(HttpState == EHttpState::DONE && m_pAetherOnlineRequest->StatusCode() >= 200 && m_pAetherOnlineRequest->StatusCode() < 400)
	{
		json_value *pJson = m_pAetherOnlineRequest->ResultJson();
		const json_value *pPlayers = pJson && pJson->type == json_object ? json_object_get(pJson, "players") : nullptr;
		ApplyAetherOnlinePlayers(pPlayers);
		m_LastAetherOnlineResponseTime = time_get();
		str_format(m_aAetherOnlineStatus, sizeof(m_aAetherOnlineStatus), "Loaded %d Aether player(s).", m_AetherOnlineCount);
		if(pJson)
			json_value_free(pJson);
	}
	else
	{
		const int Status = HttpState == EHttpState::DONE ? m_pAetherOnlineRequest->StatusCode() : 0;
		if(Status == 404)
			str_copy(m_aAetherOnlineStatus, "Aether API is not updated. Deploy latest API.", sizeof(m_aAetherOnlineStatus));
		else if(HttpState == EHttpState::ABORTED)
			str_copy(m_aAetherOnlineStatus, "Aether online request aborted.", sizeof(m_aAetherOnlineStatus));
		else if(Status > 0)
			str_format(m_aAetherOnlineStatus, sizeof(m_aAetherOnlineStatus), "Aether online request failed: HTTP %d.", Status);
		else
			str_copy(m_aAetherOnlineStatus, "Aether online request failed: network error.", sizeof(m_aAetherOnlineStatus));
	}
	m_pAetherOnlineRequest = nullptr;
}

void CAetherBadges::PumpChessLeaderboardRequests()
{
	auto PumpOne = [&](std::shared_ptr<CHttpRequest> &pRequest, const char *pPeriod) {
		if(!pRequest)
			return;

		const EHttpState HttpState = pRequest->State();
		if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
			return;

		if(HttpState == EHttpState::DONE && pRequest->StatusCode() >= 200 && pRequest->StatusCode() < 400)
		{
			unsigned char *pResult = nullptr;
			size_t ResultLength = 0;
			pRequest->Result(&pResult, &ResultLength);
			if(pResult && ResultLength > 0)
			{
				if(m_vChessMessages.size() >= 64)
					m_vChessMessages.erase(m_vChessMessages.begin());
				m_vChessMessages.emplace_back(reinterpret_cast<const char *>(pResult), ResultLength);
			}
			str_copy(m_aChessStatus, "Chess leaderboard refreshed.", sizeof(m_aChessStatus));
		}
		else if(!m_pChessActionRequest)
		{
			int StatusCode = 0;
			if(HttpState == EHttpState::DONE)
				StatusCode = pRequest->StatusCode();
			str_format(m_aChessLastError, sizeof(m_aChessLastError), "Chess leaderboard %s failed (%d).", pPeriod, StatusCode);
			str_copy(m_aChessStatus, m_aChessLastError, sizeof(m_aChessStatus));
		}

		pRequest = nullptr;
	};

	PumpOne(m_pChessLeaderboardAllRequest, "all");
	PumpOne(m_pChessLeaderboardMonthlyRequest, "monthly");
}

void CAetherBadges::ApplyChessHttpResponse(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const json_value *pMessages = json_object_get(pRoot, "messages");
	if(pMessages && pMessages->type == json_array)
	{
		for(int i = 0; i < json_array_length(pMessages); ++i)
			ApplyChessMessage(json_array_get(pMessages, i));
		return;
	}
	const char *pType = JsonStringValue(json_object_get(pRoot, "type"));
	if(str_startswith(pType, "chess:"))
		ApplyChessMessage(pRoot);
}

void CAetherBadges::PumpChessActionRequest()
{
	if(!m_pChessActionRequest)
		return;

	const EHttpState HttpState = m_pChessActionRequest->State();
	if(HttpState != EHttpState::DONE && HttpState != EHttpState::ERROR && HttpState != EHttpState::ABORTED)
		return;

	if(HttpState == EHttpState::DONE && m_pChessActionRequest->StatusCode() >= 200 && m_pChessActionRequest->StatusCode() < 400)
	{
		unsigned char *pResult = nullptr;
		size_t ResultLength = 0;
		m_pChessActionRequest->Result(&pResult, &ResultLength);
		if(pResult && ResultLength > 0)
		{
			if(m_vChessMessages.size() >= 64)
				m_vChessMessages.erase(m_vChessMessages.begin());
			m_vChessMessages.emplace_back(reinterpret_cast<const char *>(pResult), ResultLength);
		}

		json_value *pJson = m_pChessActionRequest->ResultJson();
		ApplyChessHttpResponse(pJson);
		if(pJson)
			json_value_free(pJson);
		m_aChessLastError[0] = '\0';
	}
	else
	{
		int StatusCode = 0;
		json_value *pJson = nullptr;
		if(HttpState == EHttpState::DONE)
		{
			StatusCode = m_pChessActionRequest->StatusCode();
			pJson = m_pChessActionRequest->ResultJson();
		}
		const char *pError = pJson && pJson->type == json_object ? JsonStringValue(json_object_get(pJson, "error"), "request failed") : (HttpState == EHttpState::ABORTED ? "request aborted" : "request failed");
		str_format(m_aChessLastError, sizeof(m_aChessLastError), "Chess HTTP %d: %s", StatusCode, pError);
		str_copy(m_aChessStatus, m_aChessLastError, sizeof(m_aChessStatus));
		if(pJson)
			json_value_free(pJson);
	}
	m_pChessActionRequest = nullptr;
	m_ChessAction = EChessHttpAction::NONE;
}

void CAetherBadges::OnUpdate()
{
	if(!m_IconTexturesLoaded && (m_LastIconRetryTime == 0 || time_get() - m_LastIconRetryTime > time_freq()))
		LoadIconTextures();

	PumpRealtimeMessages();
	PumpResolveRequest();
	PumpHeartbeatRequest();
	PumpChessOnlineRequest();
	PumpAetherOnlineRequest();
	PumpChessLeaderboardRequests();
	PumpChessActionRequest();
	PumpPingRequests();
	PumpClanRequest();

	if(m_LastRefreshSeconds != g_Config.m_AeBadgesRefreshSeconds || str_comp(m_aLastApiUrl, g_Config.m_AeBadgesApiUrl) != 0)
	{
		m_LastRefreshSeconds = g_Config.m_AeBadgesRefreshSeconds;
		str_copy(m_aLastApiUrl, g_Config.m_AeBadgesApiUrl, sizeof(m_aLastApiUrl));
		m_LastResolveTime = 0;
		m_LastHeartbeatTime = 0;
		m_LastRealtimeHelloTime = 0;
		m_LastAetherOnlineRequestTime = 0;
		m_LastAetherOnlineResponseTime = 0;
		m_LastClanDirectoryTime = 0;
		m_ClanManagementAvailable = true;
		str_copy(m_aAetherOnlineStatus, "Aether online idle.", sizeof(m_aAetherOnlineStatus));
		m_Realtime.SetEndpointFromHttpBase(g_Config.m_AeBadgesApiUrl);
	}

	std::string PresencePayload;
	if(BuildPresencePayload(PresencePayload))
	{
		const int64_t Now = time_get();
		if(PresencePayload != m_LastRealtimePayload || m_LastRealtimeHelloTime == 0 || Now - m_LastRealtimeHelloTime > (int64_t)CLIENT_REALTIME_HELLO_SECONDS * time_freq())
		{
			m_Realtime.SetHelloPayload(PresencePayload);
			m_LastRealtimePayload = PresencePayload;
			m_LastRealtimeHelloTime = Now;
		}
	}
	else
	{
		m_LastRealtimePayload.clear();
		m_Realtime.SetHelloPayload("");
	}

	RequestHeartbeat(false);
	if(g_Config.m_AeBadges)
		RequestResolve(false);
	RequestChessRoomSnapshot(false);
	RequestPingPoll(false);
	RequestClanMine(false);
	RequestClanDirectory(false);
	ScanAutoHelpPings();
}

void CAetherBadges::RenderChessInviteButton(const CUIRect &Rect, const char *pText, ColorRGBA Color) const
{
	Rect.Draw(Color, IGraphics::CORNER_ALL, 7.0f);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Ui()->DoLabel(&Rect, pText, 11.0f, TEXTALIGN_MC);
}

void CAetherBadges::RenderChessInvitePopup()
{
	if(!m_ChessInviteActive)
		return;
	if(m_ChessInviteExpireTime > 0 && time_get() > m_ChessInviteExpireTime)
	{
		m_ChessInviteActive = false;
		m_aChessInviteId[0] = '\0';
		m_aChessInviteFrom[0] = '\0';
		str_copy(m_aChessStatus, "Chess invite expired.", sizeof(m_aChessStatus));
		return;
	}

	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	const float Width = 346.0f;
	const float Height = 104.0f;
	CUIRect Panel(ScreenW * 0.5f - Width * 0.5f, 34.0f, Width, Height);
	Panel.Draw(ColorRGBA(0.04f, 0.055f, 0.085f, 0.92f), IGraphics::CORNER_ALL, 12.0f);
	Panel.Margin(10.0f, &Panel);

	CUIRect Title, Hint, Buttons;
	Panel.HSplitTop(25.0f, &Title, &Panel);
	Panel.HSplitTop(18.0f, &Hint, &Panel);
	Panel.HSplitBottom(32.0f, nullptr, &Buttons);

	char aTitle[160];
	str_format(aTitle, sizeof(aTitle), "%s challenged you to chess", m_aChessInviteFrom);
	TextRender()->TextColor(0.92f, 0.97f, 1.0f, 1.0f);
	Ui()->DoLabel(&Title, aTitle, 15.0f, TEXTALIGN_MC);
	TextRender()->TextColor(0.66f, 0.74f, 0.86f, 1.0f);
	Ui()->DoLabel(&Hint, "F1 Accept   F2 Refuse   F3 Open Chess", 10.0f, TEXTALIGN_MC);

	CUIRect Accept, Refuse, Open;
	Buttons.VSplitLeft(102.0f, &Accept, &Buttons);
	Buttons.VSplitLeft(8.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(102.0f, &Refuse, &Buttons);
	Buttons.VSplitLeft(8.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(102.0f, &Open, nullptr);
	m_ChessInviteAcceptButton = Accept;
	m_ChessInviteDeclineButton = Refuse;
	m_ChessInviteOpenButton = Open;
	RenderChessInviteButton(Accept, "Accept", ColorRGBA(0.14f, 0.56f, 0.33f, 0.95f));
	RenderChessInviteButton(Refuse, "Refuse", ColorRGBA(0.56f, 0.16f, 0.22f, 0.95f));
	RenderChessInviteButton(Open, "Open Chess", ColorRGBA(0.18f, 0.32f, 0.62f, 0.95f));

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBadges::OnRender()
{
	RenderPings();
	RenderPingWheel();
	RenderChessInvitePopup();
}

bool CAetherBadges::OnInput(const IInput::CEvent &Event)
{
	if(m_PingWheelActive && Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			m_PingWheelActive = false;
			m_PingWheelWasActive = false;
			m_PingWheelHasSelection = false;
		}
		return true;
	}
	if(!m_ChessInviteActive || !(Event.m_Flags & IInput::FLAG_PRESS))
		return false;
	if(Event.m_Key == KEY_F1)
	{
		SendChessInviteReply("chess:accept");
		return true;
	}
	if(Event.m_Key == KEY_F2)
	{
		SendChessInviteReply("chess:decline");
		return true;
	}
	if(Event.m_Key == KEY_F3)
	{
		OpenChessOnlineMenu();
		return true;
	}
	if(Event.m_Key != KEY_MOUSE_1)
		return false;
	if(Ui()->MouseInside(&m_ChessInviteAcceptButton))
	{
		SendChessInviteReply("chess:accept");
		return true;
	}
	if(Ui()->MouseInside(&m_ChessInviteDeclineButton))
	{
		SendChessInviteReply("chess:decline");
		return true;
	}
	if(Ui()->MouseInside(&m_ChessInviteOpenButton))
	{
		OpenChessOnlineMenu();
		return true;
	}
	return false;
}

bool CAetherBadges::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_PingWheelActive)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_PingWheelMouse += vec2(x, y);
	const float MaxLen = 130.0f;
	if(length(m_PingWheelMouse) > MaxLen)
		m_PingWheelMouse = normalize(m_PingWheelMouse) * MaxLen;
	m_PingWheelHasSelection = length(m_PingWheelMouse) > 40.0f;
	m_PingWheelSelected = PingTypeFromWheelVector(m_PingWheelMouse);
	return true;
}

void CAetherBadges::OpenChessOnlineMenu()
{
	GameClient()->m_Menus.OpenAetherChessOnline();
}

void CAetherBadges::OnReset()
{
	Clear();
	RestartRealtime();
}

void CAetherBadges::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState)
	{
		Clear();
		RestartRealtime();
	}
}

void CAetherBadges::OnShutdown()
{
	Clear();
	m_Realtime.Stop();
	UnloadIconTextures();
}

void CAetherBadges::RefreshNow()
{
	m_LastResolveTime = 0;
	m_LastHeartbeatTime = 0;
	m_LastRealtimeHelloTime = 0;
	RequestHeartbeat(true);
	RequestResolve(true);
}

void CAetherBadges::SendRealtimePayload(const std::string &Payload)
{
	m_Realtime.SetEndpointFromHttpBase(g_Config.m_AeBadgesApiUrl);
	std::string PresencePayload;
	if(BuildPresencePayload(PresencePayload))
	{
		m_Realtime.SetHelloPayload(PresencePayload);
		m_LastRealtimePayload = PresencePayload;
		m_LastRealtimeHelloTime = time_get();
	}
	else
	{
		m_LastRealtimePayload.clear();
		m_Realtime.SetHelloPayload("");
		str_copy(m_aChessStatus, "Set a player name before using online chess.", sizeof(m_aChessStatus));
	}
	m_Realtime.QueuePayload(Payload);
}

void CAetherBadges::PumpChessMessages(std::vector<std::string> &vMessages)
{
	vMessages.insert(vMessages.end(), std::make_move_iterator(m_vChessMessages.begin()), std::make_move_iterator(m_vChessMessages.end()));
	m_vChessMessages.clear();
}

const CAetherBadges::SChessOnlinePlayer *CAetherBadges::ChessOnlinePlayer(int Index) const
{
	if(Index < 0 || Index >= m_ChessOnlineCount)
		return nullptr;
	return &m_aChessOnline[Index];
}

const CAetherBadges::SChessOnlinePlayer *CAetherBadges::AetherOnlinePlayer(int Index) const
{
	if(Index < 0 || Index >= m_AetherOnlineCount)
		return nullptr;
	return &m_aAetherOnline[Index];
}

bool CAetherBadges::ShouldRenderBadge(const SBadge &Badge) const
{
	return !g_Config.m_AeBadgesClientOnly || IsClientBadgeKey(Badge.m_aKey);
}

bool CAetherBadges::HasBadges(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !g_Config.m_AeBadges)
		return false;
	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(!ClientBadges.m_Valid || str_comp(ClientBadges.m_aName, pName) != 0)
		return false;
	return std::any_of(ClientBadges.m_vBadges.begin(), ClientBadges.m_vBadges.end(), [this](const SBadge &Badge) {
		return ShouldRenderBadge(Badge);
	});
}

bool CAetherBadges::FormatBadgeText(int ClientId, char *pOut, int OutSize, int MaxBadges) const
{
	if(!pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';
	if(!HasBadges(ClientId))
		return false;

	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	str_copy(pOut, " [", OutSize);
	int Count = 0;
	for(const SBadge &Badge : ClientBadges.m_vBadges)
	{
		if(!ShouldRenderBadge(Badge))
			continue;
		if(Count > 0)
			str_append(pOut, " ", OutSize);
		str_append(pOut, BadgeShortLabel(Badge.m_aKey, Badge.m_aName), OutSize);
		++Count;
		if(Count >= std::max(1, MaxBadges))
			break;
	}
	const int TotalRenderable = std::count_if(ClientBadges.m_vBadges.begin(), ClientBadges.m_vBadges.end(), [this](const SBadge &Badge) {
		return ShouldRenderBadge(Badge);
	});
	if(TotalRenderable > Count)
		str_append(pOut, "+", OutSize);
	str_append(pOut, "]", OutSize);
	return Count > 0;
}

int CAetherBadges::BadgeIconCount(int ClientId, int MaxBadges) const
{
	if(!HasBadges(ClientId))
		return 0;

	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	int IconCount = 0;
	for(const SBadge &Badge : ClientBadges.m_vBadges)
	{
		if(!ShouldRenderBadge(Badge))
			continue;
		const int IconIndex = BadgeIconIndex(Badge.m_aKey);
		if(IconIndex >= 0 && IconIndex < (int)m_aIconTextures.size() && m_aIconTextures[IconIndex].IsValid())
			++IconCount;
		if(IconCount >= std::max(1, MaxBadges))
			break;
	}
	return IconCount;
}

float CAetherBadges::BadgeIconsWidth(int ClientId, float IconSize, int MaxBadges) const
{
	const int IconCount = BadgeIconCount(ClientId, MaxBadges);
	if(IconCount <= 0)
		return 0.0f;
	const float Spacing = IconSize * 0.04f;
	return IconCount * IconSize + (IconCount - 1) * Spacing;
}

bool CAetherBadges::RenderBadgeIcons(int ClientId, float x, float y, float IconSize, float Alpha, int MaxBadges) const
{
	if(IconSize <= 0.0f || Alpha <= 0.0f || !HasBadges(ClientId))
		return false;

	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	const float Spacing = IconSize * 0.04f;
	int Drawn = 0;
	Graphics()->WrapClamp();
	for(const SBadge &Badge : ClientBadges.m_vBadges)
	{
		if(!ShouldRenderBadge(Badge))
			continue;
		const int IconIndex = BadgeIconIndex(Badge.m_aKey);
		if(IconIndex < 0 || IconIndex >= (int)m_aIconTextures.size() || !m_aIconTextures[IconIndex].IsValid())
			continue;

		Graphics()->TextureSet(m_aIconTextures[IconIndex]);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem QuadItem(x + Drawn * (IconSize + Spacing), y, IconSize, IconSize);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		++Drawn;
		if(Drawn >= std::max(1, MaxBadges))
			break;
	}
	Graphics()->TextureClear();
	Graphics()->WrapNormal();
	return Drawn > 0;
}

int CAetherBadges::ClientBadgeIconCount(int ClientId) const
{
	if(!HasBadges(ClientId))
		return 0;

	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	for(const SBadge &Badge : ClientBadges.m_vBadges)
	{
		if(!IsClientBadgeKey(Badge.m_aKey) || !ShouldRenderBadge(Badge))
			continue;
		const int IconIndex = BadgeIconIndex(Badge.m_aKey);
		if(IconIndex >= 0 && IconIndex < (int)m_aIconTextures.size() && m_aIconTextures[IconIndex].IsValid())
			return 1;
	}
	return 0;
}

float CAetherBadges::ClientBadgeIconsWidth(int ClientId, float IconSize) const
{
	return ClientBadgeIconCount(ClientId) > 0 ? IconSize : 0.0f;
}

bool CAetherBadges::RenderClientBadgeIcons(int ClientId, float x, float y, float IconSize, float Alpha) const
{
	if(IconSize <= 0.0f || Alpha <= 0.0f || !HasBadges(ClientId))
		return false;

	const SClientBadges &ClientBadges = m_aClientBadges[ClientId];
	for(const SBadge &Badge : ClientBadges.m_vBadges)
	{
		if(!IsClientBadgeKey(Badge.m_aKey) || !ShouldRenderBadge(Badge))
			continue;
		const int IconIndex = BadgeIconIndex(Badge.m_aKey);
		if(IconIndex < 0 || IconIndex >= (int)m_aIconTextures.size() || !m_aIconTextures[IconIndex].IsValid())
			continue;

		Graphics()->WrapClamp();
		Graphics()->TextureSet(m_aIconTextures[IconIndex]);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem QuadItem(x, y, IconSize, IconSize);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->TextureClear();
		Graphics()->WrapNormal();
		return true;
	}
	return false;
}

bool CAetherBadges::RenderClientBadgeKey(const char *pClientKey, float x, float y, float IconSize, float Alpha) const
{
	if(!pClientKey || !pClientKey[0] || IconSize <= 0.0f || Alpha <= 0.0f)
		return false;

	char aKey[64];
	if(str_startswith(pClientKey, "client_"))
		str_copy(aKey, pClientKey, sizeof(aKey));
	else
		str_format(aKey, sizeof(aKey), "client_%s", pClientKey);

	const int IconIndex = BadgeIconIndex(aKey);
	if(IconIndex < 0 || IconIndex >= (int)m_aIconTextures.size() || !m_aIconTextures[IconIndex].IsValid())
		return false;

	Graphics()->WrapClamp();
	Graphics()->TextureSet(m_aIconTextures[IconIndex]);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	IGraphics::CQuadItem QuadItem(x, y, IconSize, IconSize);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();
	Graphics()->WrapNormal();
	return true;
}
