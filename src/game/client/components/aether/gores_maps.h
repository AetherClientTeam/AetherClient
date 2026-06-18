#ifndef GAME_CLIENT_COMPONENTS_AETHER_GORES_MAPS_H
#define GAME_CLIENT_COMPONENTS_AETHER_GORES_MAPS_H

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

class CAetherGoresMaps : public CComponent
{
public:
	struct SMapEntry
	{
		std::string m_Name;
		std::string m_Category;
		int m_Stars = 1;
	};

private:
	struct SDownloadTask
	{
		std::shared_ptr<CHttpRequest> m_pRequest;
	};

	struct SPendingDownload
	{
		std::string m_Url;
		std::string m_RelPath;
	};

	std::vector<SMapEntry> m_vMaps;
	std::vector<SDownloadTask> m_vDownloads;
	std::vector<SPendingDownload> m_vPendingDownloads;
	std::shared_ptr<CHttpRequest> m_pManifestRequest;
	int m_SelectedIndex = -1;
	int m_SelectedCategory = 0;
	bool m_DeleteEnabled = false;
	bool m_ShowOnlySelectedCategory = true;
	bool m_ManifestLoaded = false;
	bool m_AutoFetchStarted = false;
	char m_aStatus[256] = "Press \"Refresh list\" to fetch maps.";

	void EnsureBaseFolders();
	void MapRelPath(const SMapEntry &Map, char *pOut, int OutSize) const;
	bool QueueMapDownload(const SMapEntry &Map);
	bool InstallBundledTrainingMapIfAvailable();
	void PumpRequests();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnShutdown() override;

	void RefreshList(bool Force);
	void EnsureAutoFetch();
	void OpenFolder();
	bool DownloadSelected();
	int DownloadCategory();
	int DownloadAll();
	int DownloadTraining();
	bool DeleteSelected();

	std::vector<int> DisplayIndices() const;
	const std::vector<SMapEntry> &Maps() const { return m_vMaps; }
	bool IsInstalled(const SMapEntry &Map) const;
	const char *Status() const { return m_aStatus; }
	bool ManifestLoaded() const { return m_ManifestLoaded; }
	int SelectedIndex() const { return m_SelectedIndex; }
	void SetSelectedIndex(int Index) { m_SelectedIndex = Index; }
	int SelectedCategory() const { return m_SelectedCategory; }
	void SetSelectedCategory(int Category);
	bool DeleteEnabled() const { return m_DeleteEnabled; }
	void ToggleDeleteEnabled() { m_DeleteEnabled = !m_DeleteEnabled; }
	bool ShowOnlySelectedCategory() const { return m_ShowOnlySelectedCategory; }
	void ToggleShowOnlySelectedCategory() { m_ShowOnlySelectedCategory = !m_ShowOnlySelectedCategory; }
	const char *CategoryName(int Index) const;
	const char *CategoryFolderName(int Index) const;
};

#endif
