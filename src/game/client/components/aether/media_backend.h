#ifndef GAME_CLIENT_COMPONENTS_AETHER_MEDIA_BACKEND_H
#define GAME_CLIENT_COMPONENTS_AETHER_MEDIA_BACKEND_H

#include "music_player_helpers.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CAetherMediaBackend
{
public:
	enum class ECaptureStatus
	{
		DISABLED,
		NO_SESSION,
		PROCESS_NOT_FOUND,
		START_FAILED,
		CAPTURING,
	};

	struct SSnapshot
	{
		AetherMusic::EPlaybackState m_PlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
		AetherMusic::EPlaybackState m_MediaPlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
		std::array<float, 5> m_aBands{};
		bool m_VisualizerAvailable = false;
		bool m_AudioActive = false;
		ECaptureStatus m_CaptureStatus = ECaptureStatus::DISABLED;
		uint32_t m_ProcessId = 0;
		uint32_t m_AudioProcessId = 0;
		float m_SessionPeak = 0.0f;
		float m_RootMeanSquare = 0.0f;
		uint64_t m_CapturedFrames = 0;
		std::string m_Source;
		std::string m_Title;
		std::string m_Artist;
		uint64_t m_ArtworkGeneration = 0;
		std::shared_ptr<const std::vector<uint8_t>> m_pArtworkRgba;
		uint32_t m_ArtworkWidth = 0;
		uint32_t m_ArtworkHeight = 0;
		int64_t m_LastPlayingMs = 0;
		int64_t m_ArtworkReceivedMs = 0;
	};

	CAetherMediaBackend();
	~CAetherMediaBackend();

	void Start();
	void Stop();
	bool Running() const;
	SSnapshot Snapshot() const;
	void SetVisualizer(bool Enabled, int Sensitivity);

private:
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;
};

#endif
