#include <game/client/components/aether/music_player_helpers.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

using namespace AetherMusic;

TEST(AetherMusicPlayer, TimerModel)
{
	STimerInput Input;
	Input.m_GameTick = 50 * 75;
	Input.m_TickSpeed = 50;
	EXPECT_EQ(TimerModel(Input).m_Text, "1:15");

	Input.m_TimeLimitMinutes = 2;
	Input.m_RoundStartTick = 0;
	EXPECT_EQ(TimerModel(Input).m_Text, "0:45");
	EXPECT_TRUE(TimerModel(Input).m_Urgent);

	Input.m_SuddenDeath = true;
	EXPECT_FALSE(TimerModel(Input).m_Visible);
}

TEST(AetherMusicPlayer, FiveBandMappingAndSensitivity)
{
	constexpr int SampleRate = 48000;
	std::vector<float> vSamples(1024);
	for(size_t i = 0; i < vSamples.size(); ++i)
		vSamples[i] = std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / SampleRate);
	const auto Normal = FiveBands(vSamples, SampleRate, 100);
	const auto Sensitive = FiveBands(vSamples, SampleRate, 300);
	EXPECT_GT(Normal[2], Normal[0]);
	EXPECT_GE(Sensitive[2], Normal[2]);

	for(size_t i = 0; i < vSamples.size(); ++i)
		vSamples[i] *= 0.02f;
	const auto QuietNormal = FiveBands(vSamples, SampleRate, 100);
	const auto QuietSensitive = FiveBands(vSamples, SampleRate, 300);
	EXPECT_GT(QuietSensitive[2], QuietNormal[2] * 1.5f);
}

TEST(AetherMusicPlayer, DominantAndDarkenedColor)
{
	const std::array<uint8_t, 16> aPixels = {
		220, 30, 20, 255,
		225, 35, 25, 255,
		20, 40, 220, 255,
		0, 0, 0, 0};
	const SColor Dominant = DominantColor(aPixels);
	EXPECT_GT(Dominant.m_R, Dominant.m_B);
	const SColor Darkened = DarkenForPanel(Dominant);
	EXPECT_LT(Darkened.m_R, Dominant.m_R);
}

TEST(AetherMusicPlayer, ArtworkInactivity)
{
	EXPECT_EQ(ArtworkState(EPlaybackState::PLAYING, true, 1000, 2000), EArtworkState::FULL);
	EXPECT_EQ(ArtworkState(EPlaybackState::PAUSED, true, 1000, 2000), EArtworkState::DIMMED);
	EXPECT_EQ(ArtworkState(EPlaybackState::STOPPED, true, 1000, 1000 + 5 * 60 * 1000), EArtworkState::FALLBACK);
	EXPECT_EQ(ArtworkState(EPlaybackState::PLAYING, false, 1000, 2000), EArtworkState::FALLBACK);
}

TEST(AetherMusicPlayer, EffectivePlaybackUsesRecentProcessAudio)
{
	EXPECT_EQ(EffectivePlaybackState(EPlaybackState::PAUSED, 1000, 1400), EPlaybackState::PLAYING);
	EXPECT_EQ(EffectivePlaybackState(EPlaybackState::PAUSED, 1000, 1501), EPlaybackState::PAUSED);
	EXPECT_EQ(EffectivePlaybackState(EPlaybackState::PLAYING, 0, 5000), EPlaybackState::PLAYING);
}

TEST(AetherMusicPlayer, MediaDisplayNamePrefersMetadata)
{
	EXPECT_EQ(MediaDisplayName("Song", "Artist", "Spotify"), "Song - Artist");
	EXPECT_EQ(MediaDisplayName("Song", "", "Spotify"), "Song");
	EXPECT_EQ(MediaDisplayName(" Same ", "Same", "Spotify"), "Same");
	EXPECT_EQ(MediaDisplayName("", "", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"), "Spotify");
	EXPECT_EQ(MediaDisplayName("\xF0\x9D\x90\x8C\xF0\x9D\x90\x84\xF0\x9D\x90\x93\xF0\x9D\x90\x87\xF0\x9D\x90\x8E\xF0\x9D\x90\x97\xF0\x9D\x90\x88\xF0\x9D\x90\x83\xF0\x9D\x90\x84", "", "Spotify"), "METHOXIDE");
	EXPECT_TRUE(MediaDisplayName("", "", "   ").empty());
}

TEST(AetherMusicPlayer, MarqueeOffsetWaitsThenWraps)
{
	EXPECT_FLOAT_EQ(MarqueeOffset(40.0f, 80.0f, 5000), 0.0f);
	EXPECT_FLOAT_EQ(MarqueeOffset(120.0f, 80.0f, 1000), 0.0f);
	EXPECT_GT(MarqueeOffset(120.0f, 80.0f, 2200), 0.0f);
	EXPECT_NEAR(MarqueeOffset(120.0f, 80.0f, 1200 + 7000), MarqueeOffset(120.0f, 80.0f, 1200 + 7000 + 7778), 0.02f);
}

TEST(AetherMusicPlayer, ResizeScaleKeepsAspectAndLimits)
{
	EXPECT_EQ(ResizeScalePercent(10.0f, 20.0f, 102.0f, 38.0f, 92.0f, 18.0f), 100);
	EXPECT_EQ(ResizeScalePercent(10.0f, 20.0f, 194.0f, 56.0f, 92.0f, 18.0f), 200);
	EXPECT_EQ(ResizeScalePercent(10.0f, 20.0f, 20.0f, 22.0f, 92.0f, 18.0f), 50);
}

TEST(AetherMusicPlayer, LayoutSearchAndAccordion)
{
	const SRect Rect = ClampTopCenter(300.0f, 200.0f, 92.0f, 18.0f, 1000.0f, -1000.0f);
	EXPECT_FLOAT_EQ(Rect.m_X, 208.0f);
	EXPECT_FLOAT_EQ(Rect.m_Y, 0.0f);

	const std::array<const char *, 2> apChildren = {"Visualizer sensitivity", "Background color"};
	EXPECT_TRUE(SearchMatches("music", "Music Player", apChildren));
	EXPECT_TRUE(SearchMatches("SENSITIVITY", "Music Player", apChildren));
	EXPECT_FALSE(SearchMatches("editor", "Music Player", apChildren));

	EXPECT_EQ(ToggleAccordion(EAetherFeatureId::NONE, EAetherFeatureId::MUSIC_PLAYER), EAetherFeatureId::MUSIC_PLAYER);
	EXPECT_EQ(ToggleAccordion(EAetherFeatureId::MUSIC_PLAYER, EAetherFeatureId::MUSIC_PLAYER), EAetherFeatureId::NONE);
	EXPECT_EQ(ToggleAccordion(EAetherFeatureId::MUSIC_PLAYER, EAetherFeatureId::AUTO_TEAM_LOCK), EAetherFeatureId::AUTO_TEAM_LOCK);
}
