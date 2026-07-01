#ifndef GAME_CLIENT_COMPONENTS_AETHER_SESSION_MARKERS_H
#define GAME_CLIENT_COMPONENTS_AETHER_SESSION_MARKERS_H

#include <engine/console.h>

#include <game/client/component.h>

class CAetherSessionMarkers : public CComponent
{
	int64_t m_LastFreezeMarkerTime = 0;
	bool m_LastFrozen = false;

	static void ConAddMarker(IConsole::IResult *pResult, void *pUserData);
	void AddMarker(const char *pReason);
	bool LocalFreezeActive() const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
};

#endif
