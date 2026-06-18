#include "fail_sound.h"

#include "audio_decoder.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/sound.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <vector>

namespace
{
bool FileHasExtension(const char *pName, const char *pExt)
{
	const int NameLen = str_length(pName);
	const int ExtLen = str_length(pExt);
	return NameLen >= ExtLen && str_comp_nocase(pName + NameLen - ExtLen, pExt) == 0;
}

void SanitizeFileName(const char *pInput, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pInput)
		return;

	const char *pBase = pInput;
	for(const char *p = pInput; *p; ++p)
		if(*p == '/' || *p == '\\')
			pBase = p + 1;

	str_copy(pOut, pBase, OutSize);
	for(char *p = pOut; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '<' || *p == '>' || *p == '|' || *p == '*' || *p == '?')
			*p = '_';
	}
}

int TryLoadFailSample(IStorage *pStorage, ISound *pSound, const char *pFileName)
{
	if(!pStorage || !pSound || !pFileName || pFileName[0] == '\0')
		return -1;

	char aSanitized[256];
	SanitizeFileName(pFileName, aSanitized, sizeof(aSanitized));
	if(aSanitized[0] == '\0')
		return -1;

	char aRel[IO_MAX_PATH_LENGTH];
	str_format(aRel, sizeof(aRel), "assets/failsound/%s", aSanitized);
	if(!pStorage->FileExists(aRel, IStorage::TYPE_SAVE))
		return -1;

	if(FileHasExtension(aSanitized, ".wv"))
		return pSound->LoadWV(aRel, IStorage::TYPE_SAVE);
	if(FileHasExtension(aSanitized, ".mp3"))
	{
		char aAbsolute[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, aRel, aAbsolute, sizeof(aAbsolute));
		std::vector<short> vPcm;
		int Channels = 0;
		int SampleRate = 0;
		if(!AetherAudio::DecodeAudioFileToS16Pcm(aAbsolute, vPcm, Channels, SampleRate, aSanitized))
			return -1;
		return pSound->LoadS16PcmInterleavedFromMem(vPcm.data(), (int)vPcm.size() / maximum(1, Channels), Channels, SampleRate, false, aSanitized);
	}
	return pSound->LoadOpus(aRel, IStorage::TYPE_SAVE);
}
}

unsigned CAetherFailSound::SamplesFingerprint()
{
	unsigned Hash = (unsigned)(g_Config.m_AeFreezeFailSoundLocal * 19u + g_Config.m_AeFreezeFailSoundOthers * 31u + g_Config.m_AeFreezeFailSoundTeamLast * 43u);
	for(const unsigned char *p = (const unsigned char *)g_Config.m_AeFreezeFailSoundLocalFile; p && *p; ++p)
		Hash = Hash * 131u + *p;
	for(const unsigned char *p = (const unsigned char *)g_Config.m_AeFreezeFailSoundOthersFile; p && *p; ++p)
		Hash = Hash * 137u + *p;
	for(const unsigned char *p = (const unsigned char *)g_Config.m_AeFreezeFailSoundTeamLastFile; p && *p; ++p)
		Hash = Hash * 139u + *p;
	return Hash;
}

bool CAetherFailSound::ValidDDRaceTeam(int Team)
{
	return Team > TEAM_FLOCK && Team < TEAM_SUPER;
}

bool CAetherFailSound::IsLocalClientId(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       (ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1] || ClientId == GameClient()->m_Snap.m_LocalClientId);
}

bool CAetherFailSound::ShouldPlayOthers(int ClientId) const
{
	if(ClientId < 0 || GameClient()->m_Snap.m_LocalClientId < 0)
		return false;
	const int MyTeam = GameClient()->SwitchStateTeam();
	const int OtherTeam = GameClient()->m_Teams.Team(ClientId);
	if(!ValidDDRaceTeam(MyTeam) || !ValidDDRaceTeam(OtherTeam) || MyTeam != OtherTeam)
		return false;
	if(GameClient()->m_Teams.GetSolo(GameClient()->m_Snap.m_LocalClientId) || GameClient()->m_Teams.GetSolo(ClientId))
		return false;
	return true;
}

bool CAetherFailSound::TeamLastNow() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		return false;
	const int MyTeam = GameClient()->SwitchStateTeam();
	if(!ValidDDRaceTeam(MyTeam) || GameClient()->m_Teams.GetSolo(LocalId))
		return false;

	int Unfrozen = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const auto &Char = GameClient()->m_Snap.m_aCharacters[i];
		if(!Char.m_Active || GameClient()->m_Teams.Team(i) != MyTeam || GameClient()->m_Teams.GetSolo(i))
			continue;
		if(!Char.m_HasExtendedData || Char.m_ExtendedData.m_FreezeEnd <= Client()->GameTick(g_Config.m_ClDummy))
			++Unfrozen;
	}
	return Unfrozen == 1 && GameClient()->m_Snap.m_aCharacters[LocalId].m_HasExtendedData &&
	       GameClient()->m_Snap.m_aCharacters[LocalId].m_ExtendedData.m_FreezeEnd <= Client()->GameTick(g_Config.m_ClDummy);
}

void CAetherFailSound::EnsureSamples()
{
	const unsigned Fingerprint = SamplesFingerprint();
	if(Fingerprint == m_SamplesFingerprint)
		return;
	m_SamplesFingerprint = Fingerprint;
	UnloadSamples();
	if(!Sound()->IsSoundEnabled())
		return;
	if(g_Config.m_AeFreezeFailSoundLocalFile[0])
		m_LocalSampleId = TryLoadFailSample(Storage(), Sound(), g_Config.m_AeFreezeFailSoundLocalFile);
	if(g_Config.m_AeFreezeFailSoundOthersFile[0])
		m_OthersSampleId = TryLoadFailSample(Storage(), Sound(), g_Config.m_AeFreezeFailSoundOthersFile);
	if(g_Config.m_AeFreezeFailSoundTeamLastFile[0])
		m_TeamLastSampleId = TryLoadFailSample(Storage(), Sound(), g_Config.m_AeFreezeFailSoundTeamLastFile);
}

void CAetherFailSound::UnloadSamples()
{
	if(m_LocalSampleId >= 0)
		Sound()->UnloadSample(m_LocalSampleId);
	if(m_OthersSampleId >= 0)
		Sound()->UnloadSample(m_OthersSampleId);
	if(m_TeamLastSampleId >= 0)
		Sound()->UnloadSample(m_TeamLastSampleId);
	m_LocalSampleId = -1;
	m_OthersSampleId = -1;
	m_TeamLastSampleId = -1;
}

void CAetherFailSound::PlaySample(int SampleId, int Volume)
{
	if(SampleId < 0 || Volume <= 0 || GameClient()->m_SuppressEvents || !g_Config.m_SndEnable)
		return;
	Sound()->Play(CSounds::CHN_GLOBAL, SampleId, ISound::FLAG_NO_PANNING, std::clamp(Volume / 100.0f, 0.0f, 1.0f));
}

void CAetherFailSound::OnInit()
{
	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("assets/failsound", IStorage::TYPE_SAVE);
	EnsureSamples();
}

void CAetherFailSound::OnReset()
{
	m_LastProcessedTick = -1;
	m_TeamLastPrev = false;
}

void CAetherFailSound::OnShutdown()
{
	UnloadSamples();
	m_SamplesFingerprint = 0xffffffffu;
}

void CAetherFailSound::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	OnReset();
}

void CAetherFailSound::OnRender()
{
	EnsureSamples();
	if(!g_Config.m_AeFreezeFailSound)
		return;
	if(!g_Config.m_AeFreezeFailSoundLocal && !g_Config.m_AeFreezeFailSoundOthers && !g_Config.m_AeFreezeFailSoundTeamLast)
		return;
	if(!GameClient()->m_GameInfo.m_EntitiesDDRace)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	const int PrevTick = Client()->PrevGameTick(g_Config.m_ClDummy);
	if(Tick == m_LastProcessedTick)
		return;
	m_LastProcessedTick = Tick;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const auto &Char = GameClient()->m_Snap.m_aCharacters[i];
		if(!Char.m_Active || !Char.m_HasExtendedData || !Char.m_pPrevExtendedData)
			continue;
		const int FreezeNow = Char.m_ExtendedData.m_FreezeEnd - Tick;
		const int FreezePrev = Char.m_pPrevExtendedData->m_FreezeEnd - PrevTick;
		if(FreezePrev > 0 || FreezeNow <= 0)
			continue;

		if(IsLocalClientId(i))
		{
			if(g_Config.m_AeFreezeFailSoundLocal)
				PlaySample(m_LocalSampleId, g_Config.m_AeFreezeFailSoundLocalVol);
		}
		else if(g_Config.m_AeFreezeFailSoundOthers && ShouldPlayOthers(i))
		{
			PlaySample(m_OthersSampleId, g_Config.m_AeFreezeFailSoundOthersVol);
		}
	}

	const bool LastAliveNow = TeamLastNow();
	if(g_Config.m_AeFreezeFailSoundTeamLast && LastAliveNow && !m_TeamLastPrev)
		PlaySample(m_TeamLastSampleId, g_Config.m_AeFreezeFailSoundTeamLastVol);
	m_TeamLastPrev = LastAliveNow;
}
