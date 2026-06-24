#ifndef GAME_CLIENT_COMPONENTS_AETHER_REALTIME_CLIENT_H
#define GAME_CLIENT_COMPONENTS_AETHER_REALTIME_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CAetherRealtimeClient
{
	std::thread m_Worker;
	std::atomic<bool> m_Stop{false};
	std::atomic<bool> m_Connected{false};
	std::atomic<bool> m_LastFailureUnsupportedProtocol{false};
	std::atomic<bool> m_UnsupportedProtocolLogged{false};
	mutable std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::string m_Endpoint;
	std::string m_HelloPayload;
	std::deque<std::string> m_Incoming;
	std::deque<std::string> m_Outgoing;
	std::string m_Status = "Realtime idle";

	void ThreadMain();
	bool RunConnection(const std::string &Endpoint);
	void PushMessage(const char *pData, size_t Size);
	void SetStatus(const char *pStatus);
	static bool BuildEndpoint(const char *pHttpBaseUrl, char *pOut, int OutSize);

public:
	~CAetherRealtimeClient();

	void Start();
	void Stop();
	void SetEndpointFromHttpBase(const char *pHttpBaseUrl);
	void SetHelloPayload(const std::string &Payload);
	void QueuePayload(const std::string &Payload);
	void PumpMessages(std::vector<std::string> &vMessages);
	bool Connected() const { return m_Connected.load(); }
	bool UnsupportedProtocol() const { return m_LastFailureUnsupportedProtocol.load(); }
	void Status(char *pOut, int OutSize) const;
};

#endif
