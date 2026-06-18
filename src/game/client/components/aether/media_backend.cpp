#include "media_backend.h"

#include <base/detect.h>
#include <base/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

#if defined(CONF_FAMILY_WINDOWS)
#undef WIN32_LEAN_AND_MEAN
#undef NOGDI
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000A
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <Windows.h>
#include <appmodel.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <cwctype>
#include <string>
#include <unordered_set>
#endif

using namespace std::chrono_literals;

namespace
{
int64_t SteadyMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

class CAetherMediaBackend::CImpl
{
public:
	mutable std::mutex m_Mutex;
	SSnapshot m_Snapshot;
	std::thread m_Thread;
	std::atomic_bool m_Stop{false};
	std::atomic_bool m_VisualizerEnabled{false};
	std::atomic_int m_Sensitivity{100};

#if defined(CONF_FAMILY_WINDOWS)
	class CActivationHandler final :
		public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler>
	{
	public:
		HANDLE m_Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		HRESULT m_Result = E_FAIL;
		Microsoft::WRL::ComPtr<IUnknown> m_Interface;

		~CActivationHandler() override
		{
			if(m_Event)
				CloseHandle(m_Event);
		}

		STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *pOperation) override
		{
			HRESULT ActivationResult = E_FAIL;
			const HRESULT CallResult = pOperation->GetActivateResult(&ActivationResult, &m_Interface);
			m_Result = FAILED(CallResult) ? CallResult : ActivationResult;
			SetEvent(m_Event);
			return S_OK;
		}
	};

	class CProcessCapture
	{
	public:
		struct SPollResult
		{
			std::array<float, 5> m_aBands{};
			float m_RootMeanSquare = 0.0f;
			bool m_HasSamples = false;
			uint64_t m_Frames = 0;
		};

	private:
		Microsoft::WRL::ComPtr<IAudioClient> m_AudioClient;
		Microsoft::WRL::ComPtr<IAudioCaptureClient> m_CaptureClient;
		WAVEFORMATEX m_Format{};
		std::vector<float> m_vMonoSamples;
		DWORD m_ProcessId = 0;
		HRESULT m_LastResult = S_OK;

		float ReadSample(const BYTE *pData, size_t Frame, int Channel) const
		{
			const size_t SampleIndex = Frame * m_Format.nChannels + Channel;
			return reinterpret_cast<const int16_t *>(pData)[SampleIndex] / 32768.0f;
		}

	public:
		~CProcessCapture()
		{
			Stop();
		}

		void Stop()
		{
			if(m_AudioClient)
				m_AudioClient->Stop();
			m_CaptureClient.Reset();
			m_AudioClient.Reset();
			m_Format = {};
			m_vMonoSamples.clear();
			m_ProcessId = 0;
		}

		DWORD ProcessId() const { return m_ProcessId; }
		bool Active() const { return m_AudioClient != nullptr; }
		HRESULT LastResult() const { return m_LastResult; }

		bool Start(DWORD ProcessId)
		{
			Stop();
			m_LastResult = S_OK;
			if(ProcessId == 0)
			{
				m_LastResult = E_INVALIDARG;
				return false;
			}

			AUDIOCLIENT_ACTIVATION_PARAMS Activation{};
			Activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
			Activation.ProcessLoopbackParams.TargetProcessId = ProcessId;
			Activation.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

			PROPVARIANT ActivationVariant;
			PropVariantInit(&ActivationVariant);
			ActivationVariant.vt = VT_BLOB;
			ActivationVariant.blob.cbSize = sizeof(Activation);
			ActivationVariant.blob.pBlobData = reinterpret_cast<BYTE *>(&Activation);

			auto Handler = Microsoft::WRL::Make<CActivationHandler>();
			Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> Operation;
			HRESULT Result = ActivateAudioInterfaceAsync(
				VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
				__uuidof(IAudioClient),
				&ActivationVariant,
				Handler.Get(),
				&Operation);
			if(FAILED(Result))
			{
				m_LastResult = Result;
				return false;
			}
			const DWORD WaitResult = WaitForSingleObject(Handler->m_Event, 3000);
			if(WaitResult != WAIT_OBJECT_0)
			{
				m_LastResult = WaitResult == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError());
				return false;
			}
			if(FAILED(Handler->m_Result) || !Handler->m_Interface)
			{
				m_LastResult = FAILED(Handler->m_Result) ? Handler->m_Result : E_NOINTERFACE;
				return false;
			}
			Result = Handler->m_Interface.As(&m_AudioClient);
			if(FAILED(Result))
			{
				m_LastResult = Result;
				Stop();
				return false;
			}
			m_Format.wFormatTag = WAVE_FORMAT_PCM;
			m_Format.nChannels = 2;
			m_Format.nSamplesPerSec = 44100;
			m_Format.wBitsPerSample = 16;
			m_Format.nBlockAlign = m_Format.nChannels * m_Format.wBitsPerSample / 8;
			m_Format.nAvgBytesPerSec = m_Format.nSamplesPerSec * m_Format.nBlockAlign;
			const DWORD Flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
			Result = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, Flags, 0, 0, &m_Format, nullptr);
			if(SUCCEEDED(Result))
				Result = m_AudioClient->GetService(IID_PPV_ARGS(&m_CaptureClient));
			if(SUCCEEDED(Result))
				Result = m_AudioClient->Start();
			if(FAILED(Result))
			{
				m_LastResult = Result;
				Stop();
				return false;
			}
			m_ProcessId = ProcessId;
			m_LastResult = S_OK;
			return true;
		}

		SPollResult Poll(int Sensitivity)
		{
			SPollResult Result;
			if(!m_CaptureClient || m_Format.nChannels == 0)
				return Result;
			UINT32 PacketFrames = 0;
			while(SUCCEEDED(m_CaptureClient->GetNextPacketSize(&PacketFrames)) && PacketFrames > 0)
			{
				BYTE *pData = nullptr;
				UINT32 Frames = 0;
				DWORD Flags = 0;
				if(FAILED(m_CaptureClient->GetBuffer(&pData, &Frames, &Flags, nullptr, nullptr)))
					break;
				Result.m_Frames += Frames;
				if(!(Flags & AUDCLNT_BUFFERFLAGS_SILENT))
				{
					for(UINT32 Frame = 0; Frame < Frames; ++Frame)
					{
						float Mono = 0.0f;
						for(int Channel = 0; Channel < m_Format.nChannels; ++Channel)
							Mono += ReadSample(pData, Frame, Channel);
						m_vMonoSamples.push_back(Mono / std::max<int>(m_Format.nChannels, 1));
					}
				}
				else
					m_vMonoSamples.insert(m_vMonoSamples.end(), Frames, 0.0f);
				m_CaptureClient->ReleaseBuffer(Frames);
			}
			if(m_vMonoSamples.size() < 256)
				return Result;
			if(m_vMonoSamples.size() > 2048)
				m_vMonoSamples.erase(m_vMonoSamples.begin(), m_vMonoSamples.end() - 2048);
			float SumSquares = 0.0f;
			for(const float Sample : m_vMonoSamples)
				SumSquares += Sample * Sample;
			Result.m_RootMeanSquare = std::sqrt(SumSquares / m_vMonoSamples.size());
			Result.m_aBands = AetherMusic::FiveBands(m_vMonoSamples, m_Format.nSamplesPerSec, Sensitivity);
			Result.m_HasSamples = true;
			m_vMonoSamples.clear();
			return Result;
		}
	};

	static std::wstring Lower(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t Character) { return std::towlower(Character); });
		return Value;
	}

	static DWORD FindSessionProcessId(const winrt::hstring &SourceAppUserModelId)
	{
		const std::wstring Source = Lower(SourceAppUserModelId.c_str());
		if(Source.empty())
			return 0;
		const size_t ProductEnd = Source.find_first_of(L".!");
		const std::wstring SourceProduct = Source.substr(0, ProductEnd);
		struct SCandidate
		{
			DWORD m_ProcessId;
			DWORD m_ParentProcessId;
			int m_MatchQuality;
		};
		std::vector<SCandidate> vCandidates;
		HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(Snapshot == INVALID_HANDLE_VALUE)
			return 0;
		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		if(Process32FirstW(Snapshot, &Entry))
		{
			do
			{
				int MatchQuality = 0;
				HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
				if(!Process)
					continue;
				UINT32 Length = 0;
				LONG AppIdResult = GetApplicationUserModelId(Process, &Length, nullptr);
				if(AppIdResult == ERROR_INSUFFICIENT_BUFFER && Length > 0)
				{
					std::wstring AppId(Length, L'\0');
					if(GetApplicationUserModelId(Process, &Length, AppId.data()) == ERROR_SUCCESS && Lower(AppId.c_str()) == Source)
						MatchQuality = 3;
				}
				wchar_t aPath[MAX_PATH];
				DWORD PathLength = std::size(aPath);
				if(QueryFullProcessImageNameW(Process, 0, aPath, &PathLength))
				{
					const std::wstring ProcessPath = Lower(std::wstring(aPath, PathLength));
					const wchar_t *pName = wcsrchr(ProcessPath.c_str(), L'\\');
					pName = pName ? pName + 1 : ProcessPath.c_str();
					const std::wstring ProcessName = pName;
					const size_t Extension = ProcessName.rfind(L".exe");
					const std::wstring ProcessStem = Extension == ProcessName.size() - 4 ? ProcessName.substr(0, Extension) : ProcessName;
					const std::wstring ProductDirectory = L"\\" + SourceProduct + L"\\";
					if(MatchQuality < 2 && SourceProduct.size() >= 3 && ProcessPath.find(ProductDirectory) != std::wstring::npos)
						MatchQuality = 2;
					else if(MatchQuality == 0 && (Source == ProcessName || Source == ProcessStem || Source.find(ProcessName) != std::wstring::npos || Source.find(ProcessStem) != std::wstring::npos))
						MatchQuality = 1;
				}
				CloseHandle(Process);
				if(MatchQuality > 0)
					vCandidates.push_back({Entry.th32ProcessID, Entry.th32ParentProcessID, MatchQuality});
			} while(Process32NextW(Snapshot, &Entry));
		}
		CloseHandle(Snapshot);
		if(vCandidates.empty())
			return 0;

		const int BestQuality = std::max_element(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
			return Left.m_MatchQuality < Right.m_MatchQuality;
		})->m_MatchQuality;
		std::unordered_set<DWORD> MatchingProcessIds;
		for(const SCandidate &Candidate : vCandidates)
			if(Candidate.m_MatchQuality == BestQuality)
				MatchingProcessIds.insert(Candidate.m_ProcessId);
		for(const SCandidate &Candidate : vCandidates)
			if(Candidate.m_MatchQuality == BestQuality && !MatchingProcessIds.contains(Candidate.m_ParentProcessId))
				return Candidate.m_ProcessId;
		return vCandidates.front().m_ProcessId;
	}

	struct SAudioSessionTarget
	{
		DWORD m_ProcessId = 0;
		float m_Peak = 0.0f;
		bool m_Active = false;
	};

	static std::unordered_set<DWORD> ProcessTree(DWORD RootProcessId)
	{
		std::unordered_set<DWORD> ProcessIds;
		if(RootProcessId == 0)
			return ProcessIds;
		ProcessIds.insert(RootProcessId);
		HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(Snapshot == INVALID_HANDLE_VALUE)
			return ProcessIds;
		std::vector<std::pair<DWORD, DWORD>> vProcesses;
		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		if(Process32FirstW(Snapshot, &Entry))
		{
			do
			{
				vProcesses.emplace_back(Entry.th32ProcessID, Entry.th32ParentProcessID);
			} while(Process32NextW(Snapshot, &Entry));
		}
		CloseHandle(Snapshot);
		bool Changed;
		do
		{
			Changed = false;
			for(const auto &[ProcessId, ParentProcessId] : vProcesses)
				if(ProcessIds.contains(ParentProcessId) && ProcessIds.insert(ProcessId).second)
					Changed = true;
		} while(Changed);
		return ProcessIds;
	}

	static SAudioSessionTarget FindAudioSessionTarget(DWORD RootProcessId)
	{
		SAudioSessionTarget Best;
		const std::unordered_set<DWORD> ProcessIds = ProcessTree(RootProcessId);
		if(ProcessIds.empty())
			return Best;

		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> DeviceEnumerator;
		Microsoft::WRL::ComPtr<IMMDevice> Device;
		Microsoft::WRL::ComPtr<IAudioSessionManager2> SessionManager;
		Microsoft::WRL::ComPtr<IAudioSessionEnumerator> SessionEnumerator;
		if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&DeviceEnumerator))) ||
			FAILED(DeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &Device)) ||
			FAILED(Device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &SessionManager)) ||
			FAILED(SessionManager->GetSessionEnumerator(&SessionEnumerator)))
			return Best;

		int SessionCount = 0;
		if(FAILED(SessionEnumerator->GetCount(&SessionCount)))
			return Best;
		for(int Index = 0; Index < SessionCount; ++Index)
		{
			Microsoft::WRL::ComPtr<IAudioSessionControl> SessionControl;
			Microsoft::WRL::ComPtr<IAudioSessionControl2> SessionControl2;
			if(FAILED(SessionEnumerator->GetSession(Index, &SessionControl)) ||
				FAILED(SessionControl.As(&SessionControl2)))
				continue;
			DWORD ProcessId = 0;
			if(FAILED(SessionControl2->GetProcessId(&ProcessId)) || !ProcessIds.contains(ProcessId))
				continue;
			AudioSessionState State = AudioSessionStateInactive;
			SessionControl->GetState(&State);
			Microsoft::WRL::ComPtr<IAudioMeterInformation> Meter;
			float Peak = 0.0f;
			if(SUCCEEDED(SessionControl.As(&Meter)))
				Meter->GetPeakValue(&Peak);
			const bool Active = State == AudioSessionStateActive;
			if(!Active)
				continue;
			if(Best.m_ProcessId == 0 || Peak > Best.m_Peak)
				Best = {ProcessId, Peak, Active};
		}
		return Best;
	}

	static winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession SelectMediaSession(
		const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager &Manager)
	{
		using EStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
		auto Current = Manager.GetCurrentSession();
		if(Current && Current.GetPlaybackInfo().PlaybackStatus() == EStatus::Playing)
			return Current;
		for(const auto &Session : Manager.GetSessions())
			if(Session.GetPlaybackInfo().PlaybackStatus() == EStatus::Playing)
				return Session;
		if(Current)
			return Current;
		for(const auto &Session : Manager.GetSessions())
			if(Session.GetPlaybackInfo().PlaybackStatus() == EStatus::Paused)
				return Session;
		return nullptr;
	}

	static bool DecodeArtwork(const std::vector<uint8_t> &vEncoded, std::vector<uint8_t> &vRgba, uint32_t &Width, uint32_t &Height)
	{
		if(vEncoded.empty() || vEncoded.size() > std::numeric_limits<DWORD>::max())
			return false;

		Microsoft::WRL::ComPtr<IWICImagingFactory> Factory;
		if(FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory))) &&
			FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory))))
			return false;

		Microsoft::WRL::ComPtr<IWICStream> Stream;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> Decoder;
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> Frame;
		if(FAILED(Factory->CreateStream(&Stream)) ||
			FAILED(Stream->InitializeFromMemory(const_cast<BYTE *>(vEncoded.data()), (DWORD)vEncoded.size())) ||
			FAILED(Factory->CreateDecoderFromStream(Stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &Decoder)) ||
			FAILED(Decoder->GetFrame(0, &Frame)))
			return false;

		UINT SourceWidth = 0;
		UINT SourceHeight = 0;
		if(FAILED(Frame->GetSize(&SourceWidth, &SourceHeight)) || SourceWidth == 0 || SourceHeight == 0)
			return false;

		Microsoft::WRL::ComPtr<IWICBitmapSource> Source;
		if(FAILED(Frame.As(&Source)))
			return false;
		constexpr UINT MaxDimension = 512;
		if(SourceWidth > MaxDimension || SourceHeight > MaxDimension)
		{
			const float Scale = std::min(MaxDimension / (float)SourceWidth, MaxDimension / (float)SourceHeight);
			const UINT ScaledWidth = std::max(1u, (UINT)(SourceWidth * Scale));
			const UINT ScaledHeight = std::max(1u, (UINT)(SourceHeight * Scale));
			Microsoft::WRL::ComPtr<IWICBitmapScaler> Scaler;
			if(FAILED(Factory->CreateBitmapScaler(&Scaler)) ||
				FAILED(Scaler->Initialize(Source.Get(), ScaledWidth, ScaledHeight, WICBitmapInterpolationModeFant)))
				return false;
			Source = Scaler;
			SourceWidth = ScaledWidth;
			SourceHeight = ScaledHeight;
		}

		Microsoft::WRL::ComPtr<IWICFormatConverter> Converter;
		if(FAILED(Factory->CreateFormatConverter(&Converter)) ||
			FAILED(Converter->Initialize(Source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
			return false;

		const uint64_t ByteCount = (uint64_t)SourceWidth * SourceHeight * 4;
		if(ByteCount > std::numeric_limits<UINT>::max())
			return false;
		vRgba.resize((size_t)ByteCount);
		if(FAILED(Converter->CopyPixels(nullptr, SourceWidth * 4, (UINT)ByteCount, vRgba.data())))
			return false;
		Width = SourceWidth;
		Height = SourceHeight;
		return true;
	}

	void Worker()
	{
		log_info("aether/media", "worker starting");
		try
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		}
		catch(const winrt::hresult_error &Error)
		{
			log_warn("aether/media", "WinRT initialization failed hr=0x%08X", (unsigned)Error.code().value);
			std::lock_guard Lock(m_Mutex);
			m_Snapshot.m_PlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
			return;
		}
		CProcessCapture Capture;
		DWORD RootProcessId = 0;
		DWORD AudioProcessId = 0;
		int64_t NextMetadataPoll = 0;
		int64_t NextAudioSessionPoll = 0;
		int64_t NextCaptureAttempt = 0;
		int64_t LastAudioMs = 0;
		float SessionPeak = 0.0f;
		float LastRootMeanSquare = 0.0f;
		uint64_t CapturedFrames = 0;
		bool LastLoggedAudioActive = false;
		bool LoggedCaptureFrames = false;
		bool LoggedCaptureRms = false;
		bool LoggedAudibleRms = false;
		AetherMusic::EPlaybackState MediaPlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
		std::array<float, 5> aSmoothedBands{};
		std::vector<uint8_t> vLastArtworkEncoded;
		winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager Manager{nullptr};
		try
		{
			Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			log_info("aether/media", "media session manager ready");
		}
		catch(const winrt::hresult_error &Error)
		{
			log_warn("aether/media", "media session manager failed hr=0x%08X", (unsigned)Error.code().value);
		}

		while(!m_Stop.load())
		{
			const int64_t Now = SteadyMilliseconds();
			if(Manager && Now >= NextMetadataPoll)
			{
				NextMetadataPoll = Now + 1000;
				try
				{
					auto Session = SelectMediaSession(Manager);
					if(Session)
					{
						auto PlaybackInfo = Session.GetPlaybackInfo();
						const auto Status = PlaybackInfo.PlaybackStatus();
						MediaPlaybackState = AetherMusic::EPlaybackState::STOPPED;
						if(Status == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
							MediaPlaybackState = AetherMusic::EPlaybackState::PLAYING;
						else if(Status == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused)
							MediaPlaybackState = AetherMusic::EPlaybackState::PAUSED;

						const std::string Source = winrt::to_string(Session.SourceAppUserModelId());
						const DWORD ProcessId = FindSessionProcessId(Session.SourceAppUserModelId());
						if(ProcessId != RootProcessId)
						{
							Capture.Stop();
							RootProcessId = ProcessId;
							AudioProcessId = 0;
							SessionPeak = 0.0f;
							LastRootMeanSquare = 0.0f;
							CapturedFrames = 0;
							LoggedCaptureFrames = false;
							LoggedCaptureRms = false;
							LoggedAudibleRms = false;
							LastAudioMs = 0;
							aSmoothedBands = {};
							NextAudioSessionPoll = 0;
							NextCaptureAttempt = 0;
							log_info("aether/media", "media source='%s' root_pid=%u", Source.c_str(), RootProcessId);
						}

						std::vector<uint8_t> vArtwork;
						try
						{
							auto Properties = Session.TryGetMediaPropertiesAsync().get();
							auto Thumbnail = Properties.Thumbnail();
							if(Thumbnail)
							{
								auto Stream = Thumbnail.OpenReadAsync().get();
								const uint64_t Size = std::min<uint64_t>(Stream.Size(), 16 * 1024 * 1024);
								if(Size > 0)
								{
									winrt::Windows::Storage::Streams::DataReader Reader(Stream.GetInputStreamAt(0));
									Reader.LoadAsync((uint32_t)Size).get();
									vArtwork.resize((size_t)Size);
									Reader.ReadBytes(vArtwork);
								}
							}
						}
						catch(const winrt::hresult_error &Error)
						{
							log_debug("aether/media", "artwork read failed hr=0x%08X", (unsigned)Error.code().value);
						}

						const bool ArtworkChanged = vArtwork != vLastArtworkEncoded;
						std::shared_ptr<const std::vector<uint8_t>> pArtworkRgba;
						uint32_t ArtworkWidth = 0;
						uint32_t ArtworkHeight = 0;
						if(ArtworkChanged && !vArtwork.empty())
						{
							std::vector<uint8_t> vRgba;
							if(DecodeArtwork(vArtwork, vRgba, ArtworkWidth, ArtworkHeight))
								pArtworkRgba = std::make_shared<const std::vector<uint8_t>>(std::move(vRgba));
						}

						std::lock_guard Lock(m_Mutex);
						m_Snapshot.m_MediaPlaybackState = MediaPlaybackState;
						m_Snapshot.m_Source = Source;
						m_Snapshot.m_ProcessId = ProcessId;
						if(MediaPlaybackState == AetherMusic::EPlaybackState::PLAYING)
							m_Snapshot.m_LastPlayingMs = Now;
						if(ArtworkChanged)
						{
							m_Snapshot.m_pArtworkRgba = std::move(pArtworkRgba);
							m_Snapshot.m_ArtworkWidth = m_Snapshot.m_pArtworkRgba ? ArtworkWidth : 0;
							m_Snapshot.m_ArtworkHeight = m_Snapshot.m_pArtworkRgba ? ArtworkHeight : 0;
							++m_Snapshot.m_ArtworkGeneration;
							if(m_Snapshot.m_pArtworkRgba)
							{
								m_Snapshot.m_ArtworkReceivedMs = Now;
								if(m_Snapshot.m_LastPlayingMs == 0)
									m_Snapshot.m_LastPlayingMs = Now;
							}
							vLastArtworkEncoded = std::move(vArtwork);
						}
					}
					else
					{
						RootProcessId = 0;
						AudioProcessId = 0;
						SessionPeak = 0.0f;
						LastRootMeanSquare = 0.0f;
						CapturedFrames = 0;
						MediaPlaybackState = AetherMusic::EPlaybackState::STOPPED;
						Capture.Stop();
						std::lock_guard Lock(m_Mutex);
						m_Snapshot.m_PlaybackState = AetherMusic::EPlaybackState::STOPPED;
						m_Snapshot.m_MediaPlaybackState = AetherMusic::EPlaybackState::STOPPED;
						m_Snapshot.m_AudioActive = false;
						m_Snapshot.m_CaptureStatus = ECaptureStatus::NO_SESSION;
						m_Snapshot.m_ProcessId = 0;
						m_Snapshot.m_AudioProcessId = 0;
						m_Snapshot.m_SessionPeak = 0.0f;
						m_Snapshot.m_RootMeanSquare = 0.0f;
						m_Snapshot.m_CapturedFrames = 0;
						m_Snapshot.m_Source.clear();
					}
				}
				catch(const winrt::hresult_error &)
				{
					RootProcessId = 0;
					AudioProcessId = 0;
					SessionPeak = 0.0f;
					LastRootMeanSquare = 0.0f;
					CapturedFrames = 0;
					LoggedCaptureFrames = false;
					LoggedCaptureRms = false;
					LoggedAudibleRms = false;
					MediaPlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
					Capture.Stop();
					std::lock_guard Lock(m_Mutex);
					m_Snapshot.m_PlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
					m_Snapshot.m_MediaPlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
					m_Snapshot.m_AudioActive = false;
					m_Snapshot.m_CaptureStatus = ECaptureStatus::NO_SESSION;
					m_Snapshot.m_ProcessId = 0;
					m_Snapshot.m_AudioProcessId = 0;
					m_Snapshot.m_SessionPeak = 0.0f;
					m_Snapshot.m_RootMeanSquare = 0.0f;
					m_Snapshot.m_CapturedFrames = 0;
				}
			}

			const bool VisualizerEnabled = m_VisualizerEnabled.load();
			if(VisualizerEnabled && RootProcessId != 0 && Now >= NextAudioSessionPoll)
			{
				NextAudioSessionPoll = Now + 500;
				const SAudioSessionTarget AudioTarget = FindAudioSessionTarget(RootProcessId);
				const DWORD ResolvedAudioProcessId = AudioTarget.m_ProcessId != 0 ? AudioTarget.m_ProcessId : RootProcessId;
				SessionPeak = AudioTarget.m_Peak;
				if(ResolvedAudioProcessId != AudioProcessId)
				{
					Capture.Stop();
					AudioProcessId = ResolvedAudioProcessId;
					LastRootMeanSquare = 0.0f;
					CapturedFrames = 0;
					LoggedCaptureFrames = false;
					LoggedCaptureRms = false;
					LoggedAudibleRms = false;
					LastAudioMs = 0;
					aSmoothedBands = {};
					NextCaptureAttempt = 0;
					log_info("aether/media", "audio target root_pid=%u audio_pid=%u peak=%.6f", RootProcessId, AudioProcessId, SessionPeak);
				}
			}

			const bool WantVisualizer = VisualizerEnabled && RootProcessId != 0 && AudioProcessId != 0 && MediaPlaybackState != AetherMusic::EPlaybackState::UNAVAILABLE;
			if(WantVisualizer && !Capture.Active() && Now >= NextCaptureAttempt)
			{
				if(Capture.Start(AudioProcessId))
					log_info("aether/media", "capture started root_pid=%u audio_pid=%u hr=0x%08X", RootProcessId, AudioProcessId, (unsigned)Capture.LastResult());
				else
				{
					log_warn("aether/media", "capture failed root_pid=%u audio_pid=%u hr=0x%08X", RootProcessId, AudioProcessId, (unsigned)Capture.LastResult());
					NextCaptureAttempt = Now + 2000;
				}
			}
			else if((!WantVisualizer || Capture.ProcessId() != AudioProcessId) && Capture.Active())
				Capture.Stop();

			CProcessCapture::SPollResult PollResult;
			if(Capture.Active())
				PollResult = Capture.Poll(m_Sensitivity.load());
			CapturedFrames += PollResult.m_Frames;
			if(PollResult.m_HasSamples)
				LastRootMeanSquare = PollResult.m_RootMeanSquare;
			if(!LoggedCaptureFrames && CapturedFrames > 0)
			{
				log_info("aether/media", "capture receiving root_pid=%u audio_pid=%u frames=%llu",
					RootProcessId,
					AudioProcessId,
					(unsigned long long)CapturedFrames);
				LoggedCaptureFrames = true;
			}
			if(!LoggedCaptureRms && PollResult.m_HasSamples && PollResult.m_RootMeanSquare > 0.0f)
			{
				log_info("aether/media", "capture signal root_pid=%u audio_pid=%u rms=%.6f",
					RootProcessId,
					AudioProcessId,
					PollResult.m_RootMeanSquare);
				LoggedCaptureRms = true;
			}
			if(!LoggedAudibleRms && PollResult.m_HasSamples && PollResult.m_RootMeanSquare > 0.0005f)
			{
				log_info("aether/media", "capture audible root_pid=%u audio_pid=%u frames=%llu rms=%.6f",
					RootProcessId,
					AudioProcessId,
					(unsigned long long)CapturedFrames,
					PollResult.m_RootMeanSquare);
				LoggedAudibleRms = true;
			}
			const bool AudioDetected = (PollResult.m_HasSamples && PollResult.m_RootMeanSquare > 0.0005f) || SessionPeak > 0.0001f;
			if(AudioDetected)
			{
				LastAudioMs = Now;
				if(PollResult.m_HasSamples)
					aSmoothedBands = PollResult.m_aBands;
			}
			else
			{
				for(float &Band : aSmoothedBands)
					Band = Band < 0.005f ? 0.0f : Band * 0.85f;
			}
			const bool AudioActive = LastAudioMs > 0 && Now - LastAudioMs <= 500;
			if(AudioActive != LastLoggedAudioActive)
			{
				log_info("aether/media", "audio %s root_pid=%u audio_pid=%u frames=%llu rms=%.6f peak=%.6f",
					AudioActive ? "active" : "silent",
					RootProcessId,
					AudioProcessId,
					(unsigned long long)CapturedFrames,
					LastRootMeanSquare,
					SessionPeak);
				LastLoggedAudioActive = AudioActive;
			}
			const AetherMusic::EPlaybackState EffectiveState = AetherMusic::EffectivePlaybackState(MediaPlaybackState, LastAudioMs, Now);
			{
				std::lock_guard Lock(m_Mutex);
				m_Snapshot.m_PlaybackState = EffectiveState;
				m_Snapshot.m_MediaPlaybackState = MediaPlaybackState;
				m_Snapshot.m_VisualizerAvailable = Capture.Active();
				m_Snapshot.m_AudioActive = AudioActive;
				m_Snapshot.m_aBands = EffectiveState == AetherMusic::EPlaybackState::PLAYING ? aSmoothedBands : std::array<float, 5>{};
				m_Snapshot.m_ProcessId = RootProcessId;
				m_Snapshot.m_AudioProcessId = AudioProcessId;
				m_Snapshot.m_SessionPeak = SessionPeak;
				m_Snapshot.m_RootMeanSquare = LastRootMeanSquare;
				m_Snapshot.m_CapturedFrames = CapturedFrames;
				if(AudioDetected || MediaPlaybackState == AetherMusic::EPlaybackState::PLAYING)
					m_Snapshot.m_LastPlayingMs = Now;
				if(!VisualizerEnabled)
					m_Snapshot.m_CaptureStatus = ECaptureStatus::DISABLED;
				else if(RootProcessId == 0)
					m_Snapshot.m_CaptureStatus = m_Snapshot.m_Source.empty() ? ECaptureStatus::NO_SESSION : ECaptureStatus::PROCESS_NOT_FOUND;
				else if(Capture.Active())
					m_Snapshot.m_CaptureStatus = ECaptureStatus::CAPTURING;
				else
					m_Snapshot.m_CaptureStatus = ECaptureStatus::START_FAILED;
			}
			std::this_thread::sleep_for(25ms);
		}
		Capture.Stop();
		winrt::uninit_apartment();
	}
#else
	void Worker()
	{
		std::lock_guard Lock(m_Mutex);
		m_Snapshot.m_PlaybackState = AetherMusic::EPlaybackState::UNAVAILABLE;
	}
#endif

	void Start()
	{
		if(m_Thread.joinable())
			return;
		m_Stop.store(false);
		m_Thread = std::thread([this] { Worker(); });
	}

	void Stop()
	{
		m_Stop.store(true);
		if(m_Thread.joinable())
			m_Thread.join();
		std::lock_guard Lock(m_Mutex);
		m_Snapshot.m_aBands = {};
		m_Snapshot.m_VisualizerAvailable = false;
		m_Snapshot.m_AudioActive = false;
		m_Snapshot.m_CaptureStatus = ECaptureStatus::DISABLED;
	}
};

CAetherMediaBackend::CAetherMediaBackend() :
	m_pImpl(std::make_unique<CImpl>())
{
}

CAetherMediaBackend::~CAetherMediaBackend()
{
	Stop();
}

void CAetherMediaBackend::Start()
{
	m_pImpl->Start();
}

void CAetherMediaBackend::Stop()
{
	m_pImpl->Stop();
}

bool CAetherMediaBackend::Running() const
{
	return m_pImpl->m_Thread.joinable();
}

CAetherMediaBackend::SSnapshot CAetherMediaBackend::Snapshot() const
{
	std::lock_guard Lock(m_pImpl->m_Mutex);
	return m_pImpl->m_Snapshot;
}

void CAetherMediaBackend::SetVisualizer(bool Enabled, int Sensitivity)
{
	m_pImpl->m_VisualizerEnabled.store(Enabled);
	m_pImpl->m_Sensitivity.store(std::clamp(Sensitivity, 50, 1500));
}
