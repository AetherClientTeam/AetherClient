#include "chat_bubbles.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>

#include <algorithm>

void CAetherChatBubbles::AddMessage(int ClientId, int Team, const char *pText)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pText || pText[0] == '\0')
		return;
	if(Team >= 2 && !g_Config.m_AeChatBubblesWhispers)
		return;
	if(!g_Config.m_AeChatBubblesShowOwnSent && (ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1]))
		return;

	auto &Bubbles = m_aaBubbles[ClientId];
	for(int i = 0; i < MAX_BUBBLES_PER_CLIENT - 1; ++i)
		Bubbles[i] = Bubbles[i + 1];
	SBubble &Bubble = Bubbles[MAX_BUBBLES_PER_CLIENT - 1];
	str_copy(Bubble.m_aText, pText, sizeof(Bubble.m_aText));
	Bubble.m_Team = Team;
	Bubble.m_Mention = MentionsLocalPlayer(pText);
	Bubble.m_Time = time_get();
	Bubble.m_Active = true;
}

void CAetherChatBubbles::SplitText(const char *pText, float FontSize, float MaxWidth, char *pLine1, int Line1Size, char *pLine2, int Line2Size) const
{
	pLine1[0] = '\0';
	pLine2[0] = '\0';
	if(!pText || pText[0] == '\0')
		return;

	auto CopyBytes = [](char *pDst, int DstSize, const char *pSrc, int Bytes) {
		if(DstSize <= 0)
			return;
		const int Copy = std::clamp(Bytes, 0, DstSize - 1);
		mem_copy(pDst, pSrc, Copy);
		pDst[Copy] = '\0';
		str_utf8_fix_truncation(pDst);
	};

	const int TextLen = str_length(pText);
	int FirstBreak = TextLen;
	int LastGood = 0;
	for(int Cursor = 0; Cursor < TextLen;)
	{
		const int Next = str_utf8_forward(pText, Cursor);
		char aTemp[256];
		CopyBytes(aTemp, sizeof(aTemp), pText, Next);
		if(TextRender()->TextWidth(FontSize, aTemp) > MaxWidth)
			break;
		LastGood = Next;
		if(pText[Cursor] == ' ')
			FirstBreak = Cursor;
		Cursor = Next;
	}
	if(LastGood >= TextLen)
	{
		str_copy(pLine1, pText, Line1Size);
		return;
	}
	if(FirstBreak <= 0 || FirstBreak > LastGood)
		FirstBreak = std::max(1, LastGood);
	CopyBytes(pLine1, Line1Size, pText, FirstBreak);

	const char *pRest = pText + FirstBreak;
	while(*pRest == ' ')
		++pRest;

	const int RestLen = str_length(pRest);
	int SecondGood = 0;
	for(int Cursor = 0; Cursor < RestLen;)
	{
		const int Next = str_utf8_forward(pRest, Cursor);
		char aTemp[256];
		CopyBytes(aTemp, sizeof(aTemp), pRest, Next);
		if(TextRender()->TextWidth(FontSize, aTemp) > MaxWidth)
			break;
		SecondGood = Next;
		Cursor = Next;
	}
	if(SecondGood >= RestLen)
	{
		str_copy(pLine2, pRest, Line2Size);
		return;
	}

	const char *pEllipsis = "...";
	int Bytes = std::max(0, SecondGood);
	do
	{
		CopyBytes(pLine2, Line2Size, pRest, Bytes);
		str_append(pLine2, pEllipsis, Line2Size);
		if(TextRender()->TextWidth(FontSize, pLine2) <= MaxWidth || Bytes <= 0)
			break;
		Bytes = str_utf8_rewind(pRest, Bytes);
	} while(Bytes > 0);
}

bool CAetherChatBubbles::MentionsLocalPlayer(const char *pText) const
{
	if(!pText || !g_Config.m_AeChatBubblesColoredMessages)
		return false;
	for(int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
			continue;
		const char *pName = GameClient()->m_aClients[LocalId].m_aName;
		if(pName[0] != '\0' && str_utf8_find_nocase(pText, pName))
			return true;
	}
	return false;
}

bool CAetherChatBubbles::IsPlayerVisible(vec2 PlayerPos, float Margin) const
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	return PlayerPos.x >= ScreenX0 - Margin && PlayerPos.x <= ScreenX1 + Margin &&
	       PlayerPos.y >= ScreenY0 - Margin && PlayerPos.y <= ScreenY1 + Margin;
}

void CAetherChatBubbles::DrawBubble(int ClientId, const SBubble &Bubble, vec2 PlayerPos, float StackOffset, bool LivePreview)
{
	const int64_t Now = time_get();
	const float Duration = LivePreview ? 1.0f : std::clamp(g_Config.m_AeChatBubblesDuration, 2, 8);
	const float Age = (Now - Bubble.m_Time) / (float)time_freq();
	if(!LivePreview && Age > Duration)
		return;

	const float BaseAlpha = std::clamp(g_Config.m_AeChatBubblesOpacity / 100.0f, 0.20f, 1.0f);
	const float MaxTextWidth = std::clamp((float)g_Config.m_AeChatBubblesWidth, 80.0f, 220.0f);
	const float FontSize = 12.0f;
	const float PadX = 7.0f;
	const float PadY = 4.5f;
	const float LineH = 11.0f;

	const float FadeIn = LivePreview ? 1.0f : std::clamp(Age / 0.14f, 0.0f, 1.0f);
	const float FadeOut = LivePreview ? 1.0f : std::clamp((Duration - Age) / 0.35f, 0.0f, 1.0f);
	const float Alpha = BaseAlpha * minimum(FadeIn, FadeOut);
	if(Alpha <= 0.01f)
		return;

	char aLine1[160];
	char aLine2[160];
	SplitText(Bubble.m_aText, FontSize, MaxTextWidth, aLine1, sizeof(aLine1), aLine2, sizeof(aLine2));
	if(aLine1[0] == '\0')
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float TextW = std::max(TextRender()->TextWidth(FontSize, aLine1), aLine2[0] ? TextRender()->TextWidth(FontSize, aLine2) : 0.0f);
	const float BubbleW = TextW + PadX * 2.0f;
	const float BubbleH = (aLine2[0] ? LineH * 2.0f : LineH) + PadY * 2.0f;
	float X = PlayerPos.x - BubbleW * 0.5f;
	float Y = PlayerPos.y - 74.0f - BubbleH - StackOffset;
	X = std::clamp(X, ScreenX0 + 4.0f, ScreenX1 - BubbleW - 4.0f);

	ColorRGBA Panel = ColorRGBA(0.035f, 0.045f, 0.060f, Alpha);
	ColorRGBA TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, std::clamp(Alpha + 0.12f, 0.0f, 1.0f));
	if(g_Config.m_AeChatBubblesColoredMessages)
	{
		if(Bubble.m_Mention || Bubble.m_Team >= 2)
		{
			TextColor = ColorRGBA(1.0f, 0.33f, 0.33f, std::clamp(Alpha + 0.12f, 0.0f, 1.0f));
		}
		else if(Bubble.m_Team == 1)
		{
			TextColor = ColorRGBA(0.55f, 1.0f, 0.58f, std::clamp(Alpha + 0.12f, 0.0f, 1.0f));
		}
	}

	Graphics()->DrawRect(X, Y, BubbleW, BubbleH, Panel, IGraphics::CORNER_ALL, 5.0f);
	const float TailX = std::clamp(PlayerPos.x, X + 12.0f, X + BubbleW - 12.0f);
	IGraphics::CFreeformItem Tail(
		TailX - 4.5f, Y + BubbleH - 0.5f,
		TailX + 4.5f, Y + BubbleH - 0.5f,
		TailX + 1.0f, Y + BubbleH + 6.0f,
		TailX - 1.0f, Y + BubbleH + 6.0f);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Panel);
	Graphics()->QuadsDrawFreeform(&Tail, 1);
	Graphics()->QuadsEnd();

	const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.25f * Alpha);
	TextRender()->TextColor(TextColor);
	TextRender()->Text(X + PadX, Y + PadY - 0.5f, FontSize, aLine1, -1.0f);
	if(aLine2[0])
		TextRender()->Text(X + PadX, Y + PadY + LineH - 0.5f, FontSize, aLine2, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(OldOutline);
}

void CAetherChatBubbles::OnRender()
{
	if(!g_Config.m_AeChatBubbles || g_Config.m_AeFocusMode)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int64_t Now = time_get();
	const float Duration = std::clamp(g_Config.m_AeChatBubblesDuration, 2, 8);

	Graphics()->TextureClear();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const vec2 PlayerPos = GameClient()->m_aClients[ClientId].m_RenderPos;
		if(g_Config.m_AeChatBubblesVisibleOnly && !IsPlayerVisible(PlayerPos, 80.0f))
			continue;

		float StackOffset = 0.0f;
		auto &Bubbles = m_aaBubbles[ClientId];
		const int MaxShown = std::clamp(g_Config.m_AeChatBubblesStackCount, 1, MAX_BUBBLES_PER_CLIENT);
		int ActiveCount = 0;
		for(int i = MAX_BUBBLES_PER_CLIENT - MaxShown; i < MAX_BUBBLES_PER_CLIENT; ++i)
		{
			SBubble &Bubble = Bubbles[i];
			if(Bubble.m_Active && (Now - Bubble.m_Time) / (float)time_freq() <= Duration)
				ActiveCount++;
		}
		for(int i = MAX_BUBBLES_PER_CLIENT - MaxShown; i < MAX_BUBBLES_PER_CLIENT; ++i)
		{
			SBubble &Bubble = Bubbles[i];
			if(!Bubble.m_Active)
				continue;
			if((Now - Bubble.m_Time) / (float)time_freq() > Duration)
			{
				Bubble.m_Active = false;
				continue;
			}
			StackOffset = maximum(0, ActiveCount - 1) * 34.0f;
			DrawBubble(ClientId, Bubble, PlayerPos, StackOffset, false);
			ActiveCount--;
		}
	}

	if(g_Config.m_AeChatBubblesShowOwnLive && GameClient()->m_Chat.IsActive())
	{
		const int LocalId = GameClient()->m_Snap.m_LocalClientId;
		const char *pDraft = GameClient()->m_Chat.ActiveInput();
		if(LocalId >= 0 && LocalId < MAX_CLIENTS && pDraft && pDraft[0] && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			const vec2 PlayerPos = GameClient()->m_aClients[LocalId].m_RenderPos;
			if(!g_Config.m_AeChatBubblesVisibleOnly || IsPlayerVisible(PlayerPos, 80.0f))
			{
				SBubble Live;
				str_copy(Live.m_aText, pDraft, sizeof(Live.m_aText));
				Live.m_Team = GameClient()->m_Chat.ActiveMode() == 2 ? 1 : 0;
				Live.m_Mention = false;
				Live.m_Time = Now;
				Live.m_Active = true;
				DrawBubble(LocalId, Live, PlayerPos, 0.0f, true);
			}
		}
	}
}

void CAetherChatBubbles::OnReset()
{
	for(auto &Bubbles : m_aaBubbles)
		for(SBubble &Bubble : Bubbles)
			Bubble = {};
}

void CAetherChatBubbles::OnStateChange(int NewState, int OldState)
{
	if(NewState <= IClient::STATE_CONNECTING || OldState <= IClient::STATE_CONNECTING)
		OnReset();
}
