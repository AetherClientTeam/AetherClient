#ifndef ENGINE_CLIENT_UPDATER_H
#define ENGINE_CLIENT_UPDATER_H

#include <engine/updater.h>

#include <base/detect.h>
#include <base/lock.h>
#include <base/system.h>

#include <engine/shared/http.h>

#include <memory>

class CUpdater : public IUpdater
{
	enum class ETaskKind
	{
		NONE,
		FETCH_RELEASE,
		DOWNLOAD_ARCHIVE,
	};

	class IClient *m_pClient = nullptr;
	class IStorage *m_pStorage = nullptr;
	class CHttp *m_pHttp = nullptr;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);

	std::shared_ptr<CHttpRequest> m_pCurrentTask;
	ETaskKind m_TaskKind = ETaskKind::NONE;
	bool m_CheckOnlyFetch = false;
	bool m_AutoApplyAfterDownload = false;

	char m_aLatestVersion[64];
	char m_aArchiveName[128];
	char m_aArchiveUrl[2048];
	char m_aArchivePath[IO_MAX_PATH_LENGTH];

	void ResetTask() REQUIRES(!m_Lock);
	void StartReleaseFetch() REQUIRES(!m_Lock);
	bool ParseReleaseTask() REQUIRES(!m_Lock);
	void StartArchiveDownload() REQUIRES(!m_Lock);
	bool LaunchUpdaterAndQuit() REQUIRES(!m_Lock);

	void SetCurrentState(EUpdaterState NewState) REQUIRES(!m_Lock);
	void SetStatus(const char *pStatus) REQUIRES(!m_Lock);
	void SetPercent(int Percent) REQUIRES(!m_Lock);

public:
	CUpdater();

	EUpdaterState GetCurrentState() override REQUIRES(!m_Lock);
	void GetCurrentFile(char *pBuf, int BufSize) override REQUIRES(!m_Lock);
	int GetCurrentPercent() override REQUIRES(!m_Lock);

	void CheckForUpdate() override REQUIRES(!m_Lock);
	void InitiateUpdate() override REQUIRES(!m_Lock);
	void ApplyUpdateAndRestart() override REQUIRES(!m_Lock);
	void Init(CHttp *pHttp);
	void Update() override REQUIRES(!m_Lock);
};

#endif
