#include "realtime_client.h"

#include <base/detect.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <curl/curl.h>
#include <curl/websockets.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
constexpr int RECONNECT_DELAY_SECONDS = 5;
constexpr int UNSUPPORTED_PROTOCOL_RECONNECT_DELAY_SECONDS = 60;
constexpr int CONNECT_TIMEOUT_SECONDS = 8;
constexpr int IDLE_HELLO_SECONDS = 10;

bool CurlSupportsProtocol(const char *pProtocol)
{
	const curl_version_info_data *pInfo = curl_version_info(CURLVERSION_NOW);
	if(!pInfo || !pInfo->protocols || !pProtocol)
		return true;
	for(const char *const *ppProtocol = pInfo->protocols; *ppProtocol; ++ppProtocol)
	{
		if(str_comp(*ppProtocol, pProtocol) == 0)
			return true;
	}
	return false;
}

int SocketSelectReadable(curl_socket_t Socket, int TimeoutMs)
{
	if(Socket == CURL_SOCKET_BAD)
		return 0;
	fd_set ReadSet;
	FD_ZERO(&ReadSet);
	FD_SET(Socket, &ReadSet);
	timeval Timeout;
	Timeout.tv_sec = TimeoutMs / 1000;
	Timeout.tv_usec = (TimeoutMs % 1000) * 1000;
#if defined(CONF_FAMILY_WINDOWS)
	return select(0, &ReadSet, nullptr, nullptr, &Timeout);
#else
	return select(Socket + 1, &ReadSet, nullptr, nullptr, &Timeout);
#endif
}
} // namespace

CAetherRealtimeClient::~CAetherRealtimeClient()
{
	Stop();
}

void CAetherRealtimeClient::Start()
{
	bool Expected = false;
	if(!m_Stop.compare_exchange_strong(Expected, false) && m_Worker.joinable())
		return;
	if(m_Worker.joinable())
		return;
	m_Stop.store(false);
	m_Worker = std::thread(&CAetherRealtimeClient::ThreadMain, this);
}

void CAetherRealtimeClient::Stop()
{
	m_Stop.store(true);
	m_Cv.notify_all();
	if(m_Worker.joinable())
		m_Worker.join();
	m_Connected.store(false);
}

bool CAetherRealtimeClient::BuildEndpoint(const char *pHttpBaseUrl, char *pOut, int OutSize)
{
	if(!pHttpBaseUrl || !pHttpBaseUrl[0] || !pOut || OutSize <= 0)
		return false;
	char aBase[256];
	str_copy(aBase, pHttpBaseUrl, sizeof(aBase));
	int Len = str_length(aBase);
	while(Len > 0 && aBase[Len - 1] == '/')
		aBase[--Len] = '\0';

	if(str_startswith(aBase, "https://"))
		str_format(pOut, OutSize, "wss://%s/v1/realtime/ws", aBase + 8);
	else if(str_startswith(aBase, "http://"))
		str_format(pOut, OutSize, "ws://%s/v1/realtime/ws", aBase + 7);
	else if(str_startswith(aBase, "ws://") || str_startswith(aBase, "wss://"))
		str_format(pOut, OutSize, "%s/v1/realtime/ws", aBase);
	else
		str_format(pOut, OutSize, "wss://%s/v1/realtime/ws", aBase);
	return true;
}

void CAetherRealtimeClient::SetEndpointFromHttpBase(const char *pHttpBaseUrl)
{
	char aEndpoint[320];
	if(!BuildEndpoint(pHttpBaseUrl, aEndpoint, sizeof(aEndpoint)))
		aEndpoint[0] = '\0';
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Endpoint == aEndpoint)
			return;
		m_Endpoint = aEndpoint;
	}
	m_LastFailureUnsupportedProtocol.store(false);
	m_UnsupportedProtocolLogged.store(false);
	m_Cv.notify_all();
}

void CAetherRealtimeClient::SetHelloPayload(const std::string &Payload)
{
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_HelloPayload == Payload)
			return;
		m_HelloPayload = Payload;
	}
	m_Cv.notify_all();
}

void CAetherRealtimeClient::QueuePayload(const std::string &Payload)
{
	if(Payload.empty())
		return;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Outgoing.size() >= 64)
			m_Outgoing.pop_front();
		m_Outgoing.push_back(Payload);
	}
	m_Cv.notify_all();
}

void CAetherRealtimeClient::PumpMessages(std::vector<std::string> &vMessages)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	while(!m_Incoming.empty())
	{
		vMessages.emplace_back(std::move(m_Incoming.front()));
		m_Incoming.pop_front();
	}
}

void CAetherRealtimeClient::Status(char *pOut, int OutSize) const
{
	if(!pOut || OutSize <= 0)
		return;
	std::lock_guard<std::mutex> Lock(m_Mutex);
	str_copy(pOut, m_Status.c_str(), OutSize);
}

void CAetherRealtimeClient::PushMessage(const char *pData, size_t Size)
{
	if(!pData || Size == 0)
		return;
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_Incoming.size() >= 64)
		m_Incoming.pop_front();
	m_Incoming.emplace_back(pData, Size);
}

void CAetherRealtimeClient::SetStatus(const char *pStatus)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_Status = pStatus ? pStatus : "";
}

void CAetherRealtimeClient::ThreadMain()
{
	SetStatus("Realtime idle");
	while(!m_Stop.load())
	{
		std::string Endpoint;
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			m_Cv.wait_for(Lock, std::chrono::milliseconds(250), [&] {
				return m_Stop.load() || !m_Endpoint.empty();
			});
			if(m_Stop.load())
				break;
			Endpoint = m_Endpoint;
		}
		if(Endpoint.empty())
			continue;

		if(!RunConnection(Endpoint) && !m_Stop.load())
		{
			const int DelaySeconds = m_LastFailureUnsupportedProtocol.load() ? UNSUPPORTED_PROTOCOL_RECONNECT_DELAY_SECONDS : RECONNECT_DELAY_SECONDS;
			SetStatus(m_LastFailureUnsupportedProtocol.load() ? "Realtime unsupported; HTTP fallback active" : "Realtime retrying");
			for(int i = 0; i < DelaySeconds * 10 && !m_Stop.load(); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	SetStatus("Realtime stopped");
}

bool CAetherRealtimeClient::RunConnection(const std::string &Endpoint)
{
	const bool WantsWss = str_startswith(Endpoint.c_str(), "wss://");
	const bool WantsWs = str_startswith(Endpoint.c_str(), "ws://");
	if((WantsWss && !CurlSupportsProtocol("wss")) || (WantsWs && !CurlSupportsProtocol("ws")))
	{
		m_LastFailureUnsupportedProtocol.store(true);
		SetStatus("Realtime unsupported; HTTP Oracle fallback active");
		if(!m_UnsupportedProtocolLogged.exchange(true))
			log_info("aether/realtime", "Realtime websocket unsupported; HTTP Oracle fallback active");
		m_Connected.store(false);
		return false;
	}

	CURL *pCurl = curl_easy_init();
	if(!pCurl)
	{
		SetStatus("Realtime init failed");
		return false;
	}

	curl_easy_setopt(pCurl, CURLOPT_URL, Endpoint.c_str());
	curl_easy_setopt(pCurl, CURLOPT_CONNECT_ONLY, 2L);
	curl_easy_setopt(pCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
	curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "AetherClient-Realtime/1");

	m_LastFailureUnsupportedProtocol.store(false);
	SetStatus("Realtime connecting");
	CURLcode Code = curl_easy_perform(pCurl);
	if(Code != CURLE_OK)
	{
		char aStatus[160];
		str_format(aStatus, sizeof(aStatus), "Realtime connect failed: %s", curl_easy_strerror(Code));
		if(Code == CURLE_UNSUPPORTED_PROTOCOL)
			m_LastFailureUnsupportedProtocol.store(true);
		SetStatus(aStatus);
		if(Code != CURLE_UNSUPPORTED_PROTOCOL || !m_UnsupportedProtocolLogged.exchange(true))
			log_info("aether/realtime", "%s", aStatus);
		curl_easy_cleanup(pCurl);
		m_Connected.store(false);
		return false;
	}

	curl_socket_t Socket = CURL_SOCKET_BAD;
	curl_easy_getinfo(pCurl, CURLINFO_ACTIVESOCKET, &Socket);
	m_Connected.store(true);
	m_UnsupportedProtocolLogged.store(false);
	SetStatus("Realtime connected");

	std::string LastSentHello;
	int64_t LastHelloTime = 0;
	bool Success = true;
	while(!m_Stop.load())
	{
		std::string EndpointNow;
		std::string Hello;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			EndpointNow = m_Endpoint;
			Hello = m_HelloPayload;
		}
		if(EndpointNow != Endpoint)
			break;

		const int64_t Now = time_get();
		if(!Hello.empty() && (Hello != LastSentHello || LastHelloTime == 0 || Now - LastHelloTime > (int64_t)IDLE_HELLO_SECONDS * time_freq()))
		{
			size_t Sent = 0;
			Code = curl_ws_send(pCurl, Hello.data(), Hello.size(), &Sent, 0, CURLWS_TEXT);
			if(Code != CURLE_OK)
			{
				char aStatus[160];
				str_format(aStatus, sizeof(aStatus), "Realtime send failed: %s", curl_easy_strerror(Code));
				SetStatus(aStatus);
				log_info("aether/realtime", "%s", aStatus);
				Success = false;
				break;
			}
			LastSentHello = Hello;
			LastHelloTime = Now;
		}

		while(!m_Stop.load())
		{
			std::string Payload;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Outgoing.empty())
					break;
				Payload = std::move(m_Outgoing.front());
				m_Outgoing.pop_front();
			}
			size_t Sent = 0;
			Code = curl_ws_send(pCurl, Payload.data(), Payload.size(), &Sent, 0, CURLWS_TEXT);
			if(Code != CURLE_OK)
			{
				char aStatus[160];
				str_format(aStatus, sizeof(aStatus), "Realtime send payload failed: %s", curl_easy_strerror(Code));
				SetStatus(aStatus);
				log_info("aether/realtime", "%s", aStatus);
				Success = false;
				break;
			}
		}
		if(!Success)
			break;

		const int Ready = SocketSelectReadable(Socket, 250);
		if(Ready < 0)
		{
			SetStatus("Realtime socket select failed");
			Success = false;
			break;
		}
		if(Ready == 0)
			continue;

		char aBuffer[64 * 1024];
		size_t Received = 0;
		const curl_ws_frame *pMeta = nullptr;
		Code = curl_ws_recv(pCurl, aBuffer, sizeof(aBuffer), &Received, &pMeta);
		if(Code == CURLE_AGAIN)
			continue;
		if(Code != CURLE_OK || !pMeta)
		{
			if(Code != CURLE_OK)
			{
				char aStatus[160];
				str_format(aStatus, sizeof(aStatus), "Realtime recv failed: %s", curl_easy_strerror(Code));
				SetStatus(aStatus);
				log_info("aether/realtime", "%s", aStatus);
			}
			Success = false;
			break;
		}
		if(pMeta->flags & CURLWS_CLOSE)
			break;
		if((pMeta->flags & CURLWS_TEXT) && Received > 0 && pMeta->bytesleft == 0)
			PushMessage(aBuffer, Received);
	}

	m_Connected.store(false);
	curl_easy_cleanup(pCurl);
	if(!m_Stop.load())
	{
		if(Success)
			SetStatus("Realtime reconnecting");
	}
	return Success;
}
