#include "browser_utils_helpers.h"

#include <base/system.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace
{
std::string LowerAscii(std::string_view Text)
{
	std::string Result;
	Result.reserve(Text.size());
	for(unsigned char C : Text)
		Result.push_back((char)std::tolower(C));
	return Result;
}

std::string UpperAscii(std::string_view Text)
{
	std::string Result;
	Result.reserve(Text.size());
	for(unsigned char C : Text)
		Result.push_back((char)std::toupper(C));
	return Result;
}

std::vector<std::string> Tokens(const char *pName)
{
	std::vector<std::string> vTokens;
	std::string Current;
	for(const unsigned char C : std::string_view(pName ? pName : ""))
	{
		if(std::isalnum(C))
		{
			Current.push_back((char)std::tolower(C));
		}
		else if(!Current.empty())
		{
			vTokens.push_back(Current);
			Current.clear();
		}
	}
	if(!Current.empty())
		vTokens.push_back(Current);
	return vTokens;
}

bool HasDigit(std::string_view Text)
{
	return std::any_of(Text.begin(), Text.end(), [](unsigned char C) { return std::isdigit(C) != 0; });
}

bool IsRegionToken(std::string_view Token)
{
	if(Token.size() < 2 || Token.size() > 8)
		return false;
	if(HasDigit(Token))
		return std::any_of(Token.begin(), Token.end(), [](unsigned char C) { return std::isalpha(C) != 0; });
	if(Token.size() < 2 || Token.size() > 4)
		return false;
	return std::all_of(Token.begin(), Token.end(), [](unsigned char C) { return std::isalpha(C) != 0; });
}

std::string TitleDifficulty(const char *pDifficulty)
{
	std::string Result = pDifficulty;
	bool WordStart = true;
	for(char &C : Result)
	{
		if(C == ' ')
		{
			WordStart = true;
			continue;
		}
		C = WordStart ? (char)std::toupper((unsigned char)C) : (char)std::tolower((unsigned char)C);
		WordStart = false;
	}
	return Result;
}
}

namespace AetherBrowserUtils
{
std::string ShortKoGServerName(const char *pName)
{
	if(!pName || pName[0] == '\0')
		return {};

	const std::string Lower = LowerAscii(pName);
	if(Lower.find("kog") == std::string::npos)
		return pName;

	const std::vector<std::string> vTokens = Tokens(pName);
	std::string Region;
	for(const std::string &Token : vTokens)
	{
		if(Token == "kog" || Token == "a" || Token == "tw")
			continue;
		if(IsRegionToken(Token))
		{
			Region = Token;
			break;
		}
	}

	std::string State;
	if(Lower.find("maintaince") != std::string::npos || Lower.find("maintenance") != std::string::npos)
		State = "Maintenance";
	else if(Lower.find("test") != std::string::npos)
		State = "Test";
	else if(Lower.find("main gores") != std::string::npos)
		State = "Main";
	else if(Lower.find("mods gores") != std::string::npos || Lower.find("mod gores") != std::string::npos)
		State = "Mods";
	else if(Lower.find("extreme gores") != std::string::npos)
		State = "Extreme";
	else
	{
		static constexpr const char *s_apDifficulties[] = {
			"novice", "moderate", "brutal", "insane", "dummy", "ddmax", "oldschool", "solo", "race", "easy", "hard", "mods", "main", "extreme"};
		for(const char *pDifficulty : s_apDifficulties)
		{
			if(Lower.find(pDifficulty) != std::string::npos)
			{
				State = TitleDifficulty(pDifficulty);
				break;
			}
		}
	}

	if(Region.empty() || State.empty())
		return pName;
	return UpperAscii(Region) + " - " + State;
}
}
