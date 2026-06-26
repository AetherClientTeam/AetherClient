#include "updater.h"

#include <base/system.h>
#include <base/process.h>

#include <engine/client.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

static constexpr const char *UPDATE_ARCHIVE_PATH = "update/aether-update.zip";
static constexpr const char *UPDATER_BINARY_PATH = "tools/updater/AetherUpdater.exe";
static constexpr const char *PENDING_UPDATER_BINARY_PATH = "tools/updater/AetherUpdater.exe.new";
static constexpr const char *LEGACY_UPDATER_BINARY_PATH = "AetherUpdater.exe";

static void BuildGitHubReleaseUrl(char *pBuf, int BufSize)
{
	// Match the start-menu update check: list releases (default page size) and pick
	// the highest semver in ParseLatestRelease. per_page=1 only saw the newest-by-date
	// row, which can disagree with the menu's "best release" and confuse users.
	const char *pUrl = AETHERCLIENT_UPDATE_RELEASE_API_URL;
	const char *pSep = str_find(pUrl, "?") ? "&" : "?";
	str_format(pBuf, BufSize, "%s%st=%lld", pUrl, pSep, (long long)time_timestamp());
}

static std::string ToLowerAscii(const char *pStr)
{
	std::string Lower;
	if(!pStr)
		return Lower;

	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pStr); *p != '\0'; ++p)
		Lower.push_back(static_cast<char>(std::tolower(*p)));
	return Lower;
}

static void NormalizeVersionString(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareVersionStrings(const char *pLeft, const char *pRight)
{
	char aLeftNormalized[64];
	char aRightNormalized[64];
	NormalizeVersionString(pLeft, aLeftNormalized, sizeof(aLeftNormalized));
	NormalizeVersionString(pRight, aRightNormalized, sizeof(aRightNormalized));

	const std::vector<int> vLeft = ExtractVersionNumbers(aLeftNormalized);
	const std::vector<int> vRight = ExtractVersionNumbers(aRightNormalized);
	const size_t Num = std::max(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}
	return 0;
}

static int ScoreArchiveAsset(const char *pAssetName)
{
	if(!pAssetName || !str_endswith_nocase(pAssetName, ".zip"))
		return -1;

	const std::string Lower = ToLowerAscii(pAssetName);
	// Ignore GitHub auto-generated source archives; they don't contain portable binaries.
	if(Lower.find("source code") != std::string::npos || Lower.find("src") != std::string::npos)
		return -1;
	int Score = 10;

	if(Lower.find("windows") != std::string::npos || Lower.find("win64") != std::string::npos || Lower.find("win32") != std::string::npos || Lower.find("win") != std::string::npos)
		Score += 20;
	if(Lower.find("aether") != std::string::npos)
		Score += 15;
	if(Lower.find("aetherclient-v") != std::string::npos)
		Score += 30;
	if(Lower.find("aetherclient") != std::string::npos)
		Score += 10;
	if(Lower.find("portable") != std::string::npos || Lower.find("client") != std::string::npos)
		Score += 10;

#if defined(CONF_ARCH_AMD64)
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 10;
#elif defined(CONF_ARCH_IA32)
	if(Lower.find("x86") != std::string::npos || Lower.find("32") != std::string::npos)
		Score += 10;
#endif

	if(Lower.find("linux") != std::string::npos || Lower.find("mac") != std::string::npos || Lower.find("android") != std::string::npos)
		Score -= 10;

	return Score;
}

static bool ParseReleaseObject(const json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson || pJson->type != json_object)
		return false;

	const char *pReleaseVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pReleaseVersion)
		pReleaseVersion = json_string_get(json_object_get(pJson, "name"));
	if(!pReleaseVersion)
		return false;

	const json_value *pAssets = json_object_get(pJson, "assets");
	if(!pAssets || pAssets->type != json_array)
		return false;

	int BestScore = -1;
	char aBestName[128] = "";
	char aBestUrl[2048] = "";

	for(int i = 0; i < json_array_length(pAssets); ++i)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		if(!pAsset || pAsset->type != json_object)
			continue;

		const char *pName = json_string_get(json_object_get(pAsset, "name"));
		const char *pUrl = json_string_get(json_object_get(pAsset, "browser_download_url"));
		const int Score = ScoreArchiveAsset(pName);
		if(!pName || !pUrl || Score < BestScore)
			continue;

		BestScore = Score;
		str_copy(aBestName, pName, sizeof(aBestName));
		str_copy(aBestUrl, pUrl, sizeof(aBestUrl));
	}

	if(BestScore < 0)
		return false;

	str_copy(pVersion, pReleaseVersion, VersionSize);
	str_copy(pArchiveName, aBestName, ArchiveNameSize);
	str_copy(pArchiveUrl, aBestUrl, ArchiveUrlSize);
	return true;
}

static bool ParseLatestRelease(json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson)
		return false;

	if(pJson->type == json_object)
		return ParseReleaseObject(pJson, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize);

	if(pJson->type == json_array)
	{
		bool Found = false;
		char aBestVersion[64] = "";
		char aBestArchiveName[128] = "";
		char aBestArchiveUrl[2048] = "";
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			const json_value *pRelease = json_array_get(pJson, i);
			char aVersion[64];
			char aArchiveName[128];
			char aArchiveUrl[2048];
			if(!ParseReleaseObject(pRelease, aVersion, sizeof(aVersion), aArchiveName, sizeof(aArchiveName), aArchiveUrl, sizeof(aArchiveUrl)))
				continue;

			if(!Found || CompareVersionStrings(aVersion, aBestVersion) > 0)
			{
				Found = true;
				str_copy(aBestVersion, aVersion, sizeof(aBestVersion));
				str_copy(aBestArchiveName, aArchiveName, sizeof(aBestArchiveName));
				str_copy(aBestArchiveUrl, aArchiveUrl, sizeof(aBestArchiveUrl));
			}
		}

		if(Found)
		{
			str_copy(pVersion, aBestVersion, VersionSize);
			str_copy(pArchiveName, aBestArchiveName, ArchiveNameSize);
			str_copy(pArchiveUrl, aBestArchiveUrl, ArchiveUrlSize);
			return true;
		}
	}

	return false;
}

static void StripFilename(char *pPath)
{
	if(!pPath)
		return;

	for(int i = str_length(pPath) - 1; i >= 0; --i)
	{
		if(pPath[i] == '/' || pPath[i] == '\\')
		{
			pPath[i] = '\0';
			return;
		}
	}
	pPath[0] = '\0';
}

CUpdater::CUpdater()
{
	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
}

void CUpdater::Init(CHttp *pHttp)
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = pHttp;

	if(m_pStorage)
	{
		char aPendingUpdaterPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPathAbsolute(PENDING_UPDATER_BINARY_PATH, aPendingUpdaterPath, sizeof(aPendingUpdaterPath));
		if(m_pStorage->FileExists(aPendingUpdaterPath, IStorage::TYPE_ABSOLUTE))
		{
			m_pStorage->RemoveBinaryFile(UPDATER_BINARY_PATH);
			m_pStorage->RenameBinaryFile(PENDING_UPDATER_BINARY_PATH, UPDATER_BINARY_PATH);
		}

		char aUpdaterPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPathAbsolute(UPDATER_BINARY_PATH, aUpdaterPath, sizeof(aUpdaterPath));
		if(m_pStorage->FileExists(aUpdaterPath, IStorage::TYPE_ABSOLUTE))
		{
			// The old root-level updater is visible and can confuse users if they run it manually.
			// It may be locked during the first update from an old build, so failure is harmless.
			m_pStorage->RemoveBinaryFile(LEGACY_UPDATER_BINARY_PATH);
		}
	}
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	const CLockScope LockScope(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope LockScope(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	dbg_msg("updater", "Starting release fetch from: %s", AETHERCLIENT_UPDATE_RELEASE_API_URL);
	SetStatus("Checking latest release");
	SetPercent(0);
	SetCurrentState(IUpdater::GETTING_MANIFEST);

	char aUrl[2304];
	BuildGitHubReleaseUrl(aUrl, sizeof(aUrl));
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = std::shared_ptr<CHttpRequest>(HttpGet(aUrl));
	m_pCurrentTask->HeaderString("Accept", "application/vnd.github+json");
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->HeaderString("Pragma", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::ParseReleaseTask()
{
	if(!m_pCurrentTask || m_pCurrentTask->State() != EHttpState::DONE)
	{
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetStatus("Release check interrupted");
		SetCurrentState(IUpdater::FAIL);
		return false;
	}

	json_value *pJson = m_pCurrentTask->ResultJson();
	if(!pJson)
	{
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetStatus("Failed to parse release info");
		SetCurrentState(IUpdater::FAIL);
		return false;
	}

	const bool Parsed = ParseLatestRelease(pJson, m_aLatestVersion, sizeof(m_aLatestVersion), m_aArchiveName, sizeof(m_aArchiveName), m_aArchiveUrl, sizeof(m_aArchiveUrl));
	json_value_free(pJson);

	if(!Parsed)
	{
		dbg_msg("updater", "Failed to parse latest release or find suitable asset");
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetStatus("Release archive not found");
		SetCurrentState(IUpdater::FAIL);
		return false;
	}

	dbg_msg("updater", "Latest version: %s, Archive: %s, URL: %s", m_aLatestVersion, m_aArchiveName, m_aArchiveUrl);

	if(CompareVersionStrings(m_aLatestVersion, AETHERCLIENT_VERSION) <= 0)
	{
		dbg_msg("updater", "Current version %s is up to date with %s", AETHERCLIENT_VERSION, m_aLatestVersion);
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetStatus("Latest");
		SetCurrentState(IUpdater::CLEAN);
		return false;
	}

	dbg_msg("updater", "New version found: %s", m_aLatestVersion);
	if(m_CheckOnlyFetch)
	{
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetPercent(0);
		SetStatus("New Update");
		SetCurrentState(IUpdater::UPDATE_AVAILABLE);
		return false;
	}
	return true;
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->RemoveBinaryFile(m_aArchivePath);

	SetStatus(m_aArchiveName);
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);

	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
	m_pCurrentTask = std::shared_ptr<CHttpRequest>(HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_ABSOLUTE));
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::LaunchUpdaterAndQuit()
{
#if defined(CONF_FAMILY_WINDOWS)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aUpdaterPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];

	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	str_copy(aInstallDir, aExePath, sizeof(aInstallDir));
	StripFilename(aInstallDir);
	m_pStorage->GetBinaryPathAbsolute(UPDATER_BINARY_PATH, aUpdaterPath, sizeof(aUpdaterPath));
	if(!m_pStorage->FileExists(aUpdaterPath, IStorage::TYPE_ABSOLUTE))
	{
		m_pStorage->GetBinaryPathAbsolute(LEGACY_UPDATER_BINARY_PATH, aUpdaterPath, sizeof(aUpdaterPath));
		if(!m_pStorage->FileExists(aUpdaterPath, IStorage::TYPE_ABSOLUTE))
		{
			SetStatus("Aether updater missing");
			return false;
		}
	}

	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {
		"--pid",
		aPid,
		"--archive",
		aArchivePath,
		"--install-dir",
		aInstallDir,
		"--exe",
		aExePath,
	};

	if(process_execute(aUpdaterPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments), aInstallDir) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch updater");
		return false;
	}

	m_pClient->Quit();
	return true;
#else
	SetStatus("Archive updater is only available on Windows");
	return false;
#endif
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

	m_CheckOnlyFetch = false;
	m_AutoApplyAfterDownload = true;
	if(State == IUpdater::UPDATE_AVAILABLE && m_aArchiveUrl[0] != '\0')
	{
		StartArchiveDownload();
		return;
	}

	StartReleaseFetch();
}

void CUpdater::CheckForUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING || State == IUpdater::UPDATE_AVAILABLE || State == IUpdater::NEED_RESTART)
		return;

	m_CheckOnlyFetch = true;
	m_AutoApplyAfterDownload = false;
	StartReleaseFetch();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != IUpdater::NEED_RESTART)
		return;

	if(!LaunchUpdaterAndQuit())
	{
		m_AutoApplyAfterDownload = false;
		SetCurrentState(IUpdater::FAIL);
	}
}

void CUpdater::Update()
{
	if(!m_pCurrentTask)
		return;

	if(!m_pCurrentTask->Done())
	{
		if(GetCurrentState() == IUpdater::DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}

	if(m_pCurrentTask->State() != EHttpState::DONE || m_pCurrentTask->StatusCode() >= 400)
	{
		ResetTask();
		m_CheckOnlyFetch = false;
		m_AutoApplyAfterDownload = false;
		SetStatus("Update download failed");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		if(ParseReleaseTask())
			StartArchiveDownload();
		else
			ResetTask();
		return;
	}

	if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		ResetTask();
		SetPercent(100);
		SetStatus("New Update");
		SetCurrentState(IUpdater::NEED_RESTART);
		if(m_AutoApplyAfterDownload)
		{
			m_AutoApplyAfterDownload = false;
			ApplyUpdateAndRestart();
		}
	}
}
