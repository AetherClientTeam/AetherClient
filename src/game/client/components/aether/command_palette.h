#ifndef GAME_CLIENT_COMPONENTS_AETHER_COMMAND_PALETTE_H
#define GAME_CLIENT_COMPONENTS_AETHER_COMMAND_PALETTE_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui_rect.h>

#include <array>
#include <vector>

class CAetherCommandPalette : public CComponent
{
	struct SAction
	{
		const char *m_pLabel;
		const char *m_pHint;
		const char *m_pCommand;
	};

	CLineInputBuffered<96> m_SearchInput;
	std::array<CUIRect, 8> m_aActionRects;
	std::array<int, 8> m_aActionRows;
	int m_ActionRectCount = 0;
	bool m_Active = false;
	int m_Selected = 0;

	static void ConCommandPalette(IConsole::IResult *pResult, void *pUserData);
	static const std::vector<SAction> &Actions();
	void Open();
	void Close(bool KeepAbsoluteOverride = false);
	void ExecuteSelected();
	void FilteredActions(std::vector<int> &vOut) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const { return m_Active; }
};

#endif
