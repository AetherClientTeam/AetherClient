#include "psa.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/camera.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double PSA_LOW_MULT[7] = {0.50, 0.60, 0.70, 0.80, 0.90, 0.95, 0.98};
constexpr double PSA_HIGH_MULT[7] = {1.50, 1.40, 1.30, 1.20, 1.10, 1.05, 1.02};

int RoundBase(double Center)
{
	return std::clamp((int)std::llround(Center), 1, 1000000);
}

void StepLowHigh(int Step, int Base, int *pLow, int *pHigh)
{
	if(Step < 0 || Step >= 7)
		return;
	const int Low = std::clamp((int)std::llround((double)Base * PSA_LOW_MULT[Step]), 1, 1000000);
	const int High = std::clamp((int)std::llround((double)Base * PSA_HIGH_MULT[Step]), 1, 1000000);
	if(pLow)
		*pLow = Low;
	if(pHigh)
		*pHigh = High;
}
} // namespace

void CAetherPsa::ApplyPointerValue(int Value) const
{
	const int V = std::clamp(Value, 1, 1000000);
	g_Config.m_InpMousesens = V;
}

void CAetherPsa::AutoCancelInternal()
{
	m_AutoPhase = AUTO_NONE;
	m_AutoCountdown = 0;
	m_AutoCountdownNext = 0.0;
	m_AutoPhaseEnd = 0.0;
}

void CAetherPsa::AutoBeginLowPhase()
{
	ApplyCurrent(true);
	m_AutoPhase = AUTO_LOW;
	m_AutoPhaseEnd = (double)LocalTime() + (double)std::clamp(m_AutoPhaseSeconds, 1, 3600);
}

void CAetherPsa::AutoBeginHighPhase()
{
	ApplyCurrent(false);
	m_AutoPhase = AUTO_HIGH;
	m_AutoPhaseEnd = (double)LocalTime() + (double)std::clamp(m_AutoPhaseSeconds, 1, 3600);
}

void CAetherPsa::AutoStopForPick()
{
	ApplyBase();
	AutoCancelInternal();
	m_AutoPhase = AUTO_WAIT_PICK;
	m_TimerActive = false;
	m_TimerDone = true;
}

void CAetherPsa::OnReset()
{
	m_TimerActive = false;
	m_TimerDone = false;
	AutoCancelInternal();
}

void CAetherPsa::OnUpdate()
{
	if(!g_Config.m_AePsa)
	{
		AutoCancelInternal();
		m_TimerActive = false;
		return;
	}

	const double Now = (double)LocalTime();
	if(m_AutoPhase == AUTO_WAIT_START_CLICK)
		return;

	if(m_AutoPhase == AUTO_COUNTDOWN && Now >= m_AutoCountdownNext)
	{
		m_AutoCountdown--;
		if(m_AutoCountdown > 0)
			m_AutoCountdownNext = Now + 1.0;
		else
			AutoBeginLowPhase();
	}
	else if(m_AutoPhase == AUTO_LOW && Now >= m_AutoPhaseEnd)
	{
		AutoBeginHighPhase();
	}
	else if(m_AutoPhase == AUTO_HIGH && Now >= m_AutoPhaseEnd)
	{
		ApplyBase();
		AutoCancelInternal();
		m_AutoPhase = AUTO_WAIT_PICK;
		m_TimerActive = false;
		m_TimerDone = true;
	}

	if(!AutoInProgress() && m_TimerActive && Now >= m_TimerEnd)
	{
		m_TimerDone = true;
		m_TimerActive = false;
	}
}

void CAetherPsa::OnRender()
{
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi)
		return;
	if(!g_Config.m_AePsa)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(m_AutoPhase == AUTO_NONE && !m_TimerActive)
		return;
	if(m_AutoPhase == AUTO_WAIT_PICK)
		return;

	float PrevX0, PrevY0, PrevX1, PrevY1;
	Graphics()->GetScreen(&PrevX0, &PrevY0, &PrevX1, &PrevY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->m_Camera.m_Zoom);
	float ViewW = 1000.0f, ViewH = 800.0f;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &ViewW, &ViewH);
	const vec2 C = GameClient()->m_Camera.m_Center;
	const float ScreenX0 = C.x - ViewW * 0.5f;
	const float ScreenY0 = C.y - ViewH * 0.5f;
	const float ScreenX1 = C.x + ViewW * 0.5f;
	const float ScreenY1 = C.y + ViewH * 0.5f;
	const float X = ScreenX0 + 12.0f;
	const float Y = ScreenY0 + 10.0f;

	char aBuf[96];
	if(m_AutoPhase == AUTO_WAIT_START_CLICK)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.42f);
		IGraphics::CQuadItem DimQuad((ScreenX0 + ScreenX1) * 0.5f, (ScreenY0 + ScreenY1) * 0.5f, ScreenX1 - ScreenX0, ScreenY1 - ScreenY0);
		Graphics()->QuadsDraw(&DimQuad, 1);
		Graphics()->QuadsEnd();

		const char *pTitle = "Click in game to start PSA";
		const char *pHint = "Movement, fire and hook stay active";
		const float TitleSize = 22.0f;
		const float HintSize = 10.0f;
		const float TitleW = TextRender()->TextWidth(TitleSize, pTitle);
		const float HintW = TextRender()->TextWidth(HintSize, pHint);
		const float PanelW = std::max(TitleW, HintW) + 34.0f;
		const float PanelH = 58.0f;
		const float PanelX = (ScreenX0 + ScreenX1) * 0.5f - PanelW * 0.5f;
		const float PanelY = (ScreenY0 + ScreenY1) * 0.5f - PanelH * 0.5f;
		Graphics()->DrawRect(PanelX, PanelY, PanelW, PanelH, ColorRGBA(0.02f, 0.03f, 0.045f, 0.82f), IGraphics::CORNER_ALL, 9.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.96f);
		TextRender()->Text((ScreenX0 + ScreenX1) * 0.5f - TitleW * 0.5f, PanelY + 10.0f, TitleSize, pTitle);
		TextRender()->TextColor(0.72f, 0.90f, 1.0f, 0.85f);
		TextRender()->Text((ScreenX0 + ScreenX1) * 0.5f - HintW * 0.5f, PanelY + 38.0f, HintSize, pHint);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
		return;
	}
	if(m_AutoPhase == AUTO_COUNTDOWN)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.42f);
		IGraphics::CQuadItem DimQuad((ScreenX0 + ScreenX1) * 0.5f, (ScreenY0 + ScreenY1) * 0.5f, ScreenX1 - ScreenX0, ScreenY1 - ScreenY0);
		Graphics()->QuadsDraw(&DimQuad, 1);
		Graphics()->QuadsEnd();

		str_format(aBuf, sizeof(aBuf), "%d", std::clamp(m_AutoCountdown, 1, 3));
		const float FontSize = 52.0f;
		const float TextW = TextRender()->TextWidth(FontSize, aBuf);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.98f);
		TextRender()->Text((ScreenX0 + ScreenX1) * 0.5f - TextW * 0.5f, (ScreenY0 + ScreenY1) * 0.5f - 34.0f, FontSize, aBuf);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
		return;
	}
	else if(m_AutoPhase == AUTO_LOW || m_AutoPhase == AUTO_HIGH)
	{
		const int Seconds = (int)std::ceil(std::max(0.0, m_AutoPhaseEnd - (double)LocalTime()));
		str_format(aBuf, sizeof(aBuf), "PSA %s %d:%02d", m_AutoPhase == AUTO_LOW ? "Low" : "High", Seconds / 60, Seconds % 60);
	}
	else
	{
		const int Seconds = (int)std::ceil(std::max(0.0, m_TimerEnd - (double)LocalTime()));
		str_format(aBuf, sizeof(aBuf), "PSA %d:%02d", Seconds / 60, Seconds % 60);
	}

	TextRender()->TextColor(0.55f, 0.92f, 0.98f, 1.0f);
	TextRender()->Text(X, Y, 22.0f, aBuf);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
}

bool CAetherPsa::OnInput(const IInput::CEvent &Event)
{
	if(!g_Config.m_AePsa || GameClient()->m_Menus.IsActive())
		return false;
	if(m_AutoPhase == AUTO_NONE || m_AutoPhase == AUTO_WAIT_PICK)
		return false;
	if((Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2) && (Event.m_Flags & IInput::FLAG_PRESS) && m_AutoPhase == AUTO_WAIT_START_CLICK)
	{
		m_AutoPhase = AUTO_COUNTDOWN;
		m_AutoCountdown = 3;
		m_AutoCountdownNext = (double)LocalTime() + 1.0;
		m_AutoPhaseEnd = 0.0;
		return false;
	}
	return false;
}

void CAetherPsa::Start(int BaseValue)
{
	g_Config.m_AePsa = 1;
	AutoCancelInternal();
	m_Active = true;
	m_Completed = false;
	m_Step = 0;
	m_Center = (float)std::clamp(BaseValue, 1, 1000000);
}

void CAetherPsa::Reset(int BaseValue)
{
	AutoCancelInternal();
	m_Active = false;
	m_Completed = false;
	m_Step = 0;
	m_Center = (float)std::clamp(BaseValue, 1, 1000000);
	m_TimerActive = false;
	m_TimerDone = false;
}

void CAetherPsa::GetTriplet(int *pLow, int *pBase, int *pHigh) const
{
	const int Base = RoundBase((double)m_Center);
	int Low = Base;
	int High = Base;
	if(IsActive() && m_Step >= 0 && m_Step < 7)
		StepLowHigh(m_Step, Base, &Low, &High);
	if(pLow)
		*pLow = Low;
	if(pBase)
		*pBase = Base;
	if(pHigh)
		*pHigh = High;
}

void CAetherPsa::ApplyCurrent(bool UseLow) const
{
	int Low = 0, High = 0;
	GetTriplet(&Low, nullptr, &High);
	ApplyPointerValue(UseLow ? Low : High);
}

void CAetherPsa::ApplyBase() const
{
	int Base = 0;
	GetTriplet(nullptr, &Base, nullptr);
	ApplyPointerValue(Base);
}

void CAetherPsa::SelectLow()
{
	if(!IsActive() || m_Step < 0 || m_Step >= 7)
		return;
	AutoCancelInternal();
	int Low = RoundBase((double)m_Center);
	StepLowHigh(m_Step, RoundBase((double)m_Center), &Low, nullptr);
	m_Center = (float)Low;
	m_Step++;
	if(m_Step >= 7)
	{
		m_Active = false;
		m_Completed = true;
	}
}

void CAetherPsa::SelectHigh()
{
	if(!IsActive() || m_Step < 0 || m_Step >= 7)
		return;
	AutoCancelInternal();
	int High = RoundBase((double)m_Center);
	StepLowHigh(m_Step, RoundBase((double)m_Center), nullptr, &High);
	m_Center = (float)High;
	m_Step++;
	if(m_Step >= 7)
	{
		m_Active = false;
		m_Completed = true;
	}
}

void CAetherPsa::StartTimer(int Seconds)
{
	if(AutoInProgress())
		return;
	g_Config.m_AePsa = 1;
	m_TimerActive = true;
	m_TimerDone = false;
	m_TimerEnd = (double)LocalTime() + (double)std::clamp(Seconds, 1, 3600);
}

void CAetherPsa::AutoStart(int PhaseSeconds)
{
	if(!IsActive() || AutoInProgress() || m_AutoPhase == AUTO_WAIT_PICK)
		return;
	g_Config.m_AePsa = 1;
	m_AutoPhaseSeconds = std::clamp(PhaseSeconds, 1, 3600);
	m_AutoPhase = AUTO_WAIT_START_CLICK;
	m_AutoCountdown = 0;
	m_AutoCountdownNext = 0.0;
	m_AutoPhaseEnd = 0.0;
	ApplyBase();
}

void CAetherPsa::AutoCancel()
{
	if(AutoInProgress())
		ApplyBase();
	AutoCancelInternal();
}

void CAetherPsa::AutoSkip()
{
	if(m_AutoPhase == AUTO_WAIT_START_CLICK || m_AutoPhase == AUTO_COUNTDOWN)
		AutoBeginLowPhase();
	else if(m_AutoPhase == AUTO_LOW)
		AutoBeginHighPhase();
	else if(m_AutoPhase == AUTO_HIGH)
	{
		ApplyBase();
		AutoCancelInternal();
		m_AutoPhase = AUTO_WAIT_PICK;
		m_TimerActive = false;
		m_TimerDone = true;
	}
}

bool CAetherPsa::AutoInProgress() const
{
	return m_AutoPhase == AUTO_WAIT_START_CLICK || m_AutoPhase == AUTO_COUNTDOWN || m_AutoPhase == AUTO_LOW || m_AutoPhase == AUTO_HIGH;
}

int CAetherPsa::StepDisplay() const
{
	if(m_Completed)
		return 7;
	return std::clamp(m_Step + 1, 1, 7);
}

int CAetherPsa::SuggestedValue() const
{
	return RoundBase((double)m_Center);
}
