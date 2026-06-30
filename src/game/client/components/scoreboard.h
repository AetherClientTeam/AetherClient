/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <cstdint>
#include <memory>

class CHttpRequest;

class CScoreboard : public CComponent
{
	struct SKogPointsEntry;

	struct CScoreboardRenderState
	{
		float m_TeamStartX;
		float m_TeamStartY;
		int m_CurrentDDTeamSize;

		CScoreboardRenderState() :
			m_TeamStartX(0), m_TeamStartY(0), m_CurrentDDTeamSize(0) {}
	};

	void RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize);
	void RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize);
	void RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderGoals(CUIRect Goals);
	void RenderSpectators(CUIRect Spectators);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State);
	void RenderRecordingNotification(float x);
	void RenderPlayerHoverCard(int ClientId);

	static void ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData);

	const char *GetTeamName(int Team) const;
	void ResetKogPoints();
	void PumpKogPointsRequest();
	void RequestKogPoints(bool KogServer);
	bool KogPointsForClient(int ClientId, int &Points) const;
	void ApplyKogPointsForName(const char *pName, int Points);
	SKogPointsEntry *KogPointsEntryForName(const char *pName, bool Create);

	bool m_Active;

	IGraphics::CTextureHandle m_DeadTeeTexture;

	std::optional<vec2> m_LastMousePos;
	bool m_MouseUnlocked = false;
	int m_HoveredClientId = -1;

	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	class CScoreboardPopupContext : public SPopupMenuId
	{
	public:
		CScoreboard *m_pScoreboard = nullptr;
		CButtonContainer m_FriendAction;
		CButtonContainer m_MuteAction;
		CButtonContainer m_EmoticonAction;
		CButtonContainer m_CopySkinButton;
		CButtonContainer m_CopyColorButton;
		CButtonContainer m_InviteButton;
		CButtonContainer m_TeamLockButton;
		CButtonContainer m_BlockEnemyButton;
		CButtonContainer m_BlockAllyButton;
		CButtonContainer m_BlockHelperButton;
		CButtonContainer m_BlockNeutralButton;

		CButtonContainer m_SpectateButton;

		int m_ClientId;
		bool m_IsLocal;
		bool m_IsSpectating;

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_ScoreboardPopupContext;

	class CMapTitlePopupContext : public SPopupMenuId
	{
	public:
		CScoreboard *m_pScoreboard = nullptr;

		float m_FontSize;

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_MapTitlePopupContext;
	char m_MapTitleButtonId;

	class CPlayerElement
	{
	public:
		char m_PlayerButtonId;
		char m_SpectatorSecondLineButtonId;
	};
	CPlayerElement m_aPlayers[MAX_CLIENTS];

	struct SKogPointsEntry
	{
		char m_aName[MAX_NAME_LENGTH];
		int m_Points;
		bool m_HasPoints;
		int64_t m_LastUpdateTime;

		SKogPointsEntry() :
			m_aName(""), m_Points(0), m_HasPoints(false), m_LastUpdateTime(0) {}
	};

	SKogPointsEntry m_aKogPoints[MAX_CLIENTS];
	std::shared_ptr<CHttpRequest> m_pKogPointsRequest;
	char m_aaKogPointsPendingNames[MAX_CLIENTS][MAX_NAME_LENGTH];
	int m_KogPointsPendingNameCount = 0;
	int64_t m_LastKogPointsRequestTime = 0;
	int64_t m_KogPointsUnavailableUntil = 0;
	char m_aKogPointsApiUrl[256] = "";

public:
	CScoreboard();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const;
	void OpenPlayerPopup(int ClientId, bool IsSpectating);
};

#endif
