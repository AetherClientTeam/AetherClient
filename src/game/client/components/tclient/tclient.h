#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/shared/console.h>
#include <engine/shared/http.h>

#include <game/client/component.h>

#include <deque>

class CTClient : public CComponent
{
	std::deque<vec2> m_aAirRescuePositions[NUM_DUMMIES];
	void AirRescue();
	static void ConAirRescue(IConsole::IResult *pResult, void *pUserData);

	static void ConCalc(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomTee(IConsole::IResult *pResult, void *pUserData);
	static void ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void RandomBodyColor();
	static void RandomFeetColor();
	static void RandomSkin(void *pUserData);
	static void RandomFlag(void *pUserData);

	static void ConSpecId(IConsole::IResult *pResult, void *pUserData);
	static void ConFastSpec(IConsole::IResult *pResult, void *pUserData);
	void SpecId(int ClientId);
	enum class EFastSpecRequest
	{
		NONE,
		SPECTATE,
		RETURN,
		SELF_SPEC,
		SELF_RETURN,
	};
	EFastSpecRequest m_FastSpecRequest = EFastSpecRequest::NONE;
	int m_FastSpecTarget = -1;
	int m_FastSpecNextTick = 0;
	int m_FastSpecAttempts = 0;
	int m_FastSpecStartTick = 0;
	void ClearFastSpecRequest();
	bool FastSpecTargetValid(int ClientId) const;
	bool FastSpecLocalGrounded() const;
	void SendSpecCommandForTarget(int ClientId);
	void UpdateFastSpecRequest();

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);

	float m_FinishTextTimeout = 0.0f;
	void DoFinishCheck();

	bool ServerCommandExists(const char *pCommand);

public:
	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;

	void OnStateChange(int OldState, int NewState) override;
	void OnNewSnapshot() override;
	void SetForcedAspect();

	void RenderMiniVoteHud();
	void RenderCenterLines();
	void RenderCtfFlag(vec2 Pos, float Alpha);

	bool ChatDoSpecId(const char *pInput);
	void RequestFastSpecSpectate(int ClientId);
	void RequestFastSpecReturn();
	void RequestFastSpecAssist();
	const char *FastSpecStatus() const;
	Regex m_RegexChatIgnore;
};

#endif
