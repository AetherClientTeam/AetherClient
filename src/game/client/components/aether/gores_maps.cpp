#include "gores_maps.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <algorithm>
#include <array>

namespace
{
constexpr int MAX_ACTIVE_DOWNLOADS = 6;
constexpr const char *GORES_MANIFEST_URL = "https://raw.githubusercontent.com/wtfseanscool/kog-maps/main/manifest.json";
constexpr const char *GORES_MAPS_BASE_URL = "https://raw.githubusercontent.com/wtfseanscool/kog-maps/main";

void SanitizeFilename(char *pOut, int OutSize, const char *pIn)
{
	str_copy(pOut, pIn, OutSize);
	for(int i = 0; pOut[i] != '\0'; i++)
	{
		if(pOut[i] == '/' || pOut[i] == '\\' || pOut[i] == ':' || pOut[i] == '*' || pOut[i] == '?' || pOut[i] == '"' || pOut[i] == '<' || pOut[i] == '>' || pOut[i] == '|')
			pOut[i] = '_';
	}
}

bool CopyStorageFileToSave(IStorage *pStorage, const char *pRelSrc, const char *pRelDest)
{
	IOHANDLE Src = pStorage->OpenFile(pRelSrc, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!Src)
		return false;
	IOHANDLE Dst = pStorage->OpenFile(pRelDest, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!Dst)
	{
		io_close(Src);
		return false;
	}

	char aBuf[16 * 1024];
	unsigned ReadBytes;
	bool Success = true;
	while((ReadBytes = io_read(Src, aBuf, sizeof(aBuf))) > 0)
	{
		if(io_write(Dst, aBuf, ReadBytes) != ReadBytes)
		{
			Success = false;
			break;
		}
	}
	io_close(Src);
	if(io_sync(Dst) != 0 || io_close(Dst) != 0)
		Success = false;
	return Success;
}
} // namespace

const char *CAetherGoresMaps::CategoryName(int Index) const
{
	static const char *s_apNames[] = {"Easy", "Main", "Hard", "Insane", "Extreme", "Solo", "Mods", "Training"};
	return Index >= 0 && Index < (int)std::size(s_apNames) ? s_apNames[Index] : "";
}

const char *CAetherGoresMaps::CategoryFolderName(int Index) const
{
	static const char *s_apNames[] = {"1-Easy", "2-Main", "3-Hard", "4-Insane", "5-Extreme", "6-Solo", "7-Mods", "8-Training"};
	return Index >= 0 && Index < (int)std::size(s_apNames) ? s_apNames[Index] : "9-Other";
}

void CAetherGoresMaps::SetSelectedCategory(int Category)
{
	m_SelectedCategory = std::clamp(Category, 0, 7);
}

int CategoryIndexFromName(const CAetherGoresMaps *pSelf, const char *pCategory)
{
	if(!pCategory)
		return -1;
	for(int i = 0; i < 8; i++)
	{
		if(str_comp_nocase(pCategory, pSelf->CategoryName(i)) == 0)
			return i;
	}
	if(str_comp_nocase(pCategory, "Mod") == 0 ||
		str_comp_nocase(pCategory, "Mod Gores") == 0 ||
		str_comp_nocase(pCategory, "Mods Gores") == 0 ||
		str_comp_nocase(pCategory, "Modification") == 0 ||
		str_comp_nocase(pCategory, "Modifications") == 0)
		return 6;
	return -1;
}

void CAetherGoresMaps::EnsureBaseFolders()
{
	Storage()->CreateFolder("maps", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("maps/GoresMaps", IStorage::TYPE_SAVE);
	for(int i = 0; i < 8; i++)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "maps/GoresMaps/%s", CategoryFolderName(i));
		Storage()->CreateFolder(aBuf, IStorage::TYPE_SAVE);
	}
}

void CAetherGoresMaps::MapRelPath(const SMapEntry &Map, char *pOut, int OutSize) const
{
	const int Cat = CategoryIndexFromName(this, Map.m_Category.c_str());
	const char *pFolder = Cat >= 0 ? CategoryFolderName(Cat) : "9-Other";
	char aName[128];
	SanitizeFilename(aName, sizeof(aName), Map.m_Name.c_str());
	str_format(pOut, OutSize, "maps/GoresMaps/%s/%s.map", pFolder, aName);
}

bool CAetherGoresMaps::IsInstalled(const SMapEntry &Map) const
{
	char aRelPath[IO_MAX_PATH_LENGTH];
	MapRelPath(Map, aRelPath, sizeof(aRelPath));
	return Storage()->FileExists(aRelPath, IStorage::TYPE_SAVE);
}

bool CAetherGoresMaps::QueueMapDownload(const SMapEntry &Map)
{
	EnsureBaseFolders();
	char aRelPath[IO_MAX_PATH_LENGTH], aEscName[256], aEscCat[64], aUrl[1024];
	MapRelPath(Map, aRelPath, sizeof(aRelPath));
	if(Storage()->FileExists(aRelPath, IStorage::TYPE_SAVE))
		return false;
	EscapeUrl(aEscName, Map.m_Name.c_str());
	EscapeUrl(aEscCat, Map.m_Category.c_str());
	str_format(aUrl, sizeof(aUrl), "%s/%s/%dstar/%s.map", GORES_MAPS_BASE_URL, aEscCat, maximum(1, Map.m_Stars), aEscName);
	SPendingDownload Pending;
	Pending.m_Url = aUrl;
	Pending.m_RelPath = aRelPath;
	m_vPendingDownloads.push_back(Pending);
	return true;
}

bool CAetherGoresMaps::InstallBundledTrainingMapIfAvailable()
{
	SMapEntry Training;
	Training.m_Name = "AllOfGores";
	Training.m_Category = "Training";
	Training.m_Stars = 1;
	char aRelPath[IO_MAX_PATH_LENGTH];
	MapRelPath(Training, aRelPath, sizeof(aRelPath));
	if(Storage()->FileExists(aRelPath, IStorage::TYPE_SAVE))
		return true;

	const char *apBundledRelCandidates[] = {
		"maps/AllOfGores.map",
		"maps/GoresMaps/8-Training/AllOfGores.map",
		"AllOfGores.map",
	};
	for(const char *pCandidateRel : apBundledRelCandidates)
	{
		if(CopyStorageFileToSave(Storage(), pCandidateRel, aRelPath))
			return true;
	}
	return false;
}

void CAetherGoresMaps::RefreshList(bool Force)
{
	if(m_pManifestRequest && !Force)
		return;
	if(m_pManifestRequest && Force)
		m_pManifestRequest = nullptr;
	EnsureBaseFolders();
	m_pManifestRequest = HttpGet(GORES_MANIFEST_URL);
	Http()->Run(m_pManifestRequest);
	str_copy(m_aStatus, "Fetching manifest...", sizeof(m_aStatus));
}

void CAetherGoresMaps::EnsureAutoFetch()
{
	if(m_AutoFetchStarted || m_ManifestLoaded || m_pManifestRequest)
		return;
	m_AutoFetchStarted = true;
	RefreshList(false);
}

void CAetherGoresMaps::PumpRequests()
{
	if(m_pManifestRequest)
	{
		const EHttpState HttpState = m_pManifestRequest->State();
		if(HttpState == EHttpState::DONE || HttpState == EHttpState::ERROR || HttpState == EHttpState::ABORTED)
		{
			if(HttpState == EHttpState::DONE && m_pManifestRequest->StatusCode() >= 200 && m_pManifestRequest->StatusCode() < 400)
			{
				json_value *pJson = m_pManifestRequest->ResultJson();
				if(pJson && pJson->type == json_object)
				{
					const json_value *pMaps = json_object_get(pJson, "maps");
					m_vMaps.clear();
					if(pMaps && pMaps->type == json_array)
					{
						for(int i = 0; i < json_array_length(pMaps); ++i)
						{
							const json_value *pMap = json_array_get(pMaps, i);
							if(!pMap || pMap->type != json_object)
								continue;
							const char *pName = json_string_get(json_object_get(pMap, "name"));
							const char *pCategory = json_string_get(json_object_get(pMap, "category"));
							const int Stars = json_int_get(json_object_get(pMap, "stars"));
							if(!pName || !pCategory || pName[0] == '\0')
								continue;
							SMapEntry Entry;
							Entry.m_Name = pName;
							Entry.m_Category = pCategory;
							Entry.m_Stars = maximum(1, Stars);
							m_vMaps.push_back(Entry);
						}
					}
					SMapEntry Training;
					Training.m_Name = "AllOfGores";
					Training.m_Category = "Training";
					Training.m_Stars = 1;
					m_vMaps.push_back(Training);
					std::sort(m_vMaps.begin(), m_vMaps.end(), [this](const SMapEntry &A, const SMapEntry &B) {
						const int CatA = CategoryIndexFromName(this, A.m_Category.c_str());
						const int CatB = CategoryIndexFromName(this, B.m_Category.c_str());
						if(CatA != CatB)
							return CatA < CatB;
						return str_comp_filenames(A.m_Name.c_str(), B.m_Name.c_str()) < 0;
					});
					m_SelectedIndex = m_vMaps.empty() ? -1 : 0;
					m_ManifestLoaded = true;
					str_format(m_aStatus, sizeof(m_aStatus), "Loaded %d maps.", (int)m_vMaps.size());
				}
				else
					str_copy(m_aStatus, "Manifest parse failed.", sizeof(m_aStatus));
				if(pJson)
					json_value_free(pJson);
			}
			else
				str_copy(m_aStatus, "Manifest download failed.", sizeof(m_aStatus));
			m_pManifestRequest = nullptr;
		}
	}

	int FinishedDownloads = 0;
	for(auto it = m_vDownloads.begin(); it != m_vDownloads.end();)
	{
		const EHttpState DownloadState = it->m_pRequest ? it->m_pRequest->State() : EHttpState::ERROR;
		if(DownloadState == EHttpState::DONE || DownloadState == EHttpState::ERROR || DownloadState == EHttpState::ABORTED)
		{
			FinishedDownloads++;
			it = m_vDownloads.erase(it);
		}
		else
			++it;
	}
	if(FinishedDownloads > 0)
		str_format(m_aStatus, sizeof(m_aStatus), "%d map download(s) completed. Active: %d", FinishedDownloads, (int)m_vDownloads.size());

	while((int)m_vDownloads.size() < MAX_ACTIVE_DOWNLOADS && !m_vPendingDownloads.empty())
	{
		const SPendingDownload Pending = m_vPendingDownloads.front();
		m_vPendingDownloads.erase(m_vPendingDownloads.begin());
		auto pReq = HttpGetFile(Pending.m_Url.c_str(), Storage(), Pending.m_RelPath.c_str(), IStorage::TYPE_SAVE);
		SDownloadTask &Task = m_vDownloads.emplace_back();
		Task.m_pRequest = std::move(pReq);
		Http()->Run(Task.m_pRequest);
	}
}

void CAetherGoresMaps::OnUpdate()
{
	if(m_pManifestRequest || !m_vDownloads.empty() || !m_vPendingDownloads.empty())
		PumpRequests();
}

void CAetherGoresMaps::OnShutdown()
{
	m_pManifestRequest = nullptr;
	m_vDownloads.clear();
	m_vPendingDownloads.clear();
}

void CAetherGoresMaps::OpenFolder()
{
	EnsureBaseFolders();
	char aFolder[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, "maps/GoresMaps", aFolder, sizeof(aFolder));
	Client()->ViewFile(aFolder);
}

bool CAetherGoresMaps::DownloadSelected()
{
	if(m_SelectedIndex < 0 || m_SelectedIndex >= (int)m_vMaps.size())
	{
		str_copy(m_aStatus, "Select a map first.", sizeof(m_aStatus));
		return false;
	}
	const SMapEntry &Map = m_vMaps[m_SelectedIndex];
	if(str_comp_nocase(Map.m_Category.c_str(), "Training") == 0)
	{
		if(InstallBundledTrainingMapIfAvailable())
		{
			str_copy(m_aStatus, "Installed bundled AllOfGores.map into 8-Training.", sizeof(m_aStatus));
			return true;
		}
	}
	if(QueueMapDownload(Map))
	{
		str_format(m_aStatus, sizeof(m_aStatus), "Queued %s.", Map.m_Name.c_str());
		return true;
	}
	str_copy(m_aStatus, "Selected map already installed or invalid.", sizeof(m_aStatus));
	return false;
}

int CAetherGoresMaps::DownloadCategory()
{
	const char *pCat = CategoryName(m_SelectedCategory);
	if(str_comp_nocase(pCat, "Training") == 0)
		return DownloadTraining();

	int Queued = 0;
	for(const auto &Map : m_vMaps)
	{
		if(str_comp_nocase(Map.m_Category.c_str(), pCat) != 0 || str_comp_nocase(Map.m_Category.c_str(), "Training") == 0)
			continue;
		if(QueueMapDownload(Map))
			Queued++;
	}
	str_format(m_aStatus, sizeof(m_aStatus), "Queued %d map(s) for %s. Active %d, pending %d.", Queued, pCat, (int)m_vDownloads.size(), (int)m_vPendingDownloads.size());
	return Queued;
}

int CAetherGoresMaps::DownloadAll()
{
	int Queued = 0;
	const bool InstalledTraining = InstallBundledTrainingMapIfAvailable();
	for(const auto &Map : m_vMaps)
	{
		if(str_comp_nocase(Map.m_Category.c_str(), "Training") == 0 && InstalledTraining)
			continue;
		if(QueueMapDownload(Map))
			Queued++;
	}
	str_format(m_aStatus, sizeof(m_aStatus), "Queued %d map(s). Training %s. Active %d, pending %d.", Queued, InstalledTraining ? "installed" : "queued", (int)m_vDownloads.size(), (int)m_vPendingDownloads.size());
	return Queued;
}

int CAetherGoresMaps::DownloadTraining()
{
	if(InstallBundledTrainingMapIfAvailable())
	{
		str_copy(m_aStatus, "Installed bundled AllOfGores.map into 8-Training.", sizeof(m_aStatus));
		return 1;
	}

	int Queued = 0;
	for(const auto &Map : m_vMaps)
	{
		if(str_comp_nocase(Map.m_Category.c_str(), "Training") != 0)
			continue;
		if(QueueMapDownload(Map))
			Queued++;
	}
	str_format(m_aStatus, sizeof(m_aStatus), "Queued %d training map(s). Active %d, pending %d.", Queued, (int)m_vDownloads.size(), (int)m_vPendingDownloads.size());
	return Queued;
}

bool CAetherGoresMaps::DeleteSelected()
{
	if(m_SelectedIndex < 0 || m_SelectedIndex >= (int)m_vMaps.size())
	{
		str_copy(m_aStatus, "Select a map first.", sizeof(m_aStatus));
		return false;
	}
	char aRelPath[IO_MAX_PATH_LENGTH];
	MapRelPath(m_vMaps[m_SelectedIndex], aRelPath, sizeof(aRelPath));
	if(Storage()->RemoveFile(aRelPath, IStorage::TYPE_SAVE))
	{
		str_copy(m_aStatus, "Deleted selected map file.", sizeof(m_aStatus));
		return true;
	}
	str_copy(m_aStatus, "Delete failed (missing file or permission).", sizeof(m_aStatus));
	return false;
}

std::vector<int> CAetherGoresMaps::DisplayIndices() const
{
	std::vector<int> vDisplayIndices;
	vDisplayIndices.reserve(m_vMaps.size());
	for(int i = 0; i < (int)m_vMaps.size(); i++)
	{
		const int Cat = CategoryIndexFromName(this, m_vMaps[i].m_Category.c_str());
		if(!m_ShowOnlySelectedCategory || Cat == m_SelectedCategory)
			vDisplayIndices.push_back(i);
	}
	return vDisplayIndices;
}
