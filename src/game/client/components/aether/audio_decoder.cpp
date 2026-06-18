#include "audio_decoder.h"

#include <base/detect.h>
#include <base/io.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#endif

#include <algorithm>
#include <climits>
#include <cstdlib>

#if defined(CONF_FAMILY_WINDOWS)
#undef WIN32_LEAN_AND_MEAN
#undef NOGDI
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000A
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

namespace
{
struct CFfmpegMemoryReader
{
	const unsigned char *m_pData = nullptr;
	size_t m_Size = 0;
	size_t m_Pos = 0;
};

int FfmpegReadPacket(void *pOpaque, uint8_t *pBuffer, int BufferSize)
{
	auto *pReader = static_cast<CFfmpegMemoryReader *>(pOpaque);
	if(!pReader || !pBuffer || BufferSize <= 0)
		return AVERROR(EINVAL);
	if(pReader->m_Pos >= pReader->m_Size)
		return AVERROR_EOF;

	const size_t Remaining = pReader->m_Size - pReader->m_Pos;
	const size_t CopySize = std::min((size_t)BufferSize, Remaining);
	mem_copy(pBuffer, pReader->m_pData + pReader->m_Pos, CopySize);
	pReader->m_Pos += CopySize;
	return (int)CopySize;
}

int64_t FfmpegSeek(void *pOpaque, int64_t Offset, int Whence)
{
	auto *pReader = static_cast<CFfmpegMemoryReader *>(pOpaque);
	if(!pReader)
		return AVERROR(EINVAL);
	if(Whence == AVSEEK_SIZE)
		return (int64_t)pReader->m_Size;

	size_t NewPos = pReader->m_Pos;
	const int BaseWhence = Whence & 0x3;
	if(BaseWhence == SEEK_SET)
		NewPos = (size_t)maximum<int64_t>(0, Offset);
	else if(BaseWhence == SEEK_CUR)
		NewPos = (size_t)maximum<int64_t>(0, (int64_t)NewPos + Offset);
	else if(BaseWhence == SEEK_END)
		NewPos = (size_t)maximum<int64_t>(0, (int64_t)pReader->m_Size + Offset);
	else
		return AVERROR(EINVAL);

	NewPos = std::min(NewPos, pReader->m_Size);
	pReader->m_Pos = NewPos;
	return (int64_t)NewPos;
}

bool AetherAudioPathHasExtension(const char *pPath, const char *pExt)
{
	const int PathLen = str_length(pPath);
	const int ExtLen = str_length(pExt);
	return PathLen >= ExtLen && str_comp_nocase(pPath + PathLen - ExtLen, pExt) == 0;
}

const char *AetherAvError(int Error)
{
	thread_local char s_aBuffer[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(Error, s_aBuffer, sizeof(s_aBuffer));
	return s_aBuffer;
}

#if defined(CONF_FAMILY_WINDOWS)
template<class T>
void AetherSafeRelease(T *&pObject)
{
	if(pObject)
	{
		pObject->Release();
		pObject = nullptr;
	}
}

bool DecodeMp3WithMediaFoundation(const char *pPath, std::vector<short> &vInterleavedOut, int &Channels, int &SampleRate, const char *pContextName)
{
	vInterleavedOut.clear();
	Channels = 0;
	SampleRate = 0;
	if(!pPath || pPath[0] == '\0')
		return false;

	const HRESULT CoHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool CoInitialized = SUCCEEDED(CoHr);
	if(FAILED(CoHr) && CoHr != RPC_E_CHANGED_MODE)
	{
		log_error("aether/audio", "Media Foundation COM init failed hr=0x%08x (file='%s' ctx='%s')", (unsigned)CoHr, pPath, pContextName ? pContextName : "");
		return false;
	}

	HRESULT Hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if(FAILED(Hr))
	{
		if(CoInitialized)
			CoUninitialize();
		log_error("aether/audio", "MFStartup failed hr=0x%08x (file='%s' ctx='%s')", (unsigned)Hr, pPath, pContextName ? pContextName : "");
		return false;
	}

	IMFSourceReader *pReader = nullptr;
	IMFMediaType *pOutType = nullptr;
	IMFMediaType *pCurrentType = nullptr;
	bool Success = false;

	auto Finish = [&]() {
		AetherSafeRelease(pCurrentType);
		AetherSafeRelease(pOutType);
		AetherSafeRelease(pReader);
		MFShutdown();
		if(CoInitialized)
			CoUninitialize();
	};

	const std::wstring WidePath = windows_utf8_to_wide(pPath);
	Hr = MFCreateSourceReaderFromURL(WidePath.c_str(), nullptr, &pReader);
	if(FAILED(Hr))
	{
		log_error("aether/audio", "MFCreateSourceReaderFromURL failed hr=0x%08x (file='%s' ctx='%s')", (unsigned)Hr, pPath, pContextName ? pContextName : "");
		Finish();
		return false;
	}

	Hr = MFCreateMediaType(&pOutType);
	if(SUCCEEDED(Hr))
		Hr = pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if(SUCCEEDED(Hr))
		Hr = pOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if(SUCCEEDED(Hr))
		Hr = pOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if(SUCCEEDED(Hr))
		Hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pOutType);
	if(FAILED(Hr))
	{
		log_error("aether/audio", "Media Foundation PCM type failed hr=0x%08x (file='%s' ctx='%s')", (unsigned)Hr, pPath, pContextName ? pContextName : "");
		Finish();
		return false;
	}

	Hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pCurrentType);
	UINT32 MfChannels = 0;
	UINT32 MfSampleRate = 0;
	UINT32 MfBits = 0;
	if(SUCCEEDED(Hr))
		Hr = pCurrentType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &MfChannels);
	if(SUCCEEDED(Hr))
		Hr = pCurrentType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &MfSampleRate);
	if(SUCCEEDED(Hr))
		Hr = pCurrentType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &MfBits);
	if(FAILED(Hr) || (MfChannels != 1 && MfChannels != 2) || MfSampleRate == 0 || MfBits != 16)
	{
		log_error("aether/audio", "Media Foundation unsupported PCM format hr=0x%08x channels=%u rate=%u bits=%u (file='%s' ctx='%s')", (unsigned)Hr, MfChannels, MfSampleRate, MfBits, pPath, pContextName ? pContextName : "");
		Finish();
		return false;
	}

	constexpr int64_t MaxSamplesPerChannel = 48000 * 90;
	Channels = (int)MfChannels;
	SampleRate = (int)MfSampleRate;

	for(;;)
	{
		DWORD StreamIndex = 0;
		DWORD Flags = 0;
		LONGLONG Timestamp = 0;
		IMFSample *pSample = nullptr;
		Hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &StreamIndex, &Flags, &Timestamp, &pSample);
		if(FAILED(Hr))
		{
			log_error("aether/audio", "Media Foundation ReadSample failed hr=0x%08x (file='%s' ctx='%s')", (unsigned)Hr, pPath, pContextName ? pContextName : "");
			break;
		}
		if(Flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			AetherSafeRelease(pSample);
			Success = true;
			break;
		}
		if(!pSample)
			continue;

		IMFMediaBuffer *pBuffer = nullptr;
		Hr = pSample->ConvertToContiguousBuffer(&pBuffer);
		if(SUCCEEDED(Hr) && pBuffer)
		{
			BYTE *pBytes = nullptr;
			DWORD MaxLength = 0;
			DWORD CurrentLength = 0;
			Hr = pBuffer->Lock(&pBytes, &MaxLength, &CurrentLength);
			if(SUCCEEDED(Hr) && pBytes && CurrentLength >= sizeof(short))
			{
				const size_t NumShorts = (size_t)CurrentLength / sizeof(short);
				const int64_t CurFrames = (int64_t)vInterleavedOut.size() / (int64_t)maximum(1, Channels);
				const int64_t AddFrames = (int64_t)NumShorts / (int64_t)maximum(1, Channels);
				if(CurFrames + AddFrames > MaxSamplesPerChannel)
				{
					pBuffer->Unlock();
					AetherSafeRelease(pBuffer);
					AetherSafeRelease(pSample);
					Success = true;
					break;
				}

				const size_t OldSize = vInterleavedOut.size();
				vInterleavedOut.resize(OldSize + NumShorts);
				mem_copy(vInterleavedOut.data() + OldSize, pBytes, NumShorts * sizeof(short));
			}
			if(SUCCEEDED(Hr))
				pBuffer->Unlock();
		}
		AetherSafeRelease(pBuffer);
		AetherSafeRelease(pSample);
	}

	if(vInterleavedOut.empty())
		Success = false;

	if(Success)
		log_info("aether/audio", "decoded mp3 with Media Foundation frames=%d channels=%d rate=%d ctx='%s'", (int)vInterleavedOut.size() / maximum(1, Channels), Channels, SampleRate, pContextName ? pContextName : "");
	else
	{
		vInterleavedOut.clear();
		Channels = 0;
		SampleRate = 0;
	}

	Finish();
	return Success;
}
#endif

bool DecodeMp3BytesToS16Pcm(const unsigned char *pData, size_t DataSize, std::vector<short> &vInterleavedOut, int &Channels, int &SampleRate, const char *pFileName, const char *pContextName)
{
	vInterleavedOut.clear();
	Channels = 0;
	SampleRate = 0;
	if(!pData || DataSize == 0)
		return false;

	constexpr int64_t MaxSamplesPerChannel = 48000 * 90;
	const AVCodec *pCodec = avcodec_find_decoder(AV_CODEC_ID_MP3);
	if(!pCodec)
	{
		log_error("aether/audio", "mp3 decoder missing (file='%s' ctx='%s')", pFileName, pContextName ? pContextName : "");
		return false;
	}

	AVCodecParserContext *pParser = av_parser_init(AV_CODEC_ID_MP3);
	AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
	SwrContext *pSwr = nullptr;
	AVPacket *pPkt = av_packet_alloc();
	AVFrame *pFrame = av_frame_alloc();
	AVChannelLayout InLayout{};
	AVChannelLayout OutLayout{};
	bool InLayoutInited = false;
	bool OutLayoutInited = false;

	auto Cleanup = [&]() {
		if(pFrame)
			av_frame_free(&pFrame);
		if(pPkt)
			av_packet_free(&pPkt);
		swr_free(&pSwr);
		avcodec_free_context(&pCodecCtx);
		if(pParser)
			av_parser_close(pParser);
		if(OutLayoutInited)
			av_channel_layout_uninit(&OutLayout);
		if(InLayoutInited)
			av_channel_layout_uninit(&InLayout);
	};

	auto Fail = [&](const char *pWhy) {
		log_error("aether/audio", "%s (file='%s' ctx='%s')", pWhy, pFileName, pContextName ? pContextName : "");
		Cleanup();
		vInterleavedOut.clear();
		Channels = 0;
		SampleRate = 0;
	};

	if(!pParser || !pCodecCtx || !pPkt || !pFrame || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
	{
		Fail("mp3 parser/decoder init failed");
		return false;
	}

	auto EnsureResampler = [&]() -> bool {
		if(pSwr)
			return true;

		if(av_channel_layout_copy(&InLayout, &pFrame->ch_layout) < 0 || InLayout.nb_channels <= 0)
		{
			av_channel_layout_uninit(&InLayout);
			if(av_channel_layout_copy(&InLayout, &pCodecCtx->ch_layout) < 0 || InLayout.nb_channels <= 0)
			{
				av_channel_layout_uninit(&InLayout);
				av_channel_layout_default(&InLayout, 2);
			}
		}
		InLayoutInited = true;

		if(InLayout.nb_channels <= 0 || InLayout.nb_channels > 2)
			return false;
		if(av_channel_layout_copy(&OutLayout, &InLayout) < 0)
			return false;
		OutLayoutInited = true;

		SampleRate = pFrame->sample_rate > 0 ? pFrame->sample_rate : (pCodecCtx->sample_rate > 0 ? pCodecCtx->sample_rate : 48000);
		Channels = InLayout.nb_channels;

		const int SwrAllocRes = swr_alloc_set_opts2(&pSwr, &OutLayout, AV_SAMPLE_FMT_S16, SampleRate, &InLayout, (AVSampleFormat)pFrame->format, SampleRate, 0, nullptr);
		return SwrAllocRes >= 0 && pSwr && swr_init(pSwr) >= 0;
	};

	auto AppendFrame = [&]() -> bool {
		if(!EnsureResampler())
			return false;

		const int DstEstimate = maximum(1, swr_get_out_samples(pSwr, pFrame->nb_samples) + 32);
		const int64_t Cur = (int64_t)vInterleavedOut.size() / (int64_t)maximum(1, Channels);
		if(Cur + DstEstimate > MaxSamplesPerChannel)
			return false;

		const size_t OldSamples = vInterleavedOut.size();
		vInterleavedOut.resize(OldSamples + (size_t)DstEstimate * (size_t)Channels);
		uint8_t *pDst = reinterpret_cast<uint8_t *>(vInterleavedOut.data() + OldSamples);
		const int Converted = swr_convert(pSwr, &pDst, DstEstimate, (const uint8_t **)pFrame->extended_data, pFrame->nb_samples);
		if(Converted < 0)
			return false;
		vInterleavedOut.resize(OldSamples + (size_t)Converted * (size_t)Channels);
		return true;
	};

	auto DecodePacket = [&]() -> bool {
		if(avcodec_send_packet(pCodecCtx, pPkt) < 0)
			return false;
		while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
		{
			if(!AppendFrame())
				return false;
		}
		return true;
	};

	const uint8_t *pCursor = pData;
	int Remaining = (int)minimum<size_t>(DataSize, (size_t)INT_MAX);
	while(Remaining > 0)
	{
		uint8_t *pParsedData = nullptr;
		int ParsedSize = 0;
		const int Consumed = av_parser_parse2(pParser, pCodecCtx, &pParsedData, &ParsedSize, pCursor, Remaining, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if(Consumed < 0)
		{
			Fail("mp3 parser failed");
			return false;
		}
		pCursor += Consumed;
		Remaining -= Consumed;

		if(ParsedSize > 0)
		{
			pPkt->data = pParsedData;
			pPkt->size = ParsedSize;
			if(!DecodePacket())
			{
				Fail("mp3 decode failed");
				return false;
			}
		}
		else if(Consumed == 0)
			break;
	}

	if(avcodec_send_packet(pCodecCtx, nullptr) >= 0)
	{
		while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
		{
			if(!AppendFrame())
			{
				Fail("mp3 flush failed");
				return false;
			}
		}
	}

	if(pSwr)
	{
		for(int Flushed = 0; Flushed < 256; ++Flushed)
		{
			const int DstEstimate = maximum(1, swr_get_out_samples(pSwr, 0) + 8);
			const int64_t Cur = (int64_t)vInterleavedOut.size() / (int64_t)maximum(1, Channels);
			if(Cur + DstEstimate > MaxSamplesPerChannel)
				break;

			const size_t OldSamples = vInterleavedOut.size();
			vInterleavedOut.resize(OldSamples + (size_t)DstEstimate * (size_t)Channels);
			uint8_t *pDst = reinterpret_cast<uint8_t *>(vInterleavedOut.data() + OldSamples);
			const int Converted = swr_convert(pSwr, &pDst, DstEstimate, nullptr, 0);
			if(Converted <= 0)
			{
				vInterleavedOut.resize(OldSamples);
				break;
			}
			vInterleavedOut.resize(OldSamples + (size_t)Converted * (size_t)Channels);
		}
	}

	if(vInterleavedOut.empty())
	{
		Fail("mp3 decoded zero samples");
		return false;
	}

	Cleanup();
	return true;
}
}

namespace AetherAudio
{
bool DecodeAudioFileToS16Pcm(const char *pAbsolutePath, std::vector<short> &vInterleavedOut, int &Channels, int &SampleRate, const char *pContextName)
{
	vInterleavedOut.clear();
	Channels = 0;
	SampleRate = 0;
	if(!pAbsolutePath || pAbsolutePath[0] == '\0')
		return false;

	constexpr int64_t MaxSamplesPerChannel = 48000 * 90;

	AVFormatContext *pFmt = nullptr;
	AVCodecContext *pCodecCtx = nullptr;
	SwrContext *pSwr = nullptr;
	AVPacket *pPkt = nullptr;
	AVFrame *pFrame = nullptr;
	AVIOContext *pIoCtx = nullptr;
	void *pFileData = nullptr;
	AVChannelLayout InLayout{};
	AVChannelLayout OutLayout{};
	bool InLayoutInited = false;
	bool OutLayoutInited = false;

	auto Cleanup = [&]() {
		if(pFrame)
			av_frame_free(&pFrame);
		if(pPkt)
			av_packet_free(&pPkt);
		swr_free(&pSwr);
		avcodec_free_context(&pCodecCtx);
		if(pFmt)
		{
			if(pFmt->pb == pIoCtx)
				pFmt->pb = nullptr;
			avformat_close_input(&pFmt);
		}
		if(pIoCtx)
			avio_context_free(&pIoCtx);
		free(pFileData);
		pFileData = nullptr;
		if(OutLayoutInited)
			av_channel_layout_uninit(&OutLayout);
		if(InLayoutInited)
			av_channel_layout_uninit(&InLayout);
	};

	auto Fail = [&](const char *pWhy) {
		log_error("aether/audio", "%s (file='%s' ctx='%s')", pWhy, pAbsolutePath, pContextName ? pContextName : "");
		Cleanup();
		vInterleavedOut.clear();
		Channels = 0;
		SampleRate = 0;
	};

	IOHANDLE File = io_open(pAbsolutePath, IOFLAG_READ);
	if(!File)
	{
		Fail("io_open failed");
		return false;
	}

	unsigned FileDataSize = 0;
	const bool ReadOk = io_read_all(File, &pFileData, &FileDataSize);
	io_close(File);
	if(!ReadOk || !pFileData || FileDataSize == 0)
	{
		Fail("io_read_all failed");
		return false;
	}

	bool Mp3Decoded = false;
	if(AetherAudioPathHasExtension(pAbsolutePath, ".mp3"))
	{
#if defined(CONF_FAMILY_WINDOWS)
		Mp3Decoded = DecodeMp3WithMediaFoundation(pAbsolutePath, vInterleavedOut, Channels, SampleRate, pContextName);
		if(!Mp3Decoded)
			Mp3Decoded = DecodeMp3BytesToS16Pcm(static_cast<const unsigned char *>(pFileData), (size_t)FileDataSize, vInterleavedOut, Channels, SampleRate, pAbsolutePath, pContextName);
#else
		Mp3Decoded = DecodeMp3BytesToS16Pcm(static_cast<const unsigned char *>(pFileData), (size_t)FileDataSize, vInterleavedOut, Channels, SampleRate, pAbsolutePath, pContextName);
#endif
	}
	if(Mp3Decoded)
	{
		free(pFileData);
		pFileData = nullptr;
		return true;
	}

	CFfmpegMemoryReader Reader{static_cast<const unsigned char *>(pFileData), (size_t)FileDataSize, 0};
	uint8_t *pIoBuffer = (uint8_t *)av_malloc(4096);
	if(!pIoBuffer)
	{
		Fail("avio buffer alloc failed");
		return false;
	}

	pIoCtx = avio_alloc_context(pIoBuffer, 4096, 0, &Reader, FfmpegReadPacket, nullptr, FfmpegSeek);
	if(!pIoCtx)
	{
		av_free(pIoBuffer);
		Fail("avio_alloc_context failed");
		return false;
	}

	pFmt = avformat_alloc_context();
	if(!pFmt)
	{
		Fail("avformat_alloc_context failed");
		return false;
	}
	pFmt->pb = pIoCtx;
	pFmt->flags |= AVFMT_FLAG_CUSTOM_IO;

	const AVInputFormat *pForcedFormat = nullptr;
	if(AetherAudioPathHasExtension(pAbsolutePath, ".mp3"))
		pForcedFormat = av_find_input_format("mp3");
	const int OpenResult = avformat_open_input(&pFmt, nullptr, pForcedFormat, nullptr);
	if(OpenResult < 0)
	{
		char aError[128];
		str_format(aError, sizeof(aError), "avformat_open_input memory failed: %s", AetherAvError(OpenResult));
		Fail(aError);
		return false;
	}

	if(avformat_find_stream_info(pFmt, nullptr) < 0)
	{
		Fail("avformat_find_stream_info failed");
		return false;
	}

	const int AudioStream = av_find_best_stream(pFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if(AudioStream < 0)
	{
		Fail("no audio stream");
		return false;
	}

	AVStream *pStream = pFmt->streams[AudioStream];
	const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
	if(!pCodec)
	{
		Fail("avcodec_find_decoder failed");
		return false;
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);
	if(!pCodecCtx || avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0 || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
	{
		Fail("avcodec_open2 failed");
		return false;
	}

	if(av_channel_layout_copy(&InLayout, &pCodecCtx->ch_layout) < 0 || InLayout.nb_channels <= 0)
	{
		av_channel_layout_uninit(&InLayout);
		if(av_channel_layout_copy(&InLayout, &pStream->codecpar->ch_layout) < 0 || InLayout.nb_channels <= 0)
		{
			av_channel_layout_uninit(&InLayout);
			const int Nb = pStream->codecpar->ch_layout.nb_channels > 0 ? pStream->codecpar->ch_layout.nb_channels : 2;
			av_channel_layout_default(&InLayout, std::clamp(Nb, 1, 2));
		}
	}
	InLayoutInited = true;

	if(InLayout.nb_channels <= 0 || InLayout.nb_channels > 2)
	{
		Fail("unsupported channel count");
		return false;
	}

	if(av_channel_layout_copy(&OutLayout, &InLayout) < 0)
	{
		Fail("output channel layout failed");
		return false;
	}
	OutLayoutInited = true;

	SampleRate = pCodecCtx->sample_rate <= 0 ? 48000 : pCodecCtx->sample_rate;
	Channels = InLayout.nb_channels;

	const int SwrAllocRes = swr_alloc_set_opts2(&pSwr, &OutLayout, AV_SAMPLE_FMT_S16, SampleRate, &InLayout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, nullptr);
	if(SwrAllocRes < 0 || !pSwr || swr_init(pSwr) < 0)
	{
		Fail("swr_init failed");
		return false;
	}

	pPkt = av_packet_alloc();
	pFrame = av_frame_alloc();
	if(!pPkt || !pFrame)
	{
		Fail("packet/frame alloc failed");
		return false;
	}

	auto AppendFrame = [&]() -> bool {
		const int DstEstimate = maximum(1, swr_get_out_samples(pSwr, pFrame->nb_samples) + 32);
		const int64_t Cur = (int64_t)vInterleavedOut.size() / (int64_t)maximum(1, Channels);
		if(Cur + DstEstimate > MaxSamplesPerChannel)
			return false;

		const size_t OldSamples = vInterleavedOut.size();
		vInterleavedOut.resize(OldSamples + (size_t)DstEstimate * (size_t)Channels);
		uint8_t *pDst = reinterpret_cast<uint8_t *>(vInterleavedOut.data() + OldSamples);
		const int Converted = swr_convert(pSwr, &pDst, DstEstimate, (const uint8_t **)pFrame->extended_data, pFrame->nb_samples);
		if(Converted < 0)
			return false;
		vInterleavedOut.resize(OldSamples + (size_t)Converted * (size_t)Channels);
		return true;
	};

	while(av_read_frame(pFmt, pPkt) >= 0)
	{
		if(pPkt->stream_index != AudioStream)
		{
			av_packet_unref(pPkt);
			continue;
		}
		if(avcodec_send_packet(pCodecCtx, pPkt) < 0)
		{
			av_packet_unref(pPkt);
			break;
		}
		av_packet_unref(pPkt);
		while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
		{
			if(!AppendFrame())
			{
				Fail("decode/resample failed");
				return false;
			}
		}
	}

	if(avcodec_send_packet(pCodecCtx, nullptr) >= 0)
	{
		while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
		{
			if(!AppendFrame())
			{
				Fail("decode/resample failed");
				return false;
			}
		}
	}

	for(int Flushed = 0; Flushed < 256; ++Flushed)
	{
		const int DstEstimate = maximum(1, swr_get_out_samples(pSwr, 0) + 8);
		const int64_t Cur = (int64_t)vInterleavedOut.size() / (int64_t)maximum(1, Channels);
		if(Cur + DstEstimate > MaxSamplesPerChannel)
			break;

		const size_t OldSamples = vInterleavedOut.size();
		vInterleavedOut.resize(OldSamples + (size_t)DstEstimate * (size_t)Channels);
		uint8_t *pDst = reinterpret_cast<uint8_t *>(vInterleavedOut.data() + OldSamples);
		const int Converted = swr_convert(pSwr, &pDst, DstEstimate, nullptr, 0);
		if(Converted <= 0)
		{
			vInterleavedOut.resize(OldSamples);
			break;
		}
		vInterleavedOut.resize(OldSamples + (size_t)Converted * (size_t)Channels);
	}

	if(vInterleavedOut.empty())
	{
		Fail("decoded zero samples");
		return false;
	}

	Cleanup();
	return true;
}
}
