#ifndef GAME_CLIENT_COMPONENTS_AETHER_BADGES_H
#define GAME_CLIENT_COMPONENTS_AETHER_BADGES_H

#include "realtime_client.h"

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

class CAetherBadges : public CComponent
{
public:
	struct SBadge
	{
		char m_aKey[64] = "";
		char m_aName[64] = "";
		int m_Priority = 0;
	};
	struct SChessOnlinePlayer
	{
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aClient[32] = "";
		char m_aServer[96] = "";
		char m_aServerAddress[NETADDR_MAXSTRSIZE] = "";
		char m_aMap[64] = "";
		int m_Rating = 1200;
		bool m_Spectator = false;
		bool m_InServer = false;
	};
	struct SChessRoomPlayer
	{
		char m_aName[MAX_NAME_LENGTH] = "";
		bool m_Ready = false;
		bool m_Owner = false;
	};
	struct SChessRoomState
	{
		char m_aCode[16] = "";
		char m_aStatus[24] = "";
		char m_aMatchId[96] = "";
		std::array<SChessRoomPlayer, 2> m_aPlayers{};
		int m_PlayerCount = 0;
		bool m_Rated = true;
	};
	enum class EPingType
	{
		PLACE,
		HELP,
		DANGER,
		COME,
		WAIT,
	};
	enum class EAutoHelpPingState : unsigned char
	{
		IDLE,
		PENDING_GROUND,
		SENT,
	};

private:
	enum class EChessHttpAction
	{
		NONE,
		ROOM_CREATE,
		ROOM_JOIN,
		ROOM_READY,
		ROOM_LEAVE,
		ROOM_SNAPSHOT,
		MATCH_MOVE,
		MATCH_RESIGN,
	};
	enum class EClanHttpAction
	{
		NONE,
		CREATE,
		JOIN,
		MINE,
		MEMBERS,
		LEAVE,
		DISBAND,
		ROTATE_INVITE,
		MEMBER_REMOVE,
		PUBLIC_DIRECTORY,
		WARLIST_PUSH,
		WARLIST_PULL,
		RECOVER_MEMBERSHIP,
	};

	struct SClientBadges
	{
		char m_aName[MAX_NAME_LENGTH] = "";
		bool m_Valid = false;
		std::vector<SBadge> m_vBadges;
	};
	struct SPingEvent
	{
		int m_Seq = 0;
		EPingType m_Type = EPingType::HELP;
		char m_aPlayer[MAX_NAME_LENGTH] = "";
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int64_t m_ExpireTime = 0;
		bool m_Auto = false;
	};

public:
	struct SClanState
	{
		struct SMember
		{
			char m_aName[MAX_NAME_LENGTH] = "";
			char m_aRole[16] = "";
			char m_aJoinedAt[32] = "";
		};
		char m_aId[96] = "";
		char m_aName[64] = "";
		char m_aType[16] = "";
		char m_aInviteCode[16] = "";
		char m_aRole[16] = "";
		int m_MemberCount = 0;
		int m_MemberListCount = 0;
		bool m_Valid = false;
		bool m_MembersLoaded = false;
		std::array<SMember, MAX_CLIENTS> m_aMembers{};
	};
	struct SPublicClanMembership
	{
		char m_aPlayer[MAX_NAME_LENGTH] = "";
		char m_aClan[64] = "";
		char m_aType[16] = "";
	};

private:
	std::array<SClientBadges, MAX_CLIENTS> m_aClientBadges;
	std::array<IGraphics::CTextureHandle, 7> m_aIconTextures;
	std::array<SChessOnlinePlayer, MAX_CLIENTS> m_aChessOnline{};
	std::array<SChessOnlinePlayer, MAX_CLIENTS> m_aAetherOnline{};
	std::vector<SPingEvent> m_vPings;
	std::vector<SPublicClanMembership> m_vPublicClanMemberships;
	CAetherRealtimeClient m_Realtime;
	std::vector<std::string> m_vChessMessages;
	std::shared_ptr<CHttpRequest> m_pResolveRequest;
	std::shared_ptr<CHttpRequest> m_pHeartbeatRequest;
	std::shared_ptr<CHttpRequest> m_pChessOnlineRequest;
	std::shared_ptr<CHttpRequest> m_pAetherOnlineRequest;
	std::shared_ptr<CHttpRequest> m_pChessActionRequest;
	std::shared_ptr<CHttpRequest> m_pChessLeaderboardAllRequest;
	std::shared_ptr<CHttpRequest> m_pChessLeaderboardMonthlyRequest;
	std::shared_ptr<CHttpRequest> m_pPingSendRequest;
	std::shared_ptr<CHttpRequest> m_pPingPollRequest;
	std::shared_ptr<CHttpRequest> m_pClanRequest;
	EChessHttpAction m_ChessAction = EChessHttpAction::NONE;
	EClanHttpAction m_ClanAction = EClanHttpAction::NONE;
	EClanHttpAction m_ClanRecoveryAction = EClanHttpAction::NONE;
	int64_t m_LastResolveTime = 0;
	int64_t m_LastHeartbeatTime = 0;
	int64_t m_LastRealtimeHelloTime = 0;
	int64_t m_LastIconRetryTime = 0;
	int64_t m_LastChessOnlineRequestTime = 0;
	int64_t m_LastChessOnlineResponseTime = 0;
	int64_t m_LastAetherOnlineRequestTime = 0;
	int64_t m_LastAetherOnlineResponseTime = 0;
	int64_t m_LastChessRoomPollTime = 0;
	int64_t m_LastPingPollTime = 0;
	int64_t m_LastClanMineTime = 0;
	int64_t m_LastClanDirectoryTime = 0;
	int64_t m_ChessInviteExpireTime = 0;
	std::array<EAutoHelpPingState, MAX_CLIENTS> m_aHelpPingStates{};
	int m_LastRefreshSeconds = 0;
	int m_ChessOnlineCount = 0;
	int m_AetherOnlineCount = 0;
	int m_LastPingSeq = 0;
	SChessRoomState m_ChessRoom;
	SClanState m_Clan;
	SClanState m_GeneralClan;
	SClanState m_KogClan;
	char m_aClanRequestType[16] = "";
	char m_aClanRequestId[96] = "";
	char m_aClanRecoveryType[16] = "";
	std::string m_LastRealtimePayload;
	char m_aLastApiUrl[256] = "";
	char m_aLastHeartbeatName[MAX_NAME_LENGTH] = "";
	char m_aStatus[128] = "Idle";
	char m_aAetherOnlineStatus[128] = "Aether online idle.";
	char m_aChessInviteId[96] = "";
	char m_aChessInviteFrom[MAX_NAME_LENGTH] = "";
	char m_aChessStatus[160] = "Online chess ready.";
	char m_aChessLastError[160] = "";
	char m_aClanStatus[160] = "Clan ready.";
	char m_aClanLastError[160] = "";
	char m_aPendingAssetsUpdateCategory[32] = "";
	CUIRect m_ChessInviteAcceptButton;
	CUIRect m_ChessInviteDeclineButton;
	CUIRect m_ChessInviteOpenButton;
	vec2 m_PingWheelMouse = vec2(0.0f, 0.0f);
	EPingType m_PingWheelSelected = EPingType::PLACE;
	bool m_IconTexturesLoaded = false;
	bool m_ChessInviteActive = false;
	bool m_PingWheelActive = false;
	bool m_PingWheelWasActive = false;
	bool m_PingWheelHasSelection = false;
	bool m_ClanManagementAvailable = true;
	bool m_AssetsUpdatePending = false;
	bool m_DdraceShowOthersOn = false;
	bool m_DdraceSuperOn = false;
	bool m_DdraceDeepFlyOn = false;
	bool m_DdraceEdge2EdgeOn = false;

	void Clear();
	void BuildUrl(char *pOut, int OutSize, const char *pPath) const;
	bool HasActivePlayers() const;
	bool LocalPlayerName(char *pOut, int OutSize) const;
	bool CurrentServerKey(char *pOut, int OutSize) const;
	bool BuildPresencePayload(std::string &Payload) const;
	void RestartRealtime();
	void RequestHeartbeat(bool Force);
	void PumpHeartbeatRequest();
	void RequestResolve(bool Force);
	void PumpResolveRequest();
	void PumpRealtimeMessages();
	void LoadIconTextures();
	void UnloadIconTextures();
	void ApplyBadgeArrayForClient(int ClientId, const json_value *pBadgeArray);
	void ApplyBadgeArrayForName(const char *pName, const json_value *pBadgeArray);
	void ApplyPlayersBadgeObject(const json_value *pPlayers);
	void ApplyPresenceBadgeObject(const json_value *pRoot);
	void ApplyChessMessage(const json_value *pRoot);
	void ApplyChessOnlinePlayers(const json_value *pPlayers);
	void ApplyChessOnlineUpdate(const json_value *pPlayer);
	void ApplyChessOnlineLeft(const char *pName);
	void ApplyAetherOnlinePlayers(const json_value *pPlayers);
	void ApplyAetherOnlineUpdate(const json_value *pPlayer);
	void ApplyAetherOnlineLeft(const char *pName);
	void ApplyChessRoomSnapshot(const json_value *pRoom);
	void PumpChessOnlineRequest();
	void PumpAetherOnlineRequest();
	void PumpChessLeaderboardRequests();
	void PumpChessActionRequest();
	void ApplyChessHttpResponse(const json_value *pRoot);
	bool WriteChessPlayerName(CJsonStringWriter &Json);
	bool StartChessHttpPost(const char *pPath, const std::string &Payload, EChessHttpAction Action, const char *pStatus);
	bool StartChessHttpGet(const char *pPath, EChessHttpAction Action, const char *pStatus, bool Quiet = false);
	void RequestChessRoomSnapshot(bool Force);
	void RequestPingPoll(bool Force);
	void PumpPingRequests();
	void ScanAutoHelpPings();
	void RenderPings();
	void RenderPingWheel();
	void ExecutePingWheelSelection();
	bool AddOrMergePing(const SPingEvent &Event);
	bool LocalCharacterGrounded() const;
	void SendPing(EPingType Type, vec2 Pos, bool Auto);
	vec2 ManualPingPosition() const;
	bool StartClanHttpPost(const char *pPath, const std::string &Payload, EClanHttpAction Action, const char *pStatus);
	bool StartClanHttpGet(const char *pPath, EClanHttpAction Action, const char *pStatus, bool Quiet = false);
	bool StartClanRecovery(const SClanState &Clan, EClanHttpAction Action);
	void RetryClanRecoveryAction();
	void PumpClanRequest();
	void ApplyClanResponse(const json_value *pRoot);
	void ApplyClanObject(const json_value *pClan, const char *pSecret = nullptr);
	void ApplyClanMembers(const json_value *pRoot);
	void ApplyClanDirectory(const json_value *pRoot);
	void RequestClanMine(bool Force);
	void RequestClanDirectory(bool Force);
	bool BuildClanAuthPayload(const SClanState &Clan, std::string &Payload);
	void ClearClanByType(const char *pType);
	void SendChessOnlineRequestPayload();
	void SendChessInviteReplyPayload(const char *pType);
	void RenderChessInviteButton(const CUIRect &Rect, const char *pText, ColorRGBA Color) const;
	void OpenChessOnlineMenu();
	bool ShouldRenderBadge(const SBadge &Badge) const;
	static int BadgeIconIndex(const char *pKey);
	static int BadgeRenderRank(const SBadge &Badge);
	static const char *PingTypeName(EPingType Type);
	static const char *PingTypeDisplayName(EPingType Type);
	static const char *PingTypeGlyph(EPingType Type);
	static EPingType PingTypeFromName(const char *pName);
	static EPingType PingTypeFromWheelVector(vec2 Mouse);
	static ColorRGBA PingTypeColor(EPingType Type, float Alpha = 1.0f);
	static void ConPingWheel(IConsole::IResult *pResult, void *pUserData);
	static void ConPing(IConsole::IResult *pResult, void *pUserData);
	static void ConDdraceConfig(IConsole::IResult *pResult, void *pUserData);
	void ExecuteDdraceConfig(const char *pName, const char *pMode = nullptr);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	void RefreshNow();
	void SendRealtimePayload(const std::string &Payload);
	bool ConsumeAssetsCloudUpdate(const char *pCategory);
	void PumpChessMessages(std::vector<std::string> &vMessages);
	void RequestChessOnline(bool Force = false);
	void RequestAetherOnline(bool Force = false);
	void RequestChessLeaderboard(const char *pPeriod, bool Force = false);
	void SendChessInvite(const char *pTarget);
	void SendChessInviteReply(const char *pType);
	void SendChessRoomCreate(bool Rated = true);
	void SendChessRoomJoin(const char *pCode);
	void SendChessRoomReady(bool Ready);
	void SendChessRoomLeave();
	void SendChessMove(const char *pMatchId, const char *pUci);
	void SendChessResign(const char *pMatchId);
	void SendClanCreate(const char *pName, const char *pType);
	void SendClanJoin(const char *pInviteCode);
	void SendClanMembers(const SClanState &Clan);
	void SendClanLeave(const SClanState &Clan);
	void SendClanDisband(const SClanState &Clan);
	void SendClanRotateInvite(const SClanState &Clan);
	void SendClanMemberRemove(const SClanState &Clan, const char *pTargetPlayerName);
	void SendClanWarlistPush(const std::string &EntriesJson);
	void SendClanWarlistPull();
	void RefreshClan();
	void RenderChessInvitePopup();
	bool HasChessInvite() const { return m_ChessInviteActive; }
	const char *ChessInviteFrom() const { return m_aChessInviteFrom; }
	const char *ChessStatus() const { return m_aChessStatus; }
	const char *ChessLastError() const { return m_aChessLastError; }
	const char *ClanStatus() const { return m_aClanStatus; }
	const char *ClanLastError() const { return m_aClanLastError; }
	const SClanState &Clan() const { return m_Clan; }
	const SClanState &GeneralClan() const { return m_GeneralClan; }
	const SClanState &KogClan() const { return m_KogClan; }
	bool ClanRequestActive() const { return m_pClanRequest != nullptr; }
	bool ClanManagementAvailable() const { return m_ClanManagementAvailable; }
	bool IsPingWheelActive() const { return m_PingWheelActive; }
	bool ScoreboardClanForClient(int ClientId, bool KogServer, char *pOut, int OutSize) const;
	void ChessRealtimeStatus(char *pOut, int OutSize) const { m_Realtime.Status(pOut, OutSize); }
	int ChessOnlineCount() const { return m_ChessOnlineCount; }
	const SChessOnlinePlayer *ChessOnlinePlayer(int Index) const;
	int AetherOnlineCount() const { return m_AetherOnlineCount; }
	const SChessOnlinePlayer *AetherOnlinePlayer(int Index) const;
	const char *AetherOnlineStatus() const { return m_aAetherOnlineStatus; }
	const SChessRoomState &ChessRoom() const { return m_ChessRoom; }
	bool HasBadges(int ClientId) const;
	bool FormatBadgeText(int ClientId, char *pOut, int OutSize, int MaxBadges = 3) const;
	int BadgeIconCount(int ClientId, int MaxBadges = 3) const;
	float BadgeIconsWidth(int ClientId, float IconSize, int MaxBadges = 3) const;
	bool RenderBadgeIcons(int ClientId, float x, float y, float IconSize, float Alpha, int MaxBadges = 3) const;
	int ClientBadgeIconCount(int ClientId) const;
	float ClientBadgeIconsWidth(int ClientId, float IconSize) const;
	bool RenderClientBadgeIcons(int ClientId, float x, float y, float IconSize, float Alpha) const;
	bool RenderClientBadgeKey(const char *pClientKey, float x, float y, float IconSize, float Alpha) const;
	const char *Status() const { return m_aStatus; }
};

#endif
