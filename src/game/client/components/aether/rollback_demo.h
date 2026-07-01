#ifndef GAME_CLIENT_COMPONENTS_AETHER_ROLLBACK_DEMO_H
#define GAME_CLIENT_COMPONENTS_AETHER_ROLLBACK_DEMO_H

#include <engine/console.h>

#include <game/client/component.h>

#include <cstdint>

class CAetherRollbackDemo : public CComponent
{
	int m_LastEnabled = -1;
	int m_LastSeconds = -1;
	int64_t m_ReplayEchoSuppressUntil = 0;

	static void ConSaveRollbackDemo(IConsole::IResult *pResult, void *pUserData);
	bool NotificationsEnabled() const;
	void EmitStatus(const char *pMessage);
	void ExpectReplayEcho();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
	bool ConsumeReplayEcho(const char *pString);
};

#endif
