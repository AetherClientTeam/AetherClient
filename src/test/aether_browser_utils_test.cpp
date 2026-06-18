#include <game/client/components/aether/browser_utils_helpers.h>

#include <gtest/gtest.h>

TEST(AetherBrowserUtils, ShortKoGServerNames)
{
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("KoG | GER4 - Insane"), "GER4 - Insane");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("[KoG] GER2 Test Server"), "GER2 - Test");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("KoG GER7 Maintaince"), "GER7 - Maintenance");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("KoG GER1 Maintenance"), "GER1 - Maintenance");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("[A] |*KoG*| GER3 #3 - Main Gores [kog.tw]"), "GER3 - Main");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("[A] |*KoG*| GER3 #8 - Extreme Gores [kog.tw]"), "GER3 - Extreme");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("[A] |*KoG*| GER5 #24 - Mods Gores [kog.tw]"), "GER5 - Mods");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("[A] |*KoG*| USA #2 - Easy Gores [kog.tw]"), "USA - Easy");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("|*KoG*| BRA #6 - Hard Gores [kog.tw]"), "BRA - Hard");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("|*KoG*| BRA #2 - Insane Gores [kog.tw]"), "BRA - Insane");
}

TEST(AetherBrowserUtils, LeavesUnclearNamesUntouched)
{
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("DDNet GER2"), "DDNet GER2");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("KoG Insane"), "KoG Insane");
	EXPECT_EQ(AetherBrowserUtils::ShortKoGServerName("KoG GER4"), "KoG GER4");
}
