#include "block_awareness.h"

#include <algorithm>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/gamecore.h>

namespace
{
ColorRGBA ConfigColor(int ColorConfig, float Alpha = 1.0f)
{
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(ColorConfig));
	Color.a = Alpha;
	return Color;
}

bool RecentlyAttacked(const CGameClient *pClient, int ClientId, int MaxTicks)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const int AttackTick = pClient->m_aClients[ClientId].m_RenderCur.m_AttackTick;
	const int Tick = pClient->Client()->GameTick(g_Config.m_ClDummy);
	return AttackTick > 0 && Tick - AttackTick >= 0 && Tick - AttackTick <= MaxTicks;
}

}

void CAetherBlockAwareness::OnReset()
{
	m_Popup = {};
	m_LocalWasFrozen = false;
	m_LocalWasDead = false;
	m_LastProcessedTick = -1;
	mem_zero(m_aLastSavePopupTick, sizeof(m_aLastSavePopupTick));
	mem_zero(m_aLastBlockPopupTick, sizeof(m_aLastBlockPopupTick));
	mem_zero(m_aLastFreezeEnd, sizeof(m_aLastFreezeEnd));
	mem_zero(m_aAllyFreezeAlertTick, sizeof(m_aAllyFreezeAlertTick));
}

void CAetherBlockAwareness::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	OnReset();
}

bool CAetherBlockAwareness::IsLocalId(int ClientId) const
{
	return ClientId >= 0 && (GameClient()->m_aLocalIds[0] == ClientId || GameClient()->m_aLocalIds[1] == ClientId);
}

bool CAetherBlockAwareness::IsFrozen(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return false;
	if(IsLocalId(ClientId))
		return GameClient()->m_aClients[ClientId].m_Predicted.m_FreezeEnd != 0 || GameClient()->m_aClients[ClientId].m_RegularPredicted.m_FreezeEnd != 0;
	const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
	return Char.m_Active && Char.m_HasExtendedData && Char.m_ExtendedData.m_FreezeEnd != 0;
}

int CAetherBlockAwareness::FreezeEndTick(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return 0;
	if(IsLocalId(ClientId))
	{
		const int Pred = GameClient()->m_aClients[ClientId].m_Predicted.m_FreezeEnd;
		return Pred != 0 ? Pred : GameClient()->m_aClients[ClientId].m_RegularPredicted.m_FreezeEnd;
	}
	const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(Char.m_Active && Char.m_HasExtendedData)
		return Char.m_ExtendedData.m_FreezeEnd;
	return GameClient()->m_aClients[ClientId].m_FreezeEnd;
}

bool CAetherBlockAwareness::IsAlive(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_Active && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
}

vec2 CAetherBlockAwareness::CharacterPos(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return vec2(0.0f, 0.0f);
	return GameClient()->m_aClients[ClientId].m_RenderPos;
}

int CAetherBlockAwareness::FindSaveCandidate(int LocalId) const
{
	if(LocalId < 0)
		return -1;

	const vec2 LocalPos = CharacterPos(LocalId);
	int BestCandidate = -1;
	float BestScore = 0.0f;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == LocalId || IsLocalId(i) || !GameClient()->m_aClients[i].m_Active)
			continue;

		const vec2 Pos = CharacterPos(i);
		const float Dist = distance(Pos, LocalPos);
		float Score = 0.0f;

		if(GameClient()->m_aClients[i].m_RenderCur.m_HookedPlayer == LocalId || GameClient()->m_aClients[i].m_RenderPrev.m_HookedPlayer == LocalId)
			Score += 4.0f;

		if(GameClient()->m_aClients[i].m_RenderCur.m_Weapon == WEAPON_HAMMER && RecentlyAttacked(GameClient(), i, 14))
		{
			if(Dist < 118.0f)
				Score += 4.5f;
			else if(Dist < 160.0f)
				Score += 2.0f;
		}

		if(Dist < 160.0f)
			Score += 1.0f;
		else if(Dist < 260.0f)
			Score += 0.35f;

		if(Score > BestScore)
		{
			BestScore = Score;
			BestCandidate = i;
		}
	}

	return BestScore >= 4.0f ? BestCandidate : -1;
}

int CAetherBlockAwareness::FindBlockCandidate(int LocalId) const
{
	if(LocalId < 0)
		return -1;
	const vec2 LocalPos = CharacterPos(LocalId);
	int BestCandidate = -1;
	float BestScore = 0.0f;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == LocalId || IsLocalId(i) || !GameClient()->m_aClients[i].m_Active)
			continue;
		const EGroup Group = GroupForClient(i);
		const bool KnownEnemy = Group == EGroup::ENEMY;
		const vec2 Pos = CharacterPos(i);
		const float Dist = distance(Pos, LocalPos);
		float Score = KnownEnemy ? 1.15f : 0.0f;

		if(GameClient()->m_aClients[i].m_RenderCur.m_HookedPlayer == LocalId || GameClient()->m_aClients[i].m_RenderPrev.m_HookedPlayer == LocalId)
			Score += 3.0f;
		if(RecentlyAttacked(GameClient(), i, 20))
		{
			if(Dist < 118.0f)
				Score += 3.0f;
			else if(Dist < 190.0f)
				Score += 1.3f;
		}
		if(Dist < 130.0f)
			Score += 1.2f;
		else if(Dist < 260.0f)
			Score += 0.35f;

		if(Score > BestScore)
		{
			BestScore = Score;
			BestCandidate = i;
		}
	}
	return BestScore >= 3.6f ? BestCandidate : -1;
}

void CAetherBlockAwareness::OpenSavePopup(int ClientId)
{
	if(!g_Config.m_AeBlockSavePopup || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(Tick - m_aLastSavePopupTick[ClientId] < Client()->GameTickSpeed() * 10)
		return;
	m_aLastSavePopupTick[ClientId] = Tick;
	m_Popup.m_Active = true;
	m_Popup.m_Kind = EPopupKind::SAVE;
	m_Popup.m_ClientId = ClientId;
	m_Popup.m_DetectedTick = Tick;
	str_copy(m_Popup.m_aName, GameClient()->m_aClients[ClientId].m_aName);
}

void CAetherBlockAwareness::OpenBlockPopup(int ClientId)
{
	if(!g_Config.m_AeBlockDetectorPopup || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(Tick - m_aLastBlockPopupTick[ClientId] < Client()->GameTickSpeed() * 12)
		return;
	m_aLastBlockPopupTick[ClientId] = Tick;
	m_Popup.m_Active = true;
	m_Popup.m_Kind = EPopupKind::BLOCK;
	m_Popup.m_ClientId = ClientId;
	m_Popup.m_DetectedTick = Tick;
	str_copy(m_Popup.m_aName, GameClient()->m_aClients[ClientId].m_aName);
}

void CAetherBlockAwareness::OnNewSnapshot()
{
	if(!g_Config.m_AeBlockAwareness)
	{
		m_LocalWasFrozen = false;
		m_LocalWasDead = false;
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(Tick == m_LastProcessedTick)
		return;
	m_LastProcessedTick = Tick;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0)
	{
		m_LocalWasFrozen = false;
		return;
	}

	const bool LocalFrozen = IsFrozen(LocalId);
	const bool LocalAlive = IsAlive(LocalId);
	m_LocalWasFrozen = LocalFrozen;
	m_LocalWasDead = !LocalAlive;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const int FreezeEnd = FreezeEndTick(i);
		if(g_Config.m_AeBlockAllyFreezeAlert && FreezeEnd != 0 && m_aLastFreezeEnd[i] == 0)
		{
			const EGroup Group = GroupForClient(i);
			if(Group == EGroup::ALLY || Group == EGroup::HELPER)
				m_aAllyFreezeAlertTick[i] = Tick;
		}
		m_aLastFreezeEnd[i] = FreezeEnd;
	}
}

void CAetherBlockAwareness::DrawPopupButton(const CUIRect &Rect, const char *pText, ColorRGBA Color)
{
	const bool Hovered = Ui()->MouseHovered(&Rect);
	Rect.Draw(Hovered ? Color.WithAlpha(0.92f) : Color.WithAlpha(0.72f), IGraphics::CORNER_ALL, 8.0f);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Ui()->DoLabel(&Rect, pText, 11.0f, TEXTALIGN_MC);
}

void CAetherBlockAwareness::OnRender()
{
	if(g_Config.m_AeBlockAwareness)
	{
		RenderLocalFreezeOverlay();
		RenderEnemyCountAndDanger();
		RenderAllyFreezeAlerts();
	}

	if(!m_Popup.m_Active || !g_Config.m_AeBlockAwareness)
		return;
	m_Popup.m_Active = false;
	return;
	if((m_Popup.m_Kind == EPopupKind::SAVE && !g_Config.m_AeBlockSavePopup) || (m_Popup.m_Kind == EPopupKind::BLOCK && !g_Config.m_AeBlockDetectorPopup))
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(Tick - m_Popup.m_DetectedTick > Client()->GameTickSpeed() * 8)
	{
		m_Popup.m_Active = false;
		return;
	}

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float Width = 300.0f;
	const float Height = 88.0f;
	CUIRect Panel(ScreenX0 + (ScreenX1 - ScreenX0 - Width) * 0.5f, ScreenY0 + 68.0f, Width, Height);
	Panel.Draw(ColorRGBA(0.04f, 0.06f, 0.09f, 0.86f), IGraphics::CORNER_ALL, 12.0f);

	CUIRect Header, Buttons;
	Panel.Margin(10.0f, &Panel);
	Panel.HSplitTop(28.0f, &Header, &Buttons);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), m_Popup.m_Kind == EPopupKind::BLOCK ? "%s blocked you?" : "%s saved you", m_Popup.m_aName);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Ui()->DoLabel(&Header, aBuf, 16.0f, TEXTALIGN_MC);

	Buttons.HSplitTop(8.0f, nullptr, &Buttons);
	CUIRect Helper, Ally, Ignore;
	Buttons.VSplitLeft(86.0f, &Helper, &Buttons);
	Buttons.VSplitLeft(8.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(86.0f, &Ally, &Buttons);
	Buttons.VSplitLeft(8.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(86.0f, &Ignore, nullptr);
	m_Popup.m_HelperButton = Helper;
	m_Popup.m_AllyButton = Ally;
	m_Popup.m_IgnoreButton = Ignore;
	if(m_Popup.m_Kind == EPopupKind::BLOCK)
	{
		DrawPopupButton(Helper, "F1 Enemy", ColorForGroup(EGroup::ENEMY));
		DrawPopupButton(Ally, "F2 Ally", ColorForGroup(EGroup::ALLY));
	}
	else
	{
		DrawPopupButton(Helper, "F1 Helper", ColorForGroup(EGroup::HELPER));
		DrawPopupButton(Ally, "F2 Ally", ColorForGroup(EGroup::ALLY));
	}
	DrawPopupButton(Ignore, "F3 Ignore", ColorRGBA(0.35f, 0.37f, 0.41f, 1.0f));
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAetherBlockAwareness::AddPopupClientToWarlist(const char *pType)
{
	if(!m_Popup.m_Active || m_Popup.m_ClientId < 0 || m_Popup.m_ClientId >= MAX_CLIENTS)
		return;
	const char *pName = GameClient()->m_aClients[m_Popup.m_ClientId].m_aName;
	while(GameClient()->m_WarList.FindWarEntry(pName, "", "enemy"))
		GameClient()->m_WarList.RemoveWarEntry(pName, "", "enemy");
	while(GameClient()->m_WarList.FindWarEntry(pName, "", "team"))
		GameClient()->m_WarList.RemoveWarEntry(pName, "", "team");
	while(GameClient()->m_WarList.FindWarEntry(pName, "", "helper"))
		GameClient()->m_WarList.RemoveWarEntry(pName, "", "helper");
	GameClient()->m_WarList.AddWarEntry(pName, "", m_Popup.m_Kind == EPopupKind::BLOCK ? "blocked me" : "saved me", pType);
	GameClient()->m_WarList.UpdateWarPlayers();
	m_Popup.m_Active = false;
}

void CAetherBlockAwareness::IgnoreSavePopup()
{
	m_Popup.m_Active = false;
}

void CAetherBlockAwareness::RenderFreezeBars()
{
	if(!g_Config.m_AeBlockFreezeBars || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return;

	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	const vec2 Center = GameClient()->m_Camera.m_Center;
	float aPoints[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!IsAlive(i) || IsLocalId(i))
			continue;
		const int FreezeEnd = FreezeEndTick(i);
		if(FreezeEnd <= Tick)
			continue;

		const float Seconds = (FreezeEnd - Tick) / (float)Client()->GameTickSpeed();
		const vec2 Pos = CharacterPos(i) + vec2(-30.0f, 40.0f);
		const float Progress = std::clamp(Seconds / 3.0f, 0.0f, 1.0f);
		const ColorRGBA GroupColor = ColorForGroup(GroupForClient(i), 0.85f);
		CUIRect Back(Pos.x, Pos.y, 60.0f, 9.0f);
		Back.Draw(ColorRGBA(0.02f, 0.03f, 0.04f, 0.55f), IGraphics::CORNER_ALL, 3.0f);
		CUIRect Fill = Back;
		Fill.w *= Progress;
		Fill.Draw(GroupColor, IGraphics::CORNER_ALL, 3.0f);

		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%.2fs", Seconds);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.88f);
		TextRender()->Text(Back.x + 15.0f, Back.y - 13.0f, 8.0f, aBuf);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBlockAwareness::RenderLocalFreezeOverlay()
{
	if(!g_Config.m_AeBlockLocalFreezeOverlay)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const int FreezeEnd = FreezeEndTick(LocalId);
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(LocalId < 0 || FreezeEnd <= Tick)
		return;

	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);
	const float Seconds = (FreezeEnd - Tick) / (float)Client()->GameTickSpeed();
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "Frozen %.2fs", Seconds);
	CUIRect Panel(ScreenW * 0.5f - 105.0f, 78.0f, 210.0f, 42.0f);
	Panel.Draw(ColorRGBA(0.06f, 0.08f, 0.12f, 0.72f), IGraphics::CORNER_ALL, 10.0f);
	TextRender()->TextColor(0.66f, 0.88f, 1.0f, 1.0f);
	Ui()->DoLabel(&Panel, aBuf, 20.0f, TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBlockAwareness::RenderAllyFreezeAlerts()
{
	if(!g_Config.m_AeBlockAllyFreezeAlert)
		return;
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS && Count < 3; ++i)
	{
		if(m_aAllyFreezeAlertTick[i] <= 0 || Tick - m_aAllyFreezeAlertTick[i] > Client()->GameTickSpeed() * 3)
			continue;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s frozen", GameClient()->m_aClients[i].m_aName);
		const float W = minimum(420.0f, maximum(260.0f, TextRender()->TextWidth(20.0f, aBuf) + 42.0f));
		CUIRect Alert((ScreenW - W) * 0.5f, 112.0f + Count * 42.0f, W, 34.0f);
		Alert.Draw(ColorForGroup(GroupForClient(i), 0.82f), IGraphics::CORNER_ALL, 10.0f);
		TextRender()->TextColor(0.0f, 0.0f, 0.0f, 0.92f);
		Ui()->DoLabel(&Alert, aBuf, 20.0f, TEXTALIGN_MC);
		++Count;
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CAetherBlockAwareness::RenderEnemyCountAndDanger()
{
	if(!g_Config.m_AeBlockEnemyCountHud)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || !IsAlive(LocalId))
		return;
	const vec2 LocalPos = CharacterPos(LocalId);
	struct SNearbyEnemy
	{
		int m_ClientId;
		float m_Distance;
	};
	SNearbyEnemy aNearby[MAX_CLIENTS];
	int NearbyEnemies = 0;
	const float Radius = g_Config.m_AeBlockEnemyScanBlocks * 32.0f;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!IsAlive(i) || IsLocalId(i) || GroupForClient(i) != EGroup::ENEMY)
			continue;
		const float Dist = distance(CharacterPos(i), LocalPos);
		if(Dist <= Radius && NearbyEnemies < MAX_CLIENTS)
		{
			aNearby[NearbyEnemies].m_ClientId = i;
			aNearby[NearbyEnemies].m_Distance = Dist;
			++NearbyEnemies;
		}
	}
	std::sort(aNearby, aNearby + NearbyEnemies, [](const SNearbyEnemy &Left, const SNearbyEnemy &Right) {
		return Left.m_Distance < Right.m_Distance;
	});

	const float ScreenW = Graphics()->ScreenWidth();
	const float ScreenH = Graphics()->ScreenHeight();
	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);
	const int Shown = std::min(NearbyEnemies, 6);
	CUIRect Panel(18.0f, 78.0f, 218.0f, 30.0f + Shown * 17.0f);
	Panel.Draw(ColorRGBA(0.04f, 0.06f, 0.09f, 0.76f), IGraphics::CORNER_ALL, 8.0f);
	Panel.Margin(8.0f, &Panel);
	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "Enemies nearby: %d", NearbyEnemies);
	CUIRect Line;
	Panel.HSplitTop(16.0f, &Line, &Panel);
	TextRender()->TextColor(NearbyEnemies > 0 ? ColorRGBA(1.0f, 0.28f, 0.25f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.82f));
	Ui()->DoLabel(&Line, aBuf, 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.86f);
	for(int Index = 0; Index < Shown; ++Index)
	{
		const int ClientId = aNearby[Index].m_ClientId;
		const int Blocks = round_to_int(aNearby[Index].m_Distance / 32.0f);
		str_format(aBuf, sizeof(aBuf), "%s  %d blocks", GameClient()->m_aClients[ClientId].m_aName, Blocks);
		Panel.HSplitTop(17.0f, &Line, &Panel);
		Ui()->DoLabel(&Line, aBuf, 10.5f, TEXTALIGN_ML);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

bool CAetherBlockAwareness::OnInput(const IInput::CEvent &Event)
{
	if(!m_Popup.m_Active)
		return false;
	if(!(Event.m_Flags & IInput::FLAG_PRESS))
		return false;
	if(Event.m_Key == KEY_F1)
	{
		AddPopupClientToWarlist(m_Popup.m_Kind == EPopupKind::BLOCK ? "enemy" : "helper");
		return true;
	}
	if(Event.m_Key == KEY_F2)
	{
		AddPopupClientToWarlist("team");
		return true;
	}
	if(Event.m_Key == KEY_F3)
	{
		IgnoreSavePopup();
		return true;
	}
	if(Event.m_Key != KEY_MOUSE_1)
		return false;
	if(Ui()->MouseInside(&m_Popup.m_HelperButton))
	{
		AddPopupClientToWarlist(m_Popup.m_Kind == EPopupKind::BLOCK ? "enemy" : "helper");
		return true;
	}
	if(Ui()->MouseInside(&m_Popup.m_AllyButton))
	{
		AddPopupClientToWarlist("team");
		return true;
	}
	if(Ui()->MouseInside(&m_Popup.m_IgnoreButton))
	{
		IgnoreSavePopup();
		return true;
	}
	return false;
}

bool CAetherBlockAwareness::IsEnabledForClient(int ClientId) const
{
	if(!g_Config.m_AeBlockAwareness || ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return false;
	return !IsLocalId(ClientId);
}

CAetherBlockAwareness::EGroup CAetherBlockAwareness::GroupForClient(int ClientId) const
{
	if(!IsEnabledForClient(ClientId))
		return EGroup::NONE;
	if(GameClient()->m_WarList.HasWarType(ClientId, "enemy"))
		return EGroup::ENEMY;
	if(GameClient()->m_WarList.HasWarType(ClientId, "team"))
		return EGroup::ALLY;
	if(GameClient()->m_WarList.HasWarType(ClientId, "helper"))
		return EGroup::HELPER;
	return EGroup::UNKNOWN;
}

ColorRGBA CAetherBlockAwareness::ColorForGroup(EGroup Group, float Alpha) const
{
	switch(Group)
	{
	case EGroup::ENEMY: return ConfigColor(g_Config.m_AeBlockEnemyColor, Alpha);
	case EGroup::HELPER: return ConfigColor(g_Config.m_AeBlockHelperColor, Alpha);
	case EGroup::ALLY: return ConfigColor(g_Config.m_AeBlockAllyColor, Alpha);
	case EGroup::UNKNOWN: return ConfigColor(g_Config.m_AeBlockNeutralColor, Alpha);
	default: return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
	}
}

bool CAetherBlockAwareness::ShouldColorPlayer(int ClientId) const
{
	const EGroup Group = GroupForClient(ClientId);
	if(Group == EGroup::NONE)
		return false;
	if(Group == EGroup::UNKNOWN)
		return g_Config.m_AeBlockNeutralColorPlayers;
	return g_Config.m_AeBlockColorPlayers && !(g_Config.m_AeBlockAlliesKeepRealSkins && Group == EGroup::ALLY);
}

bool CAetherBlockAwareness::ShouldUseDefaultColorSkin(int ClientId) const
{
	const EGroup Group = GroupForClient(ClientId);
	if(Group == EGroup::UNKNOWN)
		return ShouldColorPlayer(ClientId) && !g_Config.m_AeBlockNeutralKeepRealSkins;
	return ShouldColorPlayer(ClientId);
}

bool CAetherBlockAwareness::ShouldColorName(int ClientId) const
{
	const EGroup Group = GroupForClient(ClientId);
	if(Group == EGroup::NONE)
		return false;
	if(Group == EGroup::UNKNOWN)
		return g_Config.m_AeBlockNeutralColorNames;
	return g_Config.m_AeBlockColorNames;
}

bool CAetherBlockAwareness::ShouldScaleEnemy(int ClientId) const
{
	return g_Config.m_AeBlockEnemySize && GroupForClient(ClientId) == EGroup::ENEMY;
}

float CAetherBlockAwareness::EnemyScale(int ClientId) const
{
	return PlayerScale(ClientId);
}

float CAetherBlockAwareness::PlayerScale(int ClientId) const
{
	const EGroup Group = GroupForClient(ClientId);
	if(!g_Config.m_AeBlockEnemySize && Group != EGroup::UNKNOWN)
	{
		if(IsFrozen(ClientId) && Group == EGroup::ALLY)
			return 1.10f;
		if(IsFrozen(ClientId) && Group == EGroup::HELPER)
			return 1.06f;
		return 1.0f;
	}
	switch(Group)
	{
	case EGroup::ENEMY: return std::clamp(g_Config.m_AeBlockEnemyScale / 100.0f, 1.0f, 1.5f);
	case EGroup::HELPER: return std::clamp(g_Config.m_AeBlockHelperScale / 100.0f + (IsFrozen(ClientId) ? 0.06f : 0.0f), 1.0f, 1.5f);
	case EGroup::ALLY: return std::clamp(g_Config.m_AeBlockAllyScale / 100.0f + (IsFrozen(ClientId) ? 0.10f : 0.0f), 1.0f, 1.5f);
	case EGroup::UNKNOWN: return std::clamp(g_Config.m_AeBlockNeutralScale / 100.0f, 0.8f, 1.5f);
	default: return 1.0f;
	}
}

float CAetherBlockAwareness::NameOpacity(int ClientId) const
{
	switch(GroupForClient(ClientId))
	{
	case EGroup::ENEMY: return std::clamp(g_Config.m_AeBlockEnemyNameOpacity / 100.0f, 0.0f, 1.0f);
	case EGroup::HELPER: return std::clamp(g_Config.m_AeBlockHelperNameOpacity / 100.0f, 0.0f, 1.0f);
	case EGroup::ALLY: return std::clamp(g_Config.m_AeBlockAllyNameOpacity / 100.0f, 0.0f, 1.0f);
	case EGroup::UNKNOWN: return std::clamp(g_Config.m_AeBlockNeutralNameOpacity / 100.0f, 0.0f, 1.0f);
	default: return 1.0f;
	}
}

ColorRGBA CAetherBlockAwareness::NameColorForClient(int ClientId, float BaseAlpha) const
{
	return ColorForGroup(GroupForClient(ClientId), BaseAlpha * NameOpacity(ClientId));
}

void CAetherBlockAwareness::ApplyRenderInfo(int ClientId, CTeeRenderInfo &Info) const
{
	if(!ShouldColorPlayer(ClientId))
		return;
	const EGroup Group = GroupForClient(ClientId);
	ColorRGBA Color = ColorForGroup(Group);
	if(IsFrozen(ClientId))
	{
		if(Group == EGroup::ALLY || Group == EGroup::HELPER)
		{
			Color.r = std::min(1.0f, Color.r * 1.22f + 0.08f);
			Color.g = std::min(1.0f, Color.g * 1.22f + 0.08f);
			Color.b = std::min(1.0f, Color.b * 1.22f + 0.08f);
		}
		else
		{
			Color.r *= 0.48f;
			Color.g *= 0.48f;
			Color.b *= 0.48f;
		}
	}
	Info.m_CustomColoredSkin = true;
	Info.m_ColorBody = Color;
	Info.m_ColorFeet = Color;
	for(auto &Sixup : Info.m_aSixup)
	{
		Sixup.m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
		Sixup.m_aUseCustomColors[protocol7::SKINPART_FEET] = true;
		Sixup.m_aColors[protocol7::SKINPART_BODY] = Color;
		Sixup.m_aColors[protocol7::SKINPART_FEET] = Color;
	}
}

bool CAetherBlockAwareness::DummyOwnerLabel(int ClientId, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return false;
	pBuf[0] = '\0';
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return false;

	const char *pDummyName = GameClient()->m_aClients[ClientId].m_aName;
	if(!str_find_nocase(pDummyName, "dummy"))
		return false;

	int OwnerId = -1;
	int Matches = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ClientId || !GameClient()->m_aClients[i].m_Active)
			continue;
		const char *pOwnerName = GameClient()->m_aClients[i].m_aName;
		if(str_length(pOwnerName) < 3)
			continue;
		if(!str_find_nocase(pDummyName, pOwnerName))
			continue;
		OwnerId = i;
		++Matches;
	}

	if(Matches != 1 || OwnerId < 0)
		return false;
	str_format(pBuf, BufSize, "owner: %s", GameClient()->m_aClients[OwnerId].m_aName);
	return true;
}
