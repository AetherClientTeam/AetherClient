#include "music_player_helpers.h"

#include <base/str.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace AetherMusic
{
namespace
{
std::string TrimCopy(const std::string &Value)
{
	size_t Begin = 0;
	while(Begin < Value.size() && std::isspace(static_cast<unsigned char>(Value[Begin])))
		++Begin;
	size_t End = Value.size();
	while(End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1])))
		--End;
	return Value.substr(Begin, End - Begin);
}

std::string CleanSourceName(std::string Source)
{
	Source = TrimCopy(Source);
	if(Source.empty())
		return {};

	const size_t Bang = Source.find_last_of('!');
	if(Bang != std::string::npos && Bang + 1 < Source.size())
		Source = Source.substr(Bang + 1);
	else
	{
		const size_t Slash = Source.find_last_of("\\/");
		if(Slash != std::string::npos && Slash + 1 < Source.size())
			Source = Source.substr(Slash + 1);
		const size_t Dot = Source.find_last_of('.');
		if(Dot != std::string::npos && Dot + 1 < Source.size())
			Source = Source.substr(Dot + 1);
	}
	return TrimCopy(Source);
}

bool AppendAsciiForCodepoint(std::string &Out, int Codepoint)
{
	if(Codepoint >= 32 && Codepoint <= 126)
	{
		Out.push_back((char)Codepoint);
		return true;
	}
	if(Codepoint == '\t' || Codepoint == '\n' || Codepoint == '\r' || Codepoint == 0x00A0)
	{
		Out.push_back(' ');
		return true;
	}
	if(Codepoint >= 0xFF01 && Codepoint <= 0xFF5E)
	{
		Out.push_back((char)(Codepoint - 0xFEE0));
		return true;
	}

	struct SRange
	{
		int m_Start;
		int m_Count;
		char m_Base;
	};
	static constexpr SRange s_aMathAlnumRanges[] = {
		{0x1D400, 26, 'A'}, {0x1D41A, 26, 'a'},
		{0x1D434, 26, 'A'}, {0x1D44E, 26, 'a'},
		{0x1D468, 26, 'A'}, {0x1D482, 26, 'a'},
		{0x1D4D0, 26, 'A'}, {0x1D4EA, 26, 'a'},
		{0x1D56C, 26, 'A'}, {0x1D586, 26, 'a'},
		{0x1D5A0, 26, 'A'}, {0x1D5BA, 26, 'a'},
		{0x1D5D4, 26, 'A'}, {0x1D5EE, 26, 'a'},
		{0x1D608, 26, 'A'}, {0x1D622, 26, 'a'},
		{0x1D63C, 26, 'A'}, {0x1D656, 26, 'a'},
		{0x1D670, 26, 'A'}, {0x1D68A, 26, 'a'},
		{0x1D7CE, 10, '0'}, {0x1D7D8, 10, '0'},
		{0x1D7E2, 10, '0'}, {0x1D7EC, 10, '0'},
		{0x1D7F6, 10, '0'},
	};
	for(const SRange &Range : s_aMathAlnumRanges)
	{
		if(Codepoint >= Range.m_Start && Codepoint < Range.m_Start + Range.m_Count)
		{
			Out.push_back((char)(Range.m_Base + Codepoint - Range.m_Start));
			return true;
		}
	}

	switch(Codepoint)
	{
	case 0x2010:
	case 0x2011:
	case 0x2012:
	case 0x2013:
	case 0x2014:
	case 0x2212:
		Out.push_back('-');
		return true;
	case 0x2018:
	case 0x2019:
	case 0x02BC:
		Out.push_back('\'');
		return true;
	case 0x201C:
	case 0x201D:
		Out.push_back('"');
		return true;
	case 0x00C0:
	case 0x00C1:
	case 0x00C2:
	case 0x00C3:
	case 0x00C4:
	case 0x00C5:
	case 0x0100:
	case 0x0102:
	case 0x0104:
		Out.push_back('A');
		return true;
	case 0x00E0:
	case 0x00E1:
	case 0x00E2:
	case 0x00E3:
	case 0x00E4:
	case 0x00E5:
	case 0x0101:
	case 0x0103:
	case 0x0105:
		Out.push_back('a');
		return true;
	case 0x00C7:
	case 0x0106:
	case 0x010C:
		Out.push_back('C');
		return true;
	case 0x00E7:
	case 0x0107:
	case 0x010D:
		Out.push_back('c');
		return true;
	case 0x00D0:
		Out.push_back('D');
		return true;
	case 0x00F0:
		Out.push_back('d');
		return true;
	case 0x00C8:
	case 0x00C9:
	case 0x00CA:
	case 0x00CB:
	case 0x0112:
	case 0x0118:
		Out.push_back('E');
		return true;
	case 0x00E8:
	case 0x00E9:
	case 0x00EA:
	case 0x00EB:
	case 0x0113:
	case 0x0119:
		Out.push_back('e');
		return true;
	case 0x011E:
		Out.push_back('G');
		return true;
	case 0x011F:
		Out.push_back('g');
		return true;
	case 0x00CC:
	case 0x00CD:
	case 0x00CE:
	case 0x00CF:
	case 0x0130:
	case 0x012A:
		Out.push_back('I');
		return true;
	case 0x00EC:
	case 0x00ED:
	case 0x00EE:
	case 0x00EF:
	case 0x0131:
	case 0x012B:
		Out.push_back('i');
		return true;
	case 0x00D1:
		Out.push_back('N');
		return true;
	case 0x00F1:
		Out.push_back('n');
		return true;
	case 0x00D2:
	case 0x00D3:
	case 0x00D4:
	case 0x00D5:
	case 0x00D6:
	case 0x00D8:
	case 0x014C:
		Out.push_back('O');
		return true;
	case 0x00F2:
	case 0x00F3:
	case 0x00F4:
	case 0x00F5:
	case 0x00F6:
	case 0x00F8:
	case 0x014D:
		Out.push_back('o');
		return true;
	case 0x015E:
	case 0x0160:
		Out.push_back('S');
		return true;
	case 0x015F:
	case 0x0161:
		Out.push_back('s');
		return true;
	case 0x00D9:
	case 0x00DA:
	case 0x00DB:
	case 0x00DC:
	case 0x016A:
		Out.push_back('U');
		return true;
	case 0x00F9:
	case 0x00FA:
	case 0x00FB:
	case 0x00FC:
	case 0x016B:
		Out.push_back('u');
		return true;
	case 0x00DD:
	case 0x0178:
		Out.push_back('Y');
		return true;
	case 0x00FD:
	case 0x00FF:
		Out.push_back('y');
		return true;
	case 0x00DF:
		Out.append("ss");
		return true;
	case 0x00C6:
		Out.append("AE");
		return true;
	case 0x00E6:
		Out.append("ae");
		return true;
	default:
		return false;
	}
}

std::string SanitizeDisplayText(const std::string &Value)
{
	std::string Result;
	Result.reserve(Value.size());
	const char *pCursor = Value.c_str();
	while(*pCursor)
	{
		const char *pBefore = pCursor;
		const int Codepoint = str_utf8_decode(&pCursor);
		if(Codepoint <= 0)
		{
			pCursor = pBefore + 1;
			continue;
		}
		AppendAsciiForCodepoint(Result, Codepoint);
	}

	std::string Trimmed = TrimCopy(Result);
	std::string Collapsed;
	Collapsed.reserve(Trimmed.size());
	bool LastWasSpace = false;
	for(char Character : Trimmed)
	{
		const bool IsSpace = std::isspace(static_cast<unsigned char>(Character)) != 0;
		if(IsSpace)
		{
			if(!LastWasSpace)
				Collapsed.push_back(' ');
		}
		else
			Collapsed.push_back(Character);
		LastWasSpace = IsSpace;
	}
	return Collapsed;
}
}

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
		std::snprintf(aBuffer, sizeof(aBuffer), "%d:%02d", Minutes, RemainingSeconds);
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

std::string MediaDisplayName(const std::string &Title, const std::string &Artist, const std::string &Source)
{
	const std::string CleanTitle = SanitizeDisplayText(Title);
	const std::string CleanArtist = SanitizeDisplayText(Artist);
	if(!CleanTitle.empty() && !CleanArtist.empty() && CleanTitle != CleanArtist)
		return CleanTitle + " - " + CleanArtist;
	if(!CleanTitle.empty())
		return CleanTitle;
	return SanitizeDisplayText(CleanSourceName(Source));
}

float MarqueeOffset(float TextWidth, float ViewWidth, int64_t ElapsedMs)
{
	if(TextWidth <= ViewWidth || TextWidth <= 0.0f || ViewWidth <= 0.0f)
		return 0.0f;
	constexpr int64_t DelayMs = 1200;
	constexpr float Speed = 18.0f;
	constexpr float Gap = 20.0f;
	if(ElapsedMs <= DelayMs)
		return 0.0f;
	const float CycleWidth = TextWidth + Gap;
	return std::fmod((ElapsedMs - DelayMs) * Speed / 1000.0f, CycleWidth);
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
	if(!str_utf8_check(pSearch))
		return false;
	if(pFeatureLabel && str_utf8_find_nocase(pFeatureLabel, pSearch))
		return true;
	return std::any_of(ChildLabels.begin(), ChildLabels.end(), [pSearch](const char *pLabel) {
		return pLabel && str_utf8_find_nocase(pLabel, pSearch) != nullptr;
	});
}

EAetherFeatureId ToggleAccordion(EAetherFeatureId Expanded, EAetherFeatureId Clicked)
{
	return Expanded == Clicked ? EAetherFeatureId::NONE : Clicked;
}
}
