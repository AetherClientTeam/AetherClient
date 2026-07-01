#ifndef GAME_CLIENT_COMPONENTS_AETHER_NOTIFICATIONS_H
#define GAME_CLIENT_COMPONENTS_AETHER_NOTIFICATIONS_H

#include <engine/console.h>

#include <game/client/component.h>

#include <vector>

class CAetherNotifications : public CComponent
{
	struct SNotification
	{
		char m_aTitle[48] = "";
		char m_aMessage[160] = "";
		int64_t m_Start = 0;
		int64_t m_End = 0;
	};

	std::vector<SNotification> m_vNotifications;
	int64_t m_UpdateCheckTime = 0;
	int m_LastUpdaterState = -1;
	bool m_UpdateCheckRequested = false;

	static void ConNotify(IConsole::IResult *pResult, void *pUserData);
	static void ConClear(IConsole::IResult *pResult, void *pUserData);
	static void ConCheckUpdate(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;

	void Push(const char *pTitle, const char *pMessage, float Seconds = 5.0f);
	void Clear();
};

#endif
