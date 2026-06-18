#ifndef GAME_CLIENT_COMPONENTS_AETHER_ROLLBACK_DEMO_H
#define GAME_CLIENT_COMPONENTS_AETHER_ROLLBACK_DEMO_H

#include <engine/console.h>

#include <game/client/component.h>

class CAetherRollbackDemo : public CComponent
{
	int m_LastEnabled = -1;
	int m_LastSeconds = -1;

	static void ConSaveRollbackDemo(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
};

#endif
