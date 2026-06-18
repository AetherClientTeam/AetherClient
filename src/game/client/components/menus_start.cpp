/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include "aether/client_variant.h"

#include <base/process.h>
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

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	auto LoadCoreTexture = [&](const char *pPath) {
		IGraphics::CTextureHandle Texture = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL);
		if(Texture.IsValid() || !str_startswith(pPath, "core/"))
			return Texture;
		char aFallback[256];
		str_format(aFallback, sizeof(aFallback), "aether/%s", pPath + str_length("core/"));
		return Graphics()->LoadTexture(aFallback, IStorage::TYPE_ALL);
	};

	// render logo
	static bool s_AetherStartLogoLoaded = false;
	static IGraphics::CTextureHandle s_AetherStartLogoTexture;
	if(!s_AetherStartLogoLoaded)
	{
		s_AetherStartLogoTexture = LoadCoreTexture(AetherVariant::LogoLockupPath());
		s_AetherStartLogoLoaded = true;
	}
	Graphics()->TextureSet(s_AetherStartLogoTexture.IsValid() ? s_AetherStartLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	const float LogoW = s_AetherStartLogoTexture.IsValid() ? std::min(460.0f, MainView.w * 0.56f) : 360.0f;
	const float LogoH = s_AetherStartLogoTexture.IsValid() ? LogoW * 0.25f : 103.0f;
	IGraphics::CQuadItem QuadItem(MainView.w / 2.0f - LogoW / 2.0f, s_AetherStartLogoTexture.IsValid() ? 34.0f : 60.0f, LogoW, LogoH);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	const float Rounding = 16.0f;
	const float VMargin = std::max(20.0f, MainView.w / 2 - 190.0f);
	int NewPage = -1;

	auto DrawTexture = [&](IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Alpha = 1.0f) {
		if(!Texture.IsValid())
			return;
		Graphics()->TextureSet(Texture);
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};

	auto DoModernCard = [&](CButtonContainer *pId, const char *pTitle, const char *pSubtitle, const char *pHotkey, CUIRect Rect, ColorRGBA Accent, bool Highlight = false) {
		const bool Hot = Ui()->HotItem() == pId;
		ColorRGBA Border = Hot ? Accent : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
		Border.a *= Ui()->ButtonColorMul(pId);
		Rect.Draw(Border, IGraphics::CORNER_ALL, Rounding);

		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		ColorRGBA CardColor = Hot && Highlight ? ColorRGBA(0.08f, 0.20f, 0.30f, 0.78f) : ColorRGBA(0.01f, 0.02f, 0.04f, 0.62f);
		CardColor.a *= Ui()->ButtonColorMul(pId);
		Inner.Draw(CardColor, IGraphics::CORNER_ALL, Rounding - 1.5f);

		CUIRect Text = Inner;
		Text.Margin(12.0f, &Text);
		CUIRect Hotkey, Title, Subtitle;
		Text.VSplitRight(42.0f, &Text, &Hotkey);
		Hotkey.HSplitTop(23.0f, &Hotkey, nullptr);
		Hotkey.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 8.0f);
		Ui()->DoLabel(&Hotkey, pHotkey, 11.0f, TEXTALIGN_MC);

		Text.HSplitTop(Rect.h > 92.0f ? 42.0f : 31.0f, &Title, &Subtitle);
		Ui()->DoLabel(&Title, pTitle, Rect.h > 92.0f ? 25.0f : 22.0f, TEXTALIGN_MC);
		Ui()->DoLabel(&Subtitle, pSubtitle, 11.0f, TEXTALIGN_TC, {.m_MaxWidth = Text.w});
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};

	auto DoCompactButton = [&](CButtonContainer *pId, const char *pText, CUIRect Rect, ColorRGBA Accent) {
		const bool Hot = Ui()->HotItem() == pId;
		Rect.Draw(Hot ? Accent : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 12.0f);
		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		Inner.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.44f), IGraphics::CORNER_ALL, 10.5f);
		Ui()->DoLabel(&Inner, pText, 16.0f, TEXTALIGN_MC);
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};

	static bool s_EcosystemTexturesLoaded = false;
	static IGraphics::CTextureHandle s_aEcosystemTextures[4];
	if(!s_EcosystemTexturesLoaded)
	{
		for(int i = 0; i < 4; ++i)
			s_aEcosystemTextures[i] = LoadCoreTexture(AetherVariant::IconPath(i));
		s_EcosystemTexturesLoaded = true;
	}
	const char *apEcosystemNames[] = {"Aether", "Vera", "Via", "Vex"};
	const char *apEcosystemDescriptions[] = {"All-in-one", "Gores", "DDRace", "Block"};
	auto SwitchToClient = [&](int Index) {
		char aPath[IO_MAX_PATH_LENGTH];
		Storage()->GetBinaryPathAbsolute(AetherVariant::ExecutableName(Index), aPath, sizeof(aPath));
		if(process_execute(aPath, EShellExecuteWindowState::FOREGROUND) == INVALID_PROCESS)
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "Client executable not found: %s", AetherVariant::ExecutableName(Index));
			GameClient()->Echo(aMsg);
		}
		else
			Client()->Quit();
	};

	CUIRect Content = MainView;
	Content.Margin(34.0f, &Content);
	Content.HSplitTop(158.0f, nullptr, &Content);
	Content.HSplitBottom(54.0f, &Content, nullptr);

	CUIRect EcosystemPanel;
	CUIRect Cluster = Content;
	const bool ShowEcosystem = Content.w >= 900.0f;
	const float ClusterWidth = std::min(Content.w, ShowEcosystem ? 1040.0f : 680.0f);
	const float ClusterHeight = std::min(Content.h, ShowEcosystem ? 430.0f : 500.0f);
	Cluster.VMargin(std::max(0.0f, (Cluster.w - ClusterWidth) * 0.5f), &Cluster);
	Cluster.HMargin(std::max(0.0f, (Cluster.h - ClusterHeight) * 0.5f), &Cluster);

	CUIRect MainPanel = Cluster;
	if(ShowEcosystem)
	{
		Cluster.VSplitRight(210.0f, &MainPanel, &EcosystemPanel);
		MainPanel.VSplitRight(14.0f, &MainPanel, nullptr);
	}
	else
	{
		Cluster.HSplitBottom(62.0f, &MainPanel, &EcosystemPanel);
		EcosystemPanel.HSplitTop(10.0f, nullptr, &EcosystemPanel);
	}

	MainPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 22.0f);
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
	Ui()->DoLabel(&HeaderTitle, "Aether Client", 19.0f, TEXTALIGN_ML);

#if defined(CONF_AUTOUPDATE)
	{
		CUIRect UpdateStatus, UpdateButton;
		UpdateArea.VSplitRight(126.0f, &UpdateStatus, &UpdateButton);
		UpdateStatus.VSplitRight(8.0f, &UpdateStatus, nullptr);

		const IUpdater::EUpdaterState UpdateState = Updater()->GetCurrentState();
		char aStatus[128];
		Updater()->GetCurrentFile(aStatus, sizeof(aStatus));
		const int Percent = Updater()->GetCurrentPercent();

		const char *pButtonLabel = Localize("Update");
		if(UpdateState == IUpdater::GETTING_MANIFEST || UpdateState == IUpdater::DOWNLOADING)
			pButtonLabel = Localize("Updating...");
		else if(UpdateState == IUpdater::NEED_RESTART)
			pButtonLabel = Localize("Update");
		else if(UpdateState == IUpdater::FAIL)
			pButtonLabel = Localize("Retry");

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
			str_format(aBuf, sizeof(aBuf), "%s %d%%", Localize("New Update"), Percent);
		else if(UpdateState == IUpdater::GETTING_MANIFEST)
			str_format(aBuf, sizeof(aBuf), "%s", aStatus[0] ? aStatus : Localize("Checking latest release"));
		else if(UpdateState == IUpdater::NEED_RESTART)
			str_format(aBuf, sizeof(aBuf), "%s", Localize("New Update"));
		else if(UpdateState == IUpdater::FAIL)
			str_format(aBuf, sizeof(aBuf), "%s", aStatus[0] ? aStatus : Localize("Update failed"));
		else
			str_format(aBuf, sizeof(aBuf), "%s", aStatus[0] ? aStatus : Localize("Ready"));
		SLabelProperties UpdateLabelProps;
		const bool ShowUpdateAlert = UpdateState == IUpdater::DOWNLOADING || UpdateState == IUpdater::NEED_RESTART || UpdateState == IUpdater::FAIL;
		UpdateLabelProps.SetColor(ShowUpdateAlert ? ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f) : ColorRGBA(0.75f, 0.88f, 1.0f, 1.0f));
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

	if(DoModernCard(&s_PlayCard, Localize("Play", "Start menu"), Localize("Browse servers and jump in."), "P", PlayCard, ColorRGBA(0.28f, 0.72f, 1.0f, 0.70f), true) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	if(DoModernCard(&s_DemoCard, Localize("Demos"), Localize("Watch, trim and render demos."), "D", DemoCard, ColorRGBA(0.74f, 0.54f, 1.0f, 0.56f)) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;
	if(DoModernCard(&s_EditorCard, Localize("Editor"), Localize("Create and edit maps."), "E", EditorCard, ColorRGBA(0.38f, 1.0f, 0.70f, 0.52f), GameClient()->Editor()->HasUnsavedData()) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}
	if(DoModernCard(&s_SettingsCard, Localize("Settings"), Localize("Tune DDNet, TClient and Aether."), "S", SettingsCard, ColorRGBA(1.0f, 0.78f, 0.32f, 0.55f)) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	CUIRect ServerButton, QuitButton;
	Secondary.VSplitMid(&ServerButton, &QuitButton, 7.0f);
	static CButtonContainer s_LocalServerButton;
	static CButtonContainer s_QuitButton;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(DoCompactButton(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), ServerButton, LocalServerRunning ? ColorRGBA(0.25f, 1.0f, 0.45f, 0.35f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
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
	if(DoCompactButton(&s_QuitButton, Localize("Quit"), QuitButton, ColorRGBA(1.0f, 0.32f, 0.32f, 0.28f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	if(ShowEcosystem && EcosystemPanel.w > 1.0f)
	{
		EcosystemPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 22.0f);
		CUIRect Header;
		EcosystemPanel.Margin(14.0f, &EcosystemPanel);
		EcosystemPanel.HSplitTop(32.0f, &Header, &EcosystemPanel);
		Ui()->DoLabel(&Header, "Ecosystem", 17.0f, TEXTALIGN_ML);
		static CButtonContainer s_aClientSwitchButtons[4];
		for(int i = 0; i < 4; ++i)
		{
			CUIRect Card;
			EcosystemPanel.HSplitTop(54.0f, &Card, &EcosystemPanel);
			EcosystemPanel.HSplitTop(10.0f, nullptr, &EcosystemPanel);
			const bool Active = i == AetherVariant::Index();
			Card.Draw(Active ? ColorRGBA(0.12f, 0.30f, 0.42f, 0.58f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), IGraphics::CORNER_ALL, 12.0f);
			CUIRect Icon, Text, Title, Description, Tag;
			Card.Margin(8.0f, &Card);
			Card.VSplitLeft(40.0f, &Icon, &Text);
			DrawTexture(s_aEcosystemTextures[i], Icon, Active ? 1.0f : 0.36f);
			Text.VSplitLeft(8.0f, nullptr, &Text);
			Text.HSplitTop(20.0f, &Title, &Description);
			Ui()->DoLabel(&Title, apEcosystemNames[i], 14.0f, TEXTALIGN_ML);
			if(Active)
				Ui()->DoLabel(&Description, apEcosystemDescriptions[i], 10.0f, TEXTALIGN_ML);
			else
			{
				Description.VSplitRight(76.0f, &Description, &Tag);
				Ui()->DoLabel(&Description, apEcosystemDescriptions[i], 10.0f, TEXTALIGN_ML);
				Tag.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Tag, "Open", 9.0f, TEXTALIGN_MC);
			}
			if(!Active && Ui()->DoButtonLogic(&s_aClientSwitchButtons[i], 0, &Card, BUTTONFLAG_LEFT))
				SwitchToClient(i);
		}
	}
	else if(EcosystemPanel.w > 1.0f)
	{
		static CButtonContainer s_aCompactClientSwitchButtons[4];
		EcosystemPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 16.0f);
		EcosystemPanel.Margin(8.0f, &EcosystemPanel);
		const float Gap = 8.0f;
		const float CardW = (EcosystemPanel.w - Gap * 3.0f) / 4.0f;
		for(int i = 0; i < 4; ++i)
		{
			CUIRect Card;
			EcosystemPanel.VSplitLeft(CardW, &Card, &EcosystemPanel);
			if(i < 3)
				EcosystemPanel.VSplitLeft(Gap, nullptr, &EcosystemPanel);
			const bool Active = i == AetherVariant::Index();
			const bool Hot = Ui()->HotItem() == &s_aCompactClientSwitchButtons[i];
			Card.Draw(Active ? ColorRGBA(0.12f, 0.30f, 0.42f, 0.62f) : Hot ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Icon, Text;
			Card.Margin(7.0f, &Card);
			Card.VSplitLeft(34.0f, &Icon, &Text);
			DrawTexture(s_aEcosystemTextures[i], Icon, Active ? 1.0f : 0.45f);
			Text.VSplitLeft(7.0f, nullptr, &Text);
			Ui()->DoLabel(&Text, apEcosystemNames[i], 12.0f, TEXTALIGN_ML);
			if(!Active && Ui()->DoButtonLogic(&s_aCompactClientSwitchButtons[i], 0, &Card, BUTTONFLAG_LEFT))
				SwitchToClient(i);
		}
	}

	CUIRect TClientVersion;
	MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
	TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
	char aTBuf[64];
	str_format(aTBuf, sizeof(aTBuf), "v%s", CLIENT_RELEASE_VERSION);
	Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);

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
