#include "finish_prediction.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace
{
bool AetherIsFinishTileAt(const CCollision *pCollision, vec2 Pos)
{
	if(!pCollision)
		return false;
	const int Index = pCollision->GetPureMapIndex(Pos);
	return pCollision->GetTileIndex(Index) == TILE_FINISH || pCollision->GetFrontTileIndex(Index) == TILE_FINISH;
}

bool AetherIsOnFinishTile(const CCollision *pCollision, vec2 Pos)
{
	constexpr float Radius = 9.5f;
	const vec2 aCheckPos[] = {
		Pos,
		vec2(Pos.x + Radius, Pos.y - Radius),
		vec2(Pos.x + Radius, Pos.y + Radius),
		vec2(Pos.x - Radius, Pos.y - Radius),
		vec2(Pos.x - Radius, Pos.y + Radius),
	};
	for(const vec2 CheckPos : aCheckPos)
	{
		if(AetherIsFinishTileAt(pCollision, CheckPos))
			return true;
	}
	return false;
}
}

float CAetherFinishPrediction::PanelScale() const
{
	return std::clamp(g_Config.m_AeFinishPredictionScale / 100.0f, 0.5f, 2.0f);
}

CUIRect CAetherFinishPrediction::PanelRect() const
{
	const float Scale = PanelScale();
	const float Width = 78.0f * Scale;
	const float Height = 24.0f * Scale;
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	return CUIRect(ScreenWidth * 0.5f - Width * 0.5f + g_Config.m_AeFinishPredictionOffsetX, g_Config.m_AeFinishPredictionOffsetY, Width, Height);
}

CUIRect CAetherFinishPrediction::ResizeHandleRect() const
{
	const float Scale = PanelScale();
	return CUIRect(m_LastRect.x + m_LastRect.w - 6.0f * Scale, m_LastRect.y + m_LastRect.h - 6.0f * Scale, 6.0f * Scale, 6.0f * Scale);
}

vec2 CAetherFinishPrediction::HudMousePos() const
{
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return Input()->NativeMousePos() / WindowSize * vec2(300.0f * Graphics()->ScreenAspect(), 300.0f);
}

void CAetherFinishPrediction::ClampOffsets()
{
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 300.0f;
	CUIRect Rect = PanelRect();
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, ScreenWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, ScreenHeight - Rect.h));
	g_Config.m_AeFinishPredictionOffsetX = round_to_int(Rect.x - (ScreenWidth - Rect.w) * 0.5f);
	g_Config.m_AeFinishPredictionOffsetY = round_to_int(Rect.y);
}

void CAetherFinishPrediction::ApplyCenterSnap(float ScreenWidth, float ScreenHeight, float PanelWidth, float PanelHeight)
{
	const CUIRect Rect = PanelRect();
	if(std::abs((Rect.x + PanelWidth * 0.5f) - ScreenWidth * 0.5f) <= 4.0f)
		g_Config.m_AeFinishPredictionOffsetX = 0;
	if(std::abs((Rect.y + PanelHeight * 0.5f) - ScreenHeight * 0.5f) <= 4.0f)
		g_Config.m_AeFinishPredictionOffsetY = round_to_int(ScreenHeight * 0.5f - PanelHeight * 0.5f);
	ClampOffsets();
}

void CAetherFinishPrediction::SetScaleKeepingCenter(int NewScale, vec2 Center)
{
	g_Config.m_AeFinishPredictionScale = std::clamp(NewScale, 50, 200);
	const CUIRect Rect = PanelRect();
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	g_Config.m_AeFinishPredictionOffsetX = round_to_int(Center.x - ScreenWidth * 0.5f);
	g_Config.m_AeFinishPredictionOffsetY = round_to_int(Center.y - Rect.h * 0.5f);
	ApplyCenterSnap(ScreenWidth, 300.0f, Rect.w, Rect.h);
}

void CAetherFinishPrediction::ResetRunState()
{
	m_RaceStartTick = -1;
	m_RaceStartDistance = -1.0f;
	m_LastProgress = 0.0f;
	m_SmoothedFinishTimeMs = -1;
	m_LastPredictTick = -1;
}

void CAetherFinishPrediction::ClearPathData()
{
	m_vDistances.clear();
	m_vPassable.clear();
	m_vStartTiles.clear();
	m_vFinishTiles.clear();
	m_PathMapWidth = 0;
	m_PathMapHeight = 0;
}

void CAetherFinishPrediction::OnReset()
{
	ResetRunState();
	ClearPathData();
}

bool CAetherFinishPrediction::CurrentLocalPos(vec2 *pPos) const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
		return false;
	const auto &SnapChar = GameClient()->m_Snap.m_aCharacters[LocalId];
	const vec2 PredPos = GameClient()->m_aClients[LocalId].m_Predicted.m_Pos;
	if(SnapChar.m_Active && (PredPos.x != 0.0f || PredPos.y != 0.0f))
	{
		*pPos = PredPos;
		return true;
	}
	if(GameClient()->m_Snap.m_pLocalCharacter)
	{
		*pPos = vec2(GameClient()->m_Snap.m_pLocalCharacter->m_X, GameClient()->m_Snap.m_pLocalCharacter->m_Y);
		return true;
	}
	*pPos = GameClient()->m_aClients[LocalId].m_RenderPos;
	return true;
}

bool CAetherFinishPrediction::RebuildPathData()
{
	ClearPathData();
	ResetRunState();

	if(!Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;

	m_PathMapWidth = Collision()->GetWidth();
	m_PathMapHeight = Collision()->GetHeight();
	const int MapSize = m_PathMapWidth * m_PathMapHeight;
	if(MapSize <= 0)
		return false;
	m_vDistances.assign(MapSize, -1);
	m_vPassable.assign(MapSize, 0);

	auto IsPassableTile = [&](int TileX, int TileY) {
		if(TileX < 0 || TileX >= m_PathMapWidth || TileY < 0 || TileY >= m_PathMapHeight)
			return false;
		const int Index = TileY * m_PathMapWidth + TileX;
		if(m_vPassable[Index] != 0)
			return true;
		const vec2 TileCenter(TileX * 32.0f + 16.0f, TileY * 32.0f + 16.0f);
		return !Collision()->TestBox(TileCenter, vec2(CCharacterCore::PhysicalSize(), CCharacterCore::PhysicalSize()));
	};

	for(int y = 0; y < m_PathMapHeight; ++y)
	{
		for(int x = 0; x < m_PathMapWidth; ++x)
		{
			const int Index = y * m_PathMapWidth + x;
			m_vPassable[Index] = IsPassableTile(x, y) ? 1 : 0;
		}
	}

	using TDistanceNode = std::pair<int, int>;
	std::priority_queue<TDistanceNode, std::vector<TDistanceNode>, std::greater<TDistanceNode>> PriorityQueue;
	for(int y = 0; y < m_PathMapHeight; ++y)
	{
		for(int x = 0; x < m_PathMapWidth; ++x)
		{
			const int Index = y * m_PathMapWidth + x;
			const bool StartTile = Collision()->GetTileIndex(Index) == TILE_START || Collision()->GetFrontTileIndex(Index) == TILE_START;
			const bool FinishTile = Collision()->GetTileIndex(Index) == TILE_FINISH || Collision()->GetFrontTileIndex(Index) == TILE_FINISH;
			if(StartTile)
				m_vStartTiles.emplace_back(x, y);
			if(FinishTile && m_vPassable[Index] != 0)
			{
				m_vFinishTiles.emplace_back(x, y);
				m_vDistances[Index] = 0;
				PriorityQueue.emplace(0, Index);
			}
		}
	}

	if(PriorityQueue.empty())
		return false;

	struct SDir
	{
		ivec2 m_Dir;
		int m_Cost;
	};
	static const SDir s_aDirs[] = {
		{{1, 0}, 10},
		{{-1, 0}, 10},
		{{0, 1}, 10},
		{{0, -1}, 10},
		{{1, 1}, 14},
		{{1, -1}, 14},
		{{-1, 1}, 14},
		{{-1, -1}, 14},
	};

	while(!PriorityQueue.empty())
	{
		const auto [CurDist, Index] = PriorityQueue.top();
		PriorityQueue.pop();
		if(Index < 0 || Index >= MapSize || m_vDistances[Index] != CurDist)
			continue;
		const int TileX = Index % m_PathMapWidth;
		const int TileY = Index / m_PathMapWidth;
		for(const SDir &DirInfo : s_aDirs)
		{
			const int NextX = TileX + DirInfo.m_Dir.x;
			const int NextY = TileY + DirInfo.m_Dir.y;
			if(NextX < 0 || NextX >= m_PathMapWidth || NextY < 0 || NextY >= m_PathMapHeight)
				continue;
			const int NextIndex = NextY * m_PathMapWidth + NextX;
			if(m_vPassable[NextIndex] == 0)
				continue;
			if(DirInfo.m_Dir.x != 0 && DirInfo.m_Dir.y != 0)
			{
				const int SideIndexX = TileY * m_PathMapWidth + NextX;
				const int SideIndexY = NextY * m_PathMapWidth + TileX;
				if(m_vPassable[SideIndexX] == 0 || m_vPassable[SideIndexY] == 0)
					continue;
			}
			const int NextDistance = CurDist + DirInfo.m_Cost;
			if(m_vDistances[NextIndex] >= 0 && m_vDistances[NextIndex] <= NextDistance)
				continue;
			m_vDistances[NextIndex] = NextDistance;
			PriorityQueue.emplace(NextDistance, NextIndex);
		}
	}

	return true;
}

bool CAetherFinishPrediction::EnsurePathData()
{
	if(!Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;
	if(m_PathMapWidth != Collision()->GetWidth() || m_PathMapHeight != Collision()->GetHeight() || m_vDistances.empty())
		return RebuildPathData();
	return !m_vDistances.empty();
}

float CAetherFinishPrediction::DistanceAtPos(vec2 Pos) const
{
	if(m_vDistances.empty() || m_PathMapWidth <= 0 || m_PathMapHeight <= 0)
		return -1.0f;

	const int TileX = std::clamp((int)std::floor(Pos.x / 32.0f), 0, m_PathMapWidth - 1);
	const int TileY = std::clamp((int)std::floor(Pos.y / 32.0f), 0, m_PathMapHeight - 1);

	float BestDistance = -1.0f;
	for(int Radius = 0; Radius <= 2; ++Radius)
	{
		for(int y = maximum(0, TileY - Radius); y <= minimum(m_PathMapHeight - 1, TileY + Radius); ++y)
		{
			for(int x = maximum(0, TileX - Radius); x <= minimum(m_PathMapWidth - 1, TileX + Radius); ++x)
			{
				const int Index = y * m_PathMapWidth + x;
				const int Dist = m_vDistances[Index];
				if(Dist < 0)
					continue;
				const float OffsetCost = distance(Pos, vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f)) / 32.0f;
				const float Total = Dist / 10.0f + OffsetCost;
				if(BestDistance < 0.0f || Total < BestDistance)
					BestDistance = Total;
			}
		}
		if(BestDistance >= 0.0f)
			break;
	}
	return BestDistance;
}

float CAetherFinishPrediction::StartDistance() const
{
	if(m_RaceStartDistance > 0.0f)
		return m_RaceStartDistance;

	float BestDistance = -1.0f;
	for(const ivec2 &StartTile : m_vStartTiles)
	{
		const int Index = StartTile.y * m_PathMapWidth + StartTile.x;
		if(Index < 0 || Index >= (int)m_vDistances.size())
			continue;
		const int Dist = m_vDistances[Index];
		if(Dist < 0)
			continue;
		const float DistanceTiles = Dist / 10.0f;
		if(BestDistance < 0.0f || DistanceTiles < BestDistance)
			BestDistance = DistanceTiles;
	}
	return BestDistance;
}

int64_t CAetherFinishPrediction::ScoreboardTimeMs(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return -1;
	const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	if(!pPlayerInfo)
		return -1;

	const bool Race7 = Client()->IsSixup() && GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE);
	if(Race7)
	{
		if(pPlayerInfo->m_Score == protocol7::FinishTime::NOT_FINISHED)
			return -1;
		return maximum<int64_t>(0, pPlayerInfo->m_Score);
	}

	if(GameClient()->m_GameInfo.m_TimeScore)
	{
		if(pPlayerInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE)
			return -1;
		return maximum<int64_t>(0, pPlayerInfo->m_Score) * 1000;
	}

	return -1;
}

int64_t CAetherFinishPrediction::BestTimeMs() const
{
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		return (int64_t)GameClient()->m_MapBestTimeSeconds * 1000 + GameClient()->m_MapBestTimeMillis;

	int64_t Best = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const int64_t ScoreTime = ScoreboardTimeMs(i);
		if(ScoreTime <= 0)
			continue;
		if(Best < 0 || ScoreTime < Best)
			Best = ScoreTime;
	}
	return Best;
}

int64_t CAetherFinishPrediction::PersonalBestTimeMs() const
{
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes && LocalClientId >= 0)
	{
		const auto &ClientData = GameClient()->m_aClients[LocalClientId];
		if(ClientData.m_FinishTimeSeconds != FinishTime::UNSET && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
			return (int64_t)absolute(ClientData.m_FinishTimeSeconds) * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);
	}
	return ScoreboardTimeMs(LocalClientId);
}

int64_t CAetherFinishPrediction::AverageTimeMs() const
{
	int64_t Sum = 0;
	int Count = 0;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;
			const auto &ClientData = GameClient()->m_aClients[i];
			if(ClientData.m_FinishTimeSeconds == FinishTime::UNSET || ClientData.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS)
				continue;
			Sum += (int64_t)absolute(ClientData.m_FinishTimeSeconds) * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);
			++Count;
		}
	}
	else
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const int64_t ScoreTime = ScoreboardTimeMs(i);
			if(ScoreTime <= 0)
				continue;
			Sum += ScoreTime;
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : -1;
}

int64_t CAetherFinishPrediction::ReferenceTimeMs() const
{
	const int64_t Best = BestTimeMs();
	const int64_t Personal = PersonalBestTimeMs();
	const int64_t Average = AverageTimeMs();
	if(Best > 0 && Average > 0 && Personal > 0)
		return (Best + Average + Personal) / 3;
	if(Best > 0 && Average > 0)
		return (Best + Average) / 2;
	if(Personal > 0 && Average > 0)
		return (Personal + Average) / 2;
	if(Best > 0 && Personal > 0)
		return (Best + Personal) / 2;
	if(Personal > 0)
		return Personal;
	if(Best > 0)
		return Best;
	if(Average > 0)
		return Average;
	return -1;
}

bool CAetherFinishPrediction::TimeFallback(SEstimateState *pState, int64_t CurrentTimeMs)
{
	const int64_t Reference = ReferenceTimeMs();
	const int64_t TotalMs = std::clamp<int64_t>(Reference > CurrentTimeMs ? Reference : 90000, 30000, 24 * 60 * 60 * 1000);
	const float CandidateProgress = std::clamp(CurrentTimeMs / (float)TotalMs, 0.0f, 0.995f);
	pState->m_Valid = true;
	pState->m_HasPredictedTime = Reference > CurrentTimeMs || CurrentTimeMs > 2500;
	pState->m_Progress = std::max(m_LastProgress, CandidateProgress);
	pState->m_CurrentTimeMs = CurrentTimeMs;
	pState->m_PredictedFinishTimeMs = pState->m_HasPredictedTime ? TotalMs : 0;
	pState->m_RemainingTimeMs = pState->m_HasPredictedTime ? maximum<int64_t>(0, TotalMs - CurrentTimeMs) : 0;
	m_LastProgress = pState->m_Progress;
	return pState->m_Progress >= 0.005f || g_Config.m_AeFinishPredictionShowAlways || m_EditorOpen;
}

bool CAetherFinishPrediction::EstimateState(SEstimateState *pState)
{
	*pState = {};
	if(!GameClient()->m_GameInfo.m_Race || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		ResetRunState();
		return false;
	}

	const int RaceTick = GameClient()->LastRaceTick();
	if(RaceTick < 0)
	{
		ResetRunState();
		if(!g_Config.m_AeFinishPredictionShowAlways && !m_EditorOpen)
			return false;
		pState->m_Valid = true;
		return true;
	}

	const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	const int TickSpeed = maximum(1, Client()->GameTickSpeed());
	const int64_t CurrentTimeMs = maximum<int64_t>(0, (int64_t)(CurrentTick - RaceTick) * 1000 / TickSpeed);

	vec2 Pos;
	if(!CurrentLocalPos(&Pos))
	{
		ResetRunState();
		return false;
	}

	if(AetherIsOnFinishTile(Collision(), Pos))
	{
		pState->m_Valid = true;
		pState->m_HasPredictedTime = true;
		pState->m_Progress = 1.0f;
		pState->m_CurrentTimeMs = CurrentTimeMs;
		pState->m_PredictedFinishTimeMs = CurrentTimeMs;
		pState->m_RemainingTimeMs = 0;
		m_RaceStartTick = RaceTick;
		m_RaceStartDistance = -1.0f;
		m_LastProgress = 1.0f;
		m_SmoothedFinishTimeMs = CurrentTimeMs;
		m_LastPredictTick = CurrentTick;
		return true;
	}

	if(!EnsurePathData())
		return TimeFallback(pState, CurrentTimeMs);

	const float CurrentDistance = DistanceAtPos(Pos);
	if(CurrentDistance < 0.0f)
		return TimeFallback(pState, CurrentTimeMs);

	if(m_RaceStartTick != RaceTick)
	{
		m_RaceStartTick = RaceTick;
		m_RaceStartDistance = maximum(CurrentDistance, StartDistance());
		m_LastProgress = 0.0f;
		m_SmoothedFinishTimeMs = -1;
		m_LastPredictTick = -1;
	}

	const float StartDist = m_RaceStartDistance > 0.0f ? maximum(m_RaceStartDistance, 1.0f) : maximum(StartDistance(), CurrentDistance);
	if(StartDist <= 4.0f)
		return TimeFallback(pState, CurrentTimeMs);

	float RawProgress = std::clamp(1.0f - CurrentDistance / StartDist, 0.0f, 1.0f);
	if(CurrentDistance <= 0.5f)
		RawProgress = 1.0f;

	const float EarlyProgressLimit = std::clamp((float)CurrentTimeMs / 45000.0f + 0.08f, 0.10f, 0.35f);
	if(CurrentTimeMs < 15000 && RawProgress > EarlyProgressLimit)
		return TimeFallback(pState, CurrentTimeMs);

	pState->m_Valid = true;
	pState->m_CurrentTimeMs = CurrentTimeMs;
	pState->m_Progress = std::max(m_LastProgress, RawProgress);
	m_LastProgress = pState->m_Progress;

	const int64_t CurrentPacePrediction = pState->m_Progress > 0.015f && CurrentTimeMs > 1500 ? (int64_t)(CurrentTimeMs / maximum(pState->m_Progress, 0.015f)) : -1;
	const int64_t Reference = ReferenceTimeMs();

	if(pState->m_Progress >= 0.999f)
	{
		pState->m_PredictedFinishTimeMs = CurrentTimeMs;
		pState->m_HasPredictedTime = true;
		m_SmoothedFinishTimeMs = CurrentTimeMs;
		m_LastPredictTick = CurrentTick;
	}
	else if(CurrentPacePrediction > 0 && Reference > 0)
	{
		const float ProgressConfidence = std::clamp((pState->m_Progress - 0.04f) / 0.34f, 0.0f, 1.0f);
		const float TimeConfidence = std::clamp(CurrentTimeMs / 45000.0f, 0.0f, 1.0f);
		const float Blend = std::clamp(ProgressConfidence * 0.78f + TimeConfidence * 0.22f, 0.0f, 0.96f);
		pState->m_PredictedFinishTimeMs = (int64_t)mix((float)Reference, (float)CurrentPacePrediction, Blend);
		pState->m_HasPredictedTime = true;
	}
	else if(CurrentPacePrediction > 0)
	{
		pState->m_PredictedFinishTimeMs = CurrentPacePrediction;
		pState->m_HasPredictedTime = true;
	}
	else if(Reference > 0)
	{
		pState->m_PredictedFinishTimeMs = Reference;
		pState->m_HasPredictedTime = true;
	}

	if(pState->m_HasPredictedTime)
	{
		pState->m_PredictedFinishTimeMs = maximum<int64_t>(pState->m_PredictedFinishTimeMs, CurrentTimeMs);
		if(pState->m_Progress < 0.999f)
		{
			if(m_SmoothedFinishTimeMs < 0)
			{
				m_SmoothedFinishTimeMs = pState->m_PredictedFinishTimeMs;
				m_LastPredictTick = CurrentTick;
			}
			else if(m_LastPredictTick != CurrentTick)
			{
				const int TickDelta = maximum(1, CurrentTick - maximum(0, m_LastPredictTick));
				const float Follow = pState->m_PredictedFinishTimeMs < m_SmoothedFinishTimeMs ? 0.075f : 0.045f;
				const float Blend = std::clamp(TickDelta * Follow, 0.035f, 0.30f);
				m_SmoothedFinishTimeMs = (int64_t)mix((float)m_SmoothedFinishTimeMs, (float)pState->m_PredictedFinishTimeMs, Blend);
				m_LastPredictTick = CurrentTick;
			}
			pState->m_PredictedFinishTimeMs = maximum<int64_t>(m_SmoothedFinishTimeMs, CurrentTimeMs);
		}
		pState->m_RemainingTimeMs = maximum<int64_t>(0, pState->m_PredictedFinishTimeMs - CurrentTimeMs);
	}

	return pState->m_Progress >= 0.005f || pState->m_HasPredictedTime || g_Config.m_AeFinishPredictionShowAlways || m_EditorOpen;
}

bool CAetherFinishPrediction::Estimate(float *pProgress, int *pElapsedMs, int *pLeftMs)
{
	SEstimateState State;
	if(!EstimateState(&State))
		return false;
	*pProgress = std::clamp(State.m_Progress, 0.0f, 1.0f);
	*pElapsedMs = (int)std::clamp<int64_t>(State.m_CurrentTimeMs, 0, std::numeric_limits<int>::max());
	*pLeftMs = (int)std::clamp<int64_t>(State.m_RemainingTimeMs, 0, std::numeric_limits<int>::max());
	return State.m_Valid;
}

static void AetherFormatTimeMs(int TimeMs, bool Centis, char *pBuf, int BufSize)
{
	const int TotalSeconds = std::max(0, TimeMs / 1000);
	const int Centiseconds = (TimeMs % 1000) / 10;
	if(Centis)
	{
		if(TotalSeconds >= 3600)
			str_format(pBuf, BufSize, "%d:%02d:%02d.%02d", TotalSeconds / 3600, (TotalSeconds / 60) % 60, TotalSeconds % 60, Centiseconds);
		else
			str_format(pBuf, BufSize, "%02d:%02d.%02d", (TotalSeconds / 60) % 60, TotalSeconds % 60, Centiseconds);
	}
	else
	{
		if(TotalSeconds >= 3600)
			str_format(pBuf, BufSize, "%d:%02d:%02d", TotalSeconds / 3600, (TotalSeconds / 60) % 60, TotalSeconds % 60);
		else
			str_format(pBuf, BufSize, "%02d:%02d", (TotalSeconds / 60) % 60, TotalSeconds % 60);
	}
}

void CAetherFinishPrediction::RenderPanel(CUIRect Rect)
{
	float Progress = 0.0f;
	int ElapsedMs = 0;
	int LeftMs = 0;
	if(!Estimate(&Progress, &ElapsedMs, &LeftMs) && !m_EditorOpen)
		return;

	const float Scale = PanelScale();
	Graphics()->TextureClear();
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.10f, 0.15f, 0.18f, 0.72f), IGraphics::CORNER_ALL, 4.0f * Scale);
	Graphics()->DrawRect(Rect.x, Rect.y + Rect.h - 2.4f * Scale, Rect.w * std::clamp(Progress, 0.0f, 1.0f), 2.4f * Scale, ColorRGBA(0.55f, 0.85f, 1.0f, 0.62f), IGraphics::CORNER_B, 2.0f * Scale);

	const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.25f);
	char aTime[48];
	const int ShownMs = g_Config.m_AeFinishPredictionMode == 0 ? LeftMs : ElapsedMs + LeftMs;
	AetherFormatTimeMs(ShownMs, g_Config.m_AeFinishPredictionMilliseconds != 0, aTime, sizeof(aTime));
	char aLabel[64];
	str_format(aLabel, sizeof(aLabel), "%s %s", g_Config.m_AeFinishPredictionMode == 0 ? "Left" : "Finish", Progress < 0.005f ? "--:--" : aTime);

	const float MainFont = 6.3f * Scale;
	const float SmallFont = 5.4f * Scale;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.96f);
	if(g_Config.m_AeFinishPredictionShowTime)
		TextRender()->Text(Rect.x + Rect.w * 0.5f - TextRender()->TextWidth(MainFont, aLabel) * 0.5f, Rect.y + 4.0f * Scale, MainFont, aLabel, -1.0f);
	if(g_Config.m_AeFinishPredictionPercentage)
	{
		char aPct[32];
		str_format(aPct, sizeof(aPct), "%.1f%%", Progress * 100.0f);
		TextRender()->TextColor(0.76f, 0.86f, 0.96f, 0.96f);
		TextRender()->Text(Rect.x + Rect.w * 0.5f - TextRender()->TextWidth(SmallFont, aPct) * 0.5f, Rect.y + 13.4f * Scale, SmallFont, aPct, -1.0f);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->TextOutlineColor(OldOutline);

	if(m_EditorOpen)
	{
		const ColorRGBA Theme = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		const float CenterX = ScreenWidth * 0.5f;
		const float CenterY = 150.0f;
		const float PanelCenterX = Rect.x + Rect.w * 0.5f;
		const float PanelCenterY = Rect.y + Rect.h * 0.5f;
		Graphics()->DrawRect(CenterX - 0.25f, 0.0f, 0.5f, 300.0f, Theme.WithAlpha(std::abs(PanelCenterX - CenterX) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(0.0f, CenterY - 0.25f, ScreenWidth, 0.5f, Theme.WithAlpha(std::abs(PanelCenterY - CenterY) <= 4.0f ? 0.48f : 0.18f), 0, 0.0f);
		Graphics()->DrawRect(Rect.x - 1.0f, Rect.y - 1.0f, Rect.w + 2.0f, Rect.h + 2.0f, Theme.WithAlpha(0.22f), IGraphics::CORNER_ALL, 3.0f * Scale);
		const CUIRect Handle = ResizeHandleRect();
		Graphics()->DrawRect(Handle.x, Handle.y, Handle.w, Handle.h, ColorRGBA(0.02f, 0.025f, 0.035f, 0.88f), IGraphics::CORNER_ALL, 1.0f * Scale);
		Graphics()->DrawRect(Handle.x + Handle.w - 1.3f * Scale, Handle.y + 1.0f * Scale, 1.0f * Scale, Handle.h - 2.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		Graphics()->DrawRect(Handle.x + 1.0f * Scale, Handle.y + Handle.h - 1.3f * Scale, Handle.w - 2.0f * Scale, 1.0f * Scale, Theme.WithAlpha(1.0f), 0, 0.0f);
		RenderTools()->RenderCursor(HudMousePos(), 12.0f);
	}
}

void CAetherFinishPrediction::OnRender()
{
	if(!g_Config.m_AeFinishPrediction && !m_EditorOpen)
		return;
	if(g_Config.m_AeFocusMode && g_Config.m_AeFocusModeHideAllUi && !m_EditorOpen)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, 300.0f);
	m_LastRect = PanelRect();
	RenderPanel(m_LastRect);
}

bool CAetherFinishPrediction::OpenEditor()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	m_EditorOpen = true;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(false);
	Input()->MouseModeAbsolute();
	return true;
}

void CAetherFinishPrediction::OnUpdate()
{
	if(!m_EditorOpen || m_EditorInteraction == EEditorInteraction::IDLE)
		return;
	if(!Input()->NativeMousePressed(1))
	{
		m_EditorInteraction = EEditorInteraction::IDLE;
		return;
	}
	if(m_EditorInteraction == EEditorInteraction::DRAGGING)
	{
		const vec2 Mouse = HudMousePos();
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		g_Config.m_AeFinishPredictionOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeFinishPredictionOffsetY = round_to_int(Mouse.y - m_DragOffset.y);
		ApplyCenterSnap(ScreenWidth, 300.0f, m_LastRect.w, m_LastRect.h);
	}
	else if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (78.0f * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (24.0f * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
	}
}

void CAetherFinishPrediction::CloseEditor()
{
	if(!m_EditorOpen)
		return;
	m_EditorOpen = false;
	m_EditorInteraction = EEditorInteraction::IDLE;
	Input()->SetNativeMouseCursorVisible(true);
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		Input()->MouseModeAbsolute();
	else
		Input()->MouseModeRelative();
}

void CAetherFinishPrediction::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
	{
		CloseEditor();
		OnReset();
	}
}

bool CAetherFinishPrediction::OnInput(const IInput::CEvent &Event)
{
	if(!m_EditorOpen)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		CloseEditor();
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_R)
	{
		g_Config.m_AeFinishPredictionOffsetX = 0;
		g_Config.m_AeFinishPredictionOffsetY = 58;
		g_Config.m_AeFinishPredictionScale = 100;
		return false;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		const vec2 Mouse = HudMousePos();
		if(m_LastRect.Inside(Mouse))
		{
			SetScaleKeepingCenter(g_Config.m_AeFinishPredictionScale + (Event.m_Key == KEY_MOUSE_WHEEL_UP ? 5 : -5), vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f));
			return true;
		}
	}
	if(Event.m_Key == KEY_MOUSE_1)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const vec2 Mouse = HudMousePos();
			if(ResizeHandleRect().Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::RESIZING;
				m_ResizeCenter = vec2(m_LastRect.x + m_LastRect.w * 0.5f, m_LastRect.y + m_LastRect.h * 0.5f);
				return true;
			}
			if(m_LastRect.Inside(Mouse))
			{
				m_EditorInteraction = EEditorInteraction::DRAGGING;
				m_DragOffset = Mouse - vec2(m_LastRect.x, m_LastRect.y);
				return true;
			}
		}
		if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool WasEditing = m_EditorInteraction != EEditorInteraction::IDLE;
			m_EditorInteraction = EEditorInteraction::IDLE;
			return WasEditing;
		}
	}
	return false;
}

bool CAetherFinishPrediction::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	if(!m_EditorOpen)
		return false;
	if(m_EditorInteraction == EEditorInteraction::DRAGGING)
	{
		const vec2 Mouse = HudMousePos();
		const float ScreenWidth = 300.0f * Graphics()->ScreenAspect();
		g_Config.m_AeFinishPredictionOffsetX = round_to_int(Mouse.x - m_DragOffset.x - (ScreenWidth - m_LastRect.w) * 0.5f);
		g_Config.m_AeFinishPredictionOffsetY = round_to_int(Mouse.y - m_DragOffset.y);
		ApplyCenterSnap(ScreenWidth, 300.0f, m_LastRect.w, m_LastRect.h);
		return true;
	}
	if(m_EditorInteraction == EEditorInteraction::RESIZING)
	{
		const vec2 Mouse = HudMousePos();
		const float HorizontalScale = std::abs(Mouse.x - m_ResizeCenter.x) / (78.0f * 0.5f);
		const float VerticalScale = std::abs(Mouse.y - m_ResizeCenter.y) / (24.0f * 0.5f);
		SetScaleKeepingCenter((int)std::round(std::max(HorizontalScale, VerticalScale) * 100.0f), m_ResizeCenter);
		return true;
	}
	return false;
}
