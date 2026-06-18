#include "music_player_helpers.h"

#include <base/str.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace AetherMusic
{
std::string FormatTimer(int Seconds)
{
	Seconds = std::max(Seconds, 0);
	const int Days = Seconds / 86400;
	const int Hours = (Seconds / 3600) % 24;
	const int Minutes = (Seconds / 60) % 60;
	const int RemainingSeconds = Seconds % 60;
	char aBuffer[32];
	if(Days > 0)
		std::snprintf(aBuffer, sizeof(aBuffer), "%dd %02d:%02d:%02d", Days, Hours, Minutes, RemainingSeconds);
	else if(Seconds >= 3600)
		std::snprintf(aBuffer, sizeof(aBuffer), "%02d:%02d:%02d", Seconds / 3600, Minutes, RemainingSeconds);
	else
		std::snprintf(aBuffer, sizeof(aBuffer), "%02d:%02d", Minutes, RemainingSeconds);
	return aBuffer;
}

STimerModel TimerModel(const STimerInput &Input)
{
	STimerModel Result;
	if(Input.m_SuddenDeath || Input.m_TickSpeed <= 0)
		return Result;

	if(Input.m_TimeLimitMinutes > 0 && Input.m_WarmupTimer <= 0)
	{
		Result.m_Seconds = Input.m_TimeLimitMinutes * 60 - (Input.m_GameTick - Input.m_RoundStartTick) / Input.m_TickSpeed;
		if(Input.m_GameOver)
			Result.m_Seconds = 0;
		Result.m_Urgent = Result.m_Seconds <= 60;
	}
	else if(Input.m_RaceTime)
		Result.m_Seconds = (Input.m_GameTick + Input.m_WarmupTimer) / Input.m_TickSpeed;
	else
		Result.m_Seconds = (Input.m_GameTick - Input.m_RoundStartTick) / Input.m_TickSpeed;

	Result.m_Seconds = std::max(Result.m_Seconds, 0);
	Result.m_Text = FormatTimer(Result.m_Seconds);
	Result.m_Visible = true;
	return Result;
}

std::array<float, 5> FiveBands(std::span<const float> Samples, int SampleRate, int Sensitivity)
{
	std::array<float, 5> Result{};
	if(Samples.empty() || SampleRate <= 0)
		return Result;

	static constexpr std::array<float, 6> s_aEdges = {30.0f, 115.0f, 260.0f, 800.0f, 3000.0f, 14000.0f};
	for(size_t Band = 0; Band < Result.size(); ++Band)
	{
		float Sum = 0.0f;
		int Frequencies = 0;
		const size_t Count = std::min<size_t>(Samples.size(), Band < 2 ? 2048 : 1024);
		const int Steps = Band < 2 ? 8 : 4;
		for(int Step = 0; Step < Steps; ++Step)
		{
			const float T = (Step + 0.5f) / (float)Steps;
			const float Frequency = s_aEdges[Band] * std::pow(s_aEdges[Band + 1] / s_aEdges[Band], T);
			if(Frequency >= SampleRate * 0.5f)
				continue;
			float Real = 0.0f;
			float Imaginary = 0.0f;
			for(size_t i = 0; i < Count; ++i)
			{
				const float Window = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / std::max<size_t>(Count - 1, 1));
				const float Angle = 2.0f * 3.14159265358979323846f * Frequency * i / SampleRate;
				Real += Samples[i] * Window * std::cos(Angle);
				Imaginary -= Samples[i] * Window * std::sin(Angle);
			}
			Sum += std::sqrt(Real * Real + Imaginary * Imaginary) / Count;
			++Frequencies;
		}
		if(Frequencies > 0)
		{
			const float SensitivityScale = std::pow(std::clamp(Sensitivity, 50, 1500) / 100.0f, 0.72f);
			const float BandGain = Band < 2 ? 9.6f : 24.0f;
			const float Compression = Band < 2 ? 1.10f : 2.75f;
			const float Denominator = Band < 2 ? std::log(8.0f) : std::log(9.5f);
			const float Scaled = Sum / Frequencies * SensitivityScale * BandGain;
			Result[Band] = std::clamp(std::log1p(std::max(Scaled, 0.0f) * Compression) / Denominator, 0.0f, 1.65f);
		}
	}
	return Result;
}

SColor DominantColor(std::span<const uint8_t> RgbaPixels)
{
	struct SBucket
	{
		uint64_t m_R = 0;
		uint64_t m_G = 0;
		uint64_t m_B = 0;
		uint64_t m_Count = 0;
	};
	std::unordered_map<uint16_t, SBucket> Buckets;
	for(size_t i = 0; i + 3 < RgbaPixels.size(); i += 4)
	{
		const uint8_t R = RgbaPixels[i];
		const uint8_t G = RgbaPixels[i + 1];
		const uint8_t B = RgbaPixels[i + 2];
		const uint8_t A = RgbaPixels[i + 3];
		if(A < 64 || std::max({R, G, B}) < 20)
			continue;
		const uint16_t Key = ((R >> 6) << 4) | ((G >> 6) << 2) | (B >> 6);
		auto &Bucket = Buckets[Key];
		Bucket.m_R += R;
		Bucket.m_G += G;
		Bucket.m_B += B;
		++Bucket.m_Count;
	}
	const SBucket *pBest = nullptr;
	for(const auto &[Key, Bucket] : Buckets)
	{
		(void)Key;
		if(!pBest || Bucket.m_Count > pBest->m_Count)
			pBest = &Bucket;
	}
	if(!pBest || pBest->m_Count == 0)
		return {0.10f, 0.12f, 0.16f};
	return {
		(float)pBest->m_R / pBest->m_Count / 255.0f,
		(float)pBest->m_G / pBest->m_Count / 255.0f,
		(float)pBest->m_B / pBest->m_Count / 255.0f};
}

SColor DarkenForPanel(SColor Color)
{
	const float Luminance = 0.2126f * Color.m_R + 0.7152f * Color.m_G + 0.0722f * Color.m_B;
	const float Scale = Luminance > 0.45f ? 0.30f : 0.45f;
	return {std::clamp(Color.m_R * Scale, 0.03f, 0.32f), std::clamp(Color.m_G * Scale, 0.03f, 0.32f), std::clamp(Color.m_B * Scale, 0.03f, 0.32f)};
}

EArtworkState ArtworkState(EPlaybackState PlaybackState, bool HasArtwork, int64_t LastPlayingMs, int64_t NowMs)
{
	if(!HasArtwork || LastPlayingMs <= 0 || NowMs - LastPlayingMs >= 5 * 60 * 1000)
		return EArtworkState::FALLBACK;
	if(PlaybackState == EPlaybackState::PLAYING)
		return EArtworkState::FULL;
	return EArtworkState::DIMMED;
}

EPlaybackState EffectivePlaybackState(EPlaybackState MediaState, int64_t LastAudioMs, int64_t NowMs)
{
	if(MediaState == EPlaybackState::PLAYING || (LastAudioMs > 0 && NowMs - LastAudioMs <= 500))
		return EPlaybackState::PLAYING;
	return MediaState;
}

int ResizeScalePercent(float AnchorX, float AnchorY, float MouseX, float MouseY, float BaseWidth, float BaseHeight)
{
	if(BaseWidth <= 0.0f || BaseHeight <= 0.0f)
		return 100;
	const float HorizontalScale = (MouseX - AnchorX) / BaseWidth;
	const float VerticalScale = (MouseY - AnchorY) / BaseHeight;
	return std::clamp((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), 50, 200);
}

SRect ClampTopCenter(float ScreenWidth, float ScreenHeight, float Width, float Height, float OffsetX, float OffsetY)
{
	const float X = std::clamp(ScreenWidth * 0.5f - Width * 0.5f + OffsetX, 0.0f, std::max(ScreenWidth - Width, 0.0f));
	const float Y = std::clamp(2.0f + OffsetY, 0.0f, std::max(ScreenHeight - Height, 0.0f));
	return {X, Y, Width, Height};
}

bool SearchMatches(const char *pSearch, const char *pFeatureLabel, std::span<const char *const> ChildLabels)
{
	if(!pSearch || pSearch[0] == '\0')
		return true;
	if(str_utf8_find_nocase(pFeatureLabel, pSearch))
		return true;
	return std::any_of(ChildLabels.begin(), ChildLabels.end(), [pSearch](const char *pLabel) {
		return str_utf8_find_nocase(pLabel, pSearch) != nullptr;
	});
}

EAetherFeatureId ToggleAccordion(EAetherFeatureId Expanded, EAetherFeatureId Clicked)
{
	return Expanded == Clicked ? EAetherFeatureId::NONE : Clicked;
}
}
