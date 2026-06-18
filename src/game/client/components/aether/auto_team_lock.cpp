#include "auto_team_lock.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/teamscore.h>

#include <generated/protocol.h>

int CAetherAutoTeamLock::CurrentDDTeam() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return 0;
	if(!GameClient()->m_Snap.m_apPlayerInfos[LocalId])
		return 0;
	if(GameClient()->m_aClients[LocalId].m_Team == TEAM_SPECTATORS)
		return 0;

	const int Team = GameClient()->m_Teams.Team(LocalId);
	if(Team <= 0 || Team >= TEAM_SUPER)
		return 0;
	return Team;
}

void CAetherAutoTeamLock::ResetLockState()
{
	m_LastTeam = 0;
	m_PendingTeam = 0;
	m_LockAt = 0;
	m_LockSent = false;
}

void CAetherAutoTeamLock::OnUpdate()
{
	if(!g_Config.m_AeAutoTeamLock || Client()->State() != IClient::STATE_ONLINE)
	{
		ResetLockState();
		return;
	}

	const int Team = CurrentDDTeam();
	if(Team == 0)
	{
		ResetLockState();
		return;
	}

	if(Team != m_LastTeam)
	{
		m_LastTeam = Team;
		m_PendingTeam = Team;
		m_LockSent = false;
		m_LockAt = time_get() + time_freq() * g_Config.m_AeAutoTeamLockDelay;
	}

	if(!m_LockSent && m_PendingTeam == Team && time_get() >= m_LockAt)
	{
		GameClient()->m_Chat.SendChat(0, "/lock 1");
		m_LockSent = true;
	}
}

void CAetherAutoTeamLock::OnReset()
{
	ResetLockState();
}

void CAetherAutoTeamLock::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
		ResetLockState();
}
