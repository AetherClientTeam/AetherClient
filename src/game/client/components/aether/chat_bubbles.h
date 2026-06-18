#ifndef GAME_CLIENT_COMPONENTS_AETHER_CHAT_BUBBLES_H
#define GAME_CLIENT_COMPONENTS_AETHER_CHAT_BUBBLES_H

#include <game/client/component.h>

#include <game/gamecore.h>

#include <array>

class CAetherChatBubbles : public CComponent
{
	static constexpr int MAX_BUBBLES_PER_CLIENT = 4;
	struct SBubble
	{
		char m_aText[256] = "";
		int m_Team = 0;
		bool m_Mention = false;
		int64_t m_Time = 0;
		bool m_Active = false;
	};

	std::array<std::array<SBubble, MAX_BUBBLES_PER_CLIENT>, MAX_CLIENTS> m_aaBubbles;

	void SplitText(const char *pText, float FontSize, float MaxWidth, char *pLine1, int Line1Size, char *pLine2, int Line2Size) const;
	bool MentionsLocalPlayer(const char *pText) const;
	bool IsPlayerVisible(vec2 PlayerPos, float Margin) const;
	void DrawBubble(int ClientId, const SBubble &Bubble, vec2 PlayerPos, float StackOffset, bool LivePreview);

public:
	int Sizeof() const override { return sizeof(*this); }

	void AddMessage(int ClientId, int Team, const char *pText);
	void OnRender() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif
