/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teams.h"
#include "teehistorian.h"
#include <engine/shared/config.h>

#include "entities/character.h"
#include "player.h"

CGameTeams::CGameTeams(CGameContext *pGameContext) :
	m_pGameContext(pGameContext)
{
	Reset();
}

void CGameTeams::Reset()
{
	m_Core.Reset();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_TeamState[i] = TEAMSTATE_EMPTY;
		m_TeamLocked[i] = false;
		m_Invited[i] = 0;
	}
}

void CGameTeams::ResetRoundState(int Team)
{
	ResetInvited(Team);
	ResetSwitchers(Team);
}

void CGameTeams::ResetSwitchers(int Team)
{
	if(GameServer()->Collision()->m_NumSwitchers > 0)
	{
		for(int i = 0; i < GameServer()->Collision()->m_NumSwitchers + 1; ++i)
		{
			GameServer()->Collision()->m_pSwitchers[i].m_Status[Team] = GameServer()->Collision()->m_pSwitchers[i].m_Initial;
			GameServer()->Collision()->m_pSwitchers[i].m_EndTick[Team] = 0;
			GameServer()->Collision()->m_pSwitchers[i].m_Type[Team] = TILE_SWITCHOPEN;
		}
	}
}

const char *CGameTeams::SetCharacterTeam(int ClientID, int Team)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return "Invalid client ID";
	if(Team < 0 || Team >= MAX_CLIENTS + 1)
		return "Invalid room number";
	if(Team != TEAM_SUPER && m_TeamState[Team] > TEAMSTATE_OPEN)
		return "This room started already";
	if(m_Core.Team(ClientID) == Team)
		return "You are in this room already";

	CCharacter *pChar = Character(ClientID);
	if(pChar)
	{
		if(Team == TEAM_SUPER && !pChar->m_Super)
			return "You can't join super room if you don't have super rights";
		if(Team != TEAM_SUPER && pChar->m_DDRaceState != DDRACE_NONE)
			return "You have started the game already";

		pChar->Die(ClientID, WEAPON_GAME);
	}

	CPlayer *pPlayer = GetPlayer(ClientID);
	// clear score when joining a new room
	pPlayer->m_Score = 0;

	SetForceCharacterTeam(ClientID, Team);
	return nullptr;
}

void CGameTeams::SetForceCharacterTeam(int ClientID, int Team)
{
	int OldTeam = m_Core.Team(ClientID);

	if(Team != OldTeam && (OldTeam != TEAM_FLOCK || g_Config.m_SvTeam == 3) && OldTeam != TEAM_SUPER && m_TeamState[OldTeam] != TEAMSTATE_EMPTY)
	{
		bool NoElseInOldTeam = Count(OldTeam) <= 1;
		if(NoElseInOldTeam)
		{
			m_TeamState[OldTeam] = TEAMSTATE_EMPTY;

			// unlock team when last player leaves
			SetTeamLock(OldTeam, false);
			ResetRoundState(OldTeam);
			// do not reset SaveTeamResult, because it should be logged into teehistorian even if the team leaves
		}
	}

	m_Core.Team(ClientID, Team);

	if(OldTeam != Team)
	{
		for(int LoopClientID = 0; LoopClientID < MAX_CLIENTS; ++LoopClientID)
			if(GetPlayer(LoopClientID))
				SendTeamsState(LoopClientID);
	}

	if(Team != TEAM_SUPER && (m_TeamState[Team] == TEAMSTATE_EMPTY || m_TeamLocked[Team]))
	{
		if(!m_TeamLocked[Team])
			ChangeTeamState(Team, TEAMSTATE_OPEN);

		ResetSwitchers(Team);
	}
}

int CGameTeams::Count(int Team) const
{
	if(Team == TEAM_SUPER)
		return -1;

	int Count = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(m_Core.Team(i) == Team)
			Count++;

	return Count;
}

void CGameTeams::ChangeTeamState(int Team, int State)
{
	m_TeamState[Team] = State;
}

int64 CGameTeams::TeamMask(int Team, int ExceptID, int Asker)
{
	int64 Mask = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ExceptID)
			continue; // Explicitly excluded
		if(!GetPlayer(i))
			continue; // Player doesn't exist

		if(!(GetPlayer(i)->GetTeam() == -1 || GetPlayer(i)->IsPaused()))
		{ // Not spectator
			if(i != Asker)
			{ // Actions of other players
				if(!Character(i))
					continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == 2)
				{
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == 0)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(i))
						continue; // When in solo part don't show others
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of yourself
		}
		else if(GetPlayer(i)->m_SpectatorID != SPEC_FREEVIEW)
		{ // Spectating specific player
			if(GetPlayer(i)->m_SpectatorID != Asker)
			{ // Actions of other players
				if(!Character(GetPlayer(i)->m_SpectatorID))
					continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == 2)
				{
					if(m_Core.Team(GetPlayer(i)->m_SpectatorID) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorID) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == 0)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(GetPlayer(i)->m_SpectatorID))
						continue; // When in solo part don't show others
					if(m_Core.Team(GetPlayer(i)->m_SpectatorID) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorID) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of player you're spectating
		}
		else
		{ // Freeview
			if(GetPlayer(i)->m_SpecTeam)
			{ // Show only players in own team when spectating
				if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
					continue; // in different teams
			}
		}

		Mask |= 1LL << i;
	}
	return Mask;
}

void CGameTeams::SendTeamsState(int ClientID)
{
	if(g_Config.m_SvTeam == 3)
		return;

	if(!m_pGameContext->m_apPlayers[ClientID] || m_pGameContext->m_apPlayers[ClientID]->GetClientVersion() <= VERSION_DDRACE)
		return;

	CMsgPacker Msg(NETMSGTYPE_SV_TEAMSSTATE);

	for(unsigned i = 0; i < MAX_CLIENTS; i++)
		Msg.AddInt(m_Core.Team(i));

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameTeams::OnCharacterSpawn(int ClientID)
{
	m_Core.SetSolo(ClientID, false);
	int Team = m_Core.Team(ClientID);

	if(m_Core.Team(ClientID) >= TEAM_SUPER || !m_TeamLocked[Team])
	{
		if(g_Config.m_SvTeam != 3)
			SetForceCharacterTeam(ClientID, Team);
		else
			SetForceCharacterTeam(ClientID, ClientID); // initialize team
	}
}

void CGameTeams::OnCharacterDeath(int ClientID, int Weapon)
{
	m_Core.SetSolo(ClientID, false);

	int Team = m_Core.Team(ClientID);

	if(g_Config.m_SvTeam == 3)
	{
		ChangeTeamState(Team, CGameTeams::TEAMSTATE_OPEN);
		ResetSwitchers(Team);
	}

	SetForceCharacterTeam(ClientID, Team);
}

void CGameTeams::SetTeamLock(int Team, bool Lock)
{
	if(Team > TEAM_FLOCK && Team < TEAM_SUPER)
		m_TeamLocked[Team] = Lock;
}

void CGameTeams::ResetInvited(int Team)
{
	m_Invited[Team] = 0;
}

void CGameTeams::SetClientInvited(int Team, int ClientID, bool Invited)
{
	if(Team > TEAM_FLOCK && Team < TEAM_SUPER)
	{
		if(Invited)
			m_Invited[Team] |= 1ULL << ClientID;
		else
			m_Invited[Team] &= ~(1ULL << ClientID);
	}
}
