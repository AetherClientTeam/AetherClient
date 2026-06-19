#include "aim_training.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

vec2 CAetherAimTraining::CenterWorld() const
{
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_pLocalCharacter)
		return GameClient()->m_LocalCharacterPos;
	return GameClient()->m_Camera.m_Center;
}

vec2 CAetherAimTraining::CurrentAimWorld() const
{
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_pLocalCharacter)
	{
		const int Dummy = g_Config.m_ClDummy;
		const vec2 MousePos = GameClient()->m_Controls.m_aMousePos[Dummy];
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[Dummy];
		const float Zoom = GameClient()->m_Camera.m_Zoom;
		return GameClient()->m_LocalCharacterPos + MousePos - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
		return GameClient()->m_Snap.m_SpecInfo.m_Position + GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	return GameClient()->m_Camera.m_Center;
}

float CAetherAimTraining::BaseRadius() const
{
	return std::clamp((float)g_Config.m_AeAimTrainingTargetSize, 8.0f, 96.0f);
}

float CAetherAimTraining::MaxDistance() const
{
	return std::clamp((float)g_Config.m_AeAimTrainingDistance, 64.0f, 560.0f);
}

float CAetherAimTraining::DespawnSeconds() const
{
	return std::clamp(g_Config.m_AeAimTrainingDespawnMs / 1000.0f, 0.25f, 10.0f);
}

bool CAetherAimTraining::IsConfiguredAndPlayable() const
{
	if(!g_Config.m_AeAimTraining)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	return true;
}

bool CAetherAimTraining::IsOverlayActive() const
{
	return IsConfiguredAndPlayable() && m_State != EState::IDLE;
}

bool CAetherAimTraining::IsRunning() const
{
	return IsConfiguredAndPlayable() && m_State == EState::RUNNING;
}

CAetherAimTraining::STarget CAetherAimTraining::NewTarget() const
{
	const float MaxR = MaxDistance();
	const float MinR = minimum(48.0f, MaxR * 0.35f);
	const float Angle = random_float(0.0f, 2.0f * pi);
	const float Dist = random_float(MinR, MaxR);

	STarget Target;
	Target.m_Offset = direction(Angle) * Dist;
	Target.m_StartRadius = BaseRadius() * (g_Config.m_AeAimTrainingShrink ? 1.75f : 1.0f);
	Target.m_Radius = Target.m_StartRadius;
	Target.m_SpawnTime = time_get();
	return Target;
}

void CAetherAimTraining::EnsureTargets()
{
	if(!IsRunning())
		return;

	const int64_t Now = time_get();
	const float Freq = (float)time_freq();
	if(g_Config.m_AeAimTrainingDespawn)
	{
		const float Life = DespawnSeconds();
		m_vTargets.erase(std::remove_if(m_vTargets.begin(), m_vTargets.end(), [&](const STarget &Target) {
			return (Now - Target.m_SpawnTime) / Freq > Life;
		}), m_vTargets.end());
	}

	const int TargetCount = std::clamp(g_Config.m_AeAimTrainingTargets, 1, 8);
	while((int)m_vTargets.size() < TargetCount)
		m_vTargets.push_back(NewTarget());

	if(g_Config.m_AeAimTrainingShrink)
	{
		const float ShrinkDuration = std::clamp(g_Config.m_AeAimTrainingShrinkMs / 1000.0f, 0.15f, 10.0f);
		for(STarget &Target : m_vTargets)
		{
			const float Age = std::clamp((Now - Target.m_SpawnTime) / Freq / ShrinkDuration, 0.0f, 1.0f);
			Target.m_Radius = mix(Target.m_StartRadius, BaseRadius(), Age);
		}
	}
}

void CAetherAimTraining::TryHit()
{
	if(!IsRunning())
		return;
	EnsureTargets();

	const vec2 Center = CenterWorld();
	const vec2 Aim = CurrentAimWorld();
	int Best = -1;
	float BestDist = 1e9f;
	for(size_t i = 0; i < m_vTargets.size(); ++i)
	{
		const vec2 Pos = Center + m_vTargets[i].m_Offset;
		const float Dist = distance(Aim, Pos);
		if(Dist <= m_vTargets[i].m_Radius && Dist < BestDist)
		{
			Best = (int)i;
			BestDist = Dist;
		}
	}

	if(Best >= 0)
	{
		m_vTargets.erase(m_vTargets.begin() + Best);
		m_Hits++;
		m_Combo++;
		m_BestCombo = maximum(m_BestCombo, m_Combo);
		EnsureTargets();
	}
	else
	{
		m_Misses++;
		m_Combo = 0;
	}
}

void CAetherAimTraining::BlockGameAttackInput()
{
	if(!IsConfiguredAndPlayable())
		return;
	ClearGameAttackInput();
}

void CAetherAimTraining::ClearGameAttackInput()
{
	CNetObj_PlayerInput &InputData = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	InputData.m_Fire = 0;
	InputData.m_Hook = 0;
}

void CAetherAimTraining::RenderDim(float *pPoints) const
{
	if(g_Config.m_AeAimTrainingDim <= 0)
		return;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, std::clamp(g_Config.m_AeAimTrainingDim / 100.0f, 0.0f, 0.75f));
	IGraphics::CQuadItem DimQuad((pPoints[0] + pPoints[2]) * 0.5f, (pPoints[1] + pPoints[3]) * 0.5f, pPoints[2] - pPoints[0], pPoints[3] - pPoints[1]);
	Graphics()->QuadsDraw(&DimQuad, 1);
	Graphics()->QuadsEnd();
}

void CAetherAimTraining::RenderTarget(const STarget &Target, vec2 Center, int64_t Now) const
{
	const vec2 Pos = Center + Target.m_Offset;
	const float Pulse = 0.5f + 0.5f * std::sin((Now / (float)time_freq()) * 5.0f);
	const ColorRGBA TargetColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AeAimTrainingTargetColor));
	const ColorRGBA Fill = TargetColor.WithAlpha(std::clamp(TargetColor.a * (0.18f + 0.08f * Pulse), 0.0f, 1.0f));
	const ColorRGBA Ring = TargetColor.WithAlpha(std::clamp(TargetColor.a, 0.0f, 1.0f));

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Fill);
	Graphics()->DrawCircle(Pos.x, Pos.y, Target.m_Radius, 32);
	Graphics()->SetColor(Ring);
	Graphics()->DrawCircle(Pos.x, Pos.y, maximum(2.0f, Target.m_Radius * 0.35f), 24);
	Graphics()->QuadsEnd();
}

void CAetherAimTraining::OnRender()
{
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;
	if(!IsOverlayActive())
		return;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	float aPoints[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	if(m_State == EState::WAITING_CLICK)
	{
		Graphics()->TextureClear();
		Graphics()->DrawRect(aPoints[0], aPoints[1], aPoints[2] - aPoints[0], aPoints[3] - aPoints[1], ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f), 0, 0.0f);

		const char *pTitle = "Click in game to start";
		const char *pHint = "Aim targets will appear after countdown";
		const float TitleSize = 22.0f;
		const float HintSize = 10.0f;
		const float CenterX = (aPoints[0] + aPoints[2]) * 0.5f;
		const float CenterY = (aPoints[1] + aPoints[3]) * 0.5f;
		const float TitleW = TextRender()->TextWidth(TitleSize, pTitle);
		const float HintW = TextRender()->TextWidth(HintSize, pHint);
		const float PanelW = std::max(TitleW, HintW) + 34.0f;
		const float PanelH = 58.0f;
		Graphics()->DrawRect(CenterX - PanelW * 0.5f, CenterY - PanelH * 0.5f, PanelW, PanelH, ColorRGBA(0.02f, 0.03f, 0.045f, 0.82f), IGraphics::CORNER_ALL, 9.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.96f);
		TextRender()->Text(CenterX - TitleW * 0.5f, CenterY - 19.0f, TitleSize, pTitle);
		TextRender()->TextColor(0.72f, 0.90f, 1.0f, 0.85f);
		TextRender()->Text(CenterX - HintW * 0.5f, CenterY + 10.0f, HintSize, pHint);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	if(m_State == EState::COUNTDOWN)
	{
		Graphics()->TextureClear();
		Graphics()->DrawRect(aPoints[0], aPoints[1], aPoints[2] - aPoints[0], aPoints[3] - aPoints[1], ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f), 0, 0.0f);

		const float Elapsed = (time_get() - m_CountdownStart) / (float)time_freq();
		const int Count = std::clamp(3 - (int)std::floor(Elapsed), 1, 3);
		char aBuf[8];
		str_format(aBuf, sizeof(aBuf), "%d", Count);
		const float CenterX = (aPoints[0] + aPoints[2]) * 0.5f;
		const float CenterY = (aPoints[1] + aPoints[3]) * 0.5f;
		Graphics()->DrawRect(CenterX - 42.0f, CenterY - 46.0f, 84.0f, 82.0f, ColorRGBA(0.02f, 0.03f, 0.045f, 0.72f), IGraphics::CORNER_ALL, 12.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.98f);
		TextRender()->Text(CenterX - TextRender()->TextWidth(52.0f, aBuf) * 0.5f, CenterY - 36.0f, 52.0f, aBuf);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	RenderDim(aPoints);

	EnsureTargets();
	const vec2 Anchor = CenterWorld();
	const int64_t Now = time_get();
	for(const STarget &Target : m_vTargets)
		RenderTarget(Target, Anchor, Now);

	char aStats[128];
	const int Shots = m_Hits + m_Misses;
	const int Accuracy = Shots > 0 ? round_to_int((float)m_Hits / (float)Shots * 100.0f) : 100;
	str_format(aStats, sizeof(aStats), "Aim Training  Hits: %d  Misses: %d  Acc: %d%%  Combo: %d  Best: %d", m_Hits, m_Misses, Accuracy, m_Combo, m_BestCombo);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.92f);
	TextRender()->Text(aPoints[0] + 10.0f, aPoints[1] + 10.0f, 12.0f, aStats);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAetherAimTraining::OnUpdate()
{
	if(!g_Config.m_AeAimTraining)
	{
		m_State = EState::IDLE;
		m_vTargets.clear();
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_State = EState::IDLE;
		m_vTargets.clear();
		return;
	}
	if(m_State == EState::IDLE)
		return;
	if(m_State == EState::COUNTDOWN && (time_get() - m_CountdownStart) / (float)time_freq() >= 3.0f)
	{
		m_State = EState::RUNNING;
		m_vTargets.clear();
		EnsureTargets();
	}
	if(m_State == EState::COUNTDOWN || m_State == EState::RUNNING)
		BlockGameAttackInput();
}

bool CAetherAimTraining::OnInput(const IInput::CEvent &Event)
{
	if(!IsOverlayActive())
		return false;
	if(GameClient()->m_Menus.IsActive())
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && m_State == EState::RUNNING)
	{
		Stop();
		return true;
	}
	if(Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2)
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) && m_State == EState::WAITING_CLICK)
		{
			m_State = EState::COUNTDOWN;
			m_CountdownStart = time_get();
			m_vTargets.clear();
		}
		else if((Event.m_Flags & IInput::FLAG_PRESS) && m_State == EState::RUNNING)
		{
			TryHit();
		}
		if(m_State == EState::COUNTDOWN || m_State == EState::RUNNING)
			BlockGameAttackInput();
		return true;
	}
	return false;
}

void CAetherAimTraining::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
	{
		m_vTargets.clear();
		m_State = EState::IDLE;
	}
}

void CAetherAimTraining::Restart()
{
	m_vTargets.clear();
	m_Hits = 0;
	m_Misses = 0;
	m_Combo = 0;
	m_BestCombo = 0;
	m_CountdownStart = 0;
	m_State = EState::WAITING_CLICK;
	g_Config.m_AeAimTraining = 1;
}

void CAetherAimTraining::Stop()
{
	ClearGameAttackInput();
	m_vTargets.clear();
	m_State = EState::IDLE;
	m_CountdownStart = 0;
	g_Config.m_AeAimTraining = 0;
}
