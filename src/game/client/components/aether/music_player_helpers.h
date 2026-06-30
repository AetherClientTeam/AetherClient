#ifndef GAME_CLIENT_COMPONENTS_AETHER_MUSIC_PLAYER_HELPERS_H
#define GAME_CLIENT_COMPONENTS_AETHER_MUSIC_PLAYER_HELPERS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace AetherMusic
{
enum class EPlaybackState
{
	UNAVAILABLE,
	STOPPED,
	PAUSED,
	PLAYING,
};

enum class EArtworkState
{
	FALLBACK,
	DIMMED,
	FULL,
};

enum class EAetherFeatureId
{
	NONE,
	MUSIC_PLAYER,
	AETHER_MENU_THEME,
	CUSTOM_MENU_THEME,
	KEYSTROKES,
	INPUT_VISUALIZER,
	STABILITY_TRAINER,
	SESSION_STATS,
	REAL_HITBOX,
	NINJA_TEE_PREVIEW,
	NINJA_TIMER,
	TEAM_OVERLAYS,
	SWEAT_WEAPON,
	ORBIT_AURA,
	JELLY_TEE,
	FINISH_PREDICTION,
	THREE_D_PARTICLES,
	LOADING_THEME_BACKGROUND,
	CLIENT_BADGES,
	PING_WHEEL,
	CHAT_BUBBLES,
	BLOCK_AWARENESS,
	FOCUS_MODE,
	SNAP_TAP,
	GORES_MODE,
	DDRACE_CONFIGS,
	FAST_INPUT,
	FAST_SPEC,
	TRANSLATOR,
	SILENT_TYPING,
	FAIL_SOUND,
	SOUND,
	KEYBOARD_SOUND,
	AUTO_TEAM_LOCK,
	AETHER_UI_SCALE,
	GRADIENT_TEAM_COLORS,
	BROWSER_UTILS,
	CUSTOM_RESOLUTION,
	SAVE_UNSENT_MESSAGES,
	ROLLBACK_DEMO,
	AIM_TRAINING,
	PSA,
	VAULT_CFG,
	CLOUD_CLAN,
	GORES_MAPS,
	ASSETS_EDITOR,
	MAP_BACKGROUND_BUILDER,
	MEDIA_BACKGROUND,
	OPTIMIZER,
	HUD_EDITOR,
};

struct STimerInput
{
	int m_GameTick = 0;
	int m_TickSpeed = 50;
	int m_RoundStartTick = 0;
	int m_TimeLimitMinutes = 0;
	int m_WarmupTimer = 0;
	bool m_GameOver = false;
	bool m_RaceTime = false;
	bool m_SuddenDeath = false;
};

struct STimerModel
{
	bool m_Visible = false;
	bool m_Urgent = false;
	int m_Seconds = 0;
	std::string m_Text;
};

struct SRect
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
};

struct SColor
{
	float m_R = 0.0f;
	float m_G = 0.0f;
	float m_B = 0.0f;
};

STimerModel TimerModel(const STimerInput &Input);
std::string FormatTimer(int Seconds);
std::array<float, 5> FiveBands(std::span<const float> Samples, int SampleRate, int Sensitivity);
SColor DominantColor(std::span<const uint8_t> RgbaPixels);
SColor DarkenForPanel(SColor Color);
EArtworkState ArtworkState(EPlaybackState PlaybackState, bool HasArtwork, int64_t LastPlayingMs, int64_t NowMs);
EPlaybackState EffectivePlaybackState(EPlaybackState MediaState, int64_t LastAudioMs, int64_t NowMs);
std::string MediaDisplayName(const std::string &Title, const std::string &Artist, const std::string &Source);
float MarqueeOffset(float TextWidth, float ViewWidth, int64_t ElapsedMs);
int ResizeScalePercent(float AnchorX, float AnchorY, float MouseX, float MouseY, float BaseWidth, float BaseHeight);
SRect ClampTopCenter(float ScreenWidth, float ScreenHeight, float Width, float Height, float OffsetX, float OffsetY);
bool SearchMatches(const char *pSearch, const char *pFeatureLabel, std::span<const char *const> ChildLabels);
EAetherFeatureId ToggleAccordion(EAetherFeatureId Expanded, EAetherFeatureId Clicked);
}

#endif
