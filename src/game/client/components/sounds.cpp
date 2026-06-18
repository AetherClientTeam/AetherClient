/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "sounds.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/sound.h>

#include <generated/client_data.h>

#include <game/client/components/aether/audio_decoder.h>
#include <game/client/components/camera.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <vector>

namespace
{
bool SoundPathHasExtension(const char *pPath, const char *pExt)
{
	const int PathLen = str_length(pPath);
	const int ExtLen = str_length(pExt);
	return PathLen >= ExtLen && str_comp_nocase(pPath + PathLen - ExtLen, pExt) == 0;
}

void SoundBaseName(const char *pPath, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	const char *pBase = pPath ? pPath : "";
	for(const char *p = pBase; *p; ++p)
	{
		if(*p == '/' || *p == '\\')
			pBase = p + 1;
	}
	str_copy(pOut, pBase, OutSize);
}

bool SoundPackNameIsSafe(const char *pPack)
{
	if(!pPack || pPack[0] == '\0' || str_comp(pPack, "default") == 0)
		return false;
	for(const char *p = pPack; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '<' || *p == '>' || *p == '|' || *p == '*' || *p == '?')
			return false;
	}
	return true;
}

int LoadAudioPackWav(CGameClient *pGameClient, const char *pRelPath, const char *pDebugName)
{
	char aAbsolute[IO_MAX_PATH_LENGTH];
	IOHANDLE File = pGameClient->Storage()->OpenFile(pRelPath, IOFLAG_READ, IStorage::TYPE_ALL, aAbsolute, sizeof(aAbsolute));
	if(!File)
		return -1;
	io_close(File);

	std::vector<short> vPcm;
	int Channels = 0;
	int SampleRate = 0;
	if(!AetherAudio::DecodeAudioFileToS16Pcm(aAbsolute, vPcm, Channels, SampleRate, pDebugName) || vPcm.empty() || (Channels != 1 && Channels != 2) || SampleRate <= 0)
	{
		log_error("aether/audio-pack", "unsupported or broken wav '%s', falling back", pRelPath);
		return -1;
	}

	const int NumFrames = (int)(vPcm.size() / (size_t)Channels);
	if(NumFrames <= 0)
		return -1;
	return pGameClient->Sound()->LoadS16PcmInterleavedFromMem(vPcm.data(), NumFrames, Channels, SampleRate, false, pDebugName);
}

int LoadAudioPackSample(CGameClient *pGameClient, const char *pDefaultFilename)
{
	if(!pGameClient || !pDefaultFilename)
		return -1;

	if(!SoundPackNameIsSafe(g_Config.m_ClAssetsAudio))
		return pGameClient->Sound()->LoadWV(pDefaultFilename);

	char aBase[IO_MAX_PATH_LENGTH];
	SoundBaseName(pDefaultFilename, aBase, sizeof(aBase));
	if(aBase[0] == '\0')
		return pGameClient->Sound()->LoadWV(pDefaultFilename);

	const char *apPackDirs[] = {
		"assets/audio/%s/audio/%s",
		"assets/audio/%s/%s",
	};

	for(const char *pFmt : apPackDirs)
	{
		char aCandidate[IO_MAX_PATH_LENGTH];
		str_format(aCandidate, sizeof(aCandidate), pFmt, g_Config.m_ClAssetsAudio, aBase);
		if(pGameClient->Storage()->FileExists(aCandidate, IStorage::TYPE_ALL))
		{
			const int Id = SoundPathHasExtension(aCandidate, ".wav") ? LoadAudioPackWav(pGameClient, aCandidate, aBase) : pGameClient->Sound()->LoadWV(aCandidate, IStorage::TYPE_ALL);
			if(Id >= 0)
				return Id;
		}

		if(SoundPathHasExtension(aBase, ".wv"))
		{
			char aWavBase[IO_MAX_PATH_LENGTH];
			str_copy(aWavBase, aBase, sizeof(aWavBase));
			aWavBase[str_length(aWavBase) - 3] = '\0';
			str_append(aWavBase, ".wav");
			str_format(aCandidate, sizeof(aCandidate), pFmt, g_Config.m_ClAssetsAudio, aWavBase);
			if(pGameClient->Storage()->FileExists(aCandidate, IStorage::TYPE_ALL))
			{
				const int Id = LoadAudioPackWav(pGameClient, aCandidate, aWavBase);
				if(Id >= 0)
					return Id;
			}
		}
	}

	return pGameClient->Sound()->LoadWV(pDefaultFilename);
}
}

CSoundLoading::CSoundLoading(CGameClient *pGameClient, bool Render) :
	m_pGameClient(pGameClient),
	m_Render(Render)
{
	Abortable(true);
}

void CSoundLoading::Run()
{
	for(int s = 0; s < g_pData->m_NumSounds; s++)
	{
		const char *pLoadingCaption = Localize("Loading DDNet Client");
		const char *pLoadingContent = Localize("Loading sound files");

		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; i++)
		{
			if(State() == IJob::STATE_ABORTED)
				return;

			int Id = LoadAudioPackSample(m_pGameClient, g_pData->m_aSounds[s].m_aSounds[i].m_pFilename);
			g_pData->m_aSounds[s].m_aSounds[i].m_Id = Id;
			// try to render a frame
			if(m_Render)
				m_pGameClient->m_Menus.RenderLoading(pLoadingCaption, pLoadingContent, 0);
		}

		if(m_Render)
			m_pGameClient->m_Menus.RenderLoading(pLoadingCaption, pLoadingContent, 1);
	}
}

void CSounds::UpdateChannels()
{
	const float NewGuiSoundVolume = g_Config.m_SndChatVolume / 100.0f;
	if(NewGuiSoundVolume != m_GuiSoundVolume)
	{
		m_GuiSoundVolume = NewGuiSoundVolume;
		Sound()->SetChannel(CSounds::CHN_GUI, m_GuiSoundVolume, 0.0f);
	}

	const float NewGameSoundVolume = g_Config.m_SndGameVolume / 100.0f;
	if(NewGameSoundVolume != m_GameSoundVolume)
	{
		m_GameSoundVolume = NewGameSoundVolume;
		Sound()->SetChannel(CSounds::CHN_WORLD, 0.9f * m_GameSoundVolume, 1.0f);
		Sound()->SetChannel(CSounds::CHN_GLOBAL, m_GameSoundVolume, 0.0f);
	}

	const float NewMapSoundVolume = g_Config.m_SndMapVolume / 100.0f;
	if(NewMapSoundVolume != m_MapSoundVolume)
	{
		m_MapSoundVolume = NewMapSoundVolume;
		Sound()->SetChannel(CSounds::CHN_MAPSOUND, m_MapSoundVolume, 1.0f);
	}

	const float NewBackgroundMusicVolume = g_Config.m_SndBackgroundMusicVolume / 100.0f;
	if(NewBackgroundMusicVolume != m_BackgroundMusicVolume)
	{
		m_BackgroundMusicVolume = NewBackgroundMusicVolume;
		Sound()->SetChannel(CSounds::CHN_MUSIC, m_BackgroundMusicVolume, 1.0f);
	}
}

int CSounds::GetSampleId(int SetId)
{
	if(!g_Config.m_SndEnable || !Sound()->IsSoundEnabled() || m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return -1;

	CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	if(!pSet->m_NumSounds)
		return -1;

	if(pSet->m_NumSounds == 1)
		return pSet->m_aSounds[0].m_Id;

	// return random one
	int Id;
	do
	{
		Id = rand() % pSet->m_NumSounds;
	} while(Id == pSet->m_Last);
	pSet->m_Last = Id;
	return pSet->m_aSounds[Id].m_Id;
}

void CSounds::OnInit()
{
	UpdateChannels();
	ClearQueue();

	// load sounds
	if(g_Config.m_ClThreadsoundloading)
	{
		m_pSoundJob = std::make_shared<CSoundLoading>(GameClient(), false);
		GameClient()->Engine()->AddJob(m_pSoundJob);
		m_WaitForSoundJob = true;
		GameClient()->m_Menus.RenderLoading(Localize("Loading DDNet Client"), Localize("Loading sound files"), 0);
	}
	else
	{
		CSoundLoading(GameClient(), true).Run();
		m_WaitForSoundJob = false;
	}
}

void CSounds::Reload()
{
	if(m_pSoundJob && m_WaitForSoundJob)
		m_pSoundJob->Abort();
	m_pSoundJob.reset();
	m_WaitForSoundJob = false;

	Sound()->StopAll();
	ClearQueue();

	for(int s = 0; s < g_pData->m_NumSounds; ++s)
	{
		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; ++i)
		{
			int &Id = g_pData->m_aSounds[s].m_aSounds[i].m_Id;
			if(Id >= 0)
			{
				Sound()->UnloadSample(Id);
				Id = -1;
			}
		}
	}

	CSoundLoading(GameClient(), false).Run();
	log_info("aether/audio-pack", "reloaded audio pack '%s'", g_Config.m_ClAssetsAudio);
}

void CSounds::OnReset()
{
	if(Client()->State() >= IClient::STATE_ONLINE)
	{
		Sound()->StopAll();
		ClearQueue();
	}
}

void CSounds::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
		OnReset();
}

void CSounds::OnRender()
{
	// check for sound initialisation
	if(m_WaitForSoundJob)
	{
		if(m_pSoundJob->State() == IJob::STATE_DONE)
			m_WaitForSoundJob = false;
		else
			return;
	}

	Sound()->SetListenerPosition(GameClient()->m_Camera.m_Center);
	UpdateChannels();

	// play sound from queue
	if(m_QueuePos > 0)
	{
		int64_t Now = time();
		if(m_QueueWaitTime <= Now)
		{
			Play(m_aQueue[0].m_Channel, m_aQueue[0].m_SetId, 1.0f);
			m_QueueWaitTime = Now + time_freq() * 3 / 10; // wait 300ms before playing the next one
			if(--m_QueuePos > 0)
				mem_move(m_aQueue, m_aQueue + 1, m_QueuePos * sizeof(CQueueEntry));
		}
	}
}

void CSounds::ClearQueue()
{
	mem_zero(m_aQueue, sizeof(m_aQueue));
	m_QueuePos = 0;
	m_QueueWaitTime = time();
}

void CSounds::Enqueue(int Channel, int SetId)
{
	if(GameClient()->m_SuppressEvents)
		return;
	if(m_QueuePos >= QUEUE_SIZE)
		return;
	if(Channel != CHN_MUSIC && g_Config.m_ClEditor)
		return;

	m_aQueue[m_QueuePos].m_Channel = Channel;
	m_aQueue[m_QueuePos++].m_SetId = SetId;
}

void CSounds::PlayAndRecord(int Channel, int SetId, float Volume, vec2 Position)
{
	// TODO: Volume and position are currently not recorded for sounds played with this function
	// TODO: This also causes desync sounds during demo playback of demos recorded on high ping servers:
	//       https://github.com/ddnet/ddnet/issues/1282
	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundId = SetId;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_NOSEND | MSGFLAG_RECORD);

	PlayAt(Channel, SetId, Volume, Position);
}

void CSounds::Play(int Channel, int SetId, float Volume)
{
	PlaySample(Channel, GetSampleId(SetId), 0, Volume);
}

void CSounds::PlayAt(int Channel, int SetId, float Volume, vec2 Position)
{
	PlaySampleAt(Channel, GetSampleId(SetId), 0, Volume, Position);
}

void CSounds::Stop(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return;

	const CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	for(int i = 0; i < pSet->m_NumSounds; i++)
		if(pSet->m_aSounds[i].m_Id != -1)
			Sound()->Stop(pSet->m_aSounds[i].m_Id);
}

bool CSounds::IsPlaying(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return false;

	const CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	for(int i = 0; i < pSet->m_NumSounds; i++)
		if(pSet->m_aSounds[i].m_Id != -1 && Sound()->IsPlaying(pSet->m_aSounds[i].m_Id))
			return true;
	return false;
}

ISound::CVoiceHandle CSounds::PlaySample(int Channel, int SampleId, int Flags, float Volume)
{
	if(GameClient()->m_SuppressEvents || (Channel == CHN_MUSIC && !g_Config.m_SndMusic) || SampleId == -1)
		return ISound::CVoiceHandle();

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->Play(Channel, SampleId, Flags, Volume);
}

ISound::CVoiceHandle CSounds::PlaySampleAt(int Channel, int SampleId, int Flags, float Volume, vec2 Position)
{
	if(GameClient()->m_SuppressEvents || (Channel == CHN_MUSIC && !g_Config.m_SndMusic) || SampleId == -1)
		return ISound::CVoiceHandle();

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->PlayAt(Channel, SampleId, Flags, Volume, Position);
}
