/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

#if defined(CONF_AUTOUPDATE)
	if(!m_AutoUpdateCheckStarted)
	{
		m_AutoUpdateCheckStarted = true;
		Updater()->CheckForUpdate();
	}
#endif

	auto LoadCoreTexture = [&](const char *pPath) {
		IGraphics::CTextureHandle Texture = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL);
		if(Texture.IsValid() || !str_startswith(pPath, "core/"))
			return Texture;
		char aFallback[256];
		str_format(aFallback, sizeof(aFallback), "aether/%s", pPath + str_length("core/"));
		return Graphics()->LoadTexture(aFallback, IStorage::TYPE_ALL);
	};

	const ColorRGBA VeraAccent(0.88f, 0.50f, 1.0f, 0.70f);
	const ColorRGBA VeraAccentSoft(0.74f, 0.56f, 1.0f, 0.58f);
	const float StartLogoBand = std::clamp(MainView.h * 0.22f, 94.0f, 150.0f);

	// render logo
	static bool s_AetherStartLogoLoaded = false;
	static IGraphics::CTextureHandle s_AetherStartLogoTexture;
	if(!s_AetherStartLogoLoaded)
	{
		s_AetherStartLogoTexture = LoadCoreTexture("core/logos/aether_vera_big_1024.png");
		s_AetherStartLogoLoaded = true;
	}
	const float Rounding = 16.0f;
	int NewPage = -1;
	static int64_t s_StartIntroTime = 0;
	if(s_StartIntroTime == 0)
		s_StartIntroTime = time_get();
	float IntroAlpha = 1.0f;
	if(!(g_Config.m_AeOptimizer && g_Config.m_AeOptimizerDisableMenuAnimations))
	{
		const float IntroProgress = std::clamp((time_get() - s_StartIntroTime) / (float)time_freq(), 0.0f, 0.45f) / 0.45f;
		IntroAlpha = IntroProgress * IntroProgress * (3.0f - 2.0f * IntroProgress);
	}

	auto FadeColor = [&](ColorRGBA Color) {
		Color.a *= IntroAlpha;
		return Color;
	};

	auto DoModernCard = [&](CButtonContainer *pId, const char *pTitle, const char *pSubtitle, const char *pHotkey, CUIRect Rect, ColorRGBA Accent, bool Highlight = false) {
		const bool Hot = Ui()->HotItem() == pId;
		ColorRGBA Border = Hot ? Accent : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
		Border.a *= Ui()->ButtonColorMul(pId) * IntroAlpha;
		Rect.Draw(Border, IGraphics::CORNER_ALL, Rounding);

		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		ColorRGBA CardColor = Hot && Highlight ? ColorRGBA(0.17f, 0.08f, 0.22f, 0.78f) : ColorRGBA(0.01f, 0.02f, 0.04f, 0.62f);
		CardColor.a *= Ui()->ButtonColorMul(pId) * IntroAlpha;
		Inner.Draw(CardColor, IGraphics::CORNER_ALL, Rounding - 1.5f);
		if(Hot)
		{
			CUIRect Glow = Inner;
			Glow.Margin(3.0f, &Glow);
			Glow.Draw(FadeColor(ColorRGBA(0.96f, 0.50f, 1.0f, Highlight ? 0.13f : 0.085f)), IGraphics::CORNER_ALL, Rounding - 4.0f);
			CUIRect EdgeGlow = Glow;
			EdgeGlow.HSplitTop(Glow.h * 0.34f, &EdgeGlow, nullptr);
			EdgeGlow.Draw(FadeColor(ColorRGBA(1.0f, 0.74f, 1.0f, 0.055f)), IGraphics::CORNER_T, Rounding - 4.0f);
		}

		CUIRect Text = Inner;
		Text.Margin(12.0f, &Text);
		CUIRect Hotkey, Title, Subtitle;
		Text.VSplitRight(42.0f, &Text, &Hotkey);
		Hotkey.HSplitTop(23.0f, &Hotkey, nullptr);
		Hotkey.Draw(FadeColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f)), IGraphics::CORNER_ALL, 8.0f);
		Ui()->DoLabel(&Hotkey, pHotkey, 11.0f, TEXTALIGN_MC);

		const bool ShowSubtitle = Rect.h >= 68.0f && Text.w >= 150.0f;
		Text.HSplitTop(ShowSubtitle ? (Rect.h > 92.0f ? 42.0f : 31.0f) : Text.h, &Title, &Subtitle);
		const float TitleSize = std::clamp(Rect.h * 0.30f, 16.0f, Rect.h > 92.0f ? 25.0f : 22.0f);
		Ui()->DoLabel(&Title, pTitle, TitleSize, TEXTALIGN_MC, {.m_MaxWidth = Text.w});
		if(ShowSubtitle)
			Ui()->DoLabel(&Subtitle, pSubtitle, 11.0f, TEXTALIGN_TC, {.m_MaxWidth = Text.w});
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};

	auto DoCompactButton = [&](CButtonContainer *pId, const char *pText, CUIRect Rect, ColorRGBA Accent, ColorRGBA Base = ColorRGBA(1.0f, 1.0f, 1.0f, 0.045f), ColorRGBA TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)) {
		const bool Hot = Ui()->HotItem() == pId;
		Rect.Draw(FadeColor(Hot ? Accent : Base), IGraphics::CORNER_ALL, 12.0f);
		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		Inner.Draw(FadeColor(ColorRGBA(0.01f, 0.02f, 0.04f, 0.62f)), IGraphics::CORNER_ALL, 10.5f);
		TextRender()->TextColor(TextColor);
		Ui()->DoLabel(&Inner, pText, 16.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};

	CUIRect Content = MainView;
	const float OuterMargin = std::clamp(MainView.w * 0.03f, 14.0f, 34.0f);
	Content.Margin(OuterMargin, &Content);
	Content.HSplitTop(StartLogoBand, nullptr, &Content);
	Content.HSplitBottom(std::clamp(MainView.h * 0.07f, 28.0f, 54.0f), &Content, nullptr);

	CUIRect Cluster = Content;
	const float ClusterWidth = std::min(Content.w, 720.0f);
	const float ClusterHeight = std::min(Content.h, 500.0f);
	Cluster.VMargin(std::max(0.0f, (Cluster.w - ClusterWidth) * 0.5f), &Cluster);
	Cluster.HMargin(std::max(0.0f, (Cluster.h - ClusterHeight) * 0.5f), &Cluster);

	CUIRect MainPanel = Cluster;

	if(s_AetherStartLogoTexture.IsValid())
	{
		const float LogoAspect = 256.0f / 1024.0f;
		const float LogoW = std::min({455.0f, MainView.w * 0.38f, MainPanel.w * 0.56f});
		const float LogoH = LogoW * LogoAspect;
		const float LogoGap = std::clamp(MainView.h * 0.004f, 2.0f, 6.0f);
		const float LogoY = std::max(8.0f, MainPanel.y - LogoH - LogoGap);
		Graphics()->TextureSet(s_AetherStartLogoTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, IntroAlpha);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		IGraphics::CQuadItem QuadItem(MainView.w / 2.0f - LogoW / 2.0f, LogoY, LogoW, LogoH);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}

	MainPanel.Draw(FadeColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f)), IGraphics::CORNER_ALL, 22.0f);
	CUIRect MainInner = MainPanel;
	MainInner.Margin(18.0f, &MainInner);

	CUIRect PanelHeader, GridArea, Secondary;
	MainInner.HSplitTop(44.0f, &PanelHeader, &GridArea);
	GridArea.HSplitTop(10.0f, nullptr, &GridArea);
	GridArea.HSplitBottom(48.0f, &GridArea, &Secondary);
	GridArea.HSplitBottom(12.0f, &GridArea, nullptr);

	CUIRect HeaderTitle;
	PanelHeader.HSplitTop(30.0f, &HeaderTitle, nullptr);
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateArea;
	HeaderTitle.VSplitRight(std::min(360.0f, HeaderTitle.w * 0.55f), &HeaderTitle, &UpdateArea);
	UpdateArea.VSplitLeft(14.0f, nullptr, &UpdateArea);
#endif
	char aVersionBuf[64];
	str_format(aVersionBuf, sizeof(aVersionBuf), "v%s", CLIENT_RELEASE_VERSION);
	SLabelProperties VersionLabelProps;
	VersionLabelProps.SetColor(ColorRGBA(0.92f, 0.78f, 1.0f, 1.0f));
	Ui()->DoLabel(&HeaderTitle, aVersionBuf, 11.0f, TEXTALIGN_ML, VersionLabelProps);
#if defined(CONF_AUTOUPDATE)
	{
		CUIRect UpdateStatus, UpdateButton;
		UpdateArea.VSplitRight(126.0f, &UpdateStatus, &UpdateButton);
		UpdateStatus.VSplitRight(8.0f, &UpdateStatus, nullptr);

		const IUpdater::EUpdaterState UpdateState = Updater()->GetCurrentState();
		char aStatus[128];
		Updater()->GetCurrentFile(aStatus, sizeof(aStatus));
		const int Percent = Updater()->GetCurrentPercent();

		const char *pButtonLabel = "Update";
		if(UpdateState == IUpdater::GETTING_MANIFEST)
			pButtonLabel = "Checking";
		else if(UpdateState == IUpdater::DOWNLOADING)
			pButtonLabel = "Updating";
		else if(UpdateState == IUpdater::NEED_RESTART)
			pButtonLabel = "Restart";
		else if(UpdateState == IUpdater::FAIL)
			pButtonLabel = "Retry";

		static CButtonContainer s_AetherHeaderUpdateButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_AetherHeaderUpdateButton, pButtonLabel, 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 8.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f)))
		{
			if(UpdateState == IUpdater::NEED_RESTART)
				Updater()->ApplyUpdateAndRestart();
			else if(UpdateState != IUpdater::GETTING_MANIFEST && UpdateState != IUpdater::DOWNLOADING)
				Updater()->InitiateUpdate();
		}

		char aBuf[96];
		if(UpdateState == IUpdater::DOWNLOADING)
			str_format(aBuf, sizeof(aBuf), "Downloading %d%%", Percent);
		else if(UpdateState == IUpdater::GETTING_MANIFEST)
			str_format(aBuf, sizeof(aBuf), "%s", "Checking");
		else if(UpdateState == IUpdater::UPDATE_AVAILABLE)
			str_format(aBuf, sizeof(aBuf), "%s", "New Update");
		else if(UpdateState == IUpdater::NEED_RESTART)
			str_format(aBuf, sizeof(aBuf), "%s", "Restart to finish");
		else if(UpdateState == IUpdater::FAIL)
			str_format(aBuf, sizeof(aBuf), "%s", aStatus[0] ? aStatus : "Update failed");
		else
			str_format(aBuf, sizeof(aBuf), "%s", aStatus[0] ? aStatus : "Latest");
		SLabelProperties UpdateLabelProps;
		const bool ShowUpdateAlert = UpdateState == IUpdater::UPDATE_AVAILABLE || UpdateState == IUpdater::DOWNLOADING || UpdateState == IUpdater::NEED_RESTART || UpdateState == IUpdater::FAIL;
		UpdateLabelProps.SetColor(ShowUpdateAlert ? ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f) : ColorRGBA(0.92f, 0.78f, 1.0f, 1.0f));
		Ui()->DoLabel(&UpdateStatus, aBuf, 11.0f, TEXTALIGN_MR, UpdateLabelProps);
	}
#endif

	CUIRect Bento = GridArea;

	static CButtonContainer s_PlayCard;
	static CButtonContainer s_DemoCard;
	static CButtonContainer s_EditorCard;
	static CButtonContainer s_SettingsCard;
	CUIRect PlayCard, DemoCard, EditorCard, SettingsCard;
	const bool SingleColumn = Bento.w < 520.0f;
	if(SingleColumn)
	{
		const float RowSpacing = 8.0f;
		const float RowHeight = std::max(58.0f, (Bento.h - RowSpacing * 3.0f) / 4.0f);
		Bento.HSplitTop(RowHeight, &PlayCard, &Bento);
		Bento.HSplitTop(RowSpacing, nullptr, &Bento);
		Bento.HSplitTop(RowHeight, &DemoCard, &Bento);
		Bento.HSplitTop(RowSpacing, nullptr, &Bento);
		Bento.HSplitTop(RowHeight, &EditorCard, &Bento);
		Bento.HSplitTop(RowSpacing, nullptr, &Bento);
		Bento.HSplitTop(RowHeight, &SettingsCard, &Bento);
	}
	else
	{
		CUIRect TopRow, BottomRow;
		Bento.HSplitTop((Bento.h - 12.0f) * 0.5f, &TopRow, &BottomRow);
		BottomRow.HSplitTop(12.0f, nullptr, &BottomRow);
		TopRow.VSplitMid(&PlayCard, &DemoCard, 6.0f);
		BottomRow.VSplitMid(&EditorCard, &SettingsCard, 6.0f);
	}

	if(DoModernCard(&s_PlayCard, Localize("Play", "Start menu"), Localize("Browse servers and jump in."), "P", PlayCard, VeraAccent, true) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	if(DoModernCard(&s_DemoCard, Localize("Demos"), Localize("Watch, trim and render demos."), "D", DemoCard, VeraAccentSoft) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;
	if(DoModernCard(&s_EditorCard, Localize("Editor"), Localize("Create and edit maps."), "E", EditorCard, ColorRGBA(0.96f, 0.58f, 1.0f, 0.50f), GameClient()->Editor()->HasUnsavedData()) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}
	if(DoModernCard(&s_SettingsCard, Localize("Settings"), Localize("Tune DDNet, TClient and Aether."), "S", SettingsCard, ColorRGBA(0.72f, 0.48f, 1.0f, 0.56f)) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	CUIRect ServerButton, QuitButton;
	Secondary.VSplitMid(&ServerButton, &QuitButton, 7.0f);
	static CButtonContainer s_LocalServerButton;
	static CButtonContainer s_QuitButton;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(DoCompactButton(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), ServerButton, LocalServerRunning ? ColorRGBA(0.42f, 1.0f, 0.72f, 0.24f) : ColorRGBA(0.40f, 0.96f, 0.72f, 0.20f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
			GameClient()->m_LocalServer.KillServer();
		else
		{
			if(GameClient()->m_LocalServer.RunServer({}))
				Client()->Connect("localhost");
		}
	}
	bool UsedEscape = false;
	if(DoCompactButton(&s_QuitButton, Localize("Quit"), QuitButton, ColorRGBA(1.0f, 0.32f, 0.32f, 0.18f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.045f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}

void CMenusStart::ResetAutoUpdateCheck()
{
	m_AutoUpdateCheckStarted = false;
}
