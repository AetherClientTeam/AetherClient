#include "command_palette.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CAetherCommandPalette::ConCommandPalette(IConsole::IResult *pResult, void *pUserData)
{
	CAetherCommandPalette *pSelf = static_cast<CAetherCommandPalette *>(pUserData);
	if(pResult->NumArguments() > 0 && pResult->GetInteger(0) == 0)
		return;
	if(pSelf->m_Active)
		pSelf->Close();
	else
		pSelf->Open();
}

const std::vector<CAetherCommandPalette::SAction> &CAetherCommandPalette::Actions()
{
	static const std::vector<SAction> s_vActions = {
		{"Check for update", "Ask the updater to refresh release status.", "ae_check_update"},
		{"Add demo marker", "Drops a marker into every active demo recorder.", "ae_session_marker manual"},
		{"Save rollback clip", "Saves the configured replay rollback window.", "ae_save_rollback_demo"},
		{"Toggle notifications", "Show or hide Aether notification toasts.", "toggle ae_notifications 0 1"},
		{"Toggle vote panel", "Show or hide the modern Aether vote panel.", "toggle ae_vote_panel 0 1"},
		{"Toggle team invite popup", "Show or hide the modern team invite popup.", "toggle ae_team_invite_popup 0 1"},
		{"Toggle Focus Mode", "Hide or restore selected UI parts.", "toggle ae_focus_mode 0 1"},
		{"Toggle Music Player", "Show or hide the Music Player HUD.", "toggle ae_music_player 0 1"},
		{"Toggle Aether menu theme", "Enable or disable the coded Aether menu background.", "toggle ae_menu_theme_override 0 1"},
		{"Toggle badges", "Show or hide Aether badge rendering.", "toggle ae_badges 0 1"},
		{"Toggle chat bubbles", "Show or hide in-world chat bubbles.", "toggle ae_chat_bubbles 0 1"},
		{"Toggle block awareness", "Show or hide Block Awareness helpers.", "toggle ae_block_awareness 0 1"},
		{"Toggle lag guard", "Show or hide the fast input lag guard.", "toggle ae_input_lag_guard 0 1"},
		{"Toggle auto team lock", "Enable or disable automatic DDNet team locking.", "toggle ae_auto_team_lock 0 1"},
		{"Toggle rollback demo", "Keep or stop the replay rollback buffer helper.", "toggle ae_rollback_demo 0 1"},
		{"Scoreboard cursor", "Toggle clickable scoreboard cursor mode.", "toggle_scoreboard_cursor"},
		{"Clear notifications", "Clear visible Aether notifications.", "ae_notifications_clear"},
		{"Refresh badges", "Refresh badge data without flashing old entries.", "ae_badges_refresh"},
	};
	return s_vActions;
}

void CAetherCommandPalette::OnConsoleInit()
{
	Console()->Register("ae_command_palette", "", CFGFLAG_CLIENT, ConCommandPalette, this, "Open the Aether command palette");
}

void CAetherCommandPalette::OnStateChange(int NewState, int OldState)
{
	if(m_Active && (NewState == IClient::STATE_OFFLINE || OldState == IClient::STATE_DEMOPLAYBACK))
		Close(NewState == IClient::STATE_OFFLINE);
}

void CAetherCommandPalette::Open()
{
	if(!g_Config.m_AeCommandPalette)
		return;
	m_Active = true;
	m_Selected = 0;
	m_SearchInput.Clear();
	m_SearchInput.Activate(EInputPriority::UI);
	Input()->MouseModeAbsolute();
	Input()->SetNativeMouseCursorVisible(true);
}

void CAetherCommandPalette::Close(bool KeepAbsoluteOverride)
{
	if(!m_Active)
		return;
	m_Active = false;
	m_SearchInput.Deactivate();
	const bool KeepAbsolute = KeepAbsoluteOverride || GameClient()->m_Menus.IsActive() || Client()->State() == IClient::STATE_OFFLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!KeepAbsolute)
		Input()->MouseModeRelative();
	Input()->SetNativeMouseCursorVisible(KeepAbsolute);
}

void CAetherCommandPalette::FilteredActions(std::vector<int> &vOut) const
{
	vOut.clear();
	const char *pSearch = m_SearchInput.GetString();
	const auto &vActions = Actions();
	for(size_t i = 0; i < vActions.size(); ++i)
	{
		if(!pSearch || pSearch[0] == '\0' ||
			str_find_nocase(vActions[i].m_pLabel, pSearch) ||
			str_find_nocase(vActions[i].m_pHint, pSearch) ||
			str_find_nocase(vActions[i].m_pCommand, pSearch))
		{
			vOut.push_back((int)i);
		}
	}
}

void CAetherCommandPalette::ExecuteSelected()
{
	std::vector<int> vFiltered;
	FilteredActions(vFiltered);
	if(vFiltered.empty())
		return;
	const int Index = vFiltered[std::clamp(m_Selected, 0, (int)vFiltered.size() - 1)];
	const SAction &Action = Actions()[Index];
	Console()->ExecuteLine(Action.m_pCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	if(g_Config.m_AeNotificationsCommandPalette)
		GameClient()->m_AetherNotifications.Push("Command Palette", Action.m_pLabel, 3.0f);
	Close();
}

bool CAetherCommandPalette::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;
	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			Close();
			return true;
		}
		if(Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER)
		{
			ExecuteSelected();
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_1)
		{
			for(int i = 0; i < m_ActionRectCount; ++i)
			{
				if(Ui()->MouseInside(&m_aActionRects[i]))
				{
					m_Selected = m_aActionRows[i];
					ExecuteSelected();
					return true;
				}
			}
			return true;
		}
		std::vector<int> vFiltered;
		FilteredActions(vFiltered);
		if(Event.m_Key == KEY_UP)
		{
			m_Selected = std::max(0, m_Selected - 1);
			return true;
		}
		if(Event.m_Key == KEY_DOWN)
		{
			m_Selected = std::min(std::max(0, (int)vFiltered.size() - 1), m_Selected + 1);
			return true;
		}
	}
	if(m_SearchInput.ProcessInput(Event))
	{
		m_Selected = 0;
		return true;
	}
	return true;
}

void CAetherCommandPalette::OnRender()
{
	if(!m_Active)
		return;

	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	std::vector<int> vFiltered;
	FilteredActions(vFiltered);
	m_Selected = std::clamp(m_Selected, 0, std::max(0, (int)vFiltered.size() - 1));
	m_ActionRectCount = 0;

	const float Width = std::min(560.0f, ScreenW - 52.0f);
	const float RowH = 42.0f;
	const int Visible = std::min<int>(8, std::max<int>(1, vFiltered.size()));
	const float Height = 76.0f + Visible * RowH + 14.0f;
	CUIRect Panel(ScreenW * 0.5f - Width * 0.5f, std::max(36.0f, ScreenH * 0.14f), Width, Height);
	Graphics()->DrawRect(0.0f, 0.0f, ScreenW, ScreenH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), 0, 0.0f);
	Panel.Draw(ColorRGBA(0.016f, 0.018f, 0.026f, 0.94f), IGraphics::CORNER_ALL, 10.0f);

	const ColorRGBA Accent = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
	CUIRect Body = Panel;
	Body.Margin(12.0f, &Body);
	CUIRect Title, Search, List;
	Body.HSplitTop(22.0f, &Title, &Body);
	Body.HSplitTop(42.0f, &Search, &List);

	TextRender()->TextColor(1.0f, 0.83f, 1.0f, 0.98f);
	TextRender()->Text(Title.x, Title.y, 15.0f, Localize("Command Palette"), Title.w);
	TextRender()->TextColor(0.68f, 0.73f, 0.84f, 0.85f);
	const char *pHint = "Type to filter, Enter to run, Esc to close";
	const float HintW = TextRender()->TextWidth(8.5f, Localize(pHint));
	TextRender()->Text(Title.x + Title.w - HintW, Title.y + 5.0f, 8.5f, Localize(pHint));

	Search.Draw(ColorRGBA(0.055f, 0.06f, 0.075f, 0.92f), IGraphics::CORNER_ALL, 7.0f);
	CUIRect SearchInner = Search;
	SearchInner.Margin(10.0f, &SearchInner);
	m_SearchInput.Render(&SearchInner, 13.0f, TEXTALIGN_ML, true, -1.0f, 0.0f);

	List.HSplitTop(10.0f, nullptr, &List);
	for(int Row = 0; Row < Visible; ++Row)
	{
		CUIRect Item;
		List.HSplitTop(RowH - 5.0f, &Item, &List);
		List.HSplitTop(5.0f, nullptr, &List);
		const bool Selected = Row == m_Selected;
		if(vFiltered.empty())
		{
			TextRender()->TextColor(0.72f, 0.77f, 0.86f, 0.72f);
			TextRender()->Text(Item.x + 8.0f, Item.y + 10.0f, 11.0f, Localize("No matching command"), Item.w);
			break;
		}
		const SAction &Action = Actions()[vFiltered[Row]];
		if(m_ActionRectCount < (int)m_aActionRects.size())
		{
			m_aActionRects[m_ActionRectCount] = Item;
			m_aActionRows[m_ActionRectCount] = Row;
			++m_ActionRectCount;
		}
		if(Ui()->MouseInside(&Item))
			m_Selected = Row;
		Item.Draw(Selected ? Accent.WithAlpha(0.22f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.045f), IGraphics::CORNER_ALL, 7.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Selected ? 0.98f : 0.82f);
		TextRender()->Text(Item.x + 10.0f, Item.y + 5.0f, 12.0f, Localize(Action.m_pLabel), Item.w - 20.0f);
		TextRender()->TextColor(0.66f, 0.72f, 0.84f, Selected ? 0.88f : 0.66f);
		TextRender()->Text(Item.x + 10.0f, Item.y + 22.0f, 8.6f, Localize(Action.m_pHint), Item.w - 20.0f);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}
