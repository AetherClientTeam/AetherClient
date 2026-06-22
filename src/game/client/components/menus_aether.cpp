#include "menus.h"

#include <base/io.h>
#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/image.h>
#include <engine/sound.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/components/aether/audio_decoder.h>
#include <game/client/components/aether/client_variant.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/animstate.h>
#include <game/client/skin.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
enum class ESection
{
	VISUALS,
	GAMEPLAY,
	TOOLS,
	EDITORS,
};

enum class EAetherPage
{
	VISUALS,
	GAMEPLAY,
	TOOLS,
	ASSETS,
	CLAN,
	GAMES,
	INFO,
};

static EAetherPage s_AetherActivePage = EAetherPage::VISUALS;
static bool s_AetherOpenChessOnline = false;
static int s_AetherBlockAwarenessTab = 0;
static int s_AetherPingTab = 0;
static bool s_AetherOrbitStyleDropdownOpen = false;
constexpr const char *AETHER_WARLIST_DIR = "aether/warlists";
constexpr int AETHER_CLOUD_LOCAL_MAX = 1024;
constexpr int AETHER_CLOUD_REMOTE_MAX = 128;

enum class EAetherAssetsCloudAction
{
	NONE,
	LIST,
	UPLOAD,
	DOWNLOAD,
};

struct SAetherCloudAsset
{
	char m_aId[96] = "";
	char m_aCategory[24] = "";
	char m_aName[64] = "";
	char m_aPath[IO_MAX_PATH_LENGTH] = "";
	char m_aUploader[MAX_NAME_LENGTH] = "";
	char m_aCreatedAt[32] = "";
	char m_aUpdatedAt[32] = "";
	int m_DownloadCount = 0;
	IGraphics::CTextureHandle m_ThumbnailTexture;
	int m_ThumbnailWidth = 0;
	int m_ThumbnailHeight = 0;
	bool m_TriedLocalThumbnail = false;
};

struct SAetherCloudCategory
{
	const char *m_pLabel;
	const char *m_pKey;
	const char *m_pFolder;
};

constexpr std::array<SAetherCloudCategory, 5> AETHER_CLOUD_ASSET_CATEGORIES = {{
	{"Skins", "skins", "skins"},
	{"Game", "game", "assets/game"},
	{"Particles", "particles", "assets/particles"},
	{"Emoticons", "emoticons", "assets/emoticons"},
	{"Entities", "entities", "assets/entities"},
}};

static int s_AetherAssetsCloudCategory = 0;
static int s_AetherAssetsCloudLocalScannedCategory = -1;
static int s_AetherAssetsCloudLocalSelected = 0;
static int s_AetherAssetsCloudRemoteSelected = 0;
static std::vector<std::string> s_vAetherAssetsCloudLocal;
static std::array<IGraphics::CTextureHandle, AETHER_CLOUD_LOCAL_MAX> s_aAetherAssetsCloudLocalTextures;
static std::array<bool, AETHER_CLOUD_LOCAL_MAX> s_aAetherAssetsCloudLocalTextureTried;
static std::array<int, AETHER_CLOUD_LOCAL_MAX> s_aAetherAssetsCloudLocalTextureWidths;
static std::array<int, AETHER_CLOUD_LOCAL_MAX> s_aAetherAssetsCloudLocalTextureHeights;
static int s_AetherAssetsCloudLocalTextureCategory = -1;
static int s_AetherAssetsCloudLocalTextureGeneration = -1;
static int s_AetherAssetsCloudLocalGeneration = 0;
static std::array<SAetherCloudAsset, AETHER_CLOUD_REMOTE_MAX> s_aAetherAssetsCloudRemote;
static int s_AetherAssetsCloudRemoteCount = 0;
static std::shared_ptr<CHttpRequest> s_pAetherAssetsCloudRequest;
static EAetherAssetsCloudAction s_AetherAssetsCloudAction = EAetherAssetsCloudAction::NONE;
static char s_aAetherAssetsCloudStatus[192] = "Assets cloud ready.";
static char s_aAetherAssetsCloudDownloadPath[IO_MAX_PATH_LENGTH] = "";
static bool s_AetherAssetsCloudRefreshAfterRequest = false;
static bool s_AetherAssetsCloudApplyAfterDownload = false;
static bool s_AetherAssetsCloudLocalExpanded = true;
static char s_aAetherAssetsCloudSearch[64] = "";

int AetherWarlistJsonScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	if(IsDir || !pName || pName[0] == '.')
		return 0;
	const int Len = str_length(pName);
	if(Len > 5 && str_comp_nocase(pName + Len - 5, ".json") == 0)
		static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
	return 0;
}

enum class EEditorAction
{
	NONE,
	OPEN_HUD_EDITOR,
	OPEN_ASSETS_EDITOR,
};

struct SFeature
{
	AetherMusic::EAetherFeatureId m_Id;
	EAetherPage m_Page;
	ESection m_Section;
	const char *m_pLabel;
	int *m_pEnabled;
	std::span<const char *const> m_ChildLabels;
	EEditorAction m_EditorAction;
};

bool AetherFeatureAllowed(AetherMusic::EAetherFeatureId Id)
{
	using AetherMusic::EAetherFeatureId;
	if(AetherVariant::IsAether())
		return true;
	if(AetherVariant::IsVera())
	{
		switch(Id)
		{
		case EAetherFeatureId::KEYSTROKES:
		case EAetherFeatureId::MUSIC_PLAYER:
		case EAetherFeatureId::INPUT_VISUALIZER:
		case EAetherFeatureId::STABILITY_TRAINER:
		case EAetherFeatureId::SESSION_STATS:
		case EAetherFeatureId::REAL_HITBOX:
		case EAetherFeatureId::NINJA_TEE_PREVIEW:
		case EAetherFeatureId::FINISH_PREDICTION:
		case EAetherFeatureId::LOADING_THEME_BACKGROUND:
		case EAetherFeatureId::CLIENT_BADGES:
		case EAetherFeatureId::PING_WHEEL:
		case EAetherFeatureId::FOCUS_MODE:
		case EAetherFeatureId::SNAP_TAP:
		case EAetherFeatureId::GORES_MODE:
		case EAetherFeatureId::FAST_INPUT:
		case EAetherFeatureId::SILENT_TYPING:
		case EAetherFeatureId::FAIL_SOUND:
		case EAetherFeatureId::SOUND:
		case EAetherFeatureId::KEYBOARD_SOUND:
		case EAetherFeatureId::AUTO_TEAM_LOCK:
		case EAetherFeatureId::SAVE_UNSENT_MESSAGES:
		case EAetherFeatureId::ORBIT_AURA:
		case EAetherFeatureId::TRANSLATOR:
		case EAetherFeatureId::HUD_EDITOR:
		case EAetherFeatureId::ASSETS_EDITOR:
		case EAetherFeatureId::GORES_MAPS:
		case EAetherFeatureId::BROWSER_UTILS:
			return true;
		default:
			return false;
		}
	}
	if(AetherVariant::IsVia())
	{
		switch(Id)
		{
		case EAetherFeatureId::FAST_SPEC:
		case EAetherFeatureId::ORBIT_AURA:
		case EAetherFeatureId::KEYSTROKES:
		case EAetherFeatureId::DDRACE_CONFIGS:
		case EAetherFeatureId::NINJA_TIMER:
		case EAetherFeatureId::MUSIC_PLAYER:
		case EAetherFeatureId::REAL_HITBOX:
		case EAetherFeatureId::NINJA_TEE_PREVIEW:
		case EAetherFeatureId::FINISH_PREDICTION:
		case EAetherFeatureId::LOADING_THEME_BACKGROUND:
		case EAetherFeatureId::CLIENT_BADGES:
		case EAetherFeatureId::PING_WHEEL:
		case EAetherFeatureId::FOCUS_MODE:
		case EAetherFeatureId::SNAP_TAP:
		case EAetherFeatureId::FAST_INPUT:
		case EAetherFeatureId::SILENT_TYPING:
		case EAetherFeatureId::FAIL_SOUND:
		case EAetherFeatureId::SOUND:
		case EAetherFeatureId::KEYBOARD_SOUND:
		case EAetherFeatureId::AUTO_TEAM_LOCK:
		case EAetherFeatureId::SAVE_UNSENT_MESSAGES:
		case EAetherFeatureId::TRANSLATOR:
		case EAetherFeatureId::HUD_EDITOR:
		case EAetherFeatureId::ASSETS_EDITOR:
		case EAetherFeatureId::BROWSER_UTILS:
			return true;
		default:
			return false;
		}
	}
	if(AetherVariant::IsVex())
	{
		switch(Id)
		{
		case EAetherFeatureId::BLOCK_AWARENESS:
		case EAetherFeatureId::ORBIT_AURA:
		case EAetherFeatureId::KEYSTROKES:
		case EAetherFeatureId::MUSIC_PLAYER:
		case EAetherFeatureId::REAL_HITBOX:
		case EAetherFeatureId::NINJA_TEE_PREVIEW:
		case EAetherFeatureId::FINISH_PREDICTION:
		case EAetherFeatureId::LOADING_THEME_BACKGROUND:
		case EAetherFeatureId::CLIENT_BADGES:
		case EAetherFeatureId::PING_WHEEL:
		case EAetherFeatureId::FOCUS_MODE:
		case EAetherFeatureId::SNAP_TAP:
		case EAetherFeatureId::FAST_INPUT:
		case EAetherFeatureId::SILENT_TYPING:
		case EAetherFeatureId::FAIL_SOUND:
		case EAetherFeatureId::SOUND:
		case EAetherFeatureId::KEYBOARD_SOUND:
		case EAetherFeatureId::AUTO_TEAM_LOCK:
		case EAetherFeatureId::SAVE_UNSENT_MESSAGES:
		case EAetherFeatureId::TRANSLATOR:
		case EAetherFeatureId::HUD_EDITOR:
		case EAetherFeatureId::ASSETS_EDITOR:
		case EAetherFeatureId::BROWSER_UTILS:
			return true;
		default:
			return false;
		}
	}
	return true;
}

ColorRGBA AetherThemeColor(float Alpha)
{
	return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true)).WithAlpha(Alpha);
}

ColorRGBA AetherPanelColor(float Alpha)
{
	return ColorRGBA(0.0f, 0.0f, 0.0f, Alpha);
}

ColorRGBA AetherBlendColor(ColorRGBA Base, ColorRGBA Overlay, float Amount)
{
	const float T = std::clamp(Amount, 0.0f, 1.0f);
	return ColorRGBA(
		Base.r + (Overlay.r - Base.r) * T,
		Base.g + (Overlay.g - Base.g) * T,
		Base.b + (Overlay.b - Base.b) * T,
		Base.a + (Overlay.a - Base.a) * T);
}

static float s_AetherResponsiveScale = 0.8f;

void UpdateAetherSettingsScale(const CUIRect &View)
{
	const float WidthScale = View.w > 0.0f ? View.w / 1180.0f : 0.8f;
	const float HeightScale = View.h > 0.0f ? View.h / 620.0f : 0.8f;
	s_AetherResponsiveScale = std::clamp(minimum(0.8f, minimum(WidthScale, HeightScale)), 0.68f, 0.8f);
}

float AetherSettingsScale()
{
	return s_AetherResponsiveScale;
}

void AetherOptionRow(CUIRect Row, float S, CUIRect *pLabel, CUIRect *pControl)
{
	const float MinLabelWidth = minimum(150.0f * S, Row.w * 0.42f);
	const float MaxControlWidth = maximum(80.0f * S, Row.w - MinLabelWidth);
	const float ControlWidth = minimum(MaxControlWidth, minimum(390.0f * S, Row.w * 0.56f));
	Row.VSplitRight(ControlWidth, pLabel, pControl);
	if(pLabel)
		pLabel->VSplitRight(minimum(12.0f * S, pLabel->w * 0.08f), pLabel, nullptr);
}

bool IsAetherSoundFile(const char *pName)
{
	const int Len = str_length(pName);
	return (Len > 5 && str_comp_nocase(pName + Len - 5, ".opus") == 0) ||
	       (Len > 3 && str_comp_nocase(pName + Len - 3, ".wv") == 0) ||
	       (Len > 4 && str_comp_nocase(pName + Len - 4, ".mp3") == 0);
}

bool AetherSoundFileHasExtension(const char *pName, const char *pExt)
{
	const int NameLen = str_length(pName);
	const int ExtLen = str_length(pExt);
	return NameLen >= ExtLen && str_comp_nocase(pName + NameLen - ExtLen, pExt) == 0;
}

bool AetherPngFile(const char *pName)
{
	const int Len = str_length(pName);
	return Len > 4 && str_comp_nocase(pName + Len - 4, ".png") == 0;
}

void AetherSanitizeExportName(const char *pInput, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pInput)
		return;
	int Out = 0;
	for(const char *p = pInput; *p && Out < OutSize - 1; ++p)
	{
		const char c = *p;
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
			pOut[Out++] = c;
		else if(c == ' ' || c == '.')
			pOut[Out++] = '_';
	}
	pOut[Out] = '\0';
}

struct SAetherNameScanContext
{
	std::vector<std::string> *m_pNames;
};

int AetherPngPackScan(const char *pName, int IsDir, int StorageType, void *pUser)
{
	(void)StorageType;
	if(pName[0] == '.')
		return 0;
	auto *pContext = static_cast<SAetherNameScanContext *>(pUser);
	if(IsDir)
	{
		if(str_comp(pName, "default") != 0)
			pContext->m_pNames->emplace_back(pName);
		return 0;
	}
	if(AetherPngFile(pName))
	{
		char aName[IO_MAX_PATH_LENGTH];
		str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
		if(str_comp(aName, "default") != 0)
			pContext->m_pNames->emplace_back(aName);
	}
	return 0;
}

void AetherScanPngNames(IStorage *pStorage, const char *pPath, std::vector<std::string> &vNames, bool DefaultFirst)
{
	vNames.clear();
	if(DefaultFirst)
		vNames.emplace_back("default");
	if(!pStorage || !pPath)
		return;
	SAetherNameScanContext Context;
	Context.m_pNames = &vNames;
	pStorage->ListDirectory(IStorage::TYPE_ALL, pPath, AetherPngPackScan, &Context);
	std::sort(vNames.begin(), vNames.end());
	vNames.erase(std::unique(vNames.begin(), vNames.end()), vNames.end());
	if(DefaultFirst)
	{
		auto It = std::find(vNames.begin(), vNames.end(), "default");
		if(It != vNames.end() && It != vNames.begin())
			std::rotate(vNames.begin(), It, It + 1);
		else if(It == vNames.end())
			vNames.insert(vNames.begin(), "default");
	}
}

void AetherScanEntityNames(IStorage *pStorage, std::vector<std::string> &vNames)
{
	AetherScanPngNames(pStorage, "assets/entities", vNames, true);
	if(!pStorage)
		return;
	vNames.erase(std::remove_if(vNames.begin(), vNames.end(), [&](const std::string &Name) {
		if(Name == "default")
			return false;
		char aPath[IO_MAX_PATH_LENGTH];
		for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT; ++m)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", Name.c_str(), gs_apModEntitiesNames[m]);
			if(pStorage->FileExists(aPath, IStorage::TYPE_ALL))
				return false;
		}
		str_format(aPath, sizeof(aPath), "assets/entities/%s.png", Name.c_str());
		return !pStorage->FileExists(aPath, IStorage::TYPE_ALL);
	}), vNames.end());
	if(vNames.empty())
		vNames.emplace_back("default");
}

bool AetherLoadAtlasImage(IGraphics *pGraphics, const char *pCategory, int ImageId, const char *pPack, CImageInfo &Image)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!pCategory || !pPack || str_comp(pPack, "default") == 0)
		str_copy(aPath, g_pData->m_aImages[ImageId].m_pFilename);
	else
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pCategory, pPack);
	if(pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL))
		return true;
	if(pCategory && pPack && str_comp(pPack, "default") != 0)
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s/%s", pCategory, pPack, g_pData->m_aImages[ImageId].m_pFilename);
		return pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL);
	}
	return false;
}

bool AetherLoadEntitiesImage(IGraphics *pGraphics, const char *pPack, CImageInfo &Image)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!pPack || pPack[0] == '\0' || str_comp(pPack, "default") == 0)
	{
		str_copy(aPath, "editor/entities_clear/ddnet.png");
		return pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL);
	}

	for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT; ++m)
	{
		str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pPack, gs_apModEntitiesNames[m]);
		if(pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL))
			return true;
	}

	str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pPack);
	return pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL);
}

void AetherResolveAtlasPath(IStorage *pStorage, const char *pCategory, int ImageId, const char *pPack, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(!pCategory || !pPack || str_comp(pPack, "default") == 0)
	{
		str_copy(pOut, g_pData->m_aImages[ImageId].m_pFilename, OutSize);
		return;
	}
	str_format(pOut, OutSize, "assets/%s/%s.png", pCategory, pPack);
	if(pStorage && pStorage->FileExists(pOut, IStorage::TYPE_ALL))
		return;
	str_format(pOut, OutSize, "assets/%s/%s/%s", pCategory, pPack, g_pData->m_aImages[ImageId].m_pFilename);
}

void AetherResolveEntitiesPath(IStorage *pStorage, const char *pPack, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(!pPack || pPack[0] == '\0' || str_comp(pPack, "default") == 0)
	{
		str_copy(pOut, "editor/entities_clear/ddnet.png", OutSize);
		return;
	}
	for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT; ++m)
	{
		str_format(pOut, OutSize, "assets/entities/%s/%s.png", pPack, gs_apModEntitiesNames[m]);
		if(pStorage && pStorage->FileExists(pOut, IStorage::TYPE_ALL))
			return;
	}
	str_format(pOut, OutSize, "assets/entities/%s.png", pPack);
}

bool AetherLoadSkinImage(IGraphics *pGraphics, const char *pSkin, CImageInfo &Image)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!pSkin || pSkin[0] == '\0')
		pSkin = "default";
	str_format(aPath, sizeof(aPath), "skins/%s.png", pSkin);
	return pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL);
}

const SAetherCloudCategory &AetherCloudCategory()
{
	s_AetherAssetsCloudCategory = std::clamp(s_AetherAssetsCloudCategory, 0, (int)AETHER_CLOUD_ASSET_CATEGORIES.size() - 1);
	return AETHER_CLOUD_ASSET_CATEGORIES[s_AetherAssetsCloudCategory];
}

bool AetherCloudMatchesSearch(const char *pName, const char *pUploader = nullptr)
{
	if(s_aAetherAssetsCloudSearch[0] == '\0')
		return true;
	return (pName && str_find_nocase(pName, s_aAetherAssetsCloudSearch)) ||
	       (pUploader && str_find_nocase(pUploader, s_aAetherAssetsCloudSearch));
}

void AetherBuildApiUrl(char *pOut, int OutSize, const char *pPath)
{
	char aBase[256];
	str_copy(aBase, g_Config.m_AeBadgesApiUrl, sizeof(aBase));
	int Len = str_length(aBase);
	while(Len > 0 && aBase[Len - 1] == '/')
		aBase[--Len] = '\0';
	str_format(pOut, OutSize, "%s%s", aBase, pPath);
}

void AetherCloudAssetRelPath(const char *pCategory, const char *pName, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(str_comp(pCategory, "skins") == 0)
		str_format(pOut, OutSize, "skins/%s.png", pName);
	else if(str_comp(pCategory, "entities") == 0)
		str_format(pOut, OutSize, "assets/entities/%s/ddnet.png", pName);
	else
		str_format(pOut, OutSize, "assets/%s/%s.png", pCategory, pName);
}

void AetherCloudEnsureFolder(IStorage *pStorage, const char *pCategory, const char *pName)
{
	if(str_comp(pCategory, "skins") == 0)
	{
		pStorage->CreateFolder("skins", IStorage::TYPE_SAVE);
	}
	else if(str_comp(pCategory, "entities") == 0)
	{
		pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
		pStorage->CreateFolder("assets/entities", IStorage::TYPE_SAVE);
		char aFolder[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/entities/%s", pName);
		pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
	}
	else
	{
		pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
		char aFolder[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/%s", pCategory);
		pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
	}
}

void AetherCloudScanLocal(IStorage *pStorage)
{
	s_vAetherAssetsCloudLocal.clear();
	s_AetherAssetsCloudLocalScannedCategory = s_AetherAssetsCloudCategory;
	const SAetherCloudCategory &Category = AetherCloudCategory();
	if(str_comp(Category.m_pKey, "skins") == 0)
		AetherScanPngNames(pStorage, "skins", s_vAetherAssetsCloudLocal, false);
	else if(str_comp(Category.m_pKey, "entities") == 0)
	{
		AetherScanEntityNames(pStorage, s_vAetherAssetsCloudLocal);
		s_vAetherAssetsCloudLocal.erase(std::remove(s_vAetherAssetsCloudLocal.begin(), s_vAetherAssetsCloudLocal.end(), "default"), s_vAetherAssetsCloudLocal.end());
	}
	else
		AetherScanPngNames(pStorage, Category.m_pFolder, s_vAetherAssetsCloudLocal, false);
	s_AetherAssetsCloudLocalSelected = std::clamp(s_AetherAssetsCloudLocalSelected, 0, maximum(0, (int)s_vAetherAssetsCloudLocal.size() - 1));
	++s_AetherAssetsCloudLocalGeneration;
}

bool AetherCloudReadFileBase64(IStorage *pStorage, const char *pRelPath, std::string &Base64)
{
	IOHANDLE File = pStorage->OpenFile(pRelPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return false;
	void *pData = nullptr;
	unsigned DataSize = 0;
	const bool ReadOk = io_read_all(File, &pData, &DataSize);
	io_close(File);
	if(!ReadOk || !pData || DataSize == 0)
	{
		free(pData);
		return false;
	}
	std::vector<char> vEncoded(((DataSize + 2) / 3) * 4 + 1);
	str_base64(vEncoded.data(), (int)vEncoded.size(), pData, (int)DataSize);
	free(pData);
	Base64.assign(vEncoded.data());
	return true;
}

bool AetherCloudWriteBase64(IStorage *pStorage, const char *pRelPath, const char *pBase64)
{
	if(!pBase64 || pBase64[0] == '\0')
		return false;
	std::vector<unsigned char> vDecoded((str_length(pBase64) / 4) * 3 + 4);
	const int DecodedSize = str_base64_decode(vDecoded.data(), (int)vDecoded.size(), pBase64);
	if(DecodedSize <= 0)
		return false;
	char aWholePath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = pStorage->OpenFile(pRelPath, IOFLAG_WRITE, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath));
	if(!File)
		return false;
	const bool WriteOk = io_write(File, vDecoded.data(), DecodedSize) == (unsigned)DecodedSize;
	const bool SyncOk = io_sync(File) == 0;
	const bool CloseOk = io_close(File) == 0;
	return WriteOk && SyncOk && CloseOk;
}

void AetherCloudClearRemoteTextures(IGraphics *pGraphics)
{
	if(!pGraphics)
		return;
	for(int i = 0; i < s_AetherAssetsCloudRemoteCount && i < (int)s_aAetherAssetsCloudRemote.size(); ++i)
	{
		if(s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture.IsValid())
			pGraphics->UnloadTexture(&s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture);
		s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture = IGraphics::CTextureHandle();
		s_aAetherAssetsCloudRemote[i].m_ThumbnailWidth = 0;
		s_aAetherAssetsCloudRemote[i].m_ThumbnailHeight = 0;
	}
}

void AetherCloudClearLocalTextures(IGraphics *pGraphics)
{
	if(!pGraphics)
		return;
	for(auto &Texture : s_aAetherAssetsCloudLocalTextures)
	{
		if(Texture.IsValid())
			pGraphics->UnloadTexture(&Texture);
		Texture = IGraphics::CTextureHandle();
	}
	s_aAetherAssetsCloudLocalTextureTried.fill(false);
	s_aAetherAssetsCloudLocalTextureWidths.fill(0);
	s_aAetherAssetsCloudLocalTextureHeights.fill(0);
	s_AetherAssetsCloudLocalTextureCategory = s_AetherAssetsCloudCategory;
	s_AetherAssetsCloudLocalTextureGeneration = s_AetherAssetsCloudLocalGeneration;
}

IGraphics::CTextureHandle AetherCloudLoadPngTexture(IGraphics *pGraphics, const char *pPath, int *pWidth = nullptr, int *pHeight = nullptr)
{
	if(pWidth)
		*pWidth = 0;
	if(pHeight)
		*pHeight = 0;
	if(!pGraphics || !pPath || pPath[0] == '\0')
		return IGraphics::CTextureHandle();
	CImageInfo Image;
	if(!pGraphics->LoadPng(Image, pPath, IStorage::TYPE_ALL))
		return IGraphics::CTextureHandle();
	if(pWidth)
		*pWidth = Image.m_Width;
	if(pHeight)
		*pHeight = Image.m_Height;
	return pGraphics->LoadTextureRawMove(Image, 0, pPath);
}

IGraphics::CTextureHandle AetherCloudLoadFileTexture(IGraphics *pGraphics, const char *pCategory, const char *pName, int *pWidth = nullptr, int *pHeight = nullptr)
{
	if(!pGraphics || !pCategory || !pName || pName[0] == '\0')
		return IGraphics::CTextureHandle();
	char aRelPath[IO_MAX_PATH_LENGTH];
	AetherCloudAssetRelPath(pCategory, pName, aRelPath, sizeof(aRelPath));
	IGraphics::CTextureHandle Texture = AetherCloudLoadPngTexture(pGraphics, aRelPath, pWidth, pHeight);
	if(Texture.IsValid())
		return Texture;
	if(str_comp(pCategory, "entities") == 0)
	{
		for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT; ++m)
		{
			str_format(aRelPath, sizeof(aRelPath), "assets/entities/%s/%s.png", pName, gs_apModEntitiesNames[m]);
			Texture = AetherCloudLoadPngTexture(pGraphics, aRelPath, pWidth, pHeight);
			if(Texture.IsValid())
				return Texture;
		}
		str_format(aRelPath, sizeof(aRelPath), "assets/entities/%s.png", pName);
		Texture = AetherCloudLoadPngTexture(pGraphics, aRelPath, pWidth, pHeight);
		if(Texture.IsValid())
			return Texture;
		return AetherCloudLoadPngTexture(pGraphics, "editor/entities_clear/ddnet.png", pWidth, pHeight);
	}
	return Texture;
}

IGraphics::CTextureHandle AetherCloudLoadThumbnailTexture(IGraphics *pGraphics, const char *pBase64, int *pWidth = nullptr, int *pHeight = nullptr)
{
	if(pWidth)
		*pWidth = 0;
	if(pHeight)
		*pHeight = 0;
	if(!pGraphics || !pBase64 || pBase64[0] == '\0')
		return IGraphics::CTextureHandle();
	std::vector<unsigned char> vDecoded((str_length(pBase64) / 4) * 3 + 4);
	const int DecodedSize = str_base64_decode(vDecoded.data(), (int)vDecoded.size(), pBase64);
	if(DecodedSize <= 0)
		return IGraphics::CTextureHandle();
	CImageInfo Image;
	if(!pGraphics->LoadPng(Image, vDecoded.data(), DecodedSize, "aether-cloud-thumbnail"))
		return IGraphics::CTextureHandle();
	if(pWidth)
		*pWidth = Image.m_Width;
	if(pHeight)
		*pHeight = Image.m_Height;
	return pGraphics->LoadTextureRawMove(Image, 0, "aether-cloud-thumbnail");
}

struct SAetherSpriteRect
{
	int m_X = 0;
	int m_Y = 0;
	int m_W = 0;
	int m_H = 0;
};

bool AetherGetSpriteRect(const CDataSprite *pSprite, const CImageInfo &Image, SAetherSpriteRect &Rect)
{
	if(!pSprite || !pSprite->m_pSet || Image.m_Width <= 0 || Image.m_Height <= 0 || pSprite->m_pSet->m_Gridx <= 0 || pSprite->m_pSet->m_Gridy <= 0)
		return false;
	const int CellW = Image.m_Width / pSprite->m_pSet->m_Gridx;
	const int CellH = Image.m_Height / pSprite->m_pSet->m_Gridy;
	Rect.m_X = pSprite->m_X * CellW;
	Rect.m_Y = pSprite->m_Y * CellH;
	Rect.m_W = pSprite->m_W * CellW;
	Rect.m_H = pSprite->m_H * CellH;
	return Rect.m_X >= 0 && Rect.m_Y >= 0 && Rect.m_W > 0 && Rect.m_H > 0 && Rect.m_X + Rect.m_W <= Image.m_Width && Rect.m_Y + Rect.m_H <= Image.m_Height;
}

bool AetherGetAssetPartRect(int SpriteId, const CImageInfo &Image, SAetherSpriteRect &Rect)
{
	if(SpriteId >= 0)
	{
		if(SpriteId >= NUM_SPRITES)
			return false;
		return AetherGetSpriteRect(&g_pData->m_aSprites[SpriteId], Image, Rect);
	}
	const int Cell = -SpriteId - 1;
	const int Grid = 16;
	if(Cell < 0 || Cell >= Grid * Grid || Image.m_Width <= 0 || Image.m_Height <= 0)
		return false;
	const int CellW = Image.m_Width / Grid;
	const int CellH = Image.m_Height / Grid;
	Rect.m_X = (Cell % Grid) * CellW;
	Rect.m_Y = (Cell / Grid) * CellH;
	Rect.m_W = CellW;
	Rect.m_H = CellH;
	return Rect.m_W > 0 && Rect.m_H > 0;
}

ColorRGBA AetherApplyAssetTint(ColorRGBA SrcColor, unsigned TintColor, bool TintEnabled, int Opacity, bool OnlyColored, bool FullBright, bool FillInside, bool EntityTile)
{
	const ColorHSLA TintHsl(TintColor, false);
	const float OpacityScale = std::clamp(Opacity / 100.0f, 0.0f, 1.0f);
	ColorHSLA SrcHsl = color_cast<ColorHSLA>(SrcColor);
	// "Only colored pixels" is about the source sprite, not the picked tint:
	// keep white/black/gray highlights intact while allowing any target color.
	const bool ColoredSource = SrcHsl.s > 0.08f;
	const bool PreserveOutline = !EntityTile && FillInside && SrcColor.r < 0.08f && SrcColor.g < 0.08f && SrcColor.b < 0.08f;
	if(TintEnabled && !PreserveOutline && (EntityTile || !OnlyColored || ColoredSource))
	{
		if(TintHsl.s > 0.02f)
		{
			SrcHsl.h = TintHsl.h;
			SrcHsl.s = FillInside ? TintHsl.s : std::clamp(TintHsl.s * 0.85f + SrcHsl.s * 0.15f, 0.0f, 1.0f);
			if(FillInside)
				SrcHsl.l = TintHsl.l;
			else
			{
				const float LowLight = std::clamp(TintHsl.l * 0.45f, 0.0f, 1.0f);
				const float HighLight = std::clamp(TintHsl.l + (1.0f - TintHsl.l) * 0.35f, 0.0f, 1.0f);
				SrcHsl.l = LowLight + (HighLight - LowLight) * std::clamp(SrcHsl.l, 0.0f, 1.0f);
			}
		}
		else
		{
			SrcHsl.s = 0.0f;
			if(FillInside)
				SrcHsl.l = TintHsl.l;
			else
			{
				const float LowLight = std::clamp(TintHsl.l * 0.45f, 0.0f, 1.0f);
				const float HighLight = std::clamp(TintHsl.l + (1.0f - TintHsl.l) * 0.35f, 0.0f, 1.0f);
				SrcHsl.l = LowLight + (HighLight - LowLight) * std::clamp(SrcHsl.l, 0.0f, 1.0f);
			}
		}
		if(FullBright)
			SrcHsl.l = std::max(SrcHsl.l, TintHsl.s > 0.02f ? 0.72f : 1.0f);
		SrcColor = color_cast<ColorRGBA>(SrcHsl);
	}
	SrcColor.a = std::clamp(SrcColor.a * OpacityScale, 0.0f, 1.0f);
	return SrcColor;
}

void AetherBlendSprite(CImageInfo &Dest, const CImageInfo &Src, int SourceSpriteId, int TargetSpriteId, bool TintEnabled, unsigned TintColor, int Opacity, bool OnlyColored, bool FullBright, bool FillInside)
{
	SAetherSpriteRect SrcRect;
	SAetherSpriteRect DstRect;
	if(!AetherGetAssetPartRect(SourceSpriteId, Src, SrcRect) || !AetherGetAssetPartRect(TargetSpriteId, Dest, DstRect))
		return;
	const bool EntityTile = SourceSpriteId < 0 || TargetSpriteId < 0;
	for(int y = 0; y < DstRect.m_H; ++y)
	{
		for(int x = 0; x < DstRect.m_W; ++x)
		{
			const int MappedX = std::clamp((int)((x + 0.5f) * SrcRect.m_W / DstRect.m_W), 0, SrcRect.m_W - 1);
			const int MappedY = std::clamp((int)((y + 0.5f) * SrcRect.m_H / DstRect.m_H), 0, SrcRect.m_H - 1);
			const size_t SrcX = (size_t)SrcRect.m_X + MappedX;
			const size_t SrcY = (size_t)SrcRect.m_Y + MappedY;
			const size_t DstX = (size_t)DstRect.m_X + x;
			const size_t DstY = (size_t)DstRect.m_Y + y;
			ColorRGBA SrcColor = Src.PixelColor(SrcX, SrcY);
			Dest.SetPixelColor(DstX, DstY, AetherApplyAssetTint(SrcColor, TintColor, TintEnabled, Opacity, OnlyColored, FullBright, FillInside, EntityTile));
		}
	}
}

struct SAetherAssetPart
{
	const char *m_pName;
	std::array<int, 16> m_aSprites;
	int m_NumSprites;
};

struct SAetherAssetCategory
{
	const char *m_pLabel;
	const char *m_pPath;
	int m_ImageId;
	std::span<const SAetherAssetPart> m_Parts;
};

#define AETHER_ASSET_PART(Name, Sprite) {Name, {Sprite}, 1}

static const SAetherAssetPart s_aGameAssetParts[] = {
	AETHER_ASSET_PART("Health full", SPRITE_HEALTH_FULL),
	AETHER_ASSET_PART("Health empty", SPRITE_HEALTH_EMPTY),
	AETHER_ASSET_PART("Armor full", SPRITE_ARMOR_FULL),
	AETHER_ASSET_PART("Armor empty", SPRITE_ARMOR_EMPTY),
	AETHER_ASSET_PART("Hammer cursor", SPRITE_WEAPON_HAMMER_CURSOR),
	AETHER_ASSET_PART("Gun cursor", SPRITE_WEAPON_GUN_CURSOR),
	AETHER_ASSET_PART("Shotgun cursor", SPRITE_WEAPON_SHOTGUN_CURSOR),
	AETHER_ASSET_PART("Grenade cursor", SPRITE_WEAPON_GRENADE_CURSOR),
	AETHER_ASSET_PART("Laser cursor", SPRITE_WEAPON_LASER_CURSOR),
	AETHER_ASSET_PART("Ninja cursor", SPRITE_WEAPON_NINJA_CURSOR),
	AETHER_ASSET_PART("Hammer body", SPRITE_WEAPON_HAMMER_BODY),
	AETHER_ASSET_PART("Gun body", SPRITE_WEAPON_GUN_BODY),
	AETHER_ASSET_PART("Shotgun body", SPRITE_WEAPON_SHOTGUN_BODY),
	AETHER_ASSET_PART("Grenade body", SPRITE_WEAPON_GRENADE_BODY),
	AETHER_ASSET_PART("Laser body", SPRITE_WEAPON_LASER_BODY),
	AETHER_ASSET_PART("Ninja body", SPRITE_WEAPON_NINJA_BODY),
	AETHER_ASSET_PART("Gun projectile", SPRITE_WEAPON_GUN_PROJ),
	AETHER_ASSET_PART("Shotgun projectile", SPRITE_WEAPON_SHOTGUN_PROJ),
	AETHER_ASSET_PART("Grenade projectile", SPRITE_WEAPON_GRENADE_PROJ),
	AETHER_ASSET_PART("Laser projectile", SPRITE_WEAPON_LASER_PROJ),
	AETHER_ASSET_PART("Gun muzzle 1", SPRITE_WEAPON_GUN_MUZZLE1),
	AETHER_ASSET_PART("Gun muzzle 2", SPRITE_WEAPON_GUN_MUZZLE1 + 1),
	AETHER_ASSET_PART("Gun muzzle 3", SPRITE_WEAPON_GUN_MUZZLE1 + 2),
	AETHER_ASSET_PART("Shotgun muzzle 1", SPRITE_WEAPON_SHOTGUN_MUZZLE1),
	AETHER_ASSET_PART("Shotgun muzzle 2", SPRITE_WEAPON_SHOTGUN_MUZZLE1 + 1),
	AETHER_ASSET_PART("Shotgun muzzle 3", SPRITE_WEAPON_SHOTGUN_MUZZLE1 + 2),
	AETHER_ASSET_PART("Ninja muzzle 1", SPRITE_WEAPON_NINJA_MUZZLE1),
	AETHER_ASSET_PART("Ninja muzzle 2", SPRITE_WEAPON_NINJA_MUZZLE1 + 1),
	AETHER_ASSET_PART("Ninja muzzle 3", SPRITE_WEAPON_NINJA_MUZZLE1 + 2),
	AETHER_ASSET_PART("Hook chain", SPRITE_HOOK_CHAIN),
	AETHER_ASSET_PART("Hook head", SPRITE_HOOK_HEAD),
	AETHER_ASSET_PART("Pickup health", SPRITE_PICKUP_HEALTH),
	AETHER_ASSET_PART("Pickup armor", SPRITE_PICKUP_ARMOR),
	AETHER_ASSET_PART("Pickup hammer", SPRITE_PICKUP_HAMMER),
	AETHER_ASSET_PART("Pickup gun", SPRITE_PICKUP_GUN),
	AETHER_ASSET_PART("Pickup shotgun", SPRITE_PICKUP_SHOTGUN),
	AETHER_ASSET_PART("Pickup grenade", SPRITE_PICKUP_GRENADE),
	AETHER_ASSET_PART("Pickup laser", SPRITE_PICKUP_LASER),
	AETHER_ASSET_PART("Pickup ninja", SPRITE_PICKUP_NINJA),
	AETHER_ASSET_PART("Shotgun armor", SPRITE_PICKUP_ARMOR_SHOTGUN),
	AETHER_ASSET_PART("Grenade armor", SPRITE_PICKUP_ARMOR_GRENADE),
	AETHER_ASSET_PART("Laser armor", SPRITE_PICKUP_ARMOR_LASER),
	AETHER_ASSET_PART("Ninja armor", SPRITE_PICKUP_ARMOR_NINJA),
	AETHER_ASSET_PART("Blue flag", SPRITE_FLAG_BLUE),
	AETHER_ASSET_PART("Red flag", SPRITE_FLAG_RED),
	AETHER_ASSET_PART("Particle 1", SPRITE_PART1),
	AETHER_ASSET_PART("Particle 2", SPRITE_PART2),
	AETHER_ASSET_PART("Particle 3", SPRITE_PART3),
	AETHER_ASSET_PART("Particle 4", SPRITE_PART4),
	AETHER_ASSET_PART("Particle 5", SPRITE_PART5),
	AETHER_ASSET_PART("Particle 6", SPRITE_PART6),
	AETHER_ASSET_PART("Particle 7", SPRITE_PART7),
	AETHER_ASSET_PART("Particle 8", SPRITE_PART8),
	AETHER_ASSET_PART("Particle 9", SPRITE_PART9),
	AETHER_ASSET_PART("Star 1", SPRITE_STAR1),
	AETHER_ASSET_PART("Star 2", SPRITE_STAR2),
	AETHER_ASSET_PART("Star 3", SPRITE_STAR3),
};

static const SAetherAssetPart s_aHudAssetParts[] = {
	AETHER_ASSET_PART("Air jump", SPRITE_HUD_AIRJUMP),
	AETHER_ASSET_PART("Air jump empty", SPRITE_HUD_AIRJUMP_EMPTY),
	AETHER_ASSET_PART("Endless jump", SPRITE_HUD_ENDLESS_JUMP),
	AETHER_ASSET_PART("Freeze bar left", SPRITE_HUD_FREEZE_BAR_FULL_LEFT),
	AETHER_ASSET_PART("Freeze bar full", SPRITE_HUD_FREEZE_BAR_FULL),
	AETHER_ASSET_PART("Freeze bar empty", SPRITE_HUD_FREEZE_BAR_EMPTY),
	AETHER_ASSET_PART("Freeze bar right", SPRITE_HUD_FREEZE_BAR_EMPTY_RIGHT),
	AETHER_ASSET_PART("Ninja bar left", SPRITE_HUD_NINJA_BAR_FULL_LEFT),
	AETHER_ASSET_PART("Ninja bar full", SPRITE_HUD_NINJA_BAR_FULL),
	AETHER_ASSET_PART("Ninja bar empty", SPRITE_HUD_NINJA_BAR_EMPTY),
	AETHER_ASSET_PART("Ninja bar right", SPRITE_HUD_NINJA_BAR_EMPTY_RIGHT),
	AETHER_ASSET_PART("Hook disabled", SPRITE_HUD_HOOK_HIT_DISABLED),
	AETHER_ASSET_PART("Hammer disabled", SPRITE_HUD_HAMMER_HIT_DISABLED),
	AETHER_ASSET_PART("Shotgun disabled", SPRITE_HUD_SHOTGUN_HIT_DISABLED),
	AETHER_ASSET_PART("Grenade disabled", SPRITE_HUD_GRENADE_HIT_DISABLED),
	AETHER_ASSET_PART("Laser disabled", SPRITE_HUD_LASER_HIT_DISABLED),
	AETHER_ASSET_PART("Gun disabled", SPRITE_HUD_GUN_HIT_DISABLED),
	AETHER_ASSET_PART("Solo", SPRITE_HUD_SOLO),
	AETHER_ASSET_PART("Collision disabled", SPRITE_HUD_COLLISION_DISABLED),
	AETHER_ASSET_PART("Endless hook", SPRITE_HUD_ENDLESS_HOOK),
	AETHER_ASSET_PART("Jetpack", SPRITE_HUD_JETPACK),
	AETHER_ASSET_PART("Deep frozen", SPRITE_HUD_DEEP_FROZEN),
	AETHER_ASSET_PART("Live frozen", SPRITE_HUD_LIVE_FROZEN),
	AETHER_ASSET_PART("Practice mode", SPRITE_HUD_PRACTICE_MODE),
	AETHER_ASSET_PART("Lock mode", SPRITE_HUD_LOCK_MODE),
	AETHER_ASSET_PART("Team 0 mode", SPRITE_HUD_TEAM0_MODE),
	AETHER_ASSET_PART("Teleport grenade", SPRITE_HUD_TELEPORT_GRENADE),
	AETHER_ASSET_PART("Teleport gun", SPRITE_HUD_TELEPORT_GUN),
	AETHER_ASSET_PART("Teleport laser", SPRITE_HUD_TELEPORT_LASER),
	AETHER_ASSET_PART("Dummy hammer", SPRITE_HUD_DUMMY_HAMMER),
	AETHER_ASSET_PART("Dummy copy", SPRITE_HUD_DUMMY_COPY),
};

static const SAetherAssetPart s_aParticlesAssetParts[] = {
	AETHER_ASSET_PART("Slice", SPRITE_PART_SLICE),
	AETHER_ASSET_PART("Ball", SPRITE_PART_BALL),
	AETHER_ASSET_PART("Splat 01", SPRITE_PART_SPLAT01),
	AETHER_ASSET_PART("Splat 02", SPRITE_PART_SPLAT02),
	AETHER_ASSET_PART("Splat 03", SPRITE_PART_SPLAT03),
	AETHER_ASSET_PART("Smoke", SPRITE_PART_SMOKE),
	AETHER_ASSET_PART("Shell", SPRITE_PART_SHELL),
	AETHER_ASSET_PART("Explosion", SPRITE_PART_EXPL01),
	AETHER_ASSET_PART("Air jump particle", SPRITE_PART_AIRJUMP),
	AETHER_ASSET_PART("Hit particle", SPRITE_PART_HIT01),
};

static const SAetherAssetPart s_aEmoticonAssetParts[] = {
	AETHER_ASSET_PART("Oop", SPRITE_OOP),
	AETHER_ASSET_PART("Exclamation", SPRITE_EXCLAMATION),
	AETHER_ASSET_PART("Hearts", SPRITE_HEARTS),
	AETHER_ASSET_PART("Drop", SPRITE_DROP),
	AETHER_ASSET_PART("Dot dot", SPRITE_DOTDOT),
	AETHER_ASSET_PART("Music", SPRITE_MUSIC),
	AETHER_ASSET_PART("Sorry", SPRITE_SORRY),
	AETHER_ASSET_PART("Ghost", SPRITE_GHOST),
	AETHER_ASSET_PART("Sushi", SPRITE_SUSHI),
	AETHER_ASSET_PART("Splat tee", SPRITE_SPLATTEE),
	AETHER_ASSET_PART("Devil tee", SPRITE_DEVILTEE),
	AETHER_ASSET_PART("Zomg", SPRITE_ZOMG),
	AETHER_ASSET_PART("Zzz", SPRITE_ZZZ),
	AETHER_ASSET_PART("Wtf", SPRITE_WTF),
	AETHER_ASSET_PART("Eyes", SPRITE_EYES),
	AETHER_ASSET_PART("Question", SPRITE_QUESTION),
};

static const SAetherAssetPart s_aExtrasAssetParts[] = {
	AETHER_ASSET_PART("Snowflake", SPRITE_PART_SNOWFLAKE),
	AETHER_ASSET_PART("Sparkle", SPRITE_PART_SPARKLE),
	AETHER_ASSET_PART("Pulley", SPRITE_PART_PULLEY),
	AETHER_ASSET_PART("Hectagon", SPRITE_PART_HECTAGON),
};

std::span<const SAetherAssetPart> AetherEntityAssetParts()
{
	static std::array<SAetherAssetPart, 256> s_aParts;
	static char s_aaPartNames[256][16];
	static bool s_Initialized = false;
	if(!s_Initialized)
	{
		for(int i = 0; i < 256; ++i)
		{
			str_format(s_aaPartNames[i], sizeof(s_aaPartNames[i]), "Tile %03d", i);
			s_aParts[i].m_pName = s_aaPartNames[i];
			s_aParts[i].m_aSprites = {};
			s_aParts[i].m_aSprites[0] = -i - 1;
			s_aParts[i].m_NumSprites = 1;
		}
		s_Initialized = true;
	}
	return std::span<const SAetherAssetPart>(s_aParts.data(), s_aParts.size());
}

static const SAetherAssetPart s_aSkinAssetParts[] = {
	AETHER_ASSET_PART("Body", SPRITE_TEE_BODY),
	AETHER_ASSET_PART("Body outline", SPRITE_TEE_BODY_OUTLINE),
	AETHER_ASSET_PART("Foot", SPRITE_TEE_FOOT),
	AETHER_ASSET_PART("Foot outline", SPRITE_TEE_FOOT_OUTLINE),
	AETHER_ASSET_PART("Hand", SPRITE_TEE_HAND),
	AETHER_ASSET_PART("Hand outline", SPRITE_TEE_HAND_OUTLINE),
	AETHER_ASSET_PART("Eye normal", SPRITE_TEE_EYE_NORMAL),
	AETHER_ASSET_PART("Eye angry", SPRITE_TEE_EYE_ANGRY),
	AETHER_ASSET_PART("Eye pain", SPRITE_TEE_EYE_PAIN),
	AETHER_ASSET_PART("Eye happy", SPRITE_TEE_EYE_HAPPY),
	AETHER_ASSET_PART("Eye dead", SPRITE_TEE_EYE_DEAD),
	AETHER_ASSET_PART("Eye surprise", SPRITE_TEE_EYE_SURPRISE),
};

#undef AETHER_ASSET_PART

static const SAetherAssetCategory s_aAssetEditorCategories[] = {
	{"Game", "game", IMAGE_GAME, std::span<const SAetherAssetPart>(s_aGameAssetParts, std::size(s_aGameAssetParts))},
	{"HUD", "hud", IMAGE_HUD, std::span<const SAetherAssetPart>(s_aHudAssetParts, std::size(s_aHudAssetParts))},
	{"Particles", "particles", IMAGE_PARTICLES, std::span<const SAetherAssetPart>(s_aParticlesAssetParts, std::size(s_aParticlesAssetParts))},
	{"Emoticons", "emoticons", IMAGE_EMOTICONS, std::span<const SAetherAssetPart>(s_aEmoticonAssetParts, std::size(s_aEmoticonAssetParts))},
	{"Extras", "extras", IMAGE_EXTRAS, std::span<const SAetherAssetPart>(s_aExtrasAssetParts, std::size(s_aExtrasAssetParts))},
	{"Entities", "entities", -1, {}},
};

struct SAetherAssetPartState
{
	bool m_Enabled = false;
	int m_SourceIndex = 0;
	int m_SourcePartIndex = 0;
	bool m_TintEnabled = false;
	unsigned m_Color = 255;
	bool m_OnlyColored = false;
	bool m_FullBright = false;
	bool m_FillInside = false;
	int m_Opacity = 100;
};

constexpr int AETHER_ASSET_EDITOR_ENTITIES_CATEGORY = 5;
constexpr int AETHER_ASSET_EDITOR_SKINS_CATEGORY = 6;
constexpr int AETHER_ASSET_EDITOR_MAX_CATEGORIES = 7;
constexpr int AETHER_ASSET_EDITOR_MAX_PARTS = 256;

void AetherClampAssetEditorState(SAetherAssetPartState &State, int NumSources)
{
	State.m_SourceIndex = std::clamp(State.m_SourceIndex, 0, maximum(0, NumSources - 1));
	State.m_Opacity = std::clamp(State.m_Opacity, 0, 100);
}

bool AetherSavePngToStorage(IStorage *pStorage, const char *pRelPath, const CImageInfo &Image)
{
	char aWholePath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = pStorage->OpenFile(pRelPath, IOFLAG_WRITE, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath));
	if(!File)
		return false;
	return CImageLoader::SavePng(File, aWholePath, Image);
}

std::span<const SAetherAssetPart> AetherAssetEditorParts(int Category)
{
	if(Category == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
		return std::span<const SAetherAssetPart>(s_aSkinAssetParts, std::size(s_aSkinAssetParts));
	if(Category == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
		return AetherEntityAssetParts();
	return s_aAssetEditorCategories[Category].m_Parts;
}

bool AetherLoadAssetEditorImage(IGraphics *pGraphics, int Category, const char *pPack, CImageInfo &Image)
{
	if(Category == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
		return AetherLoadSkinImage(pGraphics, pPack, Image);
	if(Category == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
		return AetherLoadEntitiesImage(pGraphics, pPack, Image);
	return AetherLoadAtlasImage(pGraphics, s_aAssetEditorCategories[Category].m_pPath, s_aAssetEditorCategories[Category].m_ImageId, pPack, Image);
}

bool AetherBuildAssetPartPreviewImage(const CImageInfo &Src, int SourceSpriteId, int TargetSpriteId, int TargetImageW, int TargetImageH, const SAetherAssetPartState &State, CImageInfo &Dest)
{
	SAetherSpriteRect SrcRect;
	SAetherSpriteRect DstRect;
	CImageInfo TargetMeta;
	TargetMeta.m_Width = TargetImageW;
	TargetMeta.m_Height = TargetImageH;
	if(!AetherGetAssetPartRect(SourceSpriteId, Src, SrcRect) || !AetherGetAssetPartRect(TargetSpriteId, TargetMeta, DstRect))
		return false;
	Dest.m_Width = DstRect.m_W;
	Dest.m_Height = DstRect.m_H;
	Dest.m_Format = CImageInfo::FORMAT_RGBA;
	Dest.m_pData = static_cast<uint8_t *>(malloc(Dest.DataSize()));
	if(!Dest.m_pData)
		return false;
	mem_zero(Dest.m_pData, Dest.DataSize());

	const bool EntityTile = SourceSpriteId < 0 || TargetSpriteId < 0;
	for(int y = 0; y < DstRect.m_H; ++y)
	{
		for(int x = 0; x < DstRect.m_W; ++x)
		{
			const int MappedX = std::clamp((int)((x + 0.5f) * SrcRect.m_W / DstRect.m_W), 0, SrcRect.m_W - 1);
			const int MappedY = std::clamp((int)((y + 0.5f) * SrcRect.m_H / DstRect.m_H), 0, SrcRect.m_H - 1);
			const size_t SrcX = (size_t)SrcRect.m_X + MappedX;
			const size_t SrcY = (size_t)SrcRect.m_Y + MappedY;
			ColorRGBA SrcColor = Src.PixelColor(SrcX, SrcY);
			Dest.SetPixelColor(x, y, AetherApplyAssetTint(SrcColor, State.m_Color, State.m_TintEnabled, State.m_Opacity, State.m_OnlyColored, State.m_FullBright, State.m_FillInside, EntityTile));
		}
	}
	return true;
}

struct SAetherAssetImageCacheEntry
{
	int m_Category = -1;
	std::string m_Name;
	CImageInfo m_Image;
};

static std::vector<SAetherAssetImageCacheEntry> s_vAetherAssetEditorImageCache;

void AetherClearAssetEditorImageCache()
{
	for(SAetherAssetImageCacheEntry &Entry : s_vAetherAssetEditorImageCache)
		Entry.m_Image.Free();
	s_vAetherAssetEditorImageCache.clear();
}

const CImageInfo *AetherCachedAssetEditorImage(IGraphics *pGraphics, int Category, const char *pPack)
{
	const char *pName = pPack && pPack[0] ? pPack : "default";
	for(const SAetherAssetImageCacheEntry &Entry : s_vAetherAssetEditorImageCache)
	{
		if(Entry.m_Category == Category && Entry.m_Name == pName)
			return &Entry.m_Image;
	}

	CImageInfo Loaded;
	if(!AetherLoadAssetEditorImage(pGraphics, Category, pName, Loaded))
		return nullptr;

	SAetherAssetImageCacheEntry &Entry = s_vAetherAssetEditorImageCache.emplace_back();
	Entry.m_Category = Category;
	Entry.m_Name = pName;
	Entry.m_Image = std::move(Loaded);
	return &Entry.m_Image;
}

bool AetherBuildMixedAssetImage(IGraphics *pGraphics, int Category, const std::vector<std::string> &vNames, int BaseIndex, SAetherAssetPartState aaStates[AETHER_ASSET_EDITOR_MAX_CATEGORIES][AETHER_ASSET_EDITOR_MAX_PARTS], CImageInfo &Dest)
{
	if(vNames.empty())
		return false;
	BaseIndex = std::clamp(BaseIndex, 0, maximum(0, (int)vNames.size() - 1));
	const CImageInfo *pBase = AetherCachedAssetEditorImage(pGraphics, Category, vNames[BaseIndex].c_str());
	if(!pBase)
		return false;
	Dest = pBase->DeepCopy();

	const std::span<const SAetherAssetPart> Parts = AetherAssetEditorParts(Category);
	for(size_t Part = 0; Part < Parts.size(); ++Part)
	{
		SAetherAssetPartState &PartState = aaStates[Category][Part];
		AetherClampAssetEditorState(PartState, (int)vNames.size());
		if(!PartState.m_Enabled)
			continue;

		const CImageInfo *pSrc = AetherCachedAssetEditorImage(pGraphics, Category, vNames[PartState.m_SourceIndex].c_str());
		if(!pSrc)
			continue;
		PartState.m_SourcePartIndex = std::clamp(PartState.m_SourcePartIndex, 0, maximum(0, (int)Parts.size() - 1));
		const SAetherAssetPart &TargetPart = Parts[Part];
		const SAetherAssetPart &SourcePart = Parts[PartState.m_SourcePartIndex];
		if(TargetPart.m_NumSprites <= 0 || SourcePart.m_NumSprites <= 0)
			continue;
		for(int i = 0; i < TargetPart.m_NumSprites; ++i)
		{
			const int SourceSprite = SourcePart.m_aSprites[std::min(i, SourcePart.m_NumSprites - 1)];
			AetherBlendSprite(Dest, *pSrc, SourceSprite, TargetPart.m_aSprites[i], PartState.m_TintEnabled, PartState.m_Color, PartState.m_Opacity, PartState.m_OnlyColored, PartState.m_FullBright, PartState.m_FillInside);
		}
	}
	return true;
}

CUIRect AetherFitImageRect(const CUIRect &Bounds, int ImageW, int ImageH)
{
	if(ImageW <= 0 || ImageH <= 0 || Bounds.w <= 0.0f || Bounds.h <= 0.0f)
		return Bounds;
	const float Scale = std::min(Bounds.w / ImageW, Bounds.h / ImageH);
	CUIRect Result;
	Result.w = ImageW * Scale;
	Result.h = ImageH * Scale;
	Result.x = Bounds.x + (Bounds.w - Result.w) * 0.5f;
	Result.y = Bounds.y + (Bounds.h - Result.h) * 0.5f;
	return Result;
}

void AetherDrawTextureInRect(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect)
{
	if(!Texture.IsValid())
		return;
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
	pGraphics->WrapNormal();
}

void AetherDrawTextureSubsetInRect(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, int ImageW, int ImageH, const SAetherSpriteRect &SrcRect, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f))
{
	if(!Texture.IsValid() || ImageW <= 0 || ImageH <= 0 || SrcRect.m_W <= 0 || SrcRect.m_H <= 0)
		return;
	const float U0 = SrcRect.m_X / (float)ImageW;
	const float V0 = SrcRect.m_Y / (float)ImageH;
	const float U1 = (SrcRect.m_X + SrcRect.m_W) / (float)ImageW;
	const float V1 = (SrcRect.m_Y + SrcRect.m_H) / (float)ImageH;
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color.r, Color.g, Color.b, Color.a);
	pGraphics->QuadsSetSubset(U0, V0, U1, V1);
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
	pGraphics->WrapNormal();
}

void AetherDrawAtlasGrid(const CUIRect &Rect, int ImageW, int ImageH, float Alpha)
{
	if(ImageW <= 0 || ImageH <= 0)
		return;
	const int Columns = std::clamp(ImageW / 32, 4, 64);
	const int Rows = std::clamp(ImageH / 32, 4, 64);
	for(int i = 1; i < Columns; ++i)
	{
		CUIRect Line = {Rect.x + Rect.w * i / Columns, Rect.y, 1.0f, Rect.h};
		Line.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), IGraphics::CORNER_NONE, 0.0f);
	}
	for(int i = 1; i < Rows; ++i)
	{
		CUIRect Line = {Rect.x, Rect.y + Rect.h * i / Rows, Rect.w, 1.0f};
		Line.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), IGraphics::CORNER_NONE, 0.0f);
	}
}

bool AetherAtlasPixelFromScreen(const CUIRect &ImageRect, int ImageW, int ImageH, float ScreenX, float ScreenY, int &PixelX, int &PixelY)
{
	if(ImageW <= 0 || ImageH <= 0 || ImageRect.w <= 0.0f || ImageRect.h <= 0.0f)
		return false;
	if(ScreenX < ImageRect.x || ScreenX > ImageRect.x + ImageRect.w || ScreenY < ImageRect.y || ScreenY > ImageRect.y + ImageRect.h)
		return false;
	const float U = std::clamp((ScreenX - ImageRect.x) / ImageRect.w, 0.0f, 0.999999f);
	const float V = std::clamp((ScreenY - ImageRect.y) / ImageRect.h, 0.0f, 0.999999f);
	PixelX = std::clamp((int)(U * ImageW), 0, ImageW - 1);
	PixelY = std::clamp((int)(V * ImageH), 0, ImageH - 1);
	return true;
}

bool s_AetherAssetsEditorOpen = false;
bool s_AetherAssetsEditorWindowInit = false;
CUIRect s_AetherAssetsEditorWindow = {0.0f, 0.0f, 1120.0f, 660.0f};
int s_AetherAssetsEditorCategory = 0;
int s_AetherAssetsEditorBaseIndex = 0;
int s_AetherAssetsEditorDonorIndex = 0;
int s_AetherAssetsEditorPartIndex = 0;
int s_AetherAssetsEditorDragSource = -1;
int s_AetherAssetsEditorDragPart = -1;
int s_AetherAssetsEditorDropPart = -1;
bool s_AetherAssetsEditorShowGrid = true;
SAetherAssetPartState s_aaAetherAssetsEditorStates[AETHER_ASSET_EDITOR_MAX_CATEGORIES][AETHER_ASSET_EDITOR_MAX_PARTS] = {};
std::vector<std::string> s_vAetherAssetsEditorAssetSources;
std::vector<std::string> s_vAetherAssetsEditorSkinSources;
char s_aAetherAssetsEditorExportName[64] = "aether_mix";
char s_aAetherAssetsEditorStatus[256] = "Drag a source onto a mixed part, then tune tint and opacity.";
IGraphics::CTextureHandle s_AetherAssetsEditorSourceTexture;
char s_aAetherAssetsEditorSourcePath[IO_MAX_PATH_LENGTH] = {};
int s_AetherAssetsEditorSourceWidth = 0;
int s_AetherAssetsEditorSourceHeight = 0;
IGraphics::CTextureHandle s_AetherAssetsEditorLiveSourceTexture;
char s_aAetherAssetsEditorLiveSourcePath[IO_MAX_PATH_LENGTH] = {};
int s_AetherAssetsEditorLiveSourceWidth = 0;
int s_AetherAssetsEditorLiveSourceHeight = 0;
IGraphics::CTextureHandle s_AetherAssetsEditorLivePartTexture;
char s_aAetherAssetsEditorLivePartKey[512] = {};
IGraphics::CTextureHandle s_AetherAssetsEditorMixedTexture;
bool s_AetherAssetsEditorMixedDirty = true;
bool s_AetherAssetsEditorForcePreviewBuild = true;
int s_AetherAssetsEditorMixedWidth = 0;
int s_AetherAssetsEditorMixedHeight = 0;
CUIRect s_AetherAssetsEditorEditPanel = {0.0f, 0.0f, 360.0f, 190.0f};
bool s_AetherAssetsEditorEditPanelInit = false;
bool s_AetherAssetsEditorEditPanelOpen = false;
float s_AetherAssetsEditorEditDragOffsetX = 0.0f;
float s_AetherAssetsEditorEditDragOffsetY = 0.0f;
bool s_AetherAssetsEditorPickColor = false;

void AetherResetAssetsEditorLivePartTexture(IGraphics *pGraphics)
{
	pGraphics->UnloadTexture(&s_AetherAssetsEditorLivePartTexture);
	s_aAetherAssetsEditorLivePartKey[0] = '\0';
}

void AetherSanitizeSoundFileName(const char *pInput, char *pOut, int OutSize)
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

int LoadAetherUserSoundSample(IStorage *pStorage, ISound *pSound, const char *pFolder, const char *pFileName)
{
	if(!pStorage || !pSound || !pFolder || !pFileName || pFileName[0] == '\0')
	{
		log_info("aether/sound", "preview skipped: no file selected");
		return -1;
	}

	char aSanitized[256];
	AetherSanitizeSoundFileName(pFileName, aSanitized, sizeof(aSanitized));
	if(aSanitized[0] == '\0')
	{
		log_error("aether/sound", "preview skipped: invalid file name");
		return -1;
	}

	char aRel[IO_MAX_PATH_LENGTH];
	str_format(aRel, sizeof(aRel), "assets/%s/%s", pFolder, aSanitized);
	if(!pStorage->FileExists(aRel, IStorage::TYPE_ALL))
	{
		log_error("aether/sound", "preview file not found: %s", aRel);
		return -1;
	}

	if(AetherSoundFileHasExtension(aSanitized, ".opus"))
	{
		const int Id = pSound->LoadOpus(aRel, IStorage::TYPE_ALL);
		log_info("aether/sound", "loaded opus preview '%s' sample=%d", aRel, Id);
		return Id;
	}
	if(AetherSoundFileHasExtension(aSanitized, ".wv"))
	{
		const int Id = pSound->LoadWV(aRel, IStorage::TYPE_ALL);
		log_info("aether/sound", "loaded wv preview '%s' sample=%d", aRel, Id);
		return Id;
	}
	if(AetherSoundFileHasExtension(aSanitized, ".mp3"))
	{
		char aAbsolute[IO_MAX_PATH_LENGTH];
		IOHANDLE File = pStorage->OpenFile(aRel, IOFLAG_READ, IStorage::TYPE_ALL, aAbsolute, sizeof(aAbsolute));
		if(!File)
		{
			log_error("aether/sound", "failed to resolve mp3 preview path '%s'", aRel);
			return -1;
		}
		io_close(File);
		std::vector<short> vPcm;
		int Channels = 0;
		int SampleRate = 0;
		if(!AetherAudio::DecodeAudioFileToS16Pcm(aAbsolute, vPcm, Channels, SampleRate, aSanitized) || Channels <= 0 || vPcm.empty())
		{
			log_error("aether/sound", "failed to decode mp3 preview '%s'", aAbsolute);
			return -1;
		}
		if((Channels != 1 && Channels != 2) || SampleRate <= 0)
		{
			log_error("aether/sound", "unsupported decoded mp3 format '%s' channels=%d rate=%d", aAbsolute, Channels, SampleRate);
			return -1;
		}
		const int NumFrames = (int)(vPcm.size() / (size_t)Channels);
		if(NumFrames <= 0)
			return -1;
		const int Id = pSound->LoadS16PcmInterleavedFromMem(vPcm.data(), NumFrames, Channels, SampleRate, false, aSanitized);
		log_info("aether/sound", "loaded mp3 preview '%s' sample=%d frames=%d channels=%d rate=%d", aAbsolute, Id, NumFrames, Channels, SampleRate);
		return Id;
	}
	log_error("aether/sound", "unsupported preview file type: %s", aRel);
	return -1;
}

void UnloadAetherPreviewSound(ISound *pSound, int &SampleId, char *pLoadedFile, int LoadedFileSize)
{
	if(pSound && SampleId >= 0)
		pSound->UnloadSample(SampleId);
	SampleId = -1;
	if(pLoadedFile && LoadedFileSize > 0)
		pLoadedFile[0] = '\0';
}

bool PreviewAetherUserSound(IStorage *pStorage, ISound *pSound, const char *pFolder, const char *pFileName, int Volume, int &SampleId, char *pLoadedFile, int LoadedFileSize, int Channel)
{
	if(!pSound)
		return false;
	if(!pFileName || pFileName[0] == '\0')
	{
		log_info("aether/sound", "preview skipped: no file selected");
		return false;
	}
	if(SampleId < 0 || str_comp(pLoadedFile, pFileName) != 0)
	{
		UnloadAetherPreviewSound(pSound, SampleId, pLoadedFile, LoadedFileSize);
		SampleId = LoadAetherUserSoundSample(pStorage, pSound, pFolder, pFileName);
		str_copy(pLoadedFile, pFileName, LoadedFileSize);
	}
	if(SampleId >= 0)
	{
		log_info("aether/sound", "playing preview '%s' sample=%d channel=%d volume=%d", pFileName, SampleId, Channel, Volume);
		pSound->Play(Channel, SampleId, ISound::FLAG_NO_PANNING, std::clamp(Volume / 100.0f, 0.0f, 1.0f));
		return true;
	}
	log_error("aether/sound", "preview failed: could not load '%s'", pFileName);
	return false;
}

struct SSoundFileScanContext
{
	std::vector<std::string> *m_pFiles;
};

int AetherSoundFileScan(const char *pName, int IsDir, int StorageType, void *pUser)
{
	(void)StorageType;
	if(IsDir || !IsAetherSoundFile(pName))
		return 0;
	static_cast<SSoundFileScanContext *>(pUser)->m_pFiles->emplace_back(pName);
	return 0;
}

void BuildAetherSoundFileList(IStorage *pStorage, const char *pFolder, std::vector<std::string> &Labels, std::vector<const char *> &Ptrs)
{
	Labels.clear();
	Ptrs.clear();
	Labels.emplace_back("None");

	if(pStorage && pFolder)
	{
		char aRel[IO_MAX_PATH_LENGTH];
		str_format(aRel, sizeof(aRel), "assets/%s", pFolder);
		std::vector<std::string> vFiles;
		SSoundFileScanContext ScanContext{&vFiles};
		pStorage->ListDirectory(IStorage::TYPE_ALL, aRel, AetherSoundFileScan, &ScanContext);
		std::sort(vFiles.begin(), vFiles.end(), [](const std::string &A, const std::string &B) {
			return str_comp_nocase(A.c_str(), B.c_str()) < 0;
		});
		vFiles.erase(std::unique(vFiles.begin(), vFiles.end(), [](const std::string &A, const std::string &B) {
			return str_comp_nocase(A.c_str(), B.c_str()) == 0;
		}), vFiles.end());
		for(const std::string &File : vFiles)
			Labels.emplace_back(File);
	}

	for(const std::string &Label : Labels)
		Ptrs.push_back(Label.c_str());
}

int SelectedSoundFileIndex(const std::vector<std::string> &Labels, const char *pCurrent)
{
	if(!pCurrent || pCurrent[0] == '\0')
		return 0;
	for(size_t i = 1; i < Labels.size(); ++i)
	{
		if(str_comp(Labels[i].c_str(), pCurrent) == 0)
			return (int)i;
	}
	return 0;
}

bool AetherSoundSelectionExists(const std::vector<std::string> &Labels, const char *pCurrent)
{
	return !pCurrent || pCurrent[0] == '\0' || SelectedSoundFileIndex(Labels, pCurrent) > 0;
}
}

void CMenus::ResetAetherSettingsState()
{
	m_AetherSearchInput.Clear();
	m_AetherExpandedFeature = AetherMusic::EAetherFeatureId::NONE;
	m_AetherShowEnabledOnly = false;
	m_AetherSettingsVisible = false;
}

void CMenus::OpenAetherChessOnline()
{
	SetActive(true);
	m_MenuPage = PAGE_SETTINGS;
	m_GamePage = PAGE_SETTINGS;
	g_Config.m_UiSettingsPage = SETTINGS_AETHER;
	m_AetherSettingsVisible = true;
	m_aAetherSearch[0] = '\0';
	m_AetherSearchInput.Clear();
	m_AetherExpandedFeature = AetherMusic::EAetherFeatureId::NONE;
	s_AetherActivePage = EAetherPage::GAMES;
	s_AetherOpenChessOnline = true;
}

void CMenus::RenderSettingsAetherAutoTeamLock(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeAutoTeamLockDelay, &g_Config.m_AeAutoTeamLockDelay, &Control, "Lock delay after joining team", 0, 60, &CUi::ms_LinearScrollbarScale, 0, "s");
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "Sends /lock once after entering a normal DDNet team.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherGoresMode(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeGoresModeDisableIfWeapons, "Disable if you have shotgun, grenade or laser", g_Config.m_AeGoresModeDisableIfWeapons, &Control))
		g_Config.m_AeGoresModeDisableIfWeapons ^= 1;

	Body.HSplitTop(6.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.55f, 0.95f, 0.60f, 1.0f);
	Ui()->DoLabel(&Control, "Uses the legacy AetherClient fire/prevweapon flow while enabled.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Body.HSplitTop(34.0f * S, &Control, &Body);
	Ui()->DoLabel(&Control, "Fire temporarily adds prevweapon and the input tick returns hammer back to gun.", 11.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherFocusMode(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(10.0f * S, &Body);

	CUIRect Control;
	static CButtonContainer s_FocusModeReaderButton;
	static CButtonContainer s_FocusModeClearButton;
	DoLine_KeyReader(Body, s_FocusModeReaderButton, s_FocusModeClearButton, "Focus Mode key", "toggle ae_focus_mode 0 1");
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFocusModeHideAllUi, "Hide all UI", g_Config.m_AeFocusModeHideAllUi, &Control))
		g_Config.m_AeFocusModeHideAllUi ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFocusModeHideNameplates, "Hide nameplates", g_Config.m_AeFocusModeHideNameplates, &Control))
		g_Config.m_AeFocusModeHideNameplates ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFocusModeKeepMusicPlayer, "Keep music player visible", g_Config.m_AeFocusModeKeepMusicPlayer, &Control))
		g_Config.m_AeFocusModeKeepMusicPlayer ^= 1;
}

void CMenus::RenderSettingsAetherDdraceConfigs(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(8.0f * S, &Body);
	struct SConfigEntry
	{
		const char *m_pLabel;
		const char *m_pName;
		const char *m_pCommand;
	};
	static constexpr SConfigEntry s_aConfigs[] = {
		{"Show Others Toggle", "show_others", "ae_ddrace_config show_others"},
		{"One Key Super", "super", "ae_ddrace_config super"},
		{"Deep-Fly Toggle", "deep_fly", "ae_ddrace_config deep_fly"},
		{"Edge2Edge Freeze Tiles", "edge2edge", "ae_ddrace_config edge2edge"},
	};
	static CButtonContainer s_aApplyButtons[std::size(s_aConfigs)];
	static CButtonContainer s_aResetButtons[std::size(s_aConfigs)];
	static CButtonContainer s_aReaderButtons[std::size(s_aConfigs)];
	static CButtonContainer s_aClearButtons[std::size(s_aConfigs)];
	static CButtonContainer s_OpenFolderButton;

	auto ExecConfig = [&](const char *pName, const char *pMode) {
		char aCommand[128];
		str_format(aCommand, sizeof(aCommand), "ae_ddrace_config %s %s", pName, pMode);
		Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	};
	auto DoCompactKeyReader = [&](CUIRect KeyButton, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pCommand) {
		CBindSlot Bind(0, 0);
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
		{
			for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
			{
				const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
				if(pBind[0] && str_comp(pBind, pCommand) == 0)
				{
					Bind.m_Key = KeyId;
					Bind.m_ModifierMask = Mod;
					break;
				}
			}
		}
		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false);
		if(Result.m_Bind != Bind)
		{
			if(Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		}
	};

	const float Gap = 4.0f * S;
	for(size_t i = 0; i < std::size(s_aConfigs); ++i)
	{
		CUIRect Row;
		Body.HSplitTop(24.0f * S, &Row, &Body);
		CUIRect Label, KeyButton, Toggle, Reset;
		const float ResetW = std::clamp(54.0f * S, 46.0f, 64.0f * S);
		const float ToggleW = std::clamp(66.0f * S, 56.0f, 76.0f * S);
		const float LabelW = std::clamp(Row.w * 0.30f, 112.0f * S, 188.0f * S);
		Row.VSplitLeft(LabelW, &Label, &Row);
		Row.VSplitRight(ResetW, &Row, &Reset);
		Row.VSplitRight(Gap, &Row, nullptr);
		Row.VSplitRight(ToggleW, &Row, &Toggle);
		Row.VSplitRight(Gap, &Row, nullptr);
		KeyButton = Row;
		KeyButton.VMargin(2.0f * S, &KeyButton);
		Ui()->DoLabel(&Label, Localize(s_aConfigs[i].m_pLabel), 12.0f * S, TEXTALIGN_ML);
		DoCompactKeyReader(KeyButton, s_aReaderButtons[i], s_aClearButtons[i], s_aConfigs[i].m_pCommand);
		if(DoButton_Menu(&s_aApplyButtons[i], Localize("Toggle"), 0, &Toggle))
			ExecConfig(s_aConfigs[i].m_pName, "toggle");
		if(DoButton_Menu(&s_aResetButtons[i], Localize("Reset"), 0, &Reset))
			ExecConfig(s_aConfigs[i].m_pName, "off");

		Body.HSplitTop(3.0f * S, nullptr, &Body);
	}

	CUIRect OpenFolder;
	Body.HSplitTop(2.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &OpenFolder, &Body);
	if(OpenFolder.w > 190.0f * S)
		OpenFolder.VSplitRight(180.0f * S, nullptr, &OpenFolder);
	if(DoButton_Menu(&s_OpenFolderButton, Localize("Open folder"), 0, &OpenFolder))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_ALL, "core/ddrace_configs", aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}
}

void CMenus::RenderSettingsAetherDescription(CUIRect Body, const char *pText)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Body, pText, 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherBadges(CUIRect Body)
{
	static CButtonContainer s_RefreshButton;
	static CButtonContainer s_FriendHeartButton;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Row;
	Body.HSplitTop(22.0f * S, &Row, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeBadgesNameplates, "Show in nameplates", g_Config.m_AeBadgesNameplates, &Row))
		g_Config.m_AeBadgesNameplates ^= 1;
	Body.HSplitTop(22.0f * S, &Row, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeBadgesScoreboard, "Show in scoreboard", g_Config.m_AeBadgesScoreboard, &Row))
		g_Config.m_AeBadgesScoreboard ^= 1;
	Body.HSplitTop(22.0f * S, &Row, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeBadgesClientOnly, "Show client badges only", g_Config.m_AeBadgesClientOnly, &Row))
		g_Config.m_AeBadgesClientOnly ^= 1;
	Body.HSplitTop(22.0f * S, &Row, &Body);
	const int FriendHeart = g_Config.m_ClNamePlatesFriendMark || g_Config.m_ClMessageFriend;
	if(DoButton_CheckBox(&s_FriendHeartButton, "Show friend heart", FriendHeart, &Row))
	{
		const int NewValue = FriendHeart ? 0 : 1;
		g_Config.m_ClNamePlatesFriendMark = NewValue;
		g_Config.m_ClMessageFriend = NewValue;
	}
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	if(DoButton_Menu(&s_RefreshButton, "Refresh now", 0, &Row))
		GameClient()->m_AetherBadges.RefreshNow();
	Body.HSplitTop(10.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Row, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Row, GameClient()->m_AetherBadges.Status(), 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherPings(CUIRect Body)
{
	static std::array<CButtonContainer, 3> s_aPingTabs;
	static CButtonContainer s_PingWheelReader;
	static CButtonContainer s_PingWheelClear;
	static CButtonContainer s_PingPlaceReader;
	static CButtonContainer s_PingPlaceClear;
	static CButtonContainer s_PingHelpReader;
	static CButtonContainer s_PingHelpClear;
	static CButtonContainer s_PingDangerReader;
	static CButtonContainer s_PingDangerClear;
	static CButtonContainer s_PingComeReader;
	static CButtonContainer s_PingComeClear;
	static CButtonContainer s_PingWaitReader;
	static CButtonContainer s_PingWaitClear;
	static std::array<CButtonContainer, 3> s_aPingVisibilityButtons;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Row;
	Body.HSplitTop(24.0f * S, &Row, &Body);
	const char *apTabs[] = {"General", "Keys", "Visibility"};
	const float TabGap = 3.0f * S;
	const float TabW = (Row.w - TabGap * 2.0f) / 3.0f;
	for(int i = 0; i < 3; ++i)
	{
		CUIRect Tab;
		Row.VSplitLeft(TabW, &Tab, &Row);
		if(DoButton_Menu(&s_aPingTabs[i], apTabs[i], s_AetherPingTab == i, &Tab, BUTTONFLAG_LEFT, nullptr,
			   i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE)))
			s_AetherPingTab = i;
		if(i != 2)
			Row.VSplitLeft(TabGap, nullptr, &Row);
	}
	s_AetherPingTab = std::clamp(s_AetherPingTab, 0, 2);
	Body.HSplitTop(8.0f * S, nullptr, &Body);

	if(s_AetherPingTab == 0)
	{
		Body.HSplitTop(22.0f * S, &Row, &Body);
		if(DoButton_CheckBox(&g_Config.m_AePings, "Show Aether pings", g_Config.m_AePings, &Row))
			g_Config.m_AePings ^= 1;
		Body.HSplitTop(22.0f * S, &Row, &Body);
		if(DoButton_CheckBox(&g_Config.m_AePingAutoHelp, "Auto help on frozen ally", g_Config.m_AePingAutoHelp, &Row))
			g_Config.m_AePingAutoHelp ^= 1;
		return;
	}

	if(s_AetherPingTab == 1)
	{
		CUIRect Left, Right;
		Body.VSplitMid(&Left, &Right, 14.0f * S);
		DoLine_KeyReader(Left, s_PingWheelReader, s_PingWheelClear, "Ping wheel", "+ae_ping_wheel");
		DoLine_KeyReader(Left, s_PingPlaceReader, s_PingPlaceClear, "Place ping", "ae_ping place");
		DoLine_KeyReader(Left, s_PingHelpReader, s_PingHelpClear, "Help ping", "ae_ping help");
		DoLine_KeyReader(Right, s_PingDangerReader, s_PingDangerClear, "Danger ping", "ae_ping danger");
		DoLine_KeyReader(Right, s_PingComeReader, s_PingComeClear, "Come ping", "ae_ping come");
		DoLine_KeyReader(Right, s_PingWaitReader, s_PingWaitClear, "Wait ping", "ae_ping wait");
		return;
	}

	Body.HSplitTop(24.0f * S, &Row, &Body);
	g_Config.m_AePingHelpVisibility = std::clamp(g_Config.m_AePingHelpVisibility, 0, 2);
	CUIRect Label, Buttons;
	Row.VSplitLeft(150.0f * S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Auto help visible to", 12.0f * S, TEXTALIGN_ML);
	const char *apVisibility[] = {"Team", "Team + warlist", "All"};
	for(int i = 0; i < 3; ++i)
	{
		CUIRect Button;
		const float ButtonW = std::max(72.0f * S, (Buttons.w - (2 - i) * 4.0f * S) / (3 - i));
		Buttons.VSplitLeft(ButtonW, &Button, &Buttons);
		if(DoButton_Menu(&s_aPingVisibilityButtons[i], apVisibility[i], g_Config.m_AePingHelpVisibility == i, &Button, BUTTONFLAG_LEFT, nullptr,
			   i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE)))
			g_Config.m_AePingHelpVisibility = i;
		if(i != 2)
			Buttons.VSplitLeft(4.0f * S, nullptr, &Buttons);
	}
}

void CMenus::RenderSettingsAetherClan(CUIRect Body)
{
	static CButtonContainer s_aClanTabs[2];
	static CButtonContainer s_ClanRefresh;
	static CButtonContainer s_ClanCreateGeneral;
	static CButtonContainer s_ClanCreateKog;
	static CButtonContainer s_ClanJoin;
	static CButtonContainer s_ClanCopyGeneralInvite;
	static CButtonContainer s_ClanCopyKogInvite;
	static CButtonContainer s_ClanMembersGeneral;
	static CButtonContainer s_ClanMembersKog;
	static CButtonContainer s_ClanSelectGeneral;
	static CButtonContainer s_ClanSelectKog;
	static CButtonContainer s_ClanLeaveGeneral;
	static CButtonContainer s_ClanLeaveKog;
	static CButtonContainer s_ClanRotateGeneral;
	static CButtonContainer s_ClanRotateKog;
	static CButtonContainer s_ClanDisbandGeneral;
	static CButtonContainer s_ClanDisbandKog;
	static CButtonContainer s_ClanPushWarlist;
	static CButtonContainer s_ClanPullWarlist;
	static CButtonContainer s_ClanCopyWarlist;
	static std::array<CButtonContainer, MAX_CLIENTS> s_aClanRemoveMemberButtons;
	static CButtonContainer s_ClanConfirmRemove;
	static CButtonContainer s_ClanCancelRemove;
	static int s_ClanTab = 0;
	static int s_SelectedClanType = 0;
	static char s_aPendingRemoveName[MAX_NAME_LENGTH] = "";
	static char s_aPendingRemoveClanType[16] = "";
	static char s_aClanName[64] = "";
	static char s_aClanInvite[16] = "";
	static CLineInput s_ClanNameInput(s_aClanName, sizeof(s_aClanName));
	static CLineInput s_ClanInviteInput(s_aClanInvite, sizeof(s_aClanInvite));
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	const auto &GeneralClan = GameClient()->m_AetherBadges.GeneralClan();
	const auto &KogClan = GameClient()->m_AetherBadges.KogClan();
	const bool ManagementReady = GameClient()->m_AetherBadges.ClanManagementAvailable();
	CUIRect Row;

	CUIRect Header;
	Body.HSplitTop(20.0f * S, &Header, &Body);
	TextRender()->TextColor(0.86f, 0.92f, 1.0f, 1.0f);
	Ui()->DoLabel(&Header, "Cloud Clan", 16.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Body.HSplitTop(8.0f * S, nullptr, &Body);

	CUIRect Tabs;
	Body.HSplitTop(26.0f * S, &Tabs, &Body);
	const char *apTabs[] = {"Overview", "Members"};
	for(int i = 0; i < 2; ++i)
	{
		CUIRect Tab;
		const float ButtonW = Tabs.w / (2 - i);
		Tabs.VSplitLeft(ButtonW, &Tab, &Tabs);
		if(DoButton_Menu(&s_aClanTabs[i], apTabs[i], s_ClanTab == i, &Tab, BUTTONFLAG_LEFT, nullptr, i == 0 ? IGraphics::CORNER_L : IGraphics::CORNER_R))
			s_ClanTab = i;
	}

	auto RenderStatusBanner = [&](CUIRect Banner) {
		const bool HasError = GameClient()->m_AetherBadges.ClanLastError()[0] != '\0';
		const ColorRGBA Bg = AetherPanelColor(HasError ? 0.34f : 0.28f);
		const ColorRGBA Fg = HasError ? ColorRGBA(1.0f, 0.50f, 0.54f, 1.0f) : ColorRGBA(0.72f, 0.86f, 0.78f, 1.0f);
		Banner.Draw(Bg, IGraphics::CORNER_ALL, 5.0f * S);
		Banner.Margin(7.0f * S, &Banner);
		TextRender()->TextColor(Fg);
		Ui()->DoLabel(&Banner, GameClient()->m_AetherBadges.ClanStatus(), 11.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(25.0f * S, &Row, &Body);
	RenderStatusBanner(Row);
	Body.HSplitTop(10.0f * S, nullptr, &Body);

	auto RenderClanCard = [&](CUIRect Card, const char *pLabel, const auto &ClanState, bool Kog, CButtonContainer &MembersButton, CButtonContainer &CopyButton, CButtonContainer &LeaveButton, CButtonContainer &RotateButton, CButtonContainer &DisbandButton) {
		Card.Draw(ClanState.m_Valid ? AetherThemeColor(Kog ? 0.16f : 0.20f) : AetherPanelColor(0.24f), IGraphics::CORNER_ALL, 7.0f * S);
		Card.Margin(9.0f * S, &Card);
		CUIRect Title, Details, Buttons;
		Card.HSplitTop(22.0f * S, &Title, &Card);
		char aTitle[192];
		str_format(aTitle, sizeof(aTitle), "%s%s%s", pLabel, ClanState.m_Valid ? ": " : "", ClanState.m_Valid ? ClanState.m_aName : " - not joined");
		TextRender()->TextColor(ClanState.m_Valid ? ColorRGBA(0.90f, 0.94f, 1.0f, 1.0f) : ColorRGBA(0.62f, 0.68f, 0.76f, 1.0f));
		Ui()->DoLabel(&Title, aTitle, 14.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		Card.HSplitTop(32.0f * S, &Details, &Card);
		char aDetails[256];
		if(ClanState.m_Valid)
			str_format(aDetails, sizeof(aDetails), "role %s | members %d | code %s", ClanState.m_aRole, ClanState.m_MemberCount, ClanState.m_aInviteCode[0] ? ClanState.m_aInviteCode : "-");
		else
			str_copy(aDetails, Kog ? "KoG clan shows on KoG servers." : "General clan shows outside KoG and can share warlists.", sizeof(aDetails));
		TextRender()->TextColor(0.70f, 0.78f, 0.88f, 1.0f);
		Ui()->DoLabel(&Details, aDetails, 11.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		Card.HSplitTop(6.0f * S, nullptr, &Card);
		Card.HSplitTop(24.0f * S, &Buttons, &Card);
		CUIRect B1, B2, B3, B4, B5;
		const float Gap = 5.0f * S;
		const float ButtonW = (Buttons.w - Gap * 4.0f) / 5.0f;
		Buttons.VSplitLeft(ButtonW, &B1, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		Buttons.VSplitLeft(ButtonW, &B2, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		Buttons.VSplitLeft(ButtonW, &B3, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		Buttons.VSplitLeft(ButtonW, &B4, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		Buttons.VSplitLeft(ButtonW, &B5, nullptr);
		if(DoButton_Menu(&MembersButton, "Members", ClanState.m_Valid && ManagementReady ? 0 : -1, &B1) && ClanState.m_Valid && ManagementReady)
		{
			s_ClanTab = 1;
			s_SelectedClanType = Kog ? 1 : 0;
			GameClient()->m_AetherBadges.SendClanMembers(ClanState);
		}
		if(DoButton_Menu(&CopyButton, "Copy", ClanState.m_Valid && ClanState.m_aInviteCode[0] ? 0 : -1, &B2) && ClanState.m_Valid && ClanState.m_aInviteCode[0])
			Input()->SetClipboardText(ClanState.m_aInviteCode);
		if(DoButton_Menu(&LeaveButton, "Leave", ClanState.m_Valid && ManagementReady ? 0 : -1, &B3) && ClanState.m_Valid && ManagementReady)
			GameClient()->m_AetherBadges.SendClanLeave(ClanState);
		const bool Owner = ClanState.m_Valid && str_comp_nocase(ClanState.m_aRole, "owner") == 0;
		if(DoButton_Menu(&RotateButton, "Rotate", Owner && ManagementReady ? 0 : -1, &B4) && Owner && ManagementReady)
			GameClient()->m_AetherBadges.SendClanRotateInvite(ClanState);
		if(DoButton_Menu(&DisbandButton, "Disband", Owner && ManagementReady ? 0 : -1, &B5) && Owner && ManagementReady)
			GameClient()->m_AetherBadges.SendClanDisband(ClanState);
	};

	if(s_ClanTab == 0)
	{
		CUIRect Forms, CreateCard, JoinCard;
		Body.HSplitTop(72.0f * S, &Forms, &Body);
		Forms.VSplitMid(&CreateCard, &JoinCard, 10.0f * S);
		auto RenderFormCard = [&](CUIRect Card, const char *pTitle) {
			Card.Draw(AetherPanelColor(0.24f), IGraphics::CORNER_ALL, 7.0f * S);
			Card.Margin(9.0f * S, &Card);
			CUIRect Title;
			Card.HSplitTop(18.0f * S, &Title, &Card);
			TextRender()->TextColor(0.86f, 0.92f, 1.0f, 1.0f);
			Ui()->DoLabel(&Title, pTitle, 13.0f * S, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Card.HSplitTop(7.0f * S, nullptr, &Card);
			return Card;
		};

		CreateCard = RenderFormCard(CreateCard, "Create clan");
		CreateCard.HSplitTop(24.0f * S, &Row, &CreateCard);
		CUIRect NameInput, CreateGen, CreateKog;
		Row.VSplitRight(84.0f * S, &NameInput, &CreateKog);
		NameInput.VSplitRight(84.0f * S, &NameInput, &CreateGen);
		NameInput.VSplitRight(6.0f * S, &NameInput, nullptr);
		CreateGen.VSplitLeft(6.0f * S, nullptr, &CreateGen);
		CreateKog.VSplitLeft(6.0f * S, nullptr, &CreateKog);
		s_ClanNameInput.SetEmptyText("Clan name");
		Ui()->DoEditBox(&s_ClanNameInput, &NameInput, 12.0f * S, IGraphics::CORNER_ALL, {}, 5.0f * S);
		if(DoButton_Menu(&s_ClanCreateGeneral, "General", 0, &CreateGen))
			GameClient()->m_AetherBadges.SendClanCreate(s_ClanNameInput.GetString(), "general");
		if(DoButton_Menu(&s_ClanCreateKog, "KoG", 0, &CreateKog))
			GameClient()->m_AetherBadges.SendClanCreate(s_ClanNameInput.GetString(), "kog");

		JoinCard = RenderFormCard(JoinCard, "Join clan");
		JoinCard.HSplitTop(24.0f * S, &Row, &JoinCard);
		CUIRect InviteInput, Join, Refresh;
		Row.VSplitRight(78.0f * S, &InviteInput, &Refresh);
		InviteInput.VSplitRight(70.0f * S, &InviteInput, &Join);
		InviteInput.VSplitRight(6.0f * S, &InviteInput, nullptr);
		Join.VSplitLeft(6.0f * S, nullptr, &Join);
		Refresh.VSplitLeft(6.0f * S, nullptr, &Refresh);
		s_ClanInviteInput.SetEmptyText("Invite code");
		Ui()->DoEditBox(&s_ClanInviteInput, &InviteInput, 12.0f * S, IGraphics::CORNER_ALL, {}, 5.0f * S);
		if(DoButton_Menu(&s_ClanJoin, "Join", 0, &Join))
			GameClient()->m_AetherBadges.SendClanJoin(s_ClanInviteInput.GetString());
		if(DoButton_Menu(&s_ClanRefresh, "Refresh", 0, &Refresh))
			GameClient()->m_AetherBadges.RefreshClan();

		Body.HSplitTop(12.0f * S, nullptr, &Body);
		CUIRect Cards, GeneralCard, KogCard;
		Body.HSplitTop(100.0f * S, &Cards, &Body);
		Cards.VSplitMid(&GeneralCard, &KogCard, 12.0f * S);
		RenderClanCard(GeneralCard, "General Clan", GeneralClan, false, s_ClanMembersGeneral, s_ClanCopyGeneralInvite, s_ClanLeaveGeneral, s_ClanRotateGeneral, s_ClanDisbandGeneral);
		RenderClanCard(KogCard, "KoG Clan", KogClan, true, s_ClanMembersKog, s_ClanCopyKogInvite, s_ClanLeaveKog, s_ClanRotateKog, s_ClanDisbandKog);

		if(GeneralClan.m_Valid)
		{
			Body.HSplitTop(8.0f * S, nullptr, &Body);
			Body.HSplitTop(24.0f * S, &Row, &Body);
			CUIRect Push, Pull, Copy;
			Row.VSplitLeft(126.0f * S, &Push, &Row);
			Row.VSplitLeft(8.0f * S, nullptr, &Row);
			Row.VSplitLeft(126.0f * S, &Pull, &Row);
			Row.VSplitLeft(8.0f * S, nullptr, &Row);
			Row.VSplitLeft(126.0f * S, &Copy, nullptr);
			if(DoButton_Menu(&s_ClanPushWarlist, "Push warlist", 0, &Push))
			{
				std::string Entries;
				if(GameClient()->m_WarList.ExportJsonEntries(Entries))
					GameClient()->m_AetherBadges.SendClanWarlistPush(Entries);
			}
			if(DoButton_Menu(&s_ClanPullWarlist, "Pull warlist", 0, &Pull))
				GameClient()->m_AetherBadges.SendClanWarlistPull();
			if(DoButton_Menu(&s_ClanCopyWarlist, "Copy warlist", 0, &Copy))
			{
				std::string Entries;
				if(GameClient()->m_WarList.ExportJsonEntries(Entries))
					Input()->SetClipboardText(Entries.c_str());
			}
		}
		return;
	}

	CUIRect SelectRow, GeneralSelect, KogSelect;
	Body.HSplitTop(26.0f * S, &SelectRow, &Body);
	SelectRow.VSplitLeft(150.0f * S, &GeneralSelect, &SelectRow);
	SelectRow.VSplitLeft(8.0f * S, nullptr, &SelectRow);
	SelectRow.VSplitLeft(150.0f * S, &KogSelect, &SelectRow);
	if(DoButton_Menu(&s_ClanSelectGeneral, "General members", s_SelectedClanType == 0, &GeneralSelect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		s_SelectedClanType = 0;
	if(DoButton_Menu(&s_ClanSelectKog, "KoG members", s_SelectedClanType == 1, &KogSelect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		s_SelectedClanType = 1;
	Body.HSplitTop(10.0f * S, nullptr, &Body);

	const auto &SelectedClan = s_SelectedClanType == 1 ? KogClan : GeneralClan;
	if(SelectedClan.m_Valid && !SelectedClan.m_MembersLoaded && ManagementReady && !GameClient()->m_AetherBadges.ClanRequestActive())
	{
		static int64_t s_aLastMembersAutoRequestTime[2] = {};
		const int TypeIndex = s_SelectedClanType == 1 ? 1 : 0;
		const int64_t Now = time_get();
		if(s_aLastMembersAutoRequestTime[TypeIndex] == 0 || Now - s_aLastMembersAutoRequestTime[TypeIndex] > 2 * time_freq())
		{
			GameClient()->m_AetherBadges.SendClanMembers(SelectedClan);
			s_aLastMembersAutoRequestTime[TypeIndex] = Now;
		}
	}
	Body.Draw(AetherPanelColor(0.22f), IGraphics::CORNER_ALL, 7.0f * S);
	Body.Margin(10.0f * S, &Body);
	Body.HSplitTop(24.0f * S, &Row, &Body);
	char aMembersTitle[160];
	str_format(aMembersTitle, sizeof(aMembersTitle), "%s members", SelectedClan.m_Valid ? SelectedClan.m_aName : (s_SelectedClanType == 1 ? "KoG clan" : "General clan"));
	Ui()->DoLabel(&Row, aMembersTitle, 15.0f * S, TEXTALIGN_ML);
	if(!SelectedClan.m_Valid)
	{
		Body.HSplitTop(28.0f * S, &Row, &Body);
		TextRender()->TextColor(0.68f, 0.75f, 0.84f, 1.0f);
		Ui()->DoLabel(&Row, "Join or create this clan type first.", 12.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
	const bool Owner = str_comp_nocase(SelectedClan.m_aRole, "owner") == 0;
	if(s_aPendingRemoveName[0] && str_comp(s_aPendingRemoveClanType, SelectedClan.m_aType) == 0)
	{
		Body.HSplitTop(30.0f * S, &Row, &Body);
		Row.Draw(ColorRGBA(0.34f, 0.12f, 0.10f, 0.72f), IGraphics::CORNER_ALL, 5.0f * S);
		Row.Margin(5.0f * S, &Row);
		CUIRect Text, Confirm, Cancel;
		Row.VSplitRight(78.0f * S, &Text, &Cancel);
		Text.VSplitRight(76.0f * S, &Text, &Confirm);
		Text.VSplitRight(8.0f * S, &Text, nullptr);
		Confirm.VSplitLeft(6.0f * S, nullptr, &Confirm);
		Cancel.VSplitLeft(6.0f * S, nullptr, &Cancel);
		char aConfirmText[160];
		str_format(aConfirmText, sizeof(aConfirmText), "Remove %s from clan?", s_aPendingRemoveName);
		TextRender()->TextColor(1.0f, 0.74f, 0.62f, 1.0f);
		Ui()->DoLabel(&Text, aConfirmText, 11.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		if(DoButton_Menu(&s_ClanConfirmRemove, "Remove", 0, &Confirm))
		{
			GameClient()->m_AetherBadges.SendClanMemberRemove(SelectedClan, s_aPendingRemoveName);
			s_aPendingRemoveName[0] = '\0';
		}
		if(DoButton_Menu(&s_ClanCancelRemove, "Cancel", 0, &Cancel))
			s_aPendingRemoveName[0] = '\0';
		Body.HSplitTop(8.0f * S, nullptr, &Body);
	}
	if(!SelectedClan.m_MembersLoaded)
	{
		Body.HSplitTop(28.0f * S, &Row, &Body);
		Ui()->DoLabel(&Row, ManagementReady ? "Loading members..." : "Clan management API unavailable.", 12.0f * S, TEXTALIGN_ML);
		return;
	}
	for(int i = 0; i < SelectedClan.m_MemberListCount; ++i)
	{
		const auto &Member = SelectedClan.m_aMembers[i];
		Body.HSplitTop(30.0f * S, &Row, &Body);
		Row.Draw(AetherPanelColor(i % 2 == 0 ? 0.20f : 0.14f), IGraphics::CORNER_ALL, 5.0f * S);
		Row.Margin(7.0f * S, &Row);
		CUIRect Name, Role, Date, Remove;
		Row.VSplitRight(76.0f * S, &Name, &Remove);
		Name.VSplitRight(120.0f * S, &Name, &Date);
		Name.VSplitRight(90.0f * S, &Name, &Role);
		Role.VSplitLeft(8.0f * S, nullptr, &Role);
		Date.VSplitLeft(8.0f * S, nullptr, &Date);
		TextRender()->TextColor(str_comp_nocase(Member.m_aRole, "owner") == 0 ? ColorRGBA(1.0f, 0.84f, 0.43f, 1.0f) : ColorRGBA(0.88f, 0.92f, 1.0f, 1.0f));
		Ui()->DoLabel(&Name, Member.m_aName, 12.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(0.66f, 0.74f, 0.84f, 1.0f);
		Ui()->DoLabel(&Role, Member.m_aRole, 10.5f * S, TEXTALIGN_ML);
		Ui()->DoLabel(&Date, Member.m_aJoinedAt[0] ? Member.m_aJoinedAt : "-", 9.5f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		const bool CanRemove = Owner && str_comp_nocase(Member.m_aRole, "owner") != 0 && ManagementReady;
		if(DoButton_Menu(&s_aClanRemoveMemberButtons[i], "Remove", CanRemove ? 0 : -1, &Remove) && CanRemove)
		{
			str_copy(s_aPendingRemoveName, Member.m_aName, sizeof(s_aPendingRemoveName));
			str_copy(s_aPendingRemoveClanType, SelectedClan.m_aType, sizeof(s_aPendingRemoveClanType));
		}
		Body.HSplitTop(5.0f * S, nullptr, &Body);
	}
}

void CMenus::RenderSettingsAetherChatBubbles(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeChatBubblesDuration, &g_Config.m_AeChatBubblesDuration, &Control, "Bubble duration", 2, 8, &CUi::ms_LinearScrollbarScale, 0, "s");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeChatBubblesOpacity, &g_Config.m_AeChatBubblesOpacity, &Control, "Bubble opacity", 20, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeChatBubblesWidth, &g_Config.m_AeChatBubblesWidth, &Control, "Bubble width", 80, 220);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeChatBubblesStackCount, &g_Config.m_AeChatBubblesStackCount, &Control, "Max stacked messages", 1, 4);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeChatBubblesVisibleOnly, "Only visible tees", g_Config.m_AeChatBubblesVisibleOnly, &Control))
		g_Config.m_AeChatBubblesVisibleOnly ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeChatBubblesColoredMessages, "Colored team/mention text", g_Config.m_AeChatBubblesColoredMessages, &Control))
		g_Config.m_AeChatBubblesColoredMessages ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeChatBubblesWhispers, "Show whisper bubbles", g_Config.m_AeChatBubblesWhispers, &Control))
		g_Config.m_AeChatBubblesWhispers ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeChatBubblesShowOwnLive, "Show my live draft", g_Config.m_AeChatBubblesShowOwnLive, &Control))
		g_Config.m_AeChatBubblesShowOwnLive ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeChatBubblesShowOwnSent, "Show my sent messages", g_Config.m_AeChatBubblesShowOwnSent, &Control))
		g_Config.m_AeChatBubblesShowOwnSent ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "Shows normal and team messages above player tees. Focus Mode hides bubbles.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherFastInput(CUIRect Body)
{
	static CButtonContainer s_ModeTClient;
	static CButtonContainer s_ModeAdaptive;
	static CButtonContainer s_ModeSaikoPlus;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(24.0f * S, &Control, &Body);
	if(g_Config.m_AeFastInputMode > 2)
		g_Config.m_AeFastInputMode = 1;
	const int ActiveMode = g_Config.m_AeFastInput ? (g_Config.m_AeFastInputMode == 2 ? 3 : 2) : (g_Config.m_TcFastInput ? 1 : 0);
	CUIRect B1, B2, B3, Rest;
	const float Spacing = 2.0f * S;
	const float SlotW = (Control.w - Spacing * 2.0f) / 3.0f;
	Control.VSplitLeft(SlotW, &B1, &Rest);
	Rest.VSplitLeft(Spacing, nullptr, &Rest);
	Rest.VSplitLeft(SlotW, &B2, &Rest);
	Rest.VSplitLeft(Spacing, nullptr, &Rest);
	Rest.VSplitLeft(SlotW, &B3, &Rest);

	if(DoButton_Menu(&s_ModeTClient, "TClient", ActiveMode == 1, &B1, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
	{
		g_Config.m_AeFastInput = 0;
		g_Config.m_TcFastInput = 1;
	}
	if(DoButton_Menu(&s_ModeAdaptive, "Adaptive", ActiveMode == 2, &B2, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
	{
		g_Config.m_AeFastInput = 1;
		g_Config.m_TcFastInput = 0;
		g_Config.m_AeFastInputMode = 1;
	}
	if(DoButton_Menu(&s_ModeSaikoPlus, "Saiko+", ActiveMode == 3, &B3, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
	{
		g_Config.m_AeFastInput = 1;
		g_Config.m_TcFastInput = 0;
		g_Config.m_AeFastInputMode = 2;
		if(g_Config.m_AeSaikoPlusAmount <= 0)
			g_Config.m_AeSaikoPlusAmount = std::clamp(g_Config.m_AeFastInputMovementAmount * 5, 0, 500);
	}

	Body.HSplitTop(8.0f * S, nullptr, &Body);

	if(ActiveMode == 1)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Control, "TClient amount", 1, 40, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_TcFastInputOthers, "Fast input others", g_Config.m_TcFastInputOthers, &Control))
			g_Config.m_TcFastInputOthers ^= 1;
	}
	else if(ActiveMode == 3)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Saiko+ amount: %.2f", std::clamp(g_Config.m_AeSaikoPlusAmount, 0, 500) / 100.0f);
		CUIRect Label, ScrollBar;
		Control.VSplitMid(&Label, &ScrollBar, minimum(10.0f, Control.w * 0.05f));
		Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
		const float Relative = std::clamp(g_Config.m_AeSaikoPlusAmount, 0, 500) / 500.0f;
		g_Config.m_AeSaikoPlusAmount = std::clamp(round_to_int(Ui()->DoScrollbarH(&g_Config.m_AeSaikoPlusAmount, &ScrollBar, Relative) * 500.0f), 0, 500);
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeSaikoPlusOthers, "Saiko+ input others", g_Config.m_AeSaikoPlusOthers, &Control))
			g_Config.m_AeSaikoPlusOthers ^= 1;
	}
	else if(ActiveMode == 2)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeFastInputMovementAmount, &g_Config.m_AeFastInputMovementAmount, &Control, "Movement amount", 0, 50, &CUi::ms_LinearScrollbarScale, 0, "ms");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeFastInputActionAmount, &g_Config.m_AeFastInputActionAmount, &Control, "Hook/fire amount", 0, 50, &CUi::ms_LinearScrollbarScale, 0, "ms");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeFastInputSmoothCorrections, &g_Config.m_AeFastInputSmoothCorrections, &Control, "Correction sharpness", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputBrakePriority, "Instant A/D response", g_Config.m_AeFastInputBrakePriority, &Control))
			g_Config.m_AeFastInputBrakePriority ^= 1;
		if(g_Config.m_AeFastInputBrakePriority)
		{
			Body.HSplitTop(22.0f * S, &Control, &Body);
			Ui()->DoScrollbarOption(&g_Config.m_AeFastInputBrakeAmount, &g_Config.m_AeFastInputBrakeAmount, &Control, "Brake amount", 0, 50, &CUi::ms_LinearScrollbarScale, 0, "ms");
		}
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputAdaptiveOthers, "Adaptive input other tees", g_Config.m_AeFastInputAdaptiveOthers, &Control))
			g_Config.m_AeFastInputAdaptiveOthers ^= 1;
		if(g_Config.m_AeFastInputAdaptiveOthers)
		{
			static CButtonContainer s_OthersSmooth;
			static CButtonContainer s_OthersPrecision;
			static CButtonContainer s_OthersAggressive;
			g_Config.m_AeFastInputAdaptiveOthersStyle = std::clamp(g_Config.m_AeFastInputAdaptiveOthersStyle, 0, 2);

			Body.HSplitTop(22.0f * S, &Control, &Body);
			CUIRect Label, Buttons;
			Control.VSplitMid(&Label, &Buttons, minimum(10.0f * S, Control.w * 0.05f));
			Ui()->DoLabel(&Label, "Other tees feel", Label.h * CUi::ms_FontmodHeight * 0.78f, TEXTALIGN_ML);

			CUIRect B0, B1, B2, Rest;
			const float FeelSpacing = 2.0f * S;
			const float FeelSlotW = (Buttons.w - FeelSpacing * 2.0f) / 3.0f;
			Buttons.VSplitLeft(FeelSlotW, &B0, &Rest);
			Rest.VSplitLeft(FeelSpacing, nullptr, &Rest);
			Rest.VSplitLeft(FeelSlotW, &B1, &Rest);
			Rest.VSplitLeft(FeelSpacing, nullptr, &Rest);
			B2 = Rest;
			if(DoButton_Menu(&s_OthersSmooth, "Smooth", g_Config.m_AeFastInputAdaptiveOthersStyle == 0, &B0, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_AeFastInputAdaptiveOthersStyle = 0;
			if(DoButton_Menu(&s_OthersPrecision, "Precision", g_Config.m_AeFastInputAdaptiveOthersStyle == 1, &B1, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_AeFastInputAdaptiveOthersStyle = 1;
			if(DoButton_Menu(&s_OthersAggressive, "Aggressive", g_Config.m_AeFastInputAdaptiveOthersStyle == 2, &B2, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_AeFastInputAdaptiveOthersStyle = 2;

			Body.HSplitTop(22.0f * S, &Control, &Body);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "Other tees amount: %.2f", std::clamp(g_Config.m_AeFastInputAdaptiveOthersAmount, 0, 500) / 100.0f);
			CUIRect AmountLabel, ScrollBar;
			Control.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f * S, Control.w * 0.05f));
			Ui()->DoLabel(&AmountLabel, aBuf, AmountLabel.h * CUi::ms_FontmodHeight * 0.78f, TEXTALIGN_ML);
			const float Relative = std::clamp(g_Config.m_AeFastInputAdaptiveOthersAmount, 0, 500) / 500.0f;
			g_Config.m_AeFastInputAdaptiveOthersAmount = std::clamp(round_to_int(Ui()->DoScrollbarH(&g_Config.m_AeFastInputAdaptiveOthersAmount, &ScrollBar, Relative) * 500.0f), 0, 500);
		}
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputPingAssist, "Ping assist", g_Config.m_AeFastInputPingAssist, &Control))
			g_Config.m_AeFastInputPingAssist ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputInteractionAssist, "Interaction assist", g_Config.m_AeFastInputInteractionAssist, &Control))
			g_Config.m_AeFastInputInteractionAssist ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeFastInputInteractionStrength, &g_Config.m_AeFastInputInteractionStrength, &Control, "Interaction strength", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputDummy, "Apply to dummy", g_Config.m_AeFastInputDummy, &Control))
			g_Config.m_AeFastInputDummy ^= 1;
	}

	if(ActiveMode != 0)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_ClSubTickAiming, "Sub-Tick aiming", g_Config.m_ClSubTickAiming, &Control))
			g_Config.m_ClSubTickAiming ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputAutoMargin, "Auto margin", g_Config.m_AeFastInputAutoMargin, &Control))
			g_Config.m_AeFastInputAutoMargin ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&g_Config.m_AeFastInputDebug, "Show debug info", g_Config.m_AeFastInputDebug, &Control))
			g_Config.m_AeFastInputDebug ^= 1;
		Body.HSplitTop(4.0f * S, nullptr, &Body);
		Body.HSplitTop(34.0f * S, &Control, &Body);
		TextRender()->TextColor(0.72f, 0.78f, 0.86f, 1.0f);
		Ui()->DoLabel(&Control, "Sub-Tick aiming samples the mouse target at hook/fire time. It improves aim timing, not movement latency.", 11.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CMenus::RenderSettingsAetherFastSpec(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFastSpec, "Enable Fast Spec", g_Config.m_AeFastSpec, &Control))
		g_Config.m_AeFastSpec ^= 1;

	Body.HSplitTop(18.0f * S, &Control, &Body);
	char aStatus[64];
	str_format(aStatus, sizeof(aStatus), "Status: %s", GameClient()->m_TClient.FastSpecStatus());
	TextRender()->TextColor(0.72f, 0.78f, 0.86f, 1.0f);
	Ui()->DoLabel(&Control, aStatus, 11.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Body.HSplitTop(6.0f * S, nullptr, &Body);
	static CButtonContainer s_FastSpecReaderButton;
	static CButtonContainer s_FastSpecClearButton;
	DoLine_KeyReader(Body, s_FastSpecReaderButton, s_FastSpecClearButton, "Fast Spec key", "+ae_fast_spec");

	Body.HSplitTop(18.0f * S, &Control, &Body);
	Ui()->DoLabel(&Control, "Grounded -> /spec -> /spec return.", 11.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherTranslator(CUIRect Body)
{
	static CLineInput s_TargetLanguageInput(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Row;
	Body.HSplitTop(24.0f * S, &Row, &Body);
	if(DoButton_CheckBox(&g_Config.m_TcTranslateAutoIncoming, "Auto translate incoming messages", g_Config.m_TcTranslateAutoIncoming, &Row))
		g_Config.m_TcTranslateAutoIncoming ^= 1;
	Body.HSplitTop(24.0f * S, &Row, &Body);
	if(DoButton_CheckBox(&g_Config.m_TcTranslateOutgoing, "Translate my messages before sending", g_Config.m_TcTranslateOutgoing, &Row))
		g_Config.m_TcTranslateOutgoing ^= 1;

	auto DoTextSetting = [&](const char *pLabel, CLineInput &Input) {
		CUIRect Label, Edit;
		Body.HSplitTop(26.0f * S, &Row, &Body);
		Row.VSplitLeft(142.0f * S, &Label, &Edit);
		Label.VSplitRight(8.0f * S, &Label, nullptr);
		Ui()->DoLabel(&Label, pLabel, 13.0f * S, TEXTALIGN_ML);
		Ui()->DoEditBox(&Input, &Edit, 13.0f * S, IGraphics::CORNER_ALL, {}, 5.0f * S);
	};
	DoTextSetting("Target language", s_TargetLanguageInput);

	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(34.0f * S, &Row, &Body);
	TextRender()->TextColor(0.72f, 0.78f, 0.86f, 1.0f);
	Ui()->DoLabel(&Row, "Manual !translate and optional outgoing chat use the embedded Google backend. Target examples: en, tr, de.", 11.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherFailSound(CUIRect Body)
{
	static CUi::SDropDownState s_LocalDropState;
	static CUi::SDropDownState s_OthersDropState;
	static CUi::SDropDownState s_TeamLastDropState;
	static CScrollRegion s_LocalDropScroll;
	static CScrollRegion s_OthersDropScroll;
	static CScrollRegion s_TeamLastDropScroll;
	static CButtonContainer s_OpenFolderButton;
	static CButtonContainer s_RefreshButton;
	static CButtonContainer s_LocalTestButton;
	static CButtonContainer s_OthersTestButton;
	static CButtonContainer s_TeamLastTestButton;
	static int s_LocalPreviewSample = -1;
	static int s_OthersPreviewSample = -1;
	static int s_TeamLastPreviewSample = -1;
	static char s_aLocalPreviewFile[128] = "";
	static char s_aOthersPreviewFile[128] = "";
	static char s_aTeamLastPreviewFile[128] = "";
	static bool s_ListDirty = true;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("assets/failsound", IStorage::TYPE_SAVE);

	static std::vector<std::string> s_vFailSoundLabels;
	static std::vector<const char *> s_vFailSoundPtrs;
	if(s_ListDirty || s_vFailSoundLabels.empty())
	{
		BuildAetherSoundFileList(Storage(), "failsound", s_vFailSoundLabels, s_vFailSoundPtrs);
		if(!AetherSoundSelectionExists(s_vFailSoundLabels, g_Config.m_AeFreezeFailSoundLocalFile))
		{
			g_Config.m_AeFreezeFailSoundLocalFile[0] = '\0';
			UnloadAetherPreviewSound(Sound(), s_LocalPreviewSample, s_aLocalPreviewFile, sizeof(s_aLocalPreviewFile));
		}
		if(!AetherSoundSelectionExists(s_vFailSoundLabels, g_Config.m_AeFreezeFailSoundOthersFile))
		{
			g_Config.m_AeFreezeFailSoundOthersFile[0] = '\0';
			UnloadAetherPreviewSound(Sound(), s_OthersPreviewSample, s_aOthersPreviewFile, sizeof(s_aOthersPreviewFile));
		}
		if(!AetherSoundSelectionExists(s_vFailSoundLabels, g_Config.m_AeFreezeFailSoundTeamLastFile))
		{
			g_Config.m_AeFreezeFailSoundTeamLastFile[0] = '\0';
			UnloadAetherPreviewSound(Sound(), s_TeamLastPreviewSample, s_aTeamLastPreviewFile, sizeof(s_aTeamLastPreviewFile));
		}
		s_ListDirty = false;
	}

	auto RenderFileDropdown = [&](CUIRect Row, const char *pLabel, char *pConfig, size_t ConfigSize, CUi::SDropDownState &State, CScrollRegion &Scroll, CButtonContainer &TestButton, int Volume, int &PreviewSample, char *pPreviewFile, int PreviewFileSize) {
		CUIRect Label, Drop, Test;
		Row.VSplitLeft(124.0f * S, &Label, &Drop);
		Drop.VSplitRight(58.0f * S, &Drop, &Test);
		Drop.VSplitRight(6.0f * S, &Drop, nullptr);
		Ui()->DoLabel(&Label, pLabel, 12.0f * S, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Scroll;
		const int Cur = SelectedSoundFileIndex(s_vFailSoundLabels, pConfig);
		const int New = Ui()->DoDropDown(&Drop, Cur, s_vFailSoundPtrs.data(), (int)s_vFailSoundPtrs.size(), State);
		if(New != Cur && New >= 0 && New < (int)s_vFailSoundLabels.size())
		{
			UnloadAetherPreviewSound(Sound(), PreviewSample, pPreviewFile, PreviewFileSize);
			if(New == 0)
				pConfig[0] = '\0';
			else
				str_copy(pConfig, s_vFailSoundLabels[New].c_str(), ConfigSize);
		}
		if(DoButton_Menu(&TestButton, "Test", 0, &Test))
			PreviewAetherUserSound(Storage(), Sound(), "failsound", pConfig, Volume, PreviewSample, pPreviewFile, PreviewFileSize, CSounds::CHN_GLOBAL);
	};

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFreezeFailSoundLocal, "Local tee and dummy", g_Config.m_AeFreezeFailSoundLocal, &Control))
		g_Config.m_AeFreezeFailSoundLocal ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	RenderFileDropdown(Control, "Local sound", g_Config.m_AeFreezeFailSoundLocalFile, sizeof(g_Config.m_AeFreezeFailSoundLocalFile), s_LocalDropState, s_LocalDropScroll, s_LocalTestButton, g_Config.m_AeFreezeFailSoundLocalVol, s_LocalPreviewSample, s_aLocalPreviewFile, sizeof(s_aLocalPreviewFile));
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeFreezeFailSoundLocalVol, &g_Config.m_AeFreezeFailSoundLocalVol, &Control, "Local volume", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFreezeFailSoundOthers, "Other tees in my DDNet team", g_Config.m_AeFreezeFailSoundOthers, &Control))
		g_Config.m_AeFreezeFailSoundOthers ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	RenderFileDropdown(Control, "Team sound", g_Config.m_AeFreezeFailSoundOthersFile, sizeof(g_Config.m_AeFreezeFailSoundOthersFile), s_OthersDropState, s_OthersDropScroll, s_OthersTestButton, g_Config.m_AeFreezeFailSoundOthersVol, s_OthersPreviewSample, s_aOthersPreviewFile, sizeof(s_aOthersPreviewFile));
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeFreezeFailSoundOthersVol, &g_Config.m_AeFreezeFailSoundOthersVol, &Control, "Team volume", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFreezeFailSoundTeamLast, "Last unfrozen tee warning", g_Config.m_AeFreezeFailSoundTeamLast, &Control))
		g_Config.m_AeFreezeFailSoundTeamLast ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	RenderFileDropdown(Control, "Last sound", g_Config.m_AeFreezeFailSoundTeamLastFile, sizeof(g_Config.m_AeFreezeFailSoundTeamLastFile), s_TeamLastDropState, s_TeamLastDropScroll, s_TeamLastTestButton, g_Config.m_AeFreezeFailSoundTeamLastVol, s_TeamLastPreviewSample, s_aTeamLastPreviewFile, sizeof(s_aTeamLastPreviewFile));
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeFreezeFailSoundTeamLastVol, &g_Config.m_AeFreezeFailSoundTeamLastVol, &Control, "Last volume", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	CUIRect Refresh, OpenFolder;
	Control.VSplitMid(&Refresh, &OpenFolder, 8.0f * S);
	if(DoButton_Menu(&s_RefreshButton, "Refresh", 0, &Refresh))
	{
		s_ListDirty = true;
		s_LocalDropState.m_Init = false;
		s_OthersDropState.m_Init = false;
		s_TeamLastDropState.m_Init = false;
		UnloadAetherPreviewSound(Sound(), s_LocalPreviewSample, s_aLocalPreviewFile, sizeof(s_aLocalPreviewFile));
		UnloadAetherPreviewSound(Sound(), s_OthersPreviewSample, s_aOthersPreviewFile, sizeof(s_aOthersPreviewFile));
		UnloadAetherPreviewSound(Sound(), s_TeamLastPreviewSample, s_aTeamLastPreviewFile, sizeof(s_aTeamLastPreviewFile));
	}
	if(DoButton_Menu(&s_OpenFolderButton, "Open folder", 0, &OpenFolder))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "assets/failsound", aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}

	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	char aHint[128];
	str_format(aHint, sizeof(aHint), "%d files | .mp3 .opus .wv | assets/failsound", maximum(0, (int)s_vFailSoundLabels.size() - 1));
	Ui()->DoLabel(&Control, aHint, 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherSound(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundLocalHook, "Local hook sounds", g_Config.m_AeSoundLocalHook, &Control))
		g_Config.m_AeSoundLocalHook ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundOthersHook, "Hook sounds from other players", g_Config.m_AeSoundOthersHook, &Control))
		g_Config.m_AeSoundOthersHook ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundOthersHammer, "Hammer sounds from other players", g_Config.m_AeSoundOthersHammer, &Control))
		g_Config.m_AeSoundOthersHammer ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundLocalHammer, "Local hammer sounds", g_Config.m_AeSoundLocalHammer, &Control))
		g_Config.m_AeSoundLocalHammer ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundWeaponSwitch, "Local weapon switch sounds", g_Config.m_AeSoundWeaponSwitch, &Control))
		g_Config.m_AeSoundWeaponSwitch ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundOthersWeaponSwitch, "Weapon switch sounds from other players", g_Config.m_AeSoundOthersWeaponSwitch, &Control))
		g_Config.m_AeSoundOthersWeaponSwitch ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundAirJump, "Double jump sounds", g_Config.m_AeSoundAirJump, &Control))
		g_Config.m_AeSoundAirJump ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundLocalKillRespawn, "Local kill/respawn sounds", g_Config.m_AeSoundLocalKillRespawn, &Control))
		g_Config.m_AeSoundLocalKillRespawn ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSoundOthersKillRespawn, "Kill/respawn sounds from other players", g_Config.m_AeSoundOthersKillRespawn, &Control))
		g_Config.m_AeSoundOthersKillRespawn ^= 1;
}

void CMenus::RenderSettingsAetherBlockAwareness(CUIRect Body)
{
	const float S = AetherSettingsScale();
	CUIRect Control;
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(8.0f * S, &Body);
	static std::array<CButtonContainer, 5> s_aTabs;
	static CButtonContainer s_ColorPlayers;
	static CButtonContainer s_AlliesKeepRealSkins;
	static CButtonContainer s_ColorNames;
	static CButtonContainer s_NeutralColorPlayers;
	static CButtonContainer s_NeutralColorNames;
	static CButtonContainer s_NeutralKeepRealSkins;
	static CButtonContainer s_EnemySize;
	static CButtonContainer s_LocalFreezeOverlay;
	static CButtonContainer s_AllyFreezeAlert;
	static CButtonContainer s_EnemyCountHud;
	static CButtonContainer s_HideEnemyEmotes;
	static CButtonContainer s_ForceEnemyEyes;
	static CButtonContainer s_EnemyColorReset;
	static CButtonContainer s_HelperColorReset;
	static CButtonContainer s_AllyColorReset;
	static CButtonContainer s_NeutralColorReset;
	static CButtonContainer s_WarlistExport;
	static CButtonContainer s_WarlistImport;
	static CButtonContainer s_WarlistRefresh;
	static CButtonContainer s_WarlistOpenFolder;
	static std::array<CButtonContainer, 5> s_aWarlistFileButtons;
	static std::vector<std::string> s_vWarlistFiles;
	static bool s_WarlistListDirty = true;
	static int s_SelectedWarlistFile = -1;

	const char *apTabs[] = {"General", "Colors", "Scale", "Opacity", "Warlist"};
	CUIRect Tabs;
	Body.HSplitTop(24.0f * S, &Tabs, &Body);
	const float TabGap = 3.0f * S;
	const float TabW = (Tabs.w - TabGap * 4.0f) / 5.0f;
	for(int i = 0; i < 5; ++i)
	{
		CUIRect Tab;
		Tabs.VSplitLeft(TabW, &Tab, &Tabs);
		if(DoButton_Menu(&s_aTabs[i], apTabs[i], s_AetherBlockAwarenessTab == i, &Tab, BUTTONFLAG_LEFT, nullptr,
			   i == 0 ? IGraphics::CORNER_L : (i == 4 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE)))
			s_AetherBlockAwarenessTab = i;
		if(i != 4)
			Tabs.VSplitLeft(TabGap, nullptr, &Tabs);
	}
	s_AetherBlockAwarenessTab = std::clamp(s_AetherBlockAwarenessTab, 0, 4);
	Body.HSplitTop(8.0f * S, nullptr, &Body);

	if(s_AetherBlockAwarenessTab == 0)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_ColorPlayers, "Color player tees", g_Config.m_AeBlockColorPlayers, &Control))
			g_Config.m_AeBlockColorPlayers ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_AlliesKeepRealSkins, "Allies keep real skins", g_Config.m_AeBlockAlliesKeepRealSkins, &Control))
			g_Config.m_AeBlockAlliesKeepRealSkins ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_ColorNames, "Color nameplates and scoreboard", g_Config.m_AeBlockColorNames, &Control))
			g_Config.m_AeBlockColorNames ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_NeutralColorPlayers, "Color natural tees", g_Config.m_AeBlockNeutralColorPlayers, &Control))
			g_Config.m_AeBlockNeutralColorPlayers ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_NeutralKeepRealSkins, "Naturals keep real skins", g_Config.m_AeBlockNeutralKeepRealSkins, &Control))
			g_Config.m_AeBlockNeutralKeepRealSkins ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_NeutralColorNames, "Color natural names", g_Config.m_AeBlockNeutralColorNames, &Control))
			g_Config.m_AeBlockNeutralColorNames ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_EnemySize, "Enlarge enemies", g_Config.m_AeBlockEnemySize, &Control))
			g_Config.m_AeBlockEnemySize ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_LocalFreezeOverlay, "Large self freeze timer", g_Config.m_AeBlockLocalFreezeOverlay, &Control))
			g_Config.m_AeBlockLocalFreezeOverlay ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_AllyFreezeAlert, "Ally/helper freeze alert", g_Config.m_AeBlockAllyFreezeAlert, &Control))
			g_Config.m_AeBlockAllyFreezeAlert ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_EnemyCountHud, "Enemy count HUD", g_Config.m_AeBlockEnemyCountHud, &Control))
			g_Config.m_AeBlockEnemyCountHud ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_HideEnemyEmotes, "Hide enemy emotes", g_Config.m_AeBlockHideEnemyEmotes, &Control))
			g_Config.m_AeBlockHideEnemyEmotes ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		if(DoButton_CheckBox(&s_ForceEnemyEyes, "Force enemy eyes", g_Config.m_AeBlockForceEnemyEyes, &Control))
			g_Config.m_AeBlockForceEnemyEyes ^= 1;
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockEnemyScanBlocks, &g_Config.m_AeBlockEnemyScanBlocks, &Control, "Enemy scan range", 5, 80, &CUi::ms_LinearScrollbarScale, 0, " blocks");
		return;
	}

	if(s_AetherBlockAwarenessTab == 1)
	{
		DoLine_ColorPicker(&s_EnemyColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Enemy color", &g_Config.m_AeBlockEnemyColor, ColorRGBA(1.0f, 0.23f, 0.23f), false);
		DoLine_ColorPicker(&s_HelperColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Helper color", &g_Config.m_AeBlockHelperColor, ColorRGBA(1.0f, 0.88f, 0.38f), false);
		DoLine_ColorPicker(&s_AllyColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Ally color", &g_Config.m_AeBlockAllyColor, ColorRGBA(0.35f, 0.91f, 0.46f), false);
		DoLine_ColorPicker(&s_NeutralColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Natural color", &g_Config.m_AeBlockNeutralColor, ColorRGBA(0.85f, 0.85f, 0.85f), false);
		return;
	}

	if(s_AetherBlockAwarenessTab == 2)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockEnemyScale, &g_Config.m_AeBlockEnemyScale, &Control, "Enemy scale", 100, 150, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockHelperScale, &g_Config.m_AeBlockHelperScale, &Control, "Helper scale", 100, 150, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockAllyScale, &g_Config.m_AeBlockAllyScale, &Control, "Ally scale", 100, 150, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockNeutralScale, &g_Config.m_AeBlockNeutralScale, &Control, "Natural scale", 80, 150, &CUi::ms_LinearScrollbarScale, 0, "%");
		return;
	}

	if(s_AetherBlockAwarenessTab == 3)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockEnemyNameOpacity, &g_Config.m_AeBlockEnemyNameOpacity, &Control, "Enemy name opacity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockHelperNameOpacity, &g_Config.m_AeBlockHelperNameOpacity, &Control, "Helper name opacity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockAllyNameOpacity, &g_Config.m_AeBlockAllyNameOpacity, &Control, "Ally name opacity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeBlockNeutralNameOpacity, &g_Config.m_AeBlockNeutralNameOpacity, &Control, "Natural name opacity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		return;
	}

	if(s_WarlistListDirty)
	{
		Storage()->CreateFolder("aether", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(AETHER_WARLIST_DIR, IStorage::TYPE_SAVE);
		s_vWarlistFiles.clear();
		Storage()->ListDirectory(IStorage::TYPE_SAVE, AETHER_WARLIST_DIR, AetherWarlistJsonScan, &s_vWarlistFiles);
		std::sort(s_vWarlistFiles.begin(), s_vWarlistFiles.end());
		if(s_SelectedWarlistFile >= (int)s_vWarlistFiles.size())
			s_SelectedWarlistFile = -1;
		s_WarlistListDirty = false;
	}
	Body.HSplitTop(24.0f * S, &Control, &Body);
	CUIRect Export, Import, Refresh, Folder;
	const float BtnGap = 5.0f * S;
	const float BtnW = (Control.w - BtnGap * 3.0f) / 4.0f;
	Control.VSplitLeft(BtnW, &Export, &Control);
	Control.VSplitLeft(BtnGap, nullptr, &Control);
	Control.VSplitLeft(BtnW, &Import, &Control);
	Control.VSplitLeft(BtnGap, nullptr, &Control);
	Control.VSplitLeft(BtnW, &Refresh, &Control);
	Control.VSplitLeft(BtnGap, nullptr, &Control);
	Control.VSplitLeft(BtnW, &Folder, nullptr);
	if(DoButton_Menu(&s_WarlistExport, "Export warlist", 0, &Export))
	{
		char aTimestamp[64];
		str_timestamp(aTimestamp, sizeof(aTimestamp));
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/warlist_%s.json", AETHER_WARLIST_DIR, aTimestamp);
		if(GameClient()->m_WarList.ExportJson(aPath))
		{
			GameClient()->Echo("Warlist exported");
			s_WarlistListDirty = true;
		}
		else
			GameClient()->Echo("Warlist export failed");
	}
	if(DoButton_Menu(&s_WarlistImport, "Import selected", 0, &Import))
	{
		if(s_SelectedWarlistFile < 0 || s_SelectedWarlistFile >= (int)s_vWarlistFiles.size())
		{
			GameClient()->Echo("Select a warlist JSON first");
		}
		else
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s", AETHER_WARLIST_DIR, s_vWarlistFiles[s_SelectedWarlistFile].c_str());
			char aError[256] = "";
			if(GameClient()->m_WarList.ImportJson(aPath, aError, sizeof(aError)))
				GameClient()->Echo(aError[0] ? aError : "Warlist imported");
			else
				GameClient()->Echo(aError[0] ? aError : "Warlist import failed");
		}
	}
	if(DoButton_Menu(&s_WarlistRefresh, "Refresh", 0, &Refresh))
		s_WarlistListDirty = true;
	if(DoButton_Menu(&s_WarlistOpenFolder, "Open folder", 0, &Folder))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		Storage()->CreateFolder("aether", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(AETHER_WARLIST_DIR, IStorage::TYPE_SAVE);
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, AETHER_WARLIST_DIR, aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}
	Body.HSplitTop(5.0f * S, nullptr, &Body);
	const int VisibleFiles = std::min((int)s_vWarlistFiles.size(), (int)s_aWarlistFileButtons.size());
	for(int i = 0; i < VisibleFiles; ++i)
	{
		CUIRect Row;
		Body.HSplitTop(20.0f * S, &Row, &Body);
		if(DoButton_Menu(&s_aWarlistFileButtons[i], s_vWarlistFiles[i].c_str(), s_SelectedWarlistFile == i, &Row))
			s_SelectedWarlistFile = i;
	}
	if(VisibleFiles == 0)
	{
		CUIRect Row;
		Body.HSplitTop(18.0f * S, &Row, &Body);
		TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
		Ui()->DoLabel(&Row, "No JSON files.", 12.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CMenus::RenderSettingsAetherKeystrokes(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeKeystrokesHorizontal, "Horizontal layout", g_Config.m_AeKeystrokesHorizontal, &Control))
		g_Config.m_AeKeystrokesHorizontal ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeKeystrokesShowJump, "Show jump key", g_Config.m_AeKeystrokesShowJump, &Control))
		g_Config.m_AeKeystrokesShowJump ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeKeystrokesShowFire, "Show M1 key", g_Config.m_AeKeystrokesShowFire, &Control))
		g_Config.m_AeKeystrokesShowFire ^= 1;

	Body.HSplitTop(24.0f * S, &Control, &Body);
	CUIRect Label, Buttons, Button;
	AetherOptionRow(Control, S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Jump label", 14.0f * S, TEXTALIGN_ML);
	const float ButtonWidth = Buttons.w / 2.0f;
	static CButtonContainer s_aJumpLabelButtons[2];
	Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
	if(DoButton_Menu(&s_aJumpLabelButtons[0], "Line", g_Config.m_AeKeystrokesJumpLabel == 1, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		g_Config.m_AeKeystrokesJumpLabel = 1;
	Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
	if(DoButton_Menu(&s_aJumpLabelButtons[1], "Jump", g_Config.m_AeKeystrokesJumpLabel == 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		g_Config.m_AeKeystrokesJumpLabel = 0;
}

void CMenus::RenderSettingsAetherKeyboardSound(CUIRect Body)
{
	static CUi::SDropDownState s_KeyboardDropState;
	static CScrollRegion s_KeyboardDropScroll;
	static CButtonContainer s_KeyboardRefreshButton;
	static CButtonContainer s_KeyboardOpenFolderButton;
	static CButtonContainer s_KeyboardTestButton;
	static int s_KeyboardPreviewSample = -1;
	static char s_aKeyboardPreviewFile[128] = "";
	static bool s_ListDirty = true;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("assets/keyboard", IStorage::TYPE_SAVE);

	static std::vector<std::string> s_vKeyboardLabels;
	static std::vector<const char *> s_vKeyboardPtrs;
	if(s_ListDirty || s_vKeyboardLabels.empty())
	{
		BuildAetherSoundFileList(Storage(), "keyboard", s_vKeyboardLabels, s_vKeyboardPtrs);
		if(!AetherSoundSelectionExists(s_vKeyboardLabels, g_Config.m_AeKeyboardSoundFile))
		{
			g_Config.m_AeKeyboardSoundFile[0] = '\0';
			UnloadAetherPreviewSound(Sound(), s_KeyboardPreviewSample, s_aKeyboardPreviewFile, sizeof(s_aKeyboardPreviewFile));
		}
		s_ListDirty = false;
	}

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeKeyboardSound, "Keyboard typing sound", g_Config.m_AeKeyboardSound, &Control))
		g_Config.m_AeKeyboardSound ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	CUIRect Label, Drop, Test;
	Control.VSplitLeft(124.0f * S, &Label, &Drop);
	Drop.VSplitRight(58.0f * S, &Drop, &Test);
	Drop.VSplitRight(6.0f * S, &Drop, nullptr);
	Ui()->DoLabel(&Label, "Sound file", 12.0f * S, TEXTALIGN_ML);
	s_KeyboardDropState.m_SelectionPopupContext.m_pScrollRegion = &s_KeyboardDropScroll;
	const int CurKeyboard = SelectedSoundFileIndex(s_vKeyboardLabels, g_Config.m_AeKeyboardSoundFile);
	const int NewKeyboard = Ui()->DoDropDown(&Drop, CurKeyboard, s_vKeyboardPtrs.data(), (int)s_vKeyboardPtrs.size(), s_KeyboardDropState);
	if(NewKeyboard != CurKeyboard && NewKeyboard >= 0 && NewKeyboard < (int)s_vKeyboardLabels.size())
	{
		UnloadAetherPreviewSound(Sound(), s_KeyboardPreviewSample, s_aKeyboardPreviewFile, sizeof(s_aKeyboardPreviewFile));
		if(NewKeyboard == 0)
			g_Config.m_AeKeyboardSoundFile[0] = '\0';
		else
			str_copy(g_Config.m_AeKeyboardSoundFile, s_vKeyboardLabels[NewKeyboard].c_str(), sizeof(g_Config.m_AeKeyboardSoundFile));
	}
	if(DoButton_Menu(&s_KeyboardTestButton, "Test", 0, &Test))
		PreviewAetherUserSound(Storage(), Sound(), "keyboard", g_Config.m_AeKeyboardSoundFile, g_Config.m_AeKeyboardSoundVol, s_KeyboardPreviewSample, s_aKeyboardPreviewFile, sizeof(s_aKeyboardPreviewFile), CSounds::CHN_GUI);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeKeyboardSoundVol, &g_Config.m_AeKeyboardSoundVol, &Control, "Typing sound volume", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	CUIRect Refresh, OpenFolder;
	Control.VSplitMid(&Refresh, &OpenFolder, 8.0f * S);
	if(DoButton_Menu(&s_KeyboardRefreshButton, "Refresh", 0, &Refresh))
	{
		s_ListDirty = true;
		s_KeyboardDropState.m_Init = false;
		UnloadAetherPreviewSound(Sound(), s_KeyboardPreviewSample, s_aKeyboardPreviewFile, sizeof(s_aKeyboardPreviewFile));
	}
	if(DoButton_Menu(&s_KeyboardOpenFolderButton, "Open folder", 0, &OpenFolder))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "assets/keyboard", aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	char aHint[128];
	str_format(aHint, sizeof(aHint), "%d files | .mp3 .opus .wv | assets/keyboard", maximum(0, (int)s_vKeyboardLabels.size() - 1));
	Ui()->DoLabel(&Control, aHint, 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

bool CMenus::IsAetherAssetsEditorOpen() const
{
	return s_AetherAssetsEditorOpen;
}

void CMenus::RenderSettingsAetherInputVisualizer(CUIRect Body)
{
	static CButtonContainer s_aLaneColorReset[5];
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeInputVisualizerFlow, &g_Config.m_AeInputVisualizerFlow, &Control, "Flow speed", 40, 1000, &CUi::ms_LinearScrollbarScale, 0, "px/s");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerShowLocal, "Show local input", g_Config.m_AeInputVisualizerShowLocal, &Control))
		g_Config.m_AeInputVisualizerShowLocal ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerHookMarkers, "Hook markers", g_Config.m_AeInputVisualizerHookMarkers, &Control))
		g_Config.m_AeInputVisualizerHookMarkers ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerMouse, "Include mouse buttons", g_Config.m_AeInputVisualizerMouse, &Control))
		g_Config.m_AeInputVisualizerMouse ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerShowJump, "Show jump lane", g_Config.m_AeInputVisualizerShowJump, &Control))
		g_Config.m_AeInputVisualizerShowJump ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerShowFire, "Show M1 lane", g_Config.m_AeInputVisualizerShowFire, &Control))
		g_Config.m_AeInputVisualizerShowFire ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerLabels, "Show key names", g_Config.m_AeInputVisualizerLabels, &Control))
		g_Config.m_AeInputVisualizerLabels ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerSpectatedInput, "Use spectated/demo player input", g_Config.m_AeInputVisualizerSpectatedInput, &Control))
		g_Config.m_AeInputVisualizerSpectatedInput ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerVertical, "Vertical layout", g_Config.m_AeInputVisualizerVertical, &Control))
		g_Config.m_AeInputVisualizerVertical ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerBackground, "Panel background", g_Config.m_AeInputVisualizerBackground, &Control))
		g_Config.m_AeInputVisualizerBackground ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeInputVisualizerSharpCorners, "Sharp corners", g_Config.m_AeInputVisualizerSharpCorners, &Control))
		g_Config.m_AeInputVisualizerSharpCorners ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeInputVisualizerLength, &g_Config.m_AeInputVisualizerLength, &Control, "Panel length", 50, 250, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeInputVisualizerThickness, &g_Config.m_AeInputVisualizerThickness, &Control, "Lane thickness", 40, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeInputVisualizerOpacity, &g_Config.m_AeInputVisualizerOpacity, &Control, "Overlay opacity", 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "Lane colors", 13.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	DoLine_ColorPicker(&s_aLaneColorReset[0], 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Lane A", &g_Config.m_AeInputVisualizerColorLeft, ColorRGBA(0.90f, 0.78f, 0.22f), false);
	DoLine_ColorPicker(&s_aLaneColorReset[1], 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Lane D", &g_Config.m_AeInputVisualizerColorRight, ColorRGBA(0.25f, 0.51f, 0.90f), false);
	if(g_Config.m_AeInputVisualizerShowJump)
		DoLine_ColorPicker(&s_aLaneColorReset[2], 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Lane jump", &g_Config.m_AeInputVisualizerColorJump, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	if(g_Config.m_AeInputVisualizerMouse)
	{
		if(g_Config.m_AeInputVisualizerShowFire)
			DoLine_ColorPicker(&s_aLaneColorReset[3], 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Lane M1", &g_Config.m_AeInputVisualizerColorFire, ColorRGBA(0.31f, 0.78f, 0.47f), false);
		DoLine_ColorPicker(&s_aLaneColorReset[4], 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Lane M2", &g_Config.m_AeInputVisualizerColorHook, ColorRGBA(0.67f, 0.39f, 0.90f), false);
	}
}

void CMenus::RenderSettingsAetherStabilityTrainer(CUIRect Body)
{
	static CButtonContainer s_TrainerColorReset;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeStabilityTrainerShowBar, "Show velocity bar", g_Config.m_AeStabilityTrainerShowBar, &Control))
		g_Config.m_AeStabilityTrainerShowBar ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeStabilityTrainerSpectate, "Use spectated/demo player", g_Config.m_AeStabilityTrainerSpectate, &Control))
		g_Config.m_AeStabilityTrainerSpectate ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeStabilityTrainerColorize, "Colorize by quality", g_Config.m_AeStabilityTrainerColorize, &Control))
		g_Config.m_AeStabilityTrainerColorize ^= 1;
	if(!g_Config.m_AeStabilityTrainerColorize)
		DoLine_ColorPicker(&s_TrainerColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Trainer color", &g_Config.m_AeStabilityTrainerColor, ColorRGBA(0.94f, 0.88f, 0.31f), false);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeStabilityTrainerSharpCorners, "Sharp corners", g_Config.m_AeStabilityTrainerSharpCorners, &Control))
		g_Config.m_AeStabilityTrainerSharpCorners ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerAverageTicks, &g_Config.m_AeStabilityTrainerAverageTicks, &Control, "Average ticks", 1, 40);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerBarGlide, &g_Config.m_AeStabilityTrainerBarGlide, &Control, "Bar glide", 8, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerMinSpeed, &g_Config.m_AeStabilityTrainerMinSpeed, &Control, "Minimum speed", 0, 600, &CUi::ms_LinearScrollbarScale, 0, "x0.01");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerVelocityScale, &g_Config.m_AeStabilityTrainerVelocityScale, &Control, "Velocity scale", 50, 800, &CUi::ms_LinearScrollbarScale, 0, "x0.01");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerBarThickness, &g_Config.m_AeStabilityTrainerBarThickness, &Control, "Bar thickness", 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerTrackWidth, &g_Config.m_AeStabilityTrainerTrackWidth, &Control, "Track width", 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeStabilityTrainerBlockWidth, &g_Config.m_AeStabilityTrainerBlockWidth, &Control, "Block width", 40, 150, &CUi::ms_LinearScrollbarScale, 0, "%");
}

void CMenus::RenderSettingsAetherSessionStats(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSessionStatsShowTime, "Show session time", g_Config.m_AeSessionStatsShowTime, &Control))
		g_Config.m_AeSessionStatsShowTime ^= 1;
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "Shows only session time and personal deaths.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherNinjaTeePreview(CUIRect Body)
{
	static CButtonContainer s_BodyColorReset;
	static CButtonContainer s_FeetColorReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Preview, Settings;
	Body.VSplitLeft(120.0f * S, &Preview, &Settings);
	Settings.VSplitLeft(14.0f * S, nullptr, &Settings);

	CTeeRenderInfo TeeInfo;
	TeeInfo.Apply(GameClient()->m_Skins.Find("x_ninja"));
	ApplyAetherNinjaTeeColors(TeeInfo, g_Config.m_AeNinjaTeeBodyColor, g_Config.m_AeNinjaTeeFeetColor);
	TeeInfo.m_Size = 74.0f * S;

	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
	const vec2 RenderPos = Preview.Center() + vec2(0.0f, OffsetToMid.y);
	const vec2 Direction = normalize(vec2(Ui()->MousePos().x - RenderPos.x, maximum(Ui()->MousePos().y - RenderPos.y, 0.5f)));
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, Direction, RenderPos);

	DoLine_ColorPicker(&s_BodyColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Settings, "Ninja body", &g_Config.m_AeNinjaTeeBodyColor, ColorRGBA(0.96f, 0.92f, 0.22f), false);
	DoLine_ColorPicker(&s_FeetColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Settings, "Ninja feet", &g_Config.m_AeNinjaTeeFeetColor, ColorRGBA(0.96f, 0.92f, 0.22f), false);

	CUIRect Note;
	Settings.HSplitTop(18.0f * S, &Note, &Settings);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Note, "A clean Aether preview for tuning ninja tee colors.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherNinjaTimer(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeNinjaTimerScale, &g_Config.m_AeNinjaTimerScale, &Control, "HUD scale", 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeNinjaTimerOffsetY, &g_Config.m_AeNinjaTimerOffsetY, &Control, "Vertical offset", -1000, 1000);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "HUD Editor can move and resize this compact ninja timer.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherSweatWeapon(CUIRect Body)
{
	static CButtonContainer s_CrystalGlowReset;
	static CButtonContainer s_CrystalCoreReset;
	static CButtonContainer s_ShotgunGlowReset;
	static CButtonContainer s_ShotgunCoreReset;
	static CButtonContainer s_EntitiesGlowReset;
	static CButtonContainer s_EntitiesCoreReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSweatWeaponCustomColors, "Custom rifle/shotgun colors", g_Config.m_AeSweatWeaponCustomColors, &Control))
		g_Config.m_AeSweatWeaponCustomColors ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSweatWeaponShine, "Shine animation", g_Config.m_AeSweatWeaponShine, &Control))
		g_Config.m_AeSweatWeaponShine ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSweatWeaponElectric, "Electric arcs", g_Config.m_AeSweatWeaponElectric, &Control))
		g_Config.m_AeSweatWeaponElectric ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeSweatWeaponEntitiesLaser, "Entities laser override", g_Config.m_AeSweatWeaponEntitiesLaser, &Control))
		g_Config.m_AeSweatWeaponEntitiesLaser ^= 1;

	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponGlow, &g_Config.m_AeSweatWeaponGlow, &Control, "Glow strength", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponThickness, &g_Config.m_AeSweatWeaponThickness, &Control, "Laser thickness", 60, 300, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponSparkles, &g_Config.m_AeSweatWeaponSparkles, &Control, "Sparkle intensity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	if(g_Config.m_AeSweatWeaponElectric)
	{
		Body.HSplitTop(24.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponArcDensity, &g_Config.m_AeSweatWeaponArcDensity, &Control, "Electric arc density", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Body.HSplitTop(24.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponArcOpacity, &g_Config.m_AeSweatWeaponArcOpacity, &Control, "Electric arc opacity", 0, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(g_Config.m_AeSweatWeaponShine)
	{
		Body.HSplitTop(24.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeSweatWeaponShineSpeed, &g_Config.m_AeSweatWeaponShineSpeed, &Control, "Shine speed", 10, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
	}

	DoLine_ColorPicker(&s_CrystalGlowReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Crystal outline (glow)", &g_Config.m_AeSweatWeaponCrystalGlowColor, ColorRGBA(0.25f, 0.90f, 1.0f), false);
	DoLine_ColorPicker(&s_CrystalCoreReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Crystal inner (core)", &g_Config.m_AeSweatWeaponCrystalCoreColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	DoLine_ColorPicker(&s_ShotgunGlowReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Shotgun outline (glow)", &g_Config.m_AeSweatWeaponShotgunGlowColor, ColorRGBA(1.0f, 0.85f, 0.50f), false);
	DoLine_ColorPicker(&s_ShotgunCoreReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Shotgun inner (core)", &g_Config.m_AeSweatWeaponShotgunCoreColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	if(g_Config.m_AeSweatWeaponEntitiesLaser)
	{
		DoLine_ColorPicker(&s_EntitiesGlowReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Entities outline (glow)", &g_Config.m_AeSweatWeaponEntitiesGlowColor, ColorRGBA(0.72f, 0.42f, 1.0f), false);
		DoLine_ColorPicker(&s_EntitiesCoreReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Entities inner (core)", &g_Config.m_AeSweatWeaponEntitiesCoreColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	}

	CUIRect Label, Preview;
	Body.HSplitTop(20.0f * S, &Label, &Body);
	Ui()->DoLabel(&Label, "Crystal Laser", 13.0f * S, TEXTALIGN_ML);
	Body.HSplitTop(38.0f * S, &Preview, &Body);
	DoLaserPreview(&Preview, ColorHSLA(g_Config.m_AeSweatWeaponCrystalGlowColor), ColorHSLA(g_Config.m_AeSweatWeaponCrystalCoreColor), LASERTYPE_RIFLE);
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Label, &Body);
	Ui()->DoLabel(&Label, "Sand Shotgun", 13.0f * S, TEXTALIGN_ML);
	Body.HSplitTop(38.0f * S, &Preview, &Body);
	DoLaserPreview(&Preview, ColorHSLA(g_Config.m_AeSweatWeaponShotgunGlowColor), ColorHSLA(g_Config.m_AeSweatWeaponShotgunCoreColor), LASERTYPE_SHOTGUN);
	if(g_Config.m_AeSweatWeaponEntitiesLaser)
	{
		Body.HSplitTop(8.0f * S, nullptr, &Body);
		Body.HSplitTop(20.0f * S, &Label, &Body);
		Ui()->DoLabel(&Label, "Entities Laser", 13.0f * S, TEXTALIGN_ML);
		Body.HSplitTop(38.0f * S, &Preview, &Body);
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_AeSweatWeaponEntitiesGlowColor), ColorHSLA(g_Config.m_AeSweatWeaponEntitiesCoreColor), LASERTYPE_FREEZE);
	}
}

void CMenus::RenderSettingsAetherOrbitAura(CUIRect Body)
{
	static CButtonContainer s_StyleDropdown;
	static CButtonContainer s_aStyleOptions[3];
	static CButtonContainer s_AuraColorReset;
	static CButtonContainer s_AccentColorReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOrbitAuraIdleOnly, "Enable in idle mode only", g_Config.m_AeOrbitAuraIdleOnly, &Control))
		g_Config.m_AeOrbitAuraIdleOnly ^= 1;
	if(g_Config.m_AeOrbitAuraIdleOnly)
	{
		Body.HSplitTop(24.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraIdleDelay, &g_Config.m_AeOrbitAuraIdleDelay, &Control, "Idle delay", 0, 3000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraFadeMs, &g_Config.m_AeOrbitAuraFadeMs, &Control, "Fade duration", 0, 1500, &CUi::ms_LinearScrollbarScale, 0, "ms");

	Body.HSplitTop(28.0f * S, &Control, &Body);
	CUIRect Label, StyleRow;
	AetherOptionRow(Control, S, &Label, &StyleRow);
	Ui()->DoLabel(&Label, "Aura style", 13.0f * S, TEXTALIGN_ML);
	const char *apStyles[] = {"Orbit", "Ring", "Flame"};
	const int Style = std::clamp(g_Config.m_AeOrbitAuraStyle, 0, 2);
	char aStyleButton[32];
	str_format(aStyleButton, sizeof(aStyleButton), "%s  v", apStyles[Style]);
	if(DoButton_Menu(&s_StyleDropdown, aStyleButton, s_AetherOrbitStyleDropdownOpen, &StyleRow))
		s_AetherOrbitStyleDropdownOpen = !s_AetherOrbitStyleDropdownOpen;
	if(s_AetherOrbitStyleDropdownOpen)
	{
		Body.HSplitTop(4.0f * S, nullptr, &Body);
		for(int i = 0; i < 3; ++i)
		{
			Body.HSplitTop(23.0f * S, &Control, &Body);
			CUIRect OptionButton;
			AetherOptionRow(Control, S, nullptr, &OptionButton);
			if(DoButton_Menu(&s_aStyleOptions[i], apStyles[i], g_Config.m_AeOrbitAuraStyle == i, &OptionButton))
			{
				g_Config.m_AeOrbitAuraStyle = i;
				s_AetherOrbitStyleDropdownOpen = false;
			}
		}
	}

	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraRadius, &g_Config.m_AeOrbitAuraRadius, &Control, "Aura radius", 24, 140);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraParticles, &g_Config.m_AeOrbitAuraParticles, &Control, "Particles", 6, 96);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraAlpha, &g_Config.m_AeOrbitAuraAlpha, &Control, "Aura alpha", 5, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraSpeed, &g_Config.m_AeOrbitAuraSpeed, &Control, "Aura speed", 10, 250, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOrbitAuraSize, &g_Config.m_AeOrbitAuraSize, &Control, "Particle size", 40, 220, &CUi::ms_LinearScrollbarScale, 0, "%");

	DoLine_ColorPicker(&s_AuraColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Aura color", &g_Config.m_AeOrbitAuraColor, ColorRGBA(0.36f, 1.0f, 0.82f), false);
	DoLine_ColorPicker(&s_AccentColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Accent color", &g_Config.m_AeOrbitAuraAccentColor, ColorRGBA(0.71f, 1.0f, 0.94f), false);
}

void CMenus::RenderSettingsAetherJellyTee(CUIRect Body)
{
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeJellyTeeOthers, "Jelly others", g_Config.m_AeJellyTeeOthers, &Control))
		g_Config.m_AeJellyTeeOthers ^= 1;
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeJellyTeeStrength, &g_Config.m_AeJellyTeeStrength, &Control, "Jelly strength", 0, 1000);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeJellyTeeDuration, &g_Config.m_AeJellyTeeDuration, &Control, "Jelly duration", 20, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
}

void CMenus::RenderSettingsAetherFinishPrediction(CUIRect Body)
{
	static CButtonContainer s_aModeButtons[2];
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFinishPredictionShowTime, "Show time", g_Config.m_AeFinishPredictionShowTime, &Control))
		g_Config.m_AeFinishPredictionShowTime ^= 1;

	Body.HSplitTop(26.0f * S, &Control, &Body);
	CUIRect Label, Buttons;
	AetherOptionRow(Control, S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Time mode", 13.0f * S, TEXTALIGN_ML);
	const char *apModes[] = {"Time left", "Finish time"};
	for(int i = 0; i < 2; ++i)
	{
		CUIRect Button;
		const float ButtonWidth = Buttons.w / (2 - i);
		Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
		if(DoButton_Menu(&s_aModeButtons[i], apModes[i], g_Config.m_AeFinishPredictionMode == i, &Button, BUTTONFLAG_LEFT, nullptr, i == 0 ? IGraphics::CORNER_L : IGraphics::CORNER_R))
			g_Config.m_AeFinishPredictionMode = i;
	}

	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFinishPredictionMilliseconds, "Show milliseconds", g_Config.m_AeFinishPredictionMilliseconds, &Control))
		g_Config.m_AeFinishPredictionMilliseconds ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFinishPredictionPercentage, "Show percentage", g_Config.m_AeFinishPredictionPercentage, &Control))
		g_Config.m_AeFinishPredictionPercentage ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeFinishPredictionShowAlways, "Show always", g_Config.m_AeFinishPredictionShowAlways, &Control))
		g_Config.m_AeFinishPredictionShowAlways ^= 1;

	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "First version estimates progress from your race start and map travel.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherThreeDParticles(CUIRect Body)
{
	static CButtonContainer s_aTypeButtons[3];
	static CButtonContainer s_aColorModeButtons[2];
	static CButtonContainer s_ColorReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_Ae3DParticlesCount, &g_Config.m_Ae3DParticlesCount, &Control, "Particles count", 0, 180);

	Body.HSplitTop(28.0f * S, &Control, &Body);
	CUIRect Label, Buttons, Button;
	AetherOptionRow(Control, S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Particle type", 13.0f * S, TEXTALIGN_ML);
	const char *apTypes[] = {"Cube", "Heart", "Mixed"};
	for(int i = 0; i < 3; ++i)
	{
		const float ButtonWidth = Buttons.w / (3 - i);
		Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
		if(DoButton_Menu(&s_aTypeButtons[i], apTypes[i], g_Config.m_Ae3DParticlesType == i, &Button, BUTTONFLAG_LEFT, nullptr, i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : 0)))
			g_Config.m_Ae3DParticlesType = i;
	}
	Body.HSplitTop(4.0f * S, nullptr, &Body);

	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_Ae3DParticlesSize, &g_Config.m_Ae3DParticlesSize, &Control, "Size", 2, 28);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_Ae3DParticlesSpeed, &g_Config.m_Ae3DParticlesSpeed, &Control, "Speed", 0, 120);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_Ae3DParticlesRotationSpeed, &g_Config.m_Ae3DParticlesRotationSpeed, &Control, "Rotation speed", 0, 120);
	Body.HSplitTop(24.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_Ae3DParticlesAlpha, &g_Config.m_Ae3DParticlesAlpha, &Control, "Alpha", 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Body.HSplitTop(28.0f * S, &Control, &Body);
	AetherOptionRow(Control, S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Color mode", 13.0f * S, TEXTALIGN_ML);
	const char *apModes[] = {"Custom", "Random"};
	for(int i = 0; i < 2; ++i)
	{
		const float ButtonWidth = Buttons.w / (2 - i);
		Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
		if(DoButton_Menu(&s_aColorModeButtons[i], apModes[i], g_Config.m_Ae3DParticlesColorMode == i, &Button, BUTTONFLAG_LEFT, nullptr, i == 0 ? IGraphics::CORNER_L : IGraphics::CORNER_R))
			g_Config.m_Ae3DParticlesColorMode = i;
	}
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	if(g_Config.m_Ae3DParticlesColorMode == 0)
		DoLine_ColorPicker(&s_ColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Color", &g_Config.m_Ae3DParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_Ae3DParticlesGlow, "Glow", g_Config.m_Ae3DParticlesGlow, &Control))
		g_Config.m_Ae3DParticlesGlow ^= 1;
}

void CMenus::RenderSettingsAetherRealHitbox(CUIRect Body)
{
	static CButtonContainer s_HitboxColorReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	DoLine_ColorPicker(&s_HitboxColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Hitbox color", &g_Config.m_AeRealHitboxColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	CUIRect Note;
	Body.HSplitTop(18.0f * S, &Note, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Note, "Draws a full-opacity local tee hitbox marker.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherBrowserUtils(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeBrowserAutoRefresh, "Auto refresh server list", g_Config.m_AeBrowserAutoRefresh, &Control))
		g_Config.m_AeBrowserAutoRefresh ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeBrowserRefreshSeconds, &g_Config.m_AeBrowserRefreshSeconds, &Control, "Refresh interval", 15, 120, &CUi::ms_LinearScrollbarScale, 0, "s");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeBrowserShortKoGNames, "Shorten KoG server names", g_Config.m_AeBrowserShortKoGNames, &Control))
		g_Config.m_AeBrowserShortKoGNames ^= 1;
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "Example: KoG GER4 Insane becomes GER4 - Insane.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherUiScale(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeSettingsScale, &g_Config.m_AeSettingsScale, &Control, "Aether settings page size", 80, 140, &CUi::ms_LinearScrollbarScale, 0, "%");
}

void CMenus::RenderSettingsAetherGradientTeamColors(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	static CButtonContainer s_TeamToggle;
	static CButtonContainer s_NicknameToggle;
	static CButtonContainer s_SparkleToggle;
	static CButtonContainer s_AnimatedToggle;
	static CButtonContainer s_StartColorReset;
	static CButtonContainer s_EndColorReset;
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&s_TeamToggle, "Team background gradient (local)", g_Config.m_AeGradientTeamColors, &Control))
		g_Config.m_AeGradientTeamColors ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&s_NicknameToggle, "Nickname gradient", g_Config.m_AeGradientNicknames, &Control))
		g_Config.m_AeGradientNicknames ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&s_SparkleToggle, "Sparkle effect", g_Config.m_AeGradientNicknameStyle == 2, &Control))
		g_Config.m_AeGradientNicknameStyle = g_Config.m_AeGradientNicknameStyle == 2 ? 0 : 2;
	if(g_Config.m_AeGradientNicknameStyle != 2)
		g_Config.m_AeGradientNicknameStyle = 0;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	DoLine_ColorPicker(&s_StartColorReset, 22.0f * S, 12.0f * S, 3.0f * S, &Body, "Nickname start", &g_Config.m_AeGradientNicknameStartColor, ColorRGBA(0.39f, 0.78f, 1.0f), false);
	DoLine_ColorPicker(&s_EndColorReset, 22.0f * S, 12.0f * S, 3.0f * S, &Body, "Nickname end", &g_Config.m_AeGradientNicknameEndColor, ColorRGBA(1.0f, 0.48f, 0.85f), false);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeGradientNicknameGlow, &g_Config.m_AeGradientNicknameGlow, &Control, "Glow intensity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&s_AnimatedToggle, "Animate nickname blend", g_Config.m_AeGradientNicknameAnimated, &Control))
		g_Config.m_AeGradientNicknameAnimated ^= 1;
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(20.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeGradientNicknameSpeed, &g_Config.m_AeGradientNicknameSpeed, &Control, "Animation speed", 1, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
}

void CMenus::RenderSettingsAetherCustomResolution(CUIRect Body)
{
	static CLineInputBuffered<16> s_WidthInput;
	static CLineInputBuffered<16> s_HeightInput;
	static bool s_InputsInitialized = false;
	static bool s_WidthWasActive = false;
	static bool s_HeightWasActive = false;
	const float S = AetherSettingsScale();

	if(!s_InputsInitialized)
	{
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionWidth);
		s_WidthInput.Set(aBuf);
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionHeight);
		s_HeightInput.Set(aBuf);
		s_InputsInitialized = true;
	}

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	auto ApplyInputs = [&]() {
		int Width = 0;
		if(str_toint(s_WidthInput.GetString(), &Width))
			g_Config.m_AeCustomResolutionWidth = std::clamp(Width, 640, 7680);
		int Height = 0;
		if(str_toint(s_HeightInput.GetString(), &Height))
			g_Config.m_AeCustomResolutionHeight = std::clamp(Height, 360, 4320);
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionWidth);
		s_WidthInput.Set(aBuf);
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionHeight);
		s_HeightInput.Set(aBuf);
	};

	CUIRect Row, Label, EditBox;
	Body.HSplitTop(24.0f * S, &Row, &Body);
	Row.VSplitLeft(120.0f * S, &Label, &EditBox);
	Ui()->DoLabel(&Label, "Width", 14.0f * S, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_WidthInput, &EditBox, 14.0f * S, IGraphics::CORNER_ALL, {}, 6.0f * S);
	const bool WidthActive = s_WidthInput.IsActive();
	int Width = 0;
	if(s_WidthWasActive && !WidthActive && str_toint(s_WidthInput.GetString(), &Width))
	{
		g_Config.m_AeCustomResolutionWidth = std::clamp(Width, 640, 7680);
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionWidth);
		s_WidthInput.Set(aBuf);
	}
	s_WidthWasActive = WidthActive;

	Body.HSplitTop(6.0f * S, nullptr, &Body);
	Body.HSplitTop(24.0f * S, &Row, &Body);
	Row.VSplitLeft(120.0f * S, &Label, &EditBox);
	Ui()->DoLabel(&Label, "Height", 14.0f * S, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_HeightInput, &EditBox, 14.0f * S, IGraphics::CORNER_ALL, {}, 6.0f * S);
	const bool HeightActive = s_HeightInput.IsActive();
	int Height = 0;
	if(s_HeightWasActive && !HeightActive && str_toint(s_HeightInput.GetString(), &Height))
	{
		g_Config.m_AeCustomResolutionHeight = std::clamp(Height, 360, 4320);
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_AeCustomResolutionHeight);
		s_HeightInput.Set(aBuf);
	}
	s_HeightWasActive = HeightActive;

	if((WidthActive || HeightActive) && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
	{
		ApplyInputs();
		s_WidthInput.Deactivate();
		s_HeightInput.Deactivate();
		s_WidthWasActive = false;
		s_HeightWasActive = false;
	}

	Body.HSplitTop(18.0f * S, &Row, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Row, "Updates after you finish editing a field.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherOptimizer(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerHighPriority, "High process priority", g_Config.m_AeOptimizerHighPriority, &Control))
		g_Config.m_AeOptimizerHighPriority ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerDiscordBelowNormal, "Discord priority: Below Normal", g_Config.m_AeOptimizerDiscordBelowNormal, &Control))
		g_Config.m_AeOptimizerDiscordBelowNormal ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerDisableParticles, "Disable in-game particles", g_Config.m_AeOptimizerDisableParticles, &Control))
		g_Config.m_AeOptimizerDisableParticles ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_GfxHighDetail, "High detail map render", g_Config.m_GfxHighDetail, &Control))
		g_Config.m_GfxHighDetail ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerDisableMenuAnimations, "Disable menu animations", g_Config.m_AeOptimizerDisableMenuAnimations, &Control))
		g_Config.m_AeOptimizerDisableMenuAnimations ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerFpsFog, "FPS fog", g_Config.m_AeOptimizerFpsFog, &Control))
		g_Config.m_AeOptimizerFpsFog ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerFpsFogCullTiles, "Cull map tiles outside FPS fog", g_Config.m_AeOptimizerFpsFogCullTiles, &Control))
		g_Config.m_AeOptimizerFpsFogCullTiles ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeOptimizerFpsFogRenderRect, "Render FPS fog rectangle", g_Config.m_AeOptimizerFpsFogRenderRect, &Control))
		g_Config.m_AeOptimizerFpsFogRenderRect ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeOptimizerFpsFogRadius, &g_Config.m_AeOptimizerFpsFogRadius, &Control, "Radius (tiles)", 6, 160);
}

void CMenus::RenderSettingsAetherRollbackDemo(CUIRect Body)
{
	static CButtonContainer s_RollbackReaderButton;
	static CButtonContainer s_RollbackClearButton;

	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeRollbackDemoSeconds, &g_Config.m_AeRollbackDemoSeconds, &Control, "Clip length", 10, 600, &CUi::ms_LinearScrollbarScale, 0, "s");
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	DoLine_KeyReader(Body, s_RollbackReaderButton, s_RollbackClearButton, "Save rollback clip key", "ae_save_rollback_demo");
	Body.HSplitTop(4.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "The key saves the configured number of previous seconds.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherAimTraining(CUIRect Body)
{
	static CButtonContainer s_StartButton;
	static CButtonContainer s_TargetColorReset;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Control;
	Body.HSplitTop(24.0f * S, &Control, &Body);
	if(DoButton_Menu(&s_StartButton, "Start / arm session", 0, &Control))
		GameClient()->m_AetherAimTraining.Restart();
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingTargets, &g_Config.m_AeAimTrainingTargets, &Control, "Targets", 1, 8);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingTargetSize, &g_Config.m_AeAimTrainingTargetSize, &Control, "Target size", 8, 96);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingDistance, &g_Config.m_AeAimTrainingDistance, &Control, "Target distance", 64, 560);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingDim, &g_Config.m_AeAimTrainingDim, &Control, "Background dim", 0, 75, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoLine_ColorPicker(&s_TargetColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Target color", &g_Config.m_AeAimTrainingTargetColor, ColorRGBA(0.35f, 0.78f, 1.0f), false);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeAimTrainingShrink, "Shrink targets", g_Config.m_AeAimTrainingShrink, &Control))
		g_Config.m_AeAimTrainingShrink ^= 1;
	if(g_Config.m_AeAimTrainingShrink)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingShrinkMs, &g_Config.m_AeAimTrainingShrinkMs, &Control, "Shrink duration", 150, 10000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeAimTrainingDespawn, "Respawn missed targets", g_Config.m_AeAimTrainingDespawn, &Control))
		g_Config.m_AeAimTrainingDespawn ^= 1;
	if(g_Config.m_AeAimTrainingDespawn)
	{
		Body.HSplitTop(22.0f * S, &Control, &Body);
		Ui()->DoScrollbarOption(&g_Config.m_AeAimTrainingDespawnMs, &g_Config.m_AeAimTrainingDespawnMs, &Control, "Respawn delay", 250, 10000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Control, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Control, "After Start, close settings and click in game. A 3 second countdown begins; Esc cancels only after training starts.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherPsa(CUIRect Body)
{
	static CLineInputBuffered<16> s_BaseInput;
	static bool s_BaseInitialized = false;
	static int s_TimerSeconds = 300;
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	if(!s_BaseInitialized || (!s_BaseInput.IsActive() && !GameClient()->m_AetherPsa.IsActive() && !GameClient()->m_AetherPsa.AutoInProgress()))
	{
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", maximum(1, g_Config.m_InpMousesens));
		s_BaseInput.Set(aBuf);
		s_BaseInitialized = true;
	}

	CUIRect Row, Label, Field;
	Body.HSplitTop(24.0f * S, &Row, &Body);
	Row.VSplitLeft(150.0f * S, &Label, &Field);
	Ui()->DoLabel(&Label, "Base value", 14.0f * S, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_BaseInput, &Field, 14.0f * S, IGraphics::CORNER_ALL, {}, 6.0f * S);
	int BaseValue = maximum(1, g_Config.m_InpMousesens);
	str_toint(s_BaseInput.GetString(), &BaseValue);
	BaseValue = std::clamp(BaseValue, 1, 1000000);

	Body.HSplitTop(24.0f * S, &Row, &Body);
	Ui()->DoScrollbarOption(&s_TimerSeconds, &s_TimerSeconds, &Row, "Trial duration", 5, 900, &CUi::ms_LinearScrollbarScale, 0, "s");

	Body.HSplitTop(6.0f * S, nullptr, &Body);
	Body.HSplitTop(24.0f * S, &Row, &Body);
	CUIRect BtnStart, BtnReset;
	const float ButtonGap = 12.0f * S;
	const float ButtonW = (Row.w - ButtonGap) / 2.0f;
	Row.VSplitLeft(ButtonW, &BtnStart, &Row);
	Row.VSplitLeft(ButtonGap, nullptr, &Row);
	Row.VSplitLeft(ButtonW, &BtnReset, nullptr);
	static CButtonContainer s_StartButton;
	static CButtonContainer s_ResetButton;
	if(DoButton_Menu(&s_StartButton, "Start PSA", 0, &BtnStart))
	{
		GameClient()->m_AetherPsa.Start(BaseValue);
		GameClient()->m_AetherPsa.AutoStart(s_TimerSeconds);
	}
	if(DoButton_Menu(&s_ResetButton, "Reset", 0, &BtnReset))
		GameClient()->m_AetherPsa.Reset(BaseValue);

	if(GameClient()->m_AetherPsa.IsActive())
	{
		int Low = 0, Base = 0, High = 0;
		GameClient()->m_AetherPsa.GetTriplet(&Low, &Base, &High);

		Body.HSplitTop(8.0f * S, nullptr, &Body);
		Body.HSplitTop(18.0f * S, &Row, &Body);
		char aProgress[64];
		str_format(aProgress, sizeof(aProgress), "Step %d/7   Low: %d   Base: %d   High: %d", GameClient()->m_AetherPsa.StepDisplay(), Low, Base, High);
		Ui()->DoLabel(&Row, aProgress, 13.0f * S, TEXTALIGN_ML);

		Body.HSplitTop(8.0f * S, nullptr, &Body);
		Body.HSplitTop(24.0f * S, &Row, &Body);
		CUIRect BtnCancel, BtnSkip;
		Row.VSplitLeft(ButtonW, &BtnCancel, &Row);
		Row.VSplitLeft(ButtonGap, nullptr, &Row);
		Row.VSplitLeft(ButtonW, &BtnSkip, nullptr);
		static CButtonContainer s_SkipButton;
		static CButtonContainer s_CancelButton;
		if(DoButton_Menu(&s_CancelButton, "Stop", 0, &BtnCancel))
			GameClient()->m_AetherPsa.AutoCancel();
		if(DoButton_Menu(&s_SkipButton, "Skip", 0, &BtnSkip))
			GameClient()->m_AetherPsa.AutoSkip();

		Body.HSplitTop(8.0f * S, nullptr, &Body);
		Body.HSplitTop(24.0f * S, &Row, &Body);
		CUIRect BtnPickLow, BtnPickHigh;
		Row.VSplitLeft(ButtonW, &BtnPickLow, &Row);
		Row.VSplitLeft(ButtonGap, nullptr, &Row);
		Row.VSplitLeft(ButtonW, &BtnPickHigh, nullptr);
		static CButtonContainer s_SelectLowButton;
		static CButtonContainer s_SelectHighButton;
		if(DoButton_Menu(&s_SelectLowButton, "Select Low", 0, &BtnPickLow))
			GameClient()->m_AetherPsa.SelectLow();
		if(DoButton_Menu(&s_SelectHighButton, "Select High", 0, &BtnPickHigh))
			GameClient()->m_AetherPsa.SelectHigh();
	}
	else if(GameClient()->m_AetherPsa.IsCompleted())
	{
		Body.HSplitTop(8.0f * S, nullptr, &Body);
		Body.HSplitTop(18.0f * S, &Row, &Body);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Suggested value: %d", GameClient()->m_AetherPsa.SuggestedValue());
		Ui()->DoLabel(&Row, aBuf, 13.0f * S, TEXTALIGN_ML);
	}
}

void CMenus::RenderSettingsAetherVaultCfg(CUIRect Body)
{
	static CButtonContainer s_SaveButton;
	static CButtonContainer s_LoadButton;
	static CButtonContainer s_RenameButton;
	static CButtonContainer s_DeleteButton;
	static CButtonContainer s_RefreshButton;
	static CButtonContainer s_OpenFolderButton;
	static CLineInputBuffered<64> s_NameInput;
	static bool s_Initialized = false;
	static int s_Selected = -1;
	static std::array<CButtonContainer, 8> s_aFileButtons;
	const float S = AetherSettingsScale();

	if(!s_Initialized)
	{
		s_NameInput.Set("aether_vault");
		GameClient()->m_AetherVaultCfg.RefreshList();
		s_Initialized = true;
	}

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);

	CUIRect Row, Label, EditBox;
	Body.HSplitTop(34.0f * S, &Row, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Row, "Saved cfg presets live in vault/. KoG /login, password, token and auth lines are skipped.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Body.HSplitTop(24.0f * S, &Row, &Body);
	Row.VSplitLeft(120.0f * S, &Label, &EditBox);
	Ui()->DoLabel(&Label, "CFG name", 13.0f * S, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_NameInput, &EditBox, 13.0f * S, IGraphics::CORNER_ALL, {}, 6.0f * S);

	const auto &vFiles = GameClient()->m_AetherVaultCfg.Files();
	if(s_Selected >= (int)vFiles.size())
		s_Selected = -1;

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	CUIRect SaveButton, LoadButton, RenameButton;
	const float Gap = 6.0f * S;
	const float BtnW = (Row.w - Gap * 2.0f) / 3.0f;
	Row.VSplitLeft(BtnW, &SaveButton, &Row);
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &LoadButton, &Row);
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &RenameButton, nullptr);
	if(DoButton_Menu(&s_SaveButton, "Save", 0, &SaveButton))
	{
		if(GameClient()->m_AetherVaultCfg.Save(s_NameInput.GetString()))
			PopupWarning("Vault CFG", "Saved sanitized config preset.", Localize("Ok"), std::chrono::seconds(3));
		else
			PopupWarning("Vault CFG", GameClient()->m_AetherVaultCfg.LastError(), Localize("Ok"), std::chrono::seconds(4));
	}
	if(DoButton_Menu(&s_LoadButton, "Load", 0, &LoadButton))
	{
		const char *pName = s_Selected >= 0 && s_Selected < (int)vFiles.size() ? vFiles[s_Selected].c_str() : s_NameInput.GetString();
		if(GameClient()->m_AetherVaultCfg.Load(pName))
			PopupWarning("Vault CFG", "Loaded selected cfg preset.", Localize("Ok"), std::chrono::seconds(3));
		else
			PopupWarning("Vault CFG", GameClient()->m_AetherVaultCfg.LastError(), Localize("Ok"), std::chrono::seconds(4));
	}
	if(DoButton_Menu(&s_RenameButton, "Rename", 0, &RenameButton))
	{
		if(s_Selected < 0 || s_Selected >= (int)vFiles.size())
			PopupWarning("Vault CFG", "Select a cfg before renaming.", Localize("Ok"), std::chrono::seconds(3));
		else if(GameClient()->m_AetherVaultCfg.Rename(vFiles[s_Selected].c_str(), s_NameInput.GetString()))
		{
			s_Selected = -1;
			PopupWarning("Vault CFG", "Renamed selected cfg preset.", Localize("Ok"), std::chrono::seconds(3));
		}
		else
			PopupWarning("Vault CFG", GameClient()->m_AetherVaultCfg.LastError(), Localize("Ok"), std::chrono::seconds(4));
	}

	Body.HSplitTop(5.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	CUIRect DeleteButton, RefreshButton, OpenFolderButton;
	Row.VSplitLeft(BtnW, &DeleteButton, &Row);
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &RefreshButton, &Row);
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &OpenFolderButton, nullptr);
	if(DoButton_Menu(&s_DeleteButton, "Delete", 0, &DeleteButton))
	{
		if(s_Selected < 0 || s_Selected >= (int)vFiles.size())
			PopupWarning("Vault CFG", "Select a cfg before deleting.", Localize("Ok"), std::chrono::seconds(3));
		else if(GameClient()->m_AetherVaultCfg.Delete(vFiles[s_Selected].c_str()))
		{
			s_Selected = -1;
			PopupWarning("Vault CFG", "Deleted selected cfg preset.", Localize("Ok"), std::chrono::seconds(3));
		}
		else
			PopupWarning("Vault CFG", GameClient()->m_AetherVaultCfg.LastError(), Localize("Ok"), std::chrono::seconds(4));
	}
	if(DoButton_Menu(&s_RefreshButton, "Refresh", 0, &RefreshButton))
		GameClient()->m_AetherVaultCfg.RefreshList();
	if(DoButton_Menu(&s_OpenFolderButton, "Folder", 0, &OpenFolderButton))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "vault", aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(18.0f * S, &Row, &Body);
	char aStatus[128];
	if(GameClient()->m_AetherVaultCfg.LastPath()[0] != '\0')
		str_format(aStatus, sizeof(aStatus), "Last save: %d lines", GameClient()->m_AetherVaultCfg.LastSavedLines());
	else
		str_format(aStatus, sizeof(aStatus), "%d saved cfg preset(s)", (int)vFiles.size());
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Row, aStatus, 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	const int MaxRows = minimum((int)vFiles.size(), (int)s_aFileButtons.size());
	for(int i = 0; i < MaxRows; ++i)
	{
		Body.HSplitTop(26.0f * S, &Row, &Body);
		if(DoButton_Menu(&s_aFileButtons[i], vFiles[i].c_str(), s_Selected == i, &Row))
		{
			s_Selected = i;
			s_NameInput.Set(vFiles[i].c_str());
		}
		Body.HSplitTop(5.0f * S, nullptr, &Body);
	}
}

void CMenus::RenderSettingsAetherGoresMaps(CUIRect Body)
{
	static CButtonContainer s_RefreshBtn;
	static CButtonContainer s_OpenFolderBtn;
	static CButtonContainer s_DownloadAllBtn;
	static CButtonContainer s_DownloadCategoryBtn;
	static CButtonContainer s_DownloadSelectedBtn;
	static CButtonContainer s_DeleteBtn;
	static CButtonContainer s_DeleteToggle;
	static CButtonContainer s_FilterToggle;
	static std::array<CButtonContainer, 8> s_aCategoryButtons;
	static std::array<bool, 8192> s_aListIds{};
	static CScrollRegion s_ListScroll;
	const float S = AetherSettingsScale();
	GameClient()->m_AetherGoresMaps.EnsureAutoFetch();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(10.0f * S, &Body);

	CUIRect Row;
	Body.HSplitTop(18.0f * S, &Row, &Body);
	Ui()->DoLabel(&Row, "Target: maps/GoresMaps/[1..8]-Category", 12.0f * S, TEXTALIGN_ML);
	Body.HSplitTop(18.0f * S, &Row, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Row, GameClient()->m_AetherGoresMaps.Status(), 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Body.HSplitTop(6.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	const float CatGap = 4.0f * S;
	const float CatW = (Row.w - CatGap * 7.0f) / 8.0f;
	for(int i = 0; i < 8; i++)
	{
		CUIRect CatBtn;
		Row.VSplitLeft(CatW, &CatBtn, &Row);
		char aCatLabel[32];
		str_format(aCatLabel, sizeof(aCatLabel), "%d-%s", i + 1, GameClient()->m_AetherGoresMaps.CategoryName(i));
		if(DoButton_MenuTab(&s_aCategoryButtons[i], aCatLabel, GameClient()->m_AetherGoresMaps.SelectedCategory() == i, &CatBtn, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f * S))
			GameClient()->m_AetherGoresMaps.SetSelectedCategory(i);
		Row.VSplitLeft(CatGap, nullptr, &Row);
	}

	Body.HSplitTop(7.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	CUIRect Btn;
	const float Gap = 6.0f * S;
	const float BtnW = (Row.w - Gap * 3.0f) / 4.0f;
	Row.VSplitLeft(BtnW, &Btn, &Row);
	if(DoButton_Menu(&s_RefreshBtn, "Refresh list", 0, &Btn))
		GameClient()->m_AetherGoresMaps.RefreshList(true);
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &Btn, &Row);
	if(DoButton_Menu(&s_OpenFolderBtn, "Open folder", 0, &Btn))
		GameClient()->m_AetherGoresMaps.OpenFolder();
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &Btn, &Row);
	if(DoButton_Menu(&s_DownloadAllBtn, "Download all", GameClient()->m_AetherGoresMaps.ManifestLoaded() ? 0 : 1, &Btn) && GameClient()->m_AetherGoresMaps.ManifestLoaded())
		GameClient()->m_AetherGoresMaps.DownloadAll();
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(BtnW, &Btn, nullptr);
	char aCatAction[64];
	str_format(aCatAction, sizeof(aCatAction), "Only: %s", GameClient()->m_AetherGoresMaps.CategoryName(GameClient()->m_AetherGoresMaps.SelectedCategory()));
	if(DoButton_Menu(&s_DownloadCategoryBtn, aCatAction, GameClient()->m_AetherGoresMaps.ManifestLoaded() ? 0 : 1, &Btn) && GameClient()->m_AetherGoresMaps.ManifestLoaded())
		GameClient()->m_AetherGoresMaps.DownloadCategory();

	Body.HSplitTop(5.0f * S, nullptr, &Body);
	Body.HSplitTop(22.0f * S, &Row, &Body);
	const float SmallBtnW = (Row.w - Gap * 2.0f) / 3.0f;
	Row.VSplitLeft(SmallBtnW, &Btn, &Row);
	if(DoButton_Menu(&s_DownloadSelectedBtn, "Download selected", GameClient()->m_AetherGoresMaps.SelectedIndex() >= 0 ? 0 : 1, &Btn) && GameClient()->m_AetherGoresMaps.SelectedIndex() >= 0)
		GameClient()->m_AetherGoresMaps.DownloadSelected();
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(SmallBtnW, &Btn, &Row);
	if(DoButton_CheckBox(&s_FilterToggle, "Selected only", GameClient()->m_AetherGoresMaps.ShowOnlySelectedCategory(), &Btn))
		GameClient()->m_AetherGoresMaps.ToggleShowOnlySelectedCategory();
	Row.VSplitLeft(Gap, nullptr, &Row);
	Row.VSplitLeft(SmallBtnW, &Btn, nullptr);
	if(DoButton_CheckBox(&s_DeleteToggle, "Delete mode", GameClient()->m_AetherGoresMaps.DeleteEnabled(), &Btn))
		GameClient()->m_AetherGoresMaps.ToggleDeleteEnabled();

	if(GameClient()->m_AetherGoresMaps.DeleteEnabled())
	{
		Body.HSplitTop(5.0f * S, nullptr, &Body);
		Body.HSplitTop(22.0f * S, &Row, &Body);
		if(DoButton_Menu(&s_DeleteBtn, "Delete selected", GameClient()->m_AetherGoresMaps.SelectedIndex() >= 0 ? 0 : 1, &Row) && GameClient()->m_AetherGoresMaps.SelectedIndex() >= 0)
			GameClient()->m_AetherGoresMaps.DeleteSelected();
	}

	Body.HSplitTop(8.0f * S, nullptr, &Body);
	CUIRect ListRect = Body;
	std::vector<int> vDisplayIndices = GameClient()->m_AetherGoresMaps.DisplayIndices();
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 32.0f * S;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ListScroll.Begin(&ListRect, &ScrollOffset, &ScrollParams);
	ListRect.y += ScrollOffset.y;
	const auto &vMaps = GameClient()->m_AetherGoresMaps.Maps();
	for(size_t i = 0; i < vDisplayIndices.size() && i < s_aListIds.size(); ++i)
	{
		const int MapIndex = vDisplayIndices[i];
		if(MapIndex < 0 || MapIndex >= (int)vMaps.size())
			continue;
		CUIRect Item, IconRect, TextRect;
		ListRect.HSplitTop(23.0f * S, &Item, &ListRect);
		if(s_ListScroll.AddRect(Item))
		{
			const bool Selected = GameClient()->m_AetherGoresMaps.SelectedIndex() == MapIndex;
			const bool Installed = GameClient()->m_AetherGoresMaps.IsInstalled(vMaps[MapIndex]);
			Item.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Selected ? 0.08f : 0.035f), IGraphics::CORNER_ALL, 4.0f * S);
			if(Ui()->DoButtonLogic(&s_aListIds[i], Selected ? 1 : 0, &Item, BUTTONFLAG_LEFT))
				GameClient()->m_AetherGoresMaps.SetSelectedIndex(MapIndex);
			Item.VMargin(4.0f * S, &Item);
			Item.VSplitLeft(22.0f * S, &IconRect, &TextRect);
			TextRender()->TextColor(Installed ? ColorRGBA(0.35f, 0.95f, 0.45f, 1.0f) : ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
			Ui()->DoLabel(&IconRect, Installed ? "OK" : "--", 10.0f * S, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			char aLine[256];
			str_format(aLine, sizeof(aLine), "%s (%d*)", vMaps[MapIndex].m_Name.c_str(), maximum(1, vMaps[MapIndex].m_Stars));
			Ui()->DoLabel(&TextRect, aLine, 12.0f * S, TEXTALIGN_ML);
		}
		ListRect.HSplitTop(3.0f * S, nullptr, &ListRect);
	}
	s_ListScroll.End();
}

void CMenus::RenderSettingsAetherAssetsEditor(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	static CButtonContainer s_OpenEditor;
	CUIRect Text, Button;
	Body.VSplitRight(150.0f * S, &Text, &Button);
	TextRender()->TextColor(0.78f, 0.84f, 0.92f, 1.0f);
	Ui()->DoLabel(&Text, "Opens a dedicated mixer popup with sources on the left and the mixed asset on the right.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	if(DoButton_Menu(&s_OpenEditor, "Open editor", 0, &Button))
		s_AetherAssetsEditorOpen = true;
}

void CMenus::RenderSettingsAetherAssetsCloud(CUIRect Body)
{
	const float S = AetherSettingsScale();
	static CButtonContainer s_aCategoryButtons[5];
	static CButtonContainer s_RefreshLocalButton, s_RefreshCloudButton, s_UploadButton, s_DownloadButton, s_ApplyCloudButton, s_OpenFolderButton;
	static CButtonContainer s_LocalSizeButton;
	static std::array<CButtonContainer, AETHER_CLOUD_LOCAL_MAX> s_aLocalButtons;
	static std::array<CButtonContainer, AETHER_CLOUD_REMOTE_MAX> s_aCloudButtons;
	static CScrollRegion s_LocalScroll;
	static CScrollRegion s_CloudScroll;
	static CLineInput s_SearchInput(s_aAetherAssetsCloudSearch, sizeof(s_aAetherAssetsCloudSearch));

	auto StartListRequest = [&]() {
		char aUrl[512];
		char aPath[128];
		str_format(aPath, sizeof(aPath), "/v1/assets?category=%s&limit=100", AetherCloudCategory().m_pKey);
		AetherBuildApiUrl(aUrl, sizeof(aUrl), aPath);
		s_pAetherAssetsCloudRequest = std::make_shared<CHttpRequest>(aUrl);
		Http()->Run(s_pAetherAssetsCloudRequest);
		s_AetherAssetsCloudAction = EAetherAssetsCloudAction::LIST;
		str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Refreshing %s assets...", AetherCloudCategory().m_pLabel);
	};

	auto StartUploadRequest = [&]() {
		if(s_vAetherAssetsCloudLocal.empty())
		{
			str_copy(s_aAetherAssetsCloudStatus, "No local asset selected.");
			return;
		}
		const std::string &Name = s_vAetherAssetsCloudLocal[std::clamp(s_AetherAssetsCloudLocalSelected, 0, (int)s_vAetherAssetsCloudLocal.size() - 1)];
		char aRelPath[IO_MAX_PATH_LENGTH];
		if(str_comp(AetherCloudCategory().m_pKey, "entities") == 0)
			AetherResolveEntitiesPath(Storage(), Name.c_str(), aRelPath, sizeof(aRelPath));
		else
			AetherCloudAssetRelPath(AetherCloudCategory().m_pKey, Name.c_str(), aRelPath, sizeof(aRelPath));
		std::string Base64;
		if(!AetherCloudReadFileBase64(Storage(), aRelPath, Base64))
		{
			str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Upload failed: could not read %s.", aRelPath);
			return;
		}
		char aPlayer[MAX_NAME_LENGTH];
		str_copy(aPlayer, g_Config.m_PlayerName, sizeof(aPlayer));
		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("category");
		Json.WriteStrValue(AetherCloudCategory().m_pKey);
		Json.WriteAttribute("name");
		Json.WriteStrValue(Name.c_str());
		Json.WriteAttribute("uploader");
		Json.WriteStrValue(aPlayer);
		Json.WriteAttribute("content_base64");
		Json.WriteStrValue(Base64.c_str());
		Json.EndObject();
		char aUrl[512];
		AetherBuildApiUrl(aUrl, sizeof(aUrl), "/v1/assets/upload");
		s_pAetherAssetsCloudRequest = std::make_shared<CHttpRequest>(aUrl);
		s_pAetherAssetsCloudRequest->PostJson(Json.GetOutputString().c_str());
		Http()->Run(s_pAetherAssetsCloudRequest);
		s_AetherAssetsCloudAction = EAetherAssetsCloudAction::UPLOAD;
		str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Uploading %s...", Name.c_str());
	};

	auto StartDownloadRequest = [&](bool ApplyAfterDownload = false) {
		if(s_AetherAssetsCloudRemoteCount <= 0)
		{
			str_copy(s_aAetherAssetsCloudStatus, "No cloud asset selected.");
			return;
		}
		const SAetherCloudAsset &Asset = s_aAetherAssetsCloudRemote[std::clamp(s_AetherAssetsCloudRemoteSelected, 0, s_AetherAssetsCloudRemoteCount - 1)];
		char aPath[160];
		str_format(aPath, sizeof(aPath), "/v1/assets/%s", Asset.m_aId);
		char aUrl[512];
		AetherBuildApiUrl(aUrl, sizeof(aUrl), aPath);
		str_copy(s_aAetherAssetsCloudDownloadPath, Asset.m_aPath, sizeof(s_aAetherAssetsCloudDownloadPath));
		s_pAetherAssetsCloudRequest = std::make_shared<CHttpRequest>(aUrl);
		Http()->Run(s_pAetherAssetsCloudRequest);
		s_AetherAssetsCloudAction = EAetherAssetsCloudAction::DOWNLOAD;
		s_AetherAssetsCloudApplyAfterDownload = ApplyAfterDownload;
		str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Downloading %s...", Asset.m_aName);
	};

	auto ApplyAsset = [&](const char *pName) {
		if(!pName || pName[0] == '\0')
			return;
		const char *pCategory = AetherCloudCategory().m_pKey;
		if(str_comp(pCategory, "skins") == 0)
		{
			str_copy(g_Config.m_ClPlayerSkin, pName);
			GameClient()->m_Skins.Refresh([this]() {
				GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
			});
		}
		else if(str_comp(pCategory, "entities") == 0)
		{
			str_copy(g_Config.m_ClAssetsEntities, pName);
			GameClient()->m_MapImages.ChangeEntitiesPath(pName);
		}
		else if(str_comp(pCategory, "game") == 0)
		{
			str_copy(g_Config.m_ClAssetGame, pName);
			GameClient()->LoadGameSkin(pName);
		}
		else if(str_comp(pCategory, "particles") == 0)
		{
			str_copy(g_Config.m_ClAssetParticles, pName);
			GameClient()->LoadParticlesSkin(pName);
		}
		else if(str_comp(pCategory, "emoticons") == 0)
		{
			str_copy(g_Config.m_ClAssetEmoticons, pName);
			GameClient()->LoadEmoticonsSkin(pName);
		}
		str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Applied %s.", pName);
	};

	auto PumpRequest = [&]() {
		if(!s_pAetherAssetsCloudRequest)
			return;
		const EHttpState State = s_pAetherAssetsCloudRequest->State();
		if(State != EHttpState::DONE && State != EHttpState::ERROR && State != EHttpState::ABORTED)
			return;
		if(State == EHttpState::DONE && s_pAetherAssetsCloudRequest->StatusCode() >= 200 && s_pAetherAssetsCloudRequest->StatusCode() < 400)
		{
			json_value *pJson = s_pAetherAssetsCloudRequest->ResultJson();
			if(s_AetherAssetsCloudAction == EAetherAssetsCloudAction::LIST)
			{
				AetherCloudClearRemoteTextures(Graphics());
				s_AetherAssetsCloudRemoteCount = 0;
				const json_value *pAssets = pJson && pJson->type == json_object ? json_object_get(pJson, "assets") : nullptr;
				if(pAssets && pAssets->type == json_array)
				{
					for(int i = 0; i < json_array_length(pAssets) && s_AetherAssetsCloudRemoteCount < (int)s_aAetherAssetsCloudRemote.size(); ++i)
					{
						const json_value *pAsset = json_array_get(pAssets, i);
						if(!pAsset || pAsset->type != json_object)
							continue;
						SAetherCloudAsset &Asset = s_aAetherAssetsCloudRemote[s_AetherAssetsCloudRemoteCount++];
						str_copy(Asset.m_aId, json_string_get(json_object_get(pAsset, "id")) ? json_string_get(json_object_get(pAsset, "id")) : "", sizeof(Asset.m_aId));
						str_copy(Asset.m_aCategory, json_string_get(json_object_get(pAsset, "category")) ? json_string_get(json_object_get(pAsset, "category")) : "", sizeof(Asset.m_aCategory));
						str_copy(Asset.m_aName, json_string_get(json_object_get(pAsset, "name")) ? json_string_get(json_object_get(pAsset, "name")) : "", sizeof(Asset.m_aName));
						str_copy(Asset.m_aPath, json_string_get(json_object_get(pAsset, "path")) ? json_string_get(json_object_get(pAsset, "path")) : "", sizeof(Asset.m_aPath));
						str_copy(Asset.m_aUploader, json_string_get(json_object_get(pAsset, "uploader")) ? json_string_get(json_object_get(pAsset, "uploader")) : "", sizeof(Asset.m_aUploader));
						str_copy(Asset.m_aCreatedAt, json_string_get(json_object_get(pAsset, "created_at")) ? json_string_get(json_object_get(pAsset, "created_at")) : "", sizeof(Asset.m_aCreatedAt));
						str_copy(Asset.m_aUpdatedAt, json_string_get(json_object_get(pAsset, "updated_at")) ? json_string_get(json_object_get(pAsset, "updated_at")) : "", sizeof(Asset.m_aUpdatedAt));
						Asset.m_DownloadCount = json_int_get(json_object_get(pAsset, "download_count"));
						Asset.m_ThumbnailTexture = AetherCloudLoadThumbnailTexture(Graphics(), json_string_get(json_object_get(pAsset, "thumbnail_base64")), &Asset.m_ThumbnailWidth, &Asset.m_ThumbnailHeight);
						Asset.m_TriedLocalThumbnail = false;
					}
				}
				s_AetherAssetsCloudRemoteSelected = std::clamp(s_AetherAssetsCloudRemoteSelected, 0, maximum(0, s_AetherAssetsCloudRemoteCount - 1));
				str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Loaded %d cloud %s asset(s).", s_AetherAssetsCloudRemoteCount, AetherCloudCategory().m_pLabel);
			}
			else if(s_AetherAssetsCloudAction == EAetherAssetsCloudAction::UPLOAD)
			{
				const json_value *pAsset = pJson && pJson->type == json_object ? json_object_get(pJson, "asset") : nullptr;
				const char *pName = pAsset && pAsset->type == json_object ? json_string_get(json_object_get(pAsset, "name")) : nullptr;
				const char *pUploader = pAsset && pAsset->type == json_object ? json_string_get(json_object_get(pAsset, "uploader")) : nullptr;
				str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Upload complete: %s by %s. Refreshing cloud...", pName ? pName : "asset", pUploader && pUploader[0] ? pUploader : "unknown");
				s_AetherAssetsCloudRefreshAfterRequest = true;
			}
			else if(s_AetherAssetsCloudAction == EAetherAssetsCloudAction::DOWNLOAD)
			{
				const json_value *pAsset = pJson && pJson->type == json_object ? json_object_get(pJson, "asset") : nullptr;
				const char *pName = pAsset && pAsset->type == json_object ? json_string_get(json_object_get(pAsset, "name")) : nullptr;
				const char *pPath = pAsset && pAsset->type == json_object ? json_string_get(json_object_get(pAsset, "path")) : nullptr;
				const char *pContent = pAsset && pAsset->type == json_object ? json_string_get(json_object_get(pAsset, "content_base64")) : nullptr;
				if(pName && pPath && pContent)
				{
					AetherCloudEnsureFolder(Storage(), AetherCloudCategory().m_pKey, pName);
					if(AetherCloudWriteBase64(Storage(), pPath, pContent))
					{
						str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Downloaded %s.", pName);
						AetherCloudScanLocal(Storage());
						if(s_AetherAssetsCloudApplyAfterDownload)
							ApplyAsset(pName);
					}
					else
						str_copy(s_aAetherAssetsCloudStatus, "Download failed: could not write file.");
				}
				else
					str_copy(s_aAetherAssetsCloudStatus, "Download failed: bad response.");
			}
			if(pJson)
				json_value_free(pJson);
		}
		else
		{
			int Status = State == EHttpState::DONE ? s_pAetherAssetsCloudRequest->StatusCode() : 0;
			if(Status == 404)
				str_copy(s_aAetherAssetsCloudStatus, "Aether API is not updated. Deploy latest API.");
			else if(State == EHttpState::ABORTED)
				str_copy(s_aAetherAssetsCloudStatus, "Assets cloud request aborted.");
			else if(Status > 0)
			{
				const char *pError = "request failed";
				json_value *pJson = s_pAetherAssetsCloudRequest->ResultJson();
				if(pJson && pJson->type == json_object)
					pError = json_string_get(json_object_get(pJson, "error")) ? json_string_get(json_object_get(pJson, "error")) : pError;
				str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Assets cloud HTTP %d: %s.", Status, pError);
				if(pJson)
					json_value_free(pJson);
			}
			else
				str_copy(s_aAetherAssetsCloudStatus, "Assets cloud network error. Check API URL/deploy.");
			if(s_AetherAssetsCloudAction == EAetherAssetsCloudAction::DOWNLOAD)
				s_AetherAssetsCloudApplyAfterDownload = false;
		}
		if(s_AetherAssetsCloudAction == EAetherAssetsCloudAction::DOWNLOAD)
			s_AetherAssetsCloudApplyAfterDownload = false;
		if(s_AetherAssetsCloudAction != EAetherAssetsCloudAction::LIST || !s_pAetherAssetsCloudRequest || s_pAetherAssetsCloudRequest->State() != EHttpState::RUNNING)
		{
			s_pAetherAssetsCloudRequest = nullptr;
			s_AetherAssetsCloudAction = EAetherAssetsCloudAction::NONE;
		}
	};

	PumpRequest();
	if(!s_pAetherAssetsCloudRequest && s_AetherAssetsCloudRefreshAfterRequest)
	{
		s_AetherAssetsCloudRefreshAfterRequest = false;
		StartListRequest();
	}
	if(!s_pAetherAssetsCloudRequest && GameClient()->m_AetherBadges.ConsumeAssetsCloudUpdate(AetherCloudCategory().m_pKey))
	{
		str_format(s_aAetherAssetsCloudStatus, sizeof(s_aAetherAssetsCloudStatus), "Cloud %s assets updated. Refreshing...", AetherCloudCategory().m_pLabel);
		StartListRequest();
	}
	if(s_AetherAssetsCloudLocalScannedCategory != s_AetherAssetsCloudCategory)
		AetherCloudScanLocal(Storage());
	if(s_AetherAssetsCloudLocalTextureCategory != s_AetherAssetsCloudCategory || s_AetherAssetsCloudLocalTextureGeneration != s_AetherAssetsCloudLocalGeneration)
		AetherCloudClearLocalTextures(Graphics());
	static int s_LastRenderedCloudCategory = -1;
	if(s_LastRenderedCloudCategory != s_AetherAssetsCloudCategory && !s_pAetherAssetsCloudRequest)
	{
		s_LastRenderedCloudCategory = s_AetherAssetsCloudCategory;
		StartListRequest();
	}

	Body.Draw(AetherPanelColor(0.26f), IGraphics::CORNER_ALL, 8.0f * S);
	Body.Margin(12.0f * S, &Body);

	CUIRect Title, CategoryRow, SearchRow, Status, Actions, Lists;
	Body.HSplitTop(26.0f * S, &Title, &Body);
	Ui()->DoLabel(&Title, "Assets Cloud", 22.0f * S, TEXTALIGN_ML);
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(28.0f * S, &CategoryRow, &Body);
	for(size_t i = 0; i < AETHER_CLOUD_ASSET_CATEGORIES.size(); ++i)
	{
		CUIRect Button;
		CategoryRow.VSplitLeft(112.0f * S, &Button, &CategoryRow);
		CategoryRow.VSplitLeft(7.0f * S, nullptr, &CategoryRow);
		if(DoButton_MenuTab(&s_aCategoryButtons[i], AETHER_CLOUD_ASSET_CATEGORIES[i].m_pLabel, s_AetherAssetsCloudCategory == (int)i, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f * S))
		{
			s_AetherAssetsCloudCategory = (int)i;
			s_AetherAssetsCloudLocalScannedCategory = -1;
			s_AetherAssetsCloudLocalSelected = 0;
			s_AetherAssetsCloudRemoteSelected = 0;
			AetherCloudScanLocal(Storage());
			StartListRequest();
		}
	}
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(28.0f * S, &SearchRow, &Body);
	CUIRect SearchBox, LocalToggle;
	SearchRow.VSplitRight(136.0f * S, &SearchBox, &LocalToggle);
	SearchBox.VSplitRight(8.0f * S, &SearchBox, nullptr);
	s_SearchInput.SetEmptyText("Search assets...");
	Ui()->DoEditBox_Search(&s_SearchInput, &SearchBox, 13.0f * S, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive(), 7.0f * S);
	if(DoButton_Menu(&s_LocalSizeButton, s_AetherAssetsCloudLocalExpanded ? "Compact local" : "Expand local", 0, &LocalToggle))
		s_AetherAssetsCloudLocalExpanded = !s_AetherAssetsCloudLocalExpanded;
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitTop(24.0f * S, &Status, &Body);
	TextRender()->TextColor(0.78f, 0.84f, 0.92f, 1.0f);
	Ui()->DoLabel(&Status, s_aAetherAssetsCloudStatus, 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Body.HSplitTop(8.0f * S, nullptr, &Body);
	Body.HSplitBottom(40.0f * S, &Lists, &Actions);

	CUIRect Left, Right;
	const float ListGap = 10.0f * S;
	const float MinCloudW = std::min(300.0f * S, Lists.w * 0.48f);
	const float MaxLocalW = maximum(120.0f * S, Lists.w - ListGap - MinCloudW);
	const float CompactLocalW = std::clamp(Lists.w * 0.34f, 180.0f * S, MaxLocalW);
	const float ExpandedLocalW = std::clamp(Lists.w * 0.45f, std::min(300.0f * S, MaxLocalW), MaxLocalW);
	const float LocalW = s_AetherAssetsCloudLocalExpanded ? ExpandedLocalW : CompactLocalW;
	Lists.VSplitLeft(LocalW, &Left, &Right);
	Right.VSplitLeft(ListGap, nullptr, &Right);
	auto DrawPreview = [&](const char *pCategory, const char *pName, IGraphics::CTextureHandle Texture, int TextureW, int TextureH, const CUIRect &Rect) {
		if(!pCategory || !pName)
			return;
		if(str_comp(pCategory, "skins") == 0)
		{
			char aSkinPath[IO_MAX_PATH_LENGTH];
			str_format(aSkinPath, sizeof(aSkinPath), "skins/%s.png", pName);
			if(Storage()->FileExists(aSkinPath, IStorage::TYPE_ALL))
			{
				CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Rect.w, Rect.h), pName, false, 0, 0);
				TeeInfo.m_Size = std::min(Rect.w, Rect.h) * 0.92f;
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
				const vec2 Pos = Rect.Center() + vec2(0.0f, OffsetToMid.y + 2.0f * S);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.12f), Pos);
				return;
			}
		}
		if(Texture.IsValid())
		{
			const CUIRect ImageRect = TextureW > 0 && TextureH > 0 ? AetherFitImageRect(Rect, TextureW, TextureH) : Rect;
			AetherDrawTextureInRect(Graphics(), Texture, ImageRect);
			return;
		}
		TextRender()->TextColor(0.48f, 0.56f, 0.66f, 1.0f);
		Ui()->DoLabel(&Rect, str_comp(pCategory, "entities") == 0 ? "ENT" : "PNG", 8.0f * S, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};
	auto RenderListPanel = [&](CUIRect Panel, const char *pTitle, const char *pSubtitle, bool Cloud) {
		Panel.Draw(AetherPanelColor(0.24f), IGraphics::CORNER_ALL, 7.0f * S);
		Panel.Margin(8.0f * S, &Panel);
		CUIRect Header, List;
		Panel.HSplitTop(36.0f * S, &Header, &List);
		CUIRect HeaderTitle, HeaderSub;
		Header.HSplitTop(20.0f * S, &HeaderTitle, &HeaderSub);
		Ui()->DoLabel(&HeaderTitle, pTitle, 15.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(0.64f, 0.72f, 0.82f, 1.0f);
		Ui()->DoLabel(&HeaderSub, pSubtitle, 10.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		vec2 ScrollOffset(0.0f, 0.0f);
		CScrollRegionParams Params;
		Params.m_ScrollUnit = (Cloud ? 76.0f : 64.0f) * S;
		Params.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		CScrollRegion &Scroll = Cloud ? s_CloudScroll : s_LocalScroll;
		Scroll.Begin(&List, &ScrollOffset, &Params);
		List.y += ScrollOffset.y;
		const int Count = Cloud ? s_AetherAssetsCloudRemoteCount : (int)s_vAetherAssetsCloudLocal.size();
		const int MaxRows = Cloud ? AETHER_CLOUD_REMOTE_MAX : AETHER_CLOUD_LOCAL_MAX;
		int Displayed = 0;
		for(int i = 0; i < Count && i < MaxRows; ++i)
		{
			const char *pName = Cloud ? s_aAetherAssetsCloudRemote[i].m_aName : s_vAetherAssetsCloudLocal[i].c_str();
			const char *pUploader = Cloud ? s_aAetherAssetsCloudRemote[i].m_aUploader : nullptr;
			if(!AetherCloudMatchesSearch(pName, pUploader))
				continue;
			CUIRect Row;
			List.HSplitTop((Cloud ? 72.0f : 60.0f) * S, &Row, &List);
			++Displayed;
			if(Scroll.AddRect(Row))
			{
				const bool Selected = Cloud ? s_AetherAssetsCloudRemoteSelected == i : s_AetherAssetsCloudLocalSelected == i;
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Selected ? 0.10f : 0.035f), IGraphics::CORNER_ALL, 4.0f * S);
				if(Ui()->DoButtonLogic(Cloud ? (void *)&s_aCloudButtons[i] : (void *)&s_aLocalButtons[i], Selected ? 1 : 0, &Row, BUTTONFLAG_LEFT))
				{
					if(Cloud)
						s_AetherAssetsCloudRemoteSelected = i;
					else
						s_AetherAssetsCloudLocalSelected = i;
				}
				Row.Margin(6.0f * S, &Row);
				if(Cloud)
				{
					CUIRect Preview, Text, Name, ByLine, StatsLine;
					Row.VSplitLeft(64.0f * S, &Preview, &Text);
					Text.VSplitLeft(10.0f * S, nullptr, &Text);
					Preview.Draw(AetherPanelColor(0.18f), IGraphics::CORNER_ALL, 5.0f * S);
					Preview.Margin(5.0f * S, &Preview);
					if(!s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture.IsValid() && !s_aAetherAssetsCloudRemote[i].m_TriedLocalThumbnail)
					{
						s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture = AetherCloudLoadFileTexture(Graphics(), AetherCloudCategory().m_pKey, pName, &s_aAetherAssetsCloudRemote[i].m_ThumbnailWidth, &s_aAetherAssetsCloudRemote[i].m_ThumbnailHeight);
						s_aAetherAssetsCloudRemote[i].m_TriedLocalThumbnail = true;
					}
					DrawPreview(AetherCloudCategory().m_pKey, pName, s_aAetherAssetsCloudRemote[i].m_ThumbnailTexture, s_aAetherAssetsCloudRemote[i].m_ThumbnailWidth, s_aAetherAssetsCloudRemote[i].m_ThumbnailHeight, Preview);
					Text.HSplitTop(23.0f * S, &Name, &Text);
					Text.HSplitTop(17.0f * S, &ByLine, &StatsLine);
					const char *pUploader = s_aAetherAssetsCloudRemote[i].m_aUploader[0] ? s_aAetherAssetsCloudRemote[i].m_aUploader : "unknown";
					Ui()->DoLabel(&Name, pName, 13.0f * S, TEXTALIGN_ML);
					char aBy[96];
					str_format(aBy, sizeof(aBy), "by %s", pUploader);
					TextRender()->TextColor(0.66f, 0.74f, 0.84f, 1.0f);
					Ui()->DoLabel(&ByLine, aBy, 10.5f * S, TEXTALIGN_ML);
					char aDate[16] = "";
					if(s_aAetherAssetsCloudRemote[i].m_aCreatedAt[0])
						str_copy(aDate, s_aAetherAssetsCloudRemote[i].m_aCreatedAt, minimum<int>((int)sizeof(aDate), 11));
					else if(s_aAetherAssetsCloudRemote[i].m_aUpdatedAt[0])
						str_copy(aDate, s_aAetherAssetsCloudRemote[i].m_aUpdatedAt, minimum<int>((int)sizeof(aDate), 11));
					char aStats[96];
					if(aDate[0])
						str_format(aStats, sizeof(aStats), "%s | %d downloads", aDate, s_aAetherAssetsCloudRemote[i].m_DownloadCount);
					else
						str_format(aStats, sizeof(aStats), "%d downloads", s_aAetherAssetsCloudRemote[i].m_DownloadCount);
					TextRender()->TextColor(0.50f, 0.58f, 0.68f, 1.0f);
					Ui()->DoLabel(&StatsLine, aStats, 9.0f * S, TEXTALIGN_ML);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
				else
				{
					CUIRect Preview, Text;
					Row.VSplitLeft(58.0f * S, &Preview, &Text);
					Text.VSplitLeft(10.0f * S, nullptr, &Text);
					Preview.Draw(AetherPanelColor(0.18f), IGraphics::CORNER_ALL, 4.0f * S);
					Preview.Margin(5.0f * S, &Preview);
					if(i < (int)s_aAetherAssetsCloudLocalTextures.size() && !s_aAetherAssetsCloudLocalTextures[i].IsValid() && !s_aAetherAssetsCloudLocalTextureTried[i])
					{
						s_aAetherAssetsCloudLocalTextures[i] = AetherCloudLoadFileTexture(Graphics(), AetherCloudCategory().m_pKey, pName, &s_aAetherAssetsCloudLocalTextureWidths[i], &s_aAetherAssetsCloudLocalTextureHeights[i]);
						s_aAetherAssetsCloudLocalTextureTried[i] = true;
					}
					DrawPreview(AetherCloudCategory().m_pKey, pName, i < (int)s_aAetherAssetsCloudLocalTextures.size() ? s_aAetherAssetsCloudLocalTextures[i] : IGraphics::CTextureHandle(), i < (int)s_aAetherAssetsCloudLocalTextureWidths.size() ? s_aAetherAssetsCloudLocalTextureWidths[i] : 0, i < (int)s_aAetherAssetsCloudLocalTextureHeights.size() ? s_aAetherAssetsCloudLocalTextureHeights[i] : 0, Preview);
					Ui()->DoLabel(&Text, pName, 12.0f * S, TEXTALIGN_ML);
				}
			}
			List.HSplitTop(5.0f * S, nullptr, &List);
		}
		if(Displayed == 0 && Scroll.AddRect(List))
		{
			TextRender()->TextColor(0.56f, 0.64f, 0.74f, 1.0f);
			Ui()->DoLabel(&List, s_aAetherAssetsCloudSearch[0] ? "No assets match this search." : (Cloud ? "No cloud assets in this category yet." : "No local exports in this category yet."), 12.0f * S, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		Scroll.End();
	};
	RenderListPanel(Left, "Local exports", "Select one to upload.", false);
	RenderListPanel(Right, "Cloud library", "Download or apply public assets.", true);

	Actions.HMargin(2.0f * S, &Actions);
	const float Gap = 7.0f * S;
	const float ButtonW = (Actions.w - Gap * 5.0f) / 6.0f;
	CUIRect Button;
	Actions.VSplitLeft(ButtonW, &Button, &Actions);
	if(DoButton_Menu(&s_RefreshLocalButton, "Refresh local", 0, &Button))
	{
		s_AetherAssetsCloudLocalScannedCategory = -1;
		AetherCloudScanLocal(Storage());
	}
	Actions.VSplitLeft(Gap, nullptr, &Actions);
	Actions.VSplitLeft(ButtonW, &Button, &Actions);
	if(DoButton_Menu(&s_RefreshCloudButton, "Refresh cloud", 0, &Button))
		StartListRequest();
	Actions.VSplitLeft(Gap, nullptr, &Actions);
	Actions.VSplitLeft(ButtonW, &Button, &Actions);
	if(DoButton_Menu(&s_UploadButton, "Upload", s_vAetherAssetsCloudLocal.empty() ? 1 : 0, &Button) && !s_vAetherAssetsCloudLocal.empty())
		StartUploadRequest();
	Actions.VSplitLeft(Gap, nullptr, &Actions);
	Actions.VSplitLeft(ButtonW, &Button, &Actions);
	if(DoButton_Menu(&s_DownloadButton, "Download", s_AetherAssetsCloudRemoteCount <= 0 ? 1 : 0, &Button) && s_AetherAssetsCloudRemoteCount > 0)
		StartDownloadRequest(false);
	Actions.VSplitLeft(Gap, nullptr, &Actions);
	Actions.VSplitLeft(ButtonW, &Button, &Actions);
	if(DoButton_Menu(&s_ApplyCloudButton, "Apply cloud", s_AetherAssetsCloudRemoteCount <= 0 ? 1 : 0, &Button) && s_AetherAssetsCloudRemoteCount > 0)
		StartDownloadRequest(true);
	Actions.VSplitLeft(Gap, nullptr, &Actions);
	Actions.VSplitLeft(ButtonW, &Button, nullptr);
	if(DoButton_Menu(&s_OpenFolderButton, "Open folder", 0, &Button))
	{
		char aDir[IO_MAX_PATH_LENGTH];
		if(str_comp(AetherCloudCategory().m_pKey, "skins") == 0)
			Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		else
		{
			Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
			Storage()->CreateFolder(AetherCloudCategory().m_pFolder, IStorage::TYPE_SAVE);
		}
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, AetherCloudCategory().m_pFolder, aDir, sizeof(aDir));
		Client()->ViewFile(aDir);
	}
}

void CMenus::RenderSettingsAetherAssetsEditorPopup(CUIRect Screen)
{
	if(!s_AetherAssetsEditorOpen)
		return;

	{
		const float S = std::clamp(AetherSettingsScale(), 0.72f, 0.95f);
		const int NumCategories = AETHER_ASSET_EDITOR_MAX_CATEGORIES;
		static CLineInput s_ExportInput(s_aAetherAssetsEditorExportName, sizeof(s_aAetherAssetsEditorExportName));
		static CUi::SDropDownState s_ModeDropState;
		static CUi::SDropDownState s_DonorDropState;
		static CUi::SDropDownState s_MainDropState;
		static CScrollRegion s_ModeDropScroll;
		static CScrollRegion s_DonorDropScroll;
		static CScrollRegion s_MainDropScroll;
		static CButtonContainer s_CloseButton, s_ReloadButton, s_ExportButton, s_ShowGridButton, s_ResetAllButton, s_ResetPartButton;
		static CButtonContainer s_TintEnabledButton, s_OnlyColoredButton, s_FullBrightButton, s_FillInsideButton, s_PickColorButton, s_ColorResetButton, s_EditPanelCloseButton;
		static std::array<CButtonContainer, AETHER_ASSET_EDITOR_MAX_PARTS> s_aDonorCanvasButtons;
		static std::array<CButtonContainer, AETHER_ASSET_EDITOR_MAX_PARTS> s_aMixedCanvasButtons;
		static int s_ModalBlocker;
		static int s_EditPanelDrag;
		static int s_LastCategory = -1;
		static int64_t s_LastMixedPreviewBuildTime = 0;
		Ui()->SetHotItem(&s_ModalBlocker);

		auto ClampWindow = [&]() {
			CUIRect Full = Screen;
			Full.Margin(6.0f * S, &s_AetherAssetsEditorWindow);
		};
		ClampWindow();

		auto RefreshSources = [&]() {
			if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
				AetherScanPngNames(Storage(), "skins", s_vAetherAssetsEditorSkinSources, true);
			else if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
				AetherScanEntityNames(Storage(), s_vAetherAssetsEditorAssetSources);
			else
			{
				char aPath[64];
				str_format(aPath, sizeof(aPath), "assets/%s", s_aAssetEditorCategories[s_AetherAssetsEditorCategory].m_pPath);
				AetherScanPngNames(Storage(), aPath, s_vAetherAssetsEditorAssetSources, true);
			}
		};

		s_AetherAssetsEditorCategory = std::clamp(s_AetherAssetsEditorCategory, 0, NumCategories - 1);
		if(s_LastCategory != s_AetherAssetsEditorCategory ||
			(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY ? s_vAetherAssetsEditorSkinSources.empty() : s_vAetherAssetsEditorAssetSources.empty()))
		{
			s_LastCategory = s_AetherAssetsEditorCategory;
			s_AetherAssetsEditorBaseIndex = 0;
			s_AetherAssetsEditorDonorIndex = 0;
			s_AetherAssetsEditorPartIndex = 0;
			s_AetherAssetsEditorEditPanelOpen = false;
			s_AetherAssetsEditorPickColor = false;
			RefreshSources();
			AetherClearAssetEditorImageCache();
			s_AetherAssetsEditorMixedDirty = true;
			s_aAetherAssetsEditorSourcePath[0] = '\0';
			s_aAetherAssetsEditorLiveSourcePath[0] = '\0';
			AetherResetAssetsEditorLivePartTexture(Graphics());
		}

		std::vector<std::string> &vNames = s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY ? s_vAetherAssetsEditorSkinSources : s_vAetherAssetsEditorAssetSources;
		if(vNames.empty())
			vNames.emplace_back("default");
		std::vector<const char *> vpNamePtrs;
		vpNamePtrs.reserve(vNames.size());
		for(const std::string &Name : vNames)
			vpNamePtrs.push_back(Name.c_str());

		const std::span<const SAetherAssetPart> Parts = AetherAssetEditorParts(s_AetherAssetsEditorCategory);
		const int NumParts = (int)Parts.size();
		s_AetherAssetsEditorBaseIndex = std::clamp(s_AetherAssetsEditorBaseIndex, 0, maximum(0, (int)vNames.size() - 1));
		s_AetherAssetsEditorDonorIndex = std::clamp(s_AetherAssetsEditorDonorIndex, 0, maximum(0, (int)vNames.size() - 1));
		s_AetherAssetsEditorPartIndex = std::clamp(s_AetherAssetsEditorPartIndex, 0, maximum(0, NumParts - 1));
		auto IsEntityLiveColorOpacityEdit = [&]() {
			if(s_AetherAssetsEditorCategory != AETHER_ASSET_EDITOR_ENTITIES_CATEGORY || NumParts <= 0 || !s_AetherAssetsEditorEditPanelOpen)
				return false;
			const SAetherAssetPartState &LiveEditState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][s_AetherAssetsEditorPartIndex];
			const bool ColorPopupActive = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &LiveEditState.m_Color;
			const bool OpacityDragActive = Ui()->CheckActiveItem(&LiveEditState.m_Opacity);
			return ColorPopupActive || OpacityDragActive;
		};
		const bool DeferEntityLivePreview = IsEntityLiveColorOpacityEdit() && s_AetherAssetsEditorMixedTexture.IsValid();

		if(NumParts > 0 && s_AetherAssetsEditorEditPanelOpen)
		{
			SAetherAssetPartState &LiveEditState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][s_AetherAssetsEditorPartIndex];
			if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &LiveEditState.m_Color)
			{
				if(!LiveEditState.m_Enabled)
				{
					LiveEditState.m_Enabled = true;
					LiveEditState.m_SourceIndex = s_AetherAssetsEditorBaseIndex;
					LiveEditState.m_SourcePartIndex = s_AetherAssetsEditorPartIndex;
				}
				LiveEditState.m_TintEnabled = true;
				s_AetherAssetsEditorMixedDirty = true;
				s_AetherAssetsEditorForcePreviewBuild = !DeferEntityLivePreview;
			}
		}

		const int64_t Now = time_get();
		const int PreviewFps = s_AetherAssetsEditorEditPanelOpen ? 60 : 12;
		const bool RateLimitPreview = DeferEntityLivePreview ||
					      (!s_AetherAssetsEditorForcePreviewBuild &&
					      s_LastMixedPreviewBuildTime != 0 &&
					      Now - s_LastMixedPreviewBuildTime < time_freq() / PreviewFps);
		if(s_AetherAssetsEditorMixedDirty && !RateLimitPreview)
		{
			Graphics()->UnloadTexture(&s_AetherAssetsEditorMixedTexture);
			CImageInfo MixedImage;
			if(AetherBuildMixedAssetImage(Graphics(), s_AetherAssetsEditorCategory, vNames, s_AetherAssetsEditorBaseIndex, s_aaAetherAssetsEditorStates, MixedImage))
			{
				s_AetherAssetsEditorMixedWidth = MixedImage.m_Width;
				s_AetherAssetsEditorMixedHeight = MixedImage.m_Height;
				s_AetherAssetsEditorMixedTexture = Graphics()->LoadTextureRawMove(MixedImage, 0, "aether-assets-editor-preview");
			}
			else
			{
				s_AetherAssetsEditorMixedWidth = 0;
				s_AetherAssetsEditorMixedHeight = 0;
				str_copy(s_aAetherAssetsEditorStatus, "Could not build mixed preview from the selected base asset.");
			}
			s_AetherAssetsEditorMixedDirty = false;
			s_AetherAssetsEditorForcePreviewBuild = false;
			s_LastMixedPreviewBuildTime = Now;
		}

		auto ResolveAssetEditorPath = [&](int SourceIndex, char *pOut, int OutSize) {
			SourceIndex = std::clamp(SourceIndex, 0, maximum(0, (int)vNames.size() - 1));
			if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
				str_format(pOut, OutSize, "skins/%s.png", vNames[SourceIndex].c_str());
			else if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
				AetherResolveEntitiesPath(Storage(), vNames[SourceIndex].c_str(), pOut, OutSize);
			else
				AetherResolveAtlasPath(Storage(), s_aAssetEditorCategories[s_AetherAssetsEditorCategory].m_pPath, s_aAssetEditorCategories[s_AetherAssetsEditorCategory].m_ImageId, vNames[SourceIndex].c_str(), pOut, OutSize);
		};

		char aDonorPath[IO_MAX_PATH_LENGTH];
		ResolveAssetEditorPath(s_AetherAssetsEditorDonorIndex, aDonorPath, sizeof(aDonorPath));
		if(str_comp(s_aAetherAssetsEditorSourcePath, aDonorPath) != 0)
		{
			Graphics()->UnloadTexture(&s_AetherAssetsEditorSourceTexture);
			str_copy(s_aAetherAssetsEditorSourcePath, aDonorPath);
			if(const CImageInfo *pSourceImage = AetherCachedAssetEditorImage(Graphics(), s_AetherAssetsEditorCategory, vNames[s_AetherAssetsEditorDonorIndex].c_str()))
			{
				s_AetherAssetsEditorSourceWidth = pSourceImage->m_Width;
				s_AetherAssetsEditorSourceHeight = pSourceImage->m_Height;
			}
			else
			{
				s_AetherAssetsEditorSourceWidth = 0;
				s_AetherAssetsEditorSourceHeight = 0;
			}
			s_AetherAssetsEditorSourceTexture = Graphics()->LoadTexture(s_aAetherAssetsEditorSourcePath, IStorage::TYPE_ALL);
		}

		auto ExportCurrent = [&]() {
			char aName[64];
			AetherSanitizeExportName(s_ExportInput.GetString(), aName, sizeof(aName));
			if(aName[0] == '\0' || str_comp(aName, "default") == 0)
			{
				str_copy(s_aAetherAssetsEditorStatus, "Choose a new export name. 'default' is reserved.");
				return;
			}

			CImageInfo Dest;
			if(!AetherBuildMixedAssetImage(Graphics(), s_AetherAssetsEditorCategory, vNames, s_AetherAssetsEditorBaseIndex, s_aaAetherAssetsEditorStates, Dest))
			{
				str_copy(s_aAetherAssetsEditorStatus, "Export failed: could not build mixed asset.");
				return;
			}

			char aRel[IO_MAX_PATH_LENGTH];
			if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
			{
				Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
				str_format(aRel, sizeof(aRel), "skins/%s.png", aName);
			}
			else if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
			{
				Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
				Storage()->CreateFolder("assets/entities", IStorage::TYPE_SAVE);
				char aFolder[IO_MAX_PATH_LENGTH];
				str_format(aFolder, sizeof(aFolder), "assets/entities/%s", aName);
				Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
				str_format(aRel, sizeof(aRel), "assets/entities/%s/ddnet.png", aName);
			}
			else
			{
				Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
				char aFolder[IO_MAX_PATH_LENGTH];
				str_format(aFolder, sizeof(aFolder), "assets/%s", s_aAssetEditorCategories[s_AetherAssetsEditorCategory].m_pPath);
				Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
				str_format(aRel, sizeof(aRel), "assets/%s/%s.png", s_aAssetEditorCategories[s_AetherAssetsEditorCategory].m_pPath, aName);
			}
			if(Storage()->FileExists(aRel, IStorage::TYPE_ALL))
			{
				str_copy(s_aAetherAssetsEditorStatus, "Export blocked: a file with that name already exists.");
				Dest.Free();
				return;
			}
			if(!AetherSavePngToStorage(Storage(), aRel, Dest))
			{
				str_copy(s_aAetherAssetsEditorStatus, "Export failed: could not write PNG.");
				Dest.Free();
				return;
			}
			Dest.Free();

			if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_SKINS_CATEGORY)
			{
				GameClient()->m_Skins.Refresh([this]() {
					GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
				});
				AetherScanPngNames(Storage(), "skins", s_vAetherAssetsEditorSkinSources, true);
				str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Exported skin '%s'.", aName);
				std::string Base64;
				if(AetherCloudReadFileBase64(Storage(), aRel, Base64))
				{
					char aPlayer[MAX_NAME_LENGTH];
					str_copy(aPlayer, g_Config.m_PlayerName, sizeof(aPlayer));
					CJsonStringWriter Json;
					Json.BeginObject();
					Json.WriteAttribute("category");
					Json.WriteStrValue("skins");
					Json.WriteAttribute("name");
					Json.WriteStrValue(aName);
					Json.WriteAttribute("uploader");
					Json.WriteStrValue(aPlayer);
					Json.WriteAttribute("content_base64");
					Json.WriteStrValue(Base64.c_str());
					Json.EndObject();
					char aUrl[512];
					AetherBuildApiUrl(aUrl, sizeof(aUrl), "/v1/assets/upload");
					s_pAetherAssetsCloudRequest = std::make_shared<CHttpRequest>(aUrl);
					s_pAetherAssetsCloudRequest->PostJson(Json.GetOutputString().c_str());
					Http()->Run(s_pAetherAssetsCloudRequest);
					s_AetherAssetsCloudAction = EAetherAssetsCloudAction::UPLOAD;
					str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Exported skin '%s' and publishing to cloud...", aName);
				}
			}
			else if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
			{
				str_copy(g_Config.m_ClAssetsEntities, aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(aName);
				RefreshSources();
				str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Exported and applied Entities asset '%s'.", aName);
			}
			else
			{
				const SAetherAssetCategory &Cat = s_aAssetEditorCategories[s_AetherAssetsEditorCategory];
				if(str_comp(Cat.m_pPath, "game") == 0)
				{
					str_copy(g_Config.m_ClAssetGame, aName);
					GameClient()->LoadGameSkin(aName);
				}
				else if(str_comp(Cat.m_pPath, "hud") == 0)
				{
					str_copy(g_Config.m_ClAssetHud, aName);
					GameClient()->LoadHudSkin(aName);
				}
				else if(str_comp(Cat.m_pPath, "particles") == 0)
				{
					str_copy(g_Config.m_ClAssetParticles, aName);
					GameClient()->LoadParticlesSkin(aName);
				}
				else if(str_comp(Cat.m_pPath, "emoticons") == 0)
				{
					str_copy(g_Config.m_ClAssetEmoticons, aName);
					GameClient()->LoadEmoticonsSkin(aName);
				}
				else if(str_comp(Cat.m_pPath, "extras") == 0)
				{
					str_copy(g_Config.m_ClAssetExtras, aName);
					GameClient()->LoadExtrasSkin(aName);
				}
				RefreshSources();
				str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Exported and applied %s asset '%s'.", Cat.m_pLabel, aName);
			}
		};

		if(!GameClient()->m_MenuBackground.Render())
			RenderBackground();
		Screen.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.58f), IGraphics::CORNER_NONE, 0.0f);
		CUIRect Window = s_AetherAssetsEditorWindow;
		Window.Draw(AetherPanelColor(0.74f), IGraphics::CORNER_ALL, 8.0f * S);
		Window.DrawOutline(AetherThemeColor(0.55f));

		CUIRect Inner;
		Window.Margin(8.0f * S, &Inner);
		CUIRect Toolbar, Body, Footer, AssetRow;
		Inner.HSplitTop(34.0f * S, &Toolbar, &Body);
		Body.HSplitBottom(24.0f * S, &Body, &Footer);
		Body.HSplitBottom(38.0f * S, &Body, &AssetRow);
		Body.HSplitBottom(10.0f * S, &Body, nullptr);

		CUIRect Close, ModeLabel, ModeDrop, ExportInputRect, Reload, Export, Grid;
		Toolbar.VSplitLeft(34.0f * S, &Close, &Toolbar);
		Toolbar.VSplitLeft(8.0f * S, nullptr, &Toolbar);
		Toolbar.VSplitLeft(54.0f * S, &ModeLabel, &Toolbar);
		Toolbar.VSplitLeft(166.0f * S, &ModeDrop, &Toolbar);
		Toolbar.VSplitLeft(10.0f * S, nullptr, &Toolbar);
		Toolbar.VSplitRight(420.0f * S, &ExportInputRect, &Toolbar);
		Toolbar.VSplitLeft(8.0f * S, nullptr, &Toolbar);
		ExportInputRect.VSplitRight(128.0f * S, &ExportInputRect, &Grid);
		Grid.VSplitLeft(8.0f * S, nullptr, &Grid);
		ExportInputRect.VSplitRight(116.0f * S, &ExportInputRect, &Export);
		Export.VSplitLeft(8.0f * S, nullptr, &Export);
		ExportInputRect.VSplitRight(108.0f * S, &ExportInputRect, &Reload);
		Reload.VSplitLeft(8.0f * S, nullptr, &Reload);

		if(DoButton_Menu(&s_CloseButton, "X", 0, &Close))
		{
			s_AetherAssetsEditorOpen = false;
			s_AetherAssetsEditorPickColor = false;
			AetherResetAssetsEditorLivePartTexture(Graphics());
		}
		Ui()->DoLabel(&ModeLabel, "Mode", 16.0f * S, TEXTALIGN_ML);
		static const char *s_apModeLabels[] = {"Game", "Emoticons", "Entities", "HUD", "Particles", "Extras", "Skins"};
		static const int s_aModeCategories[] = {0, 3, AETHER_ASSET_EDITOR_ENTITIES_CATEGORY, 1, 2, 4, AETHER_ASSET_EDITOR_SKINS_CATEGORY};
		auto CategoryToMode = [](int Category) {
			switch(Category)
			{
			case 0: return 0;
			case 3: return 1;
			case 1: return 3;
			case 2: return 4;
			case 4: return 5;
			case AETHER_ASSET_EDITOR_ENTITIES_CATEGORY: return 2;
			case AETHER_ASSET_EDITOR_SKINS_CATEGORY: return 6;
			default: return 0;
			}
		};
		s_ModeDropState.m_SelectionPopupContext.m_pScrollRegion = &s_ModeDropScroll;
		CUIRect CompactModeDrop = ModeDrop;
		CompactModeDrop.HMargin(2.0f * S, &CompactModeDrop);
		const int NewMode = Ui()->DoDropDown(&CompactModeDrop, CategoryToMode(s_AetherAssetsEditorCategory), s_apModeLabels, std::size(s_apModeLabels), s_ModeDropState);
		if(NewMode >= 0 && NewMode < (int)std::size(s_aModeCategories) && s_aModeCategories[NewMode] != s_AetherAssetsEditorCategory)
		{
			s_AetherAssetsEditorCategory = s_aModeCategories[NewMode];
			s_LastCategory = -1;
			s_aAetherAssetsEditorLiveSourcePath[0] = '\0';
			AetherResetAssetsEditorLivePartTexture(Graphics());
			str_copy(s_aAetherAssetsEditorStatus, "Mode changed. Donor and mixed previews refreshed.");
			return;
		}

		Ui()->DoEditBox(&s_ExportInput, &ExportInputRect, 15.0f * S, IGraphics::CORNER_ALL, {}, 6.0f * S);
		if(DoButton_Menu(&s_ReloadButton, "Reload", 0, &Reload))
		{
			RefreshSources();
			AetherClearAssetEditorImageCache();
			s_AetherAssetsEditorMixedDirty = true;
			s_aAetherAssetsEditorSourcePath[0] = '\0';
			s_aAetherAssetsEditorLiveSourcePath[0] = '\0';
			AetherResetAssetsEditorLivePartTexture(Graphics());
			str_copy(s_aAetherAssetsEditorStatus, "Source lists refreshed.");
		}
		if(DoButton_Menu(&s_ExportButton, "Export", 0, &Export))
			ExportCurrent();
		if(DoButton_CheckBox(&s_ShowGridButton, "Show Grid", s_AetherAssetsEditorShowGrid, &Grid))
			s_AetherAssetsEditorShowGrid = !s_AetherAssetsEditorShowGrid;

		Body.HSplitTop(8.0f * S, nullptr, &Body);
		CUIRect Left, Right;
		const float Gap = 12.0f * S;
		Body.VSplitLeft((Body.w - Gap) * 0.5f, &Left, &Right);
		Right.VSplitLeft(Gap, nullptr, &Right);
		Left.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f), IGraphics::CORNER_ALL, 7.0f * S);
		Right.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f), IGraphics::CORNER_ALL, 7.0f * S);
		Left.Margin(8.0f * S, &Left);
		Right.Margin(8.0f * S, &Right);

		CUIRect LeftTitle, DonorCanvas, RightTitle, MixedCanvas;
		Left.HSplitTop(28.0f * S, &LeftTitle, &Left);
		Right.HSplitTop(28.0f * S, &RightTitle, &Right);
		Ui()->DoLabel(&LeftTitle, "Donor (drag parts from left)", 16.0f * S, TEXTALIGN_ML);
		Ui()->DoLabel(&RightTitle, "Frankenstein (drop parts on right)", 16.0f * S, TEXTALIGN_ML);
		Left.HSplitTop(8.0f * S, nullptr, &DonorCanvas);
		Right.HSplitTop(8.0f * S, nullptr, &MixedCanvas);
		DonorCanvas.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f), IGraphics::CORNER_NONE, 0.0f);
		MixedCanvas.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f), IGraphics::CORNER_NONE, 0.0f);
		DonorCanvas.Margin(4.0f * S, &DonorCanvas);
		MixedCanvas.Margin(4.0f * S, &MixedCanvas);

		const CUIRect DonorImageRect = AetherFitImageRect(DonorCanvas, s_AetherAssetsEditorSourceWidth, s_AetherAssetsEditorSourceHeight);
		const CUIRect MixedImageRect = AetherFitImageRect(MixedCanvas, s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight);
		AetherDrawTextureInRect(Graphics(), s_AetherAssetsEditorSourceTexture, DonorImageRect);
		AetherDrawTextureInRect(Graphics(), s_AetherAssetsEditorMixedTexture, MixedImageRect);
		if(s_AetherAssetsEditorShowGrid)
		{
			AetherDrawAtlasGrid(DonorImageRect, s_AetherAssetsEditorSourceWidth, s_AetherAssetsEditorSourceHeight, 0.18f);
			AetherDrawAtlasGrid(MixedImageRect, s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight, 0.18f);
		}

		auto SpriteHitRect = [&](const CUIRect &ImageRect, int ImageW, int ImageH, int SpriteId, CUIRect &Hit) {
			if(SpriteId >= NUM_SPRITES || ImageW <= 0 || ImageH <= 0)
				return false;
			SAetherSpriteRect SpriteRect;
			CImageInfo DummyImage;
			DummyImage.m_Width = ImageW;
			DummyImage.m_Height = ImageH;
			if(!AetherGetAssetPartRect(SpriteId, DummyImage, SpriteRect))
				return false;
			Hit.x = ImageRect.x + SpriteRect.m_X / (float)ImageW * ImageRect.w;
			Hit.y = ImageRect.y + SpriteRect.m_Y / (float)ImageH * ImageRect.h;
			Hit.w = SpriteRect.m_W / (float)ImageW * ImageRect.w;
			Hit.h = SpriteRect.m_H / (float)ImageH * ImageRect.h;
			const float MinHit = 9.0f * S;
			if(Hit.w < MinHit)
			{
				const float Diff = MinHit - Hit.w;
				Hit.x -= Diff * 0.5f;
				Hit.w = MinHit;
			}
			if(Hit.h < MinHit)
			{
				const float Diff = MinHit - Hit.h;
				Hit.y -= Diff * 0.5f;
				Hit.h = MinHit;
			}
			return true;
		};

		if(NumParts > 0 && s_AetherAssetsEditorEditPanelOpen && s_AetherAssetsEditorMixedDirty && !s_AetherAssetsEditorForcePreviewBuild)
		{
			const SAetherAssetPartState &LiveState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][s_AetherAssetsEditorPartIndex];
			if(LiveState.m_Enabled && LiveState.m_SourceIndex >= 0 && LiveState.m_SourceIndex < (int)vNames.size() && LiveState.m_SourcePartIndex >= 0 && LiveState.m_SourcePartIndex < NumParts)
			{
				char aLivePath[IO_MAX_PATH_LENGTH];
				ResolveAssetEditorPath(LiveState.m_SourceIndex, aLivePath, sizeof(aLivePath));
				if(str_comp(s_aAetherAssetsEditorLiveSourcePath, aLivePath) != 0)
				{
					Graphics()->UnloadTexture(&s_AetherAssetsEditorLiveSourceTexture);
					str_copy(s_aAetherAssetsEditorLiveSourcePath, aLivePath);
					if(const CImageInfo *pLiveImage = AetherCachedAssetEditorImage(Graphics(), s_AetherAssetsEditorCategory, vNames[LiveState.m_SourceIndex].c_str()))
					{
						s_AetherAssetsEditorLiveSourceWidth = pLiveImage->m_Width;
						s_AetherAssetsEditorLiveSourceHeight = pLiveImage->m_Height;
					}
					else
					{
						s_AetherAssetsEditorLiveSourceWidth = 0;
						s_AetherAssetsEditorLiveSourceHeight = 0;
					}
					s_AetherAssetsEditorLiveSourceTexture = Graphics()->LoadTexture(s_aAetherAssetsEditorLiveSourcePath, IStorage::TYPE_ALL);
				}

				const SAetherAssetPart &TargetPart = Parts[s_AetherAssetsEditorPartIndex];
				const SAetherAssetPart &SourcePart = Parts[LiveState.m_SourcePartIndex];
				ColorRGBA LiveColor(1.0f, 1.0f, 1.0f, std::clamp(LiveState.m_Opacity / 100.0f, 0.0f, 1.0f));
				if(LiveState.m_TintEnabled)
				{
					const ColorRGBA Tint = color_cast<ColorRGBA>(ColorHSLA(LiveState.m_Color, false));
					LiveColor.r = Tint.r;
					LiveColor.g = Tint.g;
					LiveColor.b = Tint.b;
				}
				for(int Sprite = 0; Sprite < TargetPart.m_NumSprites; ++Sprite)
				{
					const int SourceSprite = SourcePart.m_aSprites[std::min(Sprite, SourcePart.m_NumSprites - 1)];
					const int TargetSprite = TargetPart.m_aSprites[Sprite];
					CUIRect TargetRect;
					if(!SpriteHitRect(MixedImageRect, s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight, TargetSprite, TargetRect))
						continue;
					if(s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY)
					{
						char aLivePartKey[512];
						str_format(aLivePartKey, sizeof(aLivePartKey), "%s|%d|%d|%d|%d|%u|%d|%d|%d|%d|%d",
							vNames[LiveState.m_SourceIndex].c_str(), SourceSprite, TargetSprite,
							s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight,
							LiveState.m_Color, LiveState.m_Opacity,
							LiveState.m_TintEnabled ? 1 : 0,
							LiveState.m_OnlyColored ? 1 : 0,
							LiveState.m_FullBright ? 1 : 0,
							LiveState.m_FillInside ? 1 : 0);
						if(str_comp(s_aAetherAssetsEditorLivePartKey, aLivePartKey) != 0)
						{
							AetherResetAssetsEditorLivePartTexture(Graphics());
							if(const CImageInfo *pLiveImage = AetherCachedAssetEditorImage(Graphics(), s_AetherAssetsEditorCategory, vNames[LiveState.m_SourceIndex].c_str()))
							{
								CImageInfo LivePartImage;
								if(AetherBuildAssetPartPreviewImage(*pLiveImage, SourceSprite, TargetSprite, s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight, LiveState, LivePartImage))
								{
									s_AetherAssetsEditorLivePartTexture = Graphics()->LoadTextureRawMove(LivePartImage, 0, "aether-assets-editor-live-part");
									str_copy(s_aAetherAssetsEditorLivePartKey, aLivePartKey);
								}
							}
						}
						AetherDrawTextureInRect(Graphics(), s_AetherAssetsEditorLivePartTexture, TargetRect);
						continue;
					}
					SAetherSpriteRect SourceRect;
					CImageInfo SourceMeta;
					SourceMeta.m_Width = s_AetherAssetsEditorLiveSourceWidth;
					SourceMeta.m_Height = s_AetherAssetsEditorLiveSourceHeight;
					if(AetherGetAssetPartRect(SourceSprite, SourceMeta, SourceRect))
						AetherDrawTextureSubsetInRect(Graphics(), s_AetherAssetsEditorLiveSourceTexture, TargetRect, s_AetherAssetsEditorLiveSourceWidth, s_AetherAssetsEditorLiveSourceHeight, SourceRect, LiveColor);
				}
			}
		};

		auto SampleTintFromImage = [&](const CImageInfo &Image, const CUIRect &ImageRect, const char *pSourceLabel) {
			int PixelX = 0;
			int PixelY = 0;
			if(!AetherAtlasPixelFromScreen(ImageRect, Image.m_Width, Image.m_Height, Ui()->MouseX(), Ui()->MouseY(), PixelX, PixelY))
				return false;
			ColorRGBA Sample = Image.PixelColor(PixelX, PixelY);
			Sample.a = 1.0f;
			SAetherAssetPartState &EditState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][s_AetherAssetsEditorPartIndex];
			EditState.m_Color = color_cast<ColorHSLA>(Sample).Pack(false);
			EditState.m_TintEnabled = true;
			s_AetherAssetsEditorPickColor = false;
			s_AetherAssetsEditorMixedDirty = true;
			s_AetherAssetsEditorForcePreviewBuild = true;
			str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Picked tint from %s: #%02X%02X%02X.", pSourceLabel, (int)(std::clamp(Sample.r, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(Sample.g, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(Sample.b, 0.0f, 1.0f) * 255.0f));
			return true;
		};

		const bool PickingColorThisFrame = s_AetherAssetsEditorPickColor;
		if(PickingColorThisFrame && NumParts > 0 && Ui()->MouseButtonClicked(0))
		{
			bool Picked = false;
			if(Ui()->MouseInside(&DonorImageRect))
			{
				if(const CImageInfo *pPickImage = AetherCachedAssetEditorImage(Graphics(), s_AetherAssetsEditorCategory, vNames[s_AetherAssetsEditorDonorIndex].c_str()))
					Picked = SampleTintFromImage(*pPickImage, DonorImageRect, "donor");
			}
			else if(Ui()->MouseInside(&MixedImageRect))
			{
				CImageInfo PickImage;
				if(AetherBuildMixedAssetImage(Graphics(), s_AetherAssetsEditorCategory, vNames, s_AetherAssetsEditorBaseIndex, s_aaAetherAssetsEditorStates, PickImage))
				{
					Picked = SampleTintFromImage(PickImage, MixedImageRect, "mixed");
					PickImage.Free();
				}
			}
			if(!Picked && (Ui()->MouseInside(&DonorImageRect) || Ui()->MouseInside(&MixedImageRect)))
				str_copy(s_aAetherAssetsEditorStatus, "Could not pick a color from this atlas pixel.");
		}

		s_AetherAssetsEditorDropPart = -1;
		for(int Part = 0; Part < NumParts && Part < (int)s_aDonorCanvasButtons.size(); ++Part)
		{
			const SAetherAssetPart &PartDef = Parts[Part];
			for(int Sprite = 0; Sprite < PartDef.m_NumSprites; ++Sprite)
			{
				CUIRect Hit;
				if(!SpriteHitRect(DonorImageRect, s_AetherAssetsEditorSourceWidth, s_AetherAssetsEditorSourceHeight, PartDef.m_aSprites[Sprite], Hit))
					continue;
				const bool Hovered = Ui()->MouseInside(&Hit);
				if(Hovered)
					Hit.DrawOutline(ColorRGBA(1.0f, 0.55f, 0.05f, 0.95f));
				if(!PickingColorThisFrame && Ui()->DoButtonLogic(&s_aDonorCanvasButtons[Part], Part == s_AetherAssetsEditorPartIndex, &Hit, BUTTONFLAG_LEFT))
					s_AetherAssetsEditorPartIndex = Part;
				if(!PickingColorThisFrame && Ui()->CheckActiveItem(&s_aDonorCanvasButtons[Part]) && Ui()->MouseButton(0))
				{
					s_AetherAssetsEditorDragSource = s_AetherAssetsEditorDonorIndex;
					s_AetherAssetsEditorDragPart = Part;
				}
			}
		}
		for(int Part = 0; Part < NumParts && Part < (int)s_aMixedCanvasButtons.size(); ++Part)
		{
			const SAetherAssetPart &PartDef = Parts[Part];
			for(int Sprite = 0; Sprite < PartDef.m_NumSprites; ++Sprite)
			{
				CUIRect Hit;
				if(!SpriteHitRect(MixedImageRect, s_AetherAssetsEditorMixedWidth, s_AetherAssetsEditorMixedHeight, PartDef.m_aSprites[Sprite], Hit))
					continue;
				if(Ui()->MouseInside(&Hit))
					s_AetherAssetsEditorDropPart = Part;
				const bool Selected = Part == s_AetherAssetsEditorPartIndex;
				if(Ui()->MouseInside(&Hit) || (s_AetherAssetsEditorDragSource >= 0 && s_AetherAssetsEditorDropPart == Part))
					Hit.DrawOutline(ColorRGBA(1.0f, 0.55f, 0.05f, 0.95f));
				if(Selected)
					Hit.DrawOutline(ColorRGBA(0.25f, 0.55f, 1.0f, 1.0f));
				const int Button = PickingColorThisFrame ? 0 : Ui()->DoButtonLogic(&s_aMixedCanvasButtons[Part], Selected, &Hit, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(Button == 1)
				{
					s_AetherAssetsEditorPartIndex = Part;
					s_AetherAssetsEditorEditPanelOpen = true;
				}
				else if(Button == 2)
				{
					s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][Part] = SAetherAssetPartState{};
					s_AetherAssetsEditorPartIndex = Part;
					s_AetherAssetsEditorEditPanelOpen = true;
					s_AetherAssetsEditorMixedDirty = true;
					str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "Reset '%s'.", Parts[Part].m_pName);
				}
			}
		}
		if(s_AetherAssetsEditorDragSource >= 0 && !Ui()->MouseButton(0))
		{
			int DropPart = s_AetherAssetsEditorDropPart;
			if(DropPart < 0 && Ui()->MouseInside(&MixedCanvas))
				DropPart = s_AetherAssetsEditorDragPart;
			if(DropPart >= 0 && DropPart < NumParts)
			{
				SAetherAssetPartState &DropState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][DropPart];
				DropState = SAetherAssetPartState{};
				DropState.m_SourceIndex = s_AetherAssetsEditorDragSource;
				DropState.m_SourcePartIndex = s_AetherAssetsEditorDragPart;
				DropState.m_Enabled = true;
				s_AetherAssetsEditorPartIndex = DropPart;
				s_AetherAssetsEditorEditPanelOpen = true;
				s_AetherAssetsEditorMixedDirty = true;
				str_format(s_aAetherAssetsEditorStatus, sizeof(s_aAetherAssetsEditorStatus), "%s -> %s from %s", Parts[std::clamp(s_AetherAssetsEditorDragPart, 0, NumParts - 1)].m_pName, Parts[DropPart].m_pName, vNames[s_AetherAssetsEditorDragSource].c_str());
			}
			s_AetherAssetsEditorDragSource = -1;
			s_AetherAssetsEditorDragPart = -1;
		}

		if(NumParts > 0 && s_AetherAssetsEditorEditPanelOpen)
		{
			SAetherAssetPartState &EditState = s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][s_AetherAssetsEditorPartIndex];
			AetherClampAssetEditorState(EditState, (int)vNames.size());
			EditState.m_SourcePartIndex = std::clamp(EditState.m_SourcePartIndex, 0, maximum(0, NumParts - 1));
			auto EnsureEditableBaseSource = [&]() {
				if(EditState.m_Enabled)
					return;
				EditState.m_Enabled = true;
				EditState.m_SourceIndex = s_AetherAssetsEditorBaseIndex;
				EditState.m_SourcePartIndex = s_AetherAssetsEditorPartIndex;
			};
			if(!s_AetherAssetsEditorEditPanelInit)
			{
				s_AetherAssetsEditorEditPanel = {
					Window.x + Window.w - 384.0f * S,
					Window.y + 58.0f * S,
					360.0f * S,
					282.0f * S};
				s_AetherAssetsEditorEditPanelInit = true;
			}
			s_AetherAssetsEditorEditPanel.w = 360.0f * S;
			s_AetherAssetsEditorEditPanel.h = 282.0f * S;
			s_AetherAssetsEditorEditPanel.x = std::clamp(s_AetherAssetsEditorEditPanel.x, Window.x + 10.0f * S, Window.x + Window.w - s_AetherAssetsEditorEditPanel.w - 10.0f * S);
			s_AetherAssetsEditorEditPanel.y = std::clamp(s_AetherAssetsEditorEditPanel.y, Window.y + 44.0f * S, Window.y + Window.h - s_AetherAssetsEditorEditPanel.h - 44.0f * S);
			CUIRect EditPanel = s_AetherAssetsEditorEditPanel;
			EditPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.66f), IGraphics::CORNER_ALL, 7.0f * S);
			EditPanel.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f));
			EditPanel.Margin(8.0f * S, &EditPanel);
			CUIRect Header, Row;
			char aPartLabel[128];
			str_format(aPartLabel, sizeof(aPartLabel), "Part: %s", Parts[s_AetherAssetsEditorPartIndex].m_pName);
			EditPanel.HSplitTop(24.0f * S, &Header, &EditPanel);
			Header.Draw(AetherThemeColor(0.16f), IGraphics::CORNER_ALL, 5.0f * S);
			CUIRect HeaderDrag = Header;
			CUIRect HeaderLabel, HeaderClose;
			Header.Margin(4.0f * S, &Header);
			Header.VSplitRight(24.0f * S, &HeaderLabel, &HeaderClose);
			Ui()->DoLabel(&HeaderLabel, aPartLabel, 13.0f * S, TEXTALIGN_ML);
			if(DoButton_Menu(&s_EditPanelCloseButton, "X", 0, &HeaderClose, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f * S, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f)))
			{
				s_AetherAssetsEditorEditPanelOpen = false;
				s_AetherAssetsEditorPickColor = false;
			}
			if(Ui()->MouseInside(&HeaderDrag) && !Ui()->MouseInside(&HeaderClose) && Ui()->MouseButtonClicked(0))
			{
				Ui()->SetActiveItem(&s_EditPanelDrag);
				s_AetherAssetsEditorEditDragOffsetX = Ui()->MouseX() - s_AetherAssetsEditorEditPanel.x;
				s_AetherAssetsEditorEditDragOffsetY = Ui()->MouseY() - s_AetherAssetsEditorEditPanel.y;
			}
			if(Ui()->CheckActiveItem(&s_EditPanelDrag))
			{
				if(Ui()->MouseButton(0))
				{
					s_AetherAssetsEditorEditPanel.x = std::clamp(Ui()->MouseX() - s_AetherAssetsEditorEditDragOffsetX, Window.x + 10.0f * S, Window.x + Window.w - s_AetherAssetsEditorEditPanel.w - 10.0f * S);
					s_AetherAssetsEditorEditPanel.y = std::clamp(Ui()->MouseY() - s_AetherAssetsEditorEditDragOffsetY, Window.y + 44.0f * S, Window.y + Window.h - s_AetherAssetsEditorEditPanel.h - 44.0f * S);
				}
				else
					Ui()->SetActiveItem(nullptr);
			}
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			if(DoButton_CheckBox(&EditState.m_Enabled, "Use donor part", EditState.m_Enabled, &Row))
			{
				const bool WasEnabled = EditState.m_Enabled;
				EditState.m_Enabled = !EditState.m_Enabled;
				if(EditState.m_Enabled && !WasEnabled)
				{
					EditState.m_SourceIndex = s_AetherAssetsEditorDonorIndex;
					EditState.m_SourcePartIndex = s_AetherAssetsEditorPartIndex;
				}
				s_AetherAssetsEditorMixedDirty = true;
			}
			EditPanel.HSplitTop(20.0f * S, &Row, &EditPanel);
			char aSourceLabel[192];
			if(EditState.m_Enabled)
				str_format(aSourceLabel, sizeof(aSourceLabel), "Source: %s from %s", Parts[EditState.m_SourcePartIndex].m_pName, vNames[EditState.m_SourceIndex].c_str());
			else
				str_copy(aSourceLabel, "Source: main asset");
			TextRender()->TextColor(0.75f, 0.82f, 0.92f, 1.0f);
			Ui()->DoLabel(&Row, aSourceLabel, 11.5f * S, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			if(DoButton_CheckBox(&s_TintEnabledButton, "Tint color", EditState.m_TintEnabled, &Row))
			{
				EnsureEditableBaseSource();
				EditState.m_TintEnabled = !EditState.m_TintEnabled;
				s_AetherAssetsEditorMixedDirty = true;
			}
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			if(DoButton_CheckBox(&s_OnlyColoredButton, "Only colored pixels", EditState.m_OnlyColored, &Row))
			{
				EnsureEditableBaseSource();
				EditState.m_OnlyColored = !EditState.m_OnlyColored;
				s_AetherAssetsEditorMixedDirty = true;
			}
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			if(DoButton_CheckBox(&s_FullBrightButton, "Full bright", EditState.m_FullBright, &Row))
			{
				EnsureEditableBaseSource();
				EditState.m_FullBright = !EditState.m_FullBright;
				s_AetherAssetsEditorMixedDirty = true;
			}
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			if(DoButton_CheckBox(&s_FillInsideButton, "Keep black outline, fill inside", EditState.m_FillInside, &Row))
			{
				EnsureEditableBaseSource();
				EditState.m_FillInside = !EditState.m_FillInside;
				s_AetherAssetsEditorMixedDirty = true;
			}
			const unsigned PrevColor = EditState.m_Color;
			const int PrevOpacity = EditState.m_Opacity;
			EditPanel.HSplitTop(28.0f * S, &Row, &EditPanel);
			CUIRect ColorLabel, ColorButton, ResetColor;
			Row.VSplitRight(58.0f * S, &ColorLabel, &ResetColor);
			ColorLabel.VSplitRight(Row.h, &ColorLabel, &ColorButton);
			ColorLabel.VSplitRight(8.0f * S, &ColorLabel, nullptr);
			Ui()->DoLabel(&ColorLabel, "Color", 13.0f * S, TEXTALIGN_ML);
			const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorButton, &EditState.m_Color, false);
			if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &EditState.m_Color)
			{
				EnsureEditableBaseSource();
				EditState.m_Color = PickedColor.Pack(false);
				EditState.m_TintEnabled = true;
			}
			ResetColor.HMargin(2.0f * S, &ResetColor);
			bool ColorWasReset = false;
			if(DoButton_Menu(&s_ColorResetButton, "Reset", 0, &ResetColor, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f * S, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
			{
				EnsureEditableBaseSource();
				EditState.m_Color = 255;
				EditState.m_TintEnabled = false;
				s_AetherAssetsEditorMixedDirty = true;
				s_AetherAssetsEditorForcePreviewBuild = true;
				ColorWasReset = true;
			}
			EditPanel.HSplitTop(24.0f * S, &Row, &EditPanel);
			CUIRect PickButton, PickHint;
			Row.VSplitLeft(112.0f * S, &PickButton, &PickHint);
			PickHint.VSplitLeft(8.0f * S, nullptr, &PickHint);
			if(DoButton_Menu(&s_PickColorButton, s_AetherAssetsEditorPickColor ? "Picking..." : "Pick color", s_AetherAssetsEditorPickColor, &PickButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f * S, 0.1f, s_AetherAssetsEditorPickColor ? AetherThemeColor(0.42f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.20f)))
			{
				s_AetherAssetsEditorPickColor = !s_AetherAssetsEditorPickColor;
				str_copy(s_aAetherAssetsEditorStatus, s_AetherAssetsEditorPickColor ? "Pick color: click any donor or mixed atlas pixel." : "Color pick cancelled.");
			}
			Ui()->DoLabel(&PickHint, "Click an atlas pixel", 12.0f * S, TEXTALIGN_ML);
			EditPanel.HSplitTop(22.0f * S, &Row, &EditPanel);
			Ui()->DoScrollbarOption(&EditState.m_Opacity, &EditState.m_Opacity, &Row, "Opacity", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			if(EditState.m_Color != PrevColor || EditState.m_Opacity != PrevOpacity)
			{
				EnsureEditableBaseSource();
				if(EditState.m_Color != PrevColor && !ColorWasReset)
					EditState.m_TintEnabled = true;
				s_AetherAssetsEditorMixedDirty = true;
				const bool DeferEntityEditCommit = !ColorWasReset &&
								   s_AetherAssetsEditorCategory == AETHER_ASSET_EDITOR_ENTITIES_CATEGORY &&
								   s_AetherAssetsEditorMixedTexture.IsValid() &&
								   ((Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &EditState.m_Color) ||
									   Ui()->CheckActiveItem(&EditState.m_Opacity));
				s_AetherAssetsEditorForcePreviewBuild = !DeferEntityEditCommit;
				if(!DeferEntityEditCommit)
					s_LastMixedPreviewBuildTime = 0;
			}
		}

		CUIRect DonorAssetRow, MainAssetRow;
		AssetRow.VSplitLeft((AssetRow.w - Gap) * 0.5f, &DonorAssetRow, &MainAssetRow);
		MainAssetRow.VSplitLeft(Gap, nullptr, &MainAssetRow);
		CUIRect DonorLabel, DonorDrop, MainLabel, MainDrop, ResetAll;
		DonorAssetRow.VSplitLeft(108.0f * S, &DonorLabel, &DonorDrop);
		MainAssetRow.VSplitLeft(96.0f * S, &MainLabel, &MainAssetRow);
		MainAssetRow.VSplitRight(132.0f * S, &MainDrop, &ResetAll);
		MainDrop.VSplitRight(8.0f * S, &MainDrop, nullptr);
		Ui()->DoLabel(&DonorLabel, "Donor Asset", 14.0f * S, TEXTALIGN_ML);
		s_DonorDropState.m_SelectionPopupContext.m_pScrollRegion = &s_DonorDropScroll;
		const int NewDonor = Ui()->DoDropDown(&DonorDrop, s_AetherAssetsEditorDonorIndex, vpNamePtrs.data(), (int)vpNamePtrs.size(), s_DonorDropState);
		if(NewDonor != s_AetherAssetsEditorDonorIndex)
		{
			s_AetherAssetsEditorDonorIndex = NewDonor;
			s_aAetherAssetsEditorSourcePath[0] = '\0';
		}
		Ui()->DoLabel(&MainLabel, "Main Asset", 14.0f * S, TEXTALIGN_ML);
		s_MainDropState.m_SelectionPopupContext.m_pScrollRegion = &s_MainDropScroll;
		const int NewBase = Ui()->DoDropDown(&MainDrop, s_AetherAssetsEditorBaseIndex, vpNamePtrs.data(), (int)vpNamePtrs.size(), s_MainDropState);
		if(NewBase != s_AetherAssetsEditorBaseIndex)
		{
			s_AetherAssetsEditorBaseIndex = NewBase;
			AetherClearAssetEditorImageCache();
			s_AetherAssetsEditorMixedDirty = true;
		}
		if(DoButton_Menu(&s_ResetAllButton, "Reset All", 0, &ResetAll))
		{
			for(int i = 0; i < NumParts && i < AETHER_ASSET_EDITOR_MAX_PARTS; ++i)
				s_aaAetherAssetsEditorStates[s_AetherAssetsEditorCategory][i] = SAetherAssetPartState{};
			s_AetherAssetsEditorMixedDirty = true;
			str_copy(s_aAetherAssetsEditorStatus, "All mixed parts reset.");
		}

		CUIRect Status, Help;
		Footer.VSplitLeft(Footer.w * 0.55f, &Status, &Help);
		TextRender()->TextColor(0.45f, 1.0f, 0.45f, 1.0f);
		Ui()->DoLabel(&Status, s_aAetherAssetsEditorStatus, 12.0f * S, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Ui()->DoLabel(&Help, "Drag from left to right. Right-click a Frankenstein part to reset it.", 12.0f * S, TEXTALIGN_MR);

		if(s_AetherAssetsEditorDragSource >= 0 && Ui()->MouseButton(0))
		{
			const int SourcePartIndex = std::clamp(s_AetherAssetsEditorDragPart, 0, maximum(0, NumParts - 1));
			CUIRect Ghost = {Ui()->MouseX() + 12.0f * S, Ui()->MouseY() + 12.0f * S, 270.0f * S, 38.0f * S};
			Ghost.Draw(AetherThemeColor(0.40f), IGraphics::CORNER_ALL, 5.0f * S);
			Ghost.Margin(6.0f * S, &Ghost);
			CUIRect Thumb, Label;
			Ghost.VSplitLeft(28.0f * S, &Thumb, &Label);
			Label.VSplitLeft(8.0f * S, nullptr, &Label);
			Thumb.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 4.0f * S);
			Thumb.Margin(3.0f * S, &Thumb);
			if(NumParts > 0 && s_AetherAssetsEditorSourceTexture.IsValid())
			{
				SAetherSpriteRect SourceRect;
				CImageInfo SourceMeta;
				SourceMeta.m_Width = s_AetherAssetsEditorSourceWidth;
				SourceMeta.m_Height = s_AetherAssetsEditorSourceHeight;
				if(AetherGetAssetPartRect(Parts[SourcePartIndex].m_aSprites[0], SourceMeta, SourceRect))
					AetherDrawTextureSubsetInRect(Graphics(), s_AetherAssetsEditorSourceTexture, Thumb, s_AetherAssetsEditorSourceWidth, s_AetherAssetsEditorSourceHeight, SourceRect);
			}
			char aGhost[192];
			str_format(aGhost, sizeof(aGhost), "%s from %s", NumParts > 0 ? Parts[SourcePartIndex].m_pName : "part", vNames[s_AetherAssetsEditorDragSource].c_str());
			Ui()->DoLabel(&Label, aGhost, 12.0f * S, TEXTALIGN_ML);
		}
		return;
	}

}

void CMenus::RenderSettingsAetherSaveUnsentMessages(CUIRect Body)
{
	const float S = AetherSettingsScale();
	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	TextRender()->TextColor(0.75f, 0.80f, 0.88f, 1.0f);
	Ui()->DoLabel(&Body, "Restores the current chat draft after Escape, reconnects and map changes.", 12.0f * S, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CMenus::RenderSettingsAetherMusicPlayer(CUIRect Body)
{
	static CButtonContainer s_BackgroundColorReset;
	const float S = AetherSettingsScale();

	Body.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(12.0f * S, &Body);
	CUIRect Control;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeMusicDynamicColor, "Dynamic cover color", g_Config.m_AeMusicDynamicColor, &Control))
		g_Config.m_AeMusicDynamicColor ^= 1;
	if(!g_Config.m_AeMusicDynamicColor)
		DoLine_ColorPicker(&s_BackgroundColorReset, 25.0f * S, 13.0f * S, 4.0f * S, &Body, "Background color", &g_Config.m_AeMusicBackgroundColor, ColorRGBA(0.10f, 0.10f, 0.10f), false);
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeMusicOpacity, &g_Config.m_AeMusicOpacity, &Control, "Panel opacity", 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(DoButton_CheckBox(&g_Config.m_AeMusicVisualizer, "Visualizer", g_Config.m_AeMusicVisualizer, &Control))
		g_Config.m_AeMusicVisualizer ^= 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	if(g_Config.m_AeMusicVisualizerStyle > 1)
		g_Config.m_AeMusicVisualizerStyle = 0;
	static CButtonContainer s_aVisualizerStyleButtons[2];
	CUIRect Label, Buttons, Button;
	AetherOptionRow(Control, S, &Label, &Buttons);
	Ui()->DoLabel(&Label, "Visualizer style", 14.0f * S, TEXTALIGN_ML);
	const float ButtonWidth = Buttons.w / 2.0f;
	Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
	if(DoButton_Menu(&s_aVisualizerStyleButtons[0], "Bars", g_Config.m_AeMusicVisualizerStyle == 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		g_Config.m_AeMusicVisualizerStyle = 0;
	Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
	if(DoButton_Menu(&s_aVisualizerStyleButtons[1], "Mountain", g_Config.m_AeMusicVisualizerStyle == 1, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		g_Config.m_AeMusicVisualizerStyle = 1;
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeMusicVisualizerSensitivity, &g_Config.m_AeMusicVisualizerSensitivity, &Control, "Visualizer sensitivity", 50, 1500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Body.HSplitTop(22.0f * S, &Control, &Body);
	Ui()->DoScrollbarOption(&g_Config.m_AeMusicVisualizerGlow, &g_Config.m_AeMusicVisualizerGlow, &Control, "Visualizer glow", 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
}

void CMenus::RenderSettingsAether(CUIRect MainView)
{
	if(s_AetherAssetsEditorOpen)
	{
		return;
	}

	UpdateAetherSettingsScale(MainView);
	const float S = AetherSettingsScale();
	static const std::array<const char *, 7> s_apMusicChildren = {
		"Dynamic cover color",
		"Background color",
		"Panel opacity",
		"Visualizer",
		"Visualizer style",
		"Visualizer sensitivity",
		"Visualizer glow"};
	static const std::array<const char *, 6> s_apKeystrokesChildren = {
		"Movement keys",
		"Horizontal layout",
		"Jump key",
		"Jump label",
		"M1",
		"Mouse buttons"};
	static const std::array<const char *, 11> s_apInputVisualizerChildren = {
		"Input history",
		"Flow speed",
		"Local input",
		"Movement",
		"Jump",
		"Mouse buttons",
		"M1",
		"Key names",
		"Spectated input",
		"Demo input",
		"Hook markers"};
	static const std::array<const char *, 9> s_apStabilityTrainerChildren = {
		"Velocity bar",
		"Spectated input",
		"Quality color",
		"Average ticks",
		"Bar glide",
		"Velocity scale",
		"HUD editor",
		"Trainer color",
		"Sharp corners"};
	static const std::array<const char *, 3> s_apSessionStatsChildren = {
		"Session time",
		"Deaths",
		"Personal stats"};
	static const std::array<const char *, 2> s_apRealHitboxChildren = {
		"Hitbox color",
		"Collision box"};
	static const std::array<const char *, 3> s_apNinjaTeePreviewChildren = {
		"Ninja preview",
		"Body color",
		"Feet color"};
	static const std::array<const char *, 3> s_apNinjaTimerChildren = {
		"Timer",
		"HUD scale",
		"HUD editor"};
	static const std::array<const char *, 16> s_apSweatWeaponChildren = {
		"Crystal laser",
		"Sand shotgun",
		"Entities laser",
		"Custom rifle colors",
		"Custom shotgun colors",
		"Glow strength",
		"Shine animation",
		"Electric arcs",
		"Electric arc opacity",
		"Laser thickness",
		"Entities laser override",
		"Sparkles",
		"Crystal glow",
		"Crystal core",
		"Entities glow",
		"Entities core"};
	static const std::array<const char *, 8> s_apOrbitAuraChildren = {
		"Idle mode",
		"Idle delay",
		"Fade duration",
		"Orbit particles",
		"Energy ring",
		"Flame aura",
		"Radius",
		"Particles",
	};
	static const std::array<const char *, 4> s_apJellyTeeChildren = {
		"Jelly others",
		"Jelly strength",
		"Jelly duration",
		"Squash stretch"};
	static const std::array<const char *, 6> s_apFinishPredictionChildren = {
		"Time left",
		"Finish time",
		"Milliseconds",
		"Percentage",
		"Show always",
		"HUD editor"};
	static const std::array<const char *, 8> s_apThreeDParticlesChildren = {
		"Particle count",
		"Particle type",
		"Cube",
		"Heart",
		"Mixed",
		"Size",
		"Speed",
		"Glow"};
	static const std::array<const char *, 3> s_apLoadingThemeChildren = {
		"Loading screen",
		"Menu theme",
		"Background"};
	static const std::array<const char *, 5> s_apBadgesChildren = {
		"Nameplates",
		"Scoreboard",
		"Client badges only",
		"Friend heart",
		"Refresh now"};
	static const std::array<const char *, 8> s_apPingChildren = {
		"Ping wheel",
		"Help ping",
		"Danger ping",
		"Come ping",
		"Wait ping",
		"Auto help",
		"Frozen ally",
		"Offscreen indicator"};
	static const std::array<const char *, 9> s_apChatBubblesChildren = {
		"Bubble duration",
		"Bubble opacity",
		"Bubble width",
		"Visible tees only",
		"Colored messages",
		"Whispers",
		"Live draft",
		"Own sent messages",
		"Stacked messages"};
	static const std::array<const char *, 25> s_apBlockAwarenessChildren = {
		"Color players",
		"Allies keep real skins",
		"Color names",
		"Color natural tees",
		"Naturals keep real skins",
		"Color natural names",
		"Enemy size",
		"Self freeze timer",
		"Ally freeze alert",
		"Enemy count HUD",
		"Enemy scan range",
		"Enemy color",
		"Helper color",
		"Ally color",
		"Natural color",
		"Enemy scale",
		"Helper scale",
		"Ally scale",
		"Natural scale",
		"Enemy name opacity",
		"Helper name opacity",
		"Ally name opacity",
		"Natural name opacity",
		"Warlist export",
		"Warlist import"};
	static const std::array<const char *, 2> s_apAutoTeamLockChildren = {
		"Lock delay",
		"/lock"};
	static const std::array<const char *, 6> s_apFocusModeChildren = {
		"Keybind",
		"Hide all UI",
		"Hide nameplates",
		"Keep music player",
		"Hide HUD",
		"Hide chat"};
	static const std::array<const char *, 2> s_apSnapTapChildren = {
		"Last pressed direction",
		"Left right movement"};
	static const std::array<const char *, 3> s_apGoresModeChildren = {
		"Disable if weapons",
		"Legacy fire flow",
		"Return to gun"};
	static const std::array<const char *, 6> s_apDdraceConfigsChildren = {
		"Show Others Toggle",
		"One Key Super",
		"Deep-Fly Toggle",
		"Edge2Edge Freeze Tiles",
		"Keybinds",
		"Open folder"};
	static const std::array<const char *, 23> s_apFastInputChildren = {
		"TClient",
		"Adaptive",
		"Saiko+",
		"Movement amount",
		"Hook fire amount",
		"Saiko amount",
		"Saiko+ input others",
		"Instant A/D response",
		"Brake amount",
		"Adaptive input other tees",
		"Other tees feel",
		"Smooth",
		"Precision",
		"Aggressive",
		"Other tees amount",
		"Correction sharpness",
		"Sub-Tick aiming",
		"Auto margin",
		"Ping assist",
		"Interaction assist",
		"Interaction strength",
		"Debug"};
	static const std::array<const char *, 3> s_apFastSpecChildren = {
		"Enable Fast Spec",
		"+ae_fast_spec",
		"Status"};
	static const std::array<const char *, 3> s_apTranslatorChildren = {
		"Auto translate incoming",
		"Translate my messages",
		"Target language",
	};
	static const std::array<const char *, 2> s_apSilentTypingChildren = {
		"Typing bubble",
		"Chat privacy"};
	static const std::array<const char *, 5> s_apFailSoundChildren = {
		"Local freeze fail",
		"Team freeze fail",
		"Last unfrozen",
		"Volume",
		"Sound file"};
	static const std::array<const char *, 7> s_apSoundChildren = {
		"Local hook sounds",
		"Hook sounds",
		"Hammer sounds",
		"Weapon switch sounds",
		"Double jump sounds",
		"Local kill/respawn",
		"Others kill/respawn"};
	static const std::array<const char *, 4> s_apKeyboardSoundChildren = {
		"Keyboard typing sound",
		"Keyboard folder",
		"Typing sound file",
		"Volume"};
	static const std::array<const char *, 8> s_apGradientTeamColorChildren = {
		"Team background gradient",
		"Nickname gradient",
		"Sparkle effect",
		"Nickname start",
		"Nickname end",
		"Glow intensity",
		"Animate nickname blend",
		"Animation speed"};
	static const std::array<const char *, 4> s_apBrowserUtilsChildren = {
		"Auto refresh",
		"Refresh interval",
		"Shorten KoG server names",
		"Server list"};
	static const std::array<const char *, 2> s_apCustomResolutionChildren = {
		"Width",
		"Height"};
	static const std::array<const char *, 2> s_apSaveUnsentChildren = {
		"Chat draft",
		"Map change"};
	static const std::array<const char *, 2> s_apRollbackDemoChildren = {
		"Replay recorder",
		"Clip seconds"};
	static const std::array<const char *, 9> s_apAimTrainingChildren = {
		"Start session",
		"M1/M2 train shots",
		"Targets",
		"Target size",
		"Target distance",
		"Background dim",
		"Target color",
		"Shrink targets",
		"Respawn missed targets"};
	static const std::array<const char *, 6> s_apPsaChildren = {
		"Base value",
		"Trial duration",
		"Low",
		"High",
		"Skip",
		"Suggested value"};
	static const std::array<const char *, 3> s_apVaultCfgChildren = {
		"Config export",
		"Binds",
		"KoG login excluded"};
	static const std::array<const char *, 8> s_apClanChildren = {
		"Create clan",
		"Join clan",
		"Invite code",
		"KoG clan",
		"General clan",
		"Push warlist",
		"Pull warlist",
		"Copy warlist"};
	static const std::array<const char *, 7> s_apGoresMapsChildren = {
		"Refresh list",
		"Category filter",
		"Download selected",
		"Download category",
		"Download all",
		"Delete selected",
		"Open folder"};
	static const std::array<const char *, 10> s_apAssetsEditorChildren = {
		"Game atlas",
		"HUD atlas",
		"Particles atlas",
		"Emoticons atlas",
		"Extras atlas",
		"Skin mixer",
		"Tint",
		"Opacity",
		"Export",
		"Apply"};
	static const std::array<const char *, 9> s_apOptimizerChildren = {
		"High process priority",
		"Discord priority",
		"Disable particles",
		"High detail",
		"Disable menu animations",
		"FPS fog",
		"Map tile culling",
		"Fog rectangle",
		"Fog radius"};
	static const std::array<const char *, 0> s_apEditorChildren = {};
	const std::array<SFeature, 43> aFeatures = {{
		{AetherMusic::EAetherFeatureId::GRADIENT_TEAM_COLORS, EAetherPage::VISUALS, ESection::VISUALS, "Gradient Effects", nullptr, s_apGradientTeamColorChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::MUSIC_PLAYER, EAetherPage::VISUALS, ESection::VISUALS, "Music Player", &g_Config.m_AeMusicPlayer, s_apMusicChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::KEYSTROKES, EAetherPage::VISUALS, ESection::VISUALS, "Keystrokes", &g_Config.m_AeKeystrokes, s_apKeystrokesChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::INPUT_VISUALIZER, EAetherPage::VISUALS, ESection::VISUALS, "Input Visualizer", &g_Config.m_AeInputVisualizer, s_apInputVisualizerChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::STABILITY_TRAINER, EAetherPage::VISUALS, ESection::VISUALS, "Stability Trainer", &g_Config.m_AeStabilityTrainer, s_apStabilityTrainerChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SESSION_STATS, EAetherPage::VISUALS, ESection::VISUALS, "Session Stats", &g_Config.m_AeSessionStats, s_apSessionStatsChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::REAL_HITBOX, EAetherPage::VISUALS, ESection::VISUALS, "Show Real Hitbox", &g_Config.m_AeShowRealHitbox, s_apRealHitboxChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::NINJA_TEE_PREVIEW, EAetherPage::VISUALS, ESection::VISUALS, "Ninja Tee Preview", &g_Config.m_AeNinjaTeePreview, s_apNinjaTeePreviewChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::NINJA_TIMER, EAetherPage::VISUALS, ESection::VISUALS, "Ninja Timer", &g_Config.m_AeNinjaTimer, s_apNinjaTimerChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SWEAT_WEAPON, EAetherPage::VISUALS, ESection::VISUALS, "Sweat Weapon", &g_Config.m_AeSweatWeapon, s_apSweatWeaponChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::ORBIT_AURA, EAetherPage::VISUALS, ESection::VISUALS, "Orbit Aura", &g_Config.m_AeOrbitAura, s_apOrbitAuraChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::JELLY_TEE, EAetherPage::VISUALS, ESection::VISUALS, "Jelly Tee", &g_Config.m_AeJellyTee, s_apJellyTeeChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::FINISH_PREDICTION, EAetherPage::VISUALS, ESection::VISUALS, "Finish Prediction", &g_Config.m_AeFinishPrediction, s_apFinishPredictionChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::THREE_D_PARTICLES, EAetherPage::VISUALS, ESection::VISUALS, "3D Particles", &g_Config.m_Ae3DParticles, s_apThreeDParticlesChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::LOADING_THEME_BACKGROUND, EAetherPage::VISUALS, ESection::VISUALS, "Loading Theme Background", &g_Config.m_AeLoadingThemeBackground, s_apLoadingThemeChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::CLIENT_BADGES, EAetherPage::VISUALS, ESection::VISUALS, "Client Badges", &g_Config.m_AeBadges, s_apBadgesChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::PING_WHEEL, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Ping Wheel", &g_Config.m_AePings, s_apPingChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::CHAT_BUBBLES, EAetherPage::VISUALS, ESection::VISUALS, "Chat Bubbles", &g_Config.m_AeChatBubbles, s_apChatBubblesChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::BLOCK_AWARENESS, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Block Awareness", &g_Config.m_AeBlockAwareness, s_apBlockAwarenessChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::FOCUS_MODE, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Focus Mode", &g_Config.m_AeFocusMode, s_apFocusModeChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SNAP_TAP, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Snap Tap", &g_Config.m_AeSnapTap, s_apSnapTapChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::GORES_MODE, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Gores Mode", &g_Config.m_AeGoresMode, s_apGoresModeChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::FAST_INPUT, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Aether Fast Input", &g_Config.m_AeFastInput, s_apFastInputChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::FAST_SPEC, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Fast Spec", &g_Config.m_AeFastSpec, s_apFastSpecChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::TRANSLATOR, EAetherPage::TOOLS, ESection::TOOLS, "Translator", nullptr, s_apTranslatorChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SILENT_TYPING, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Silent Typing", &g_Config.m_AeSilentTyping, s_apSilentTypingChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::FAIL_SOUND, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Fail Sounds", &g_Config.m_AeFreezeFailSound, s_apFailSoundChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SOUND, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Gameplay Sounds", &g_Config.m_AeSound, s_apSoundChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::KEYBOARD_SOUND, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Keyboard Sounds", &g_Config.m_AeKeyboardSound, s_apKeyboardSoundChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::AUTO_TEAM_LOCK, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Auto Team Lock", &g_Config.m_AeAutoTeamLock, s_apAutoTeamLockChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::SAVE_UNSENT_MESSAGES, EAetherPage::GAMEPLAY, ESection::GAMEPLAY, "Save Unsent Messages", &g_Config.m_AeSaveUnsentMessages, s_apSaveUnsentChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::DDRACE_CONFIGS, EAetherPage::TOOLS, ESection::TOOLS, "DDRace Configs", nullptr, s_apDdraceConfigsChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::HUD_EDITOR, EAetherPage::TOOLS, ESection::EDITORS, "HUD Editor", nullptr, s_apEditorChildren, EEditorAction::OPEN_HUD_EDITOR},
		{AetherMusic::EAetherFeatureId::ASSETS_EDITOR, EAetherPage::TOOLS, ESection::EDITORS, "Assets Editor", nullptr, s_apAssetsEditorChildren, EEditorAction::OPEN_ASSETS_EDITOR},
		{AetherMusic::EAetherFeatureId::GORES_MAPS, EAetherPage::TOOLS, ESection::TOOLS, "Gores Maps", nullptr, s_apGoresMapsChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::VAULT_CFG, EAetherPage::TOOLS, ESection::TOOLS, "Vault CFG Save", nullptr, s_apVaultCfgChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::CUSTOM_RESOLUTION, EAetherPage::TOOLS, ESection::TOOLS, "Custom Aspect Ratio", &g_Config.m_AeCustomResolution, s_apCustomResolutionChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::OPTIMIZER, EAetherPage::TOOLS, ESection::TOOLS, "Optimizer", &g_Config.m_AeOptimizer, s_apOptimizerChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::BROWSER_UTILS, EAetherPage::TOOLS, ESection::TOOLS, "Browser Utils", &g_Config.m_AeBrowserUtils, s_apBrowserUtilsChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::ROLLBACK_DEMO, EAetherPage::TOOLS, ESection::TOOLS, "Rollback Demo", &g_Config.m_AeRollbackDemo, s_apRollbackDemoChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::AIM_TRAINING, EAetherPage::TOOLS, ESection::TOOLS, "Aim Training", &g_Config.m_AeAimTraining, s_apAimTrainingChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::PSA, EAetherPage::TOOLS, ESection::TOOLS, "PSA", &g_Config.m_AePsa, s_apPsaChildren, EEditorAction::NONE},
		{AetherMusic::EAetherFeatureId::CLOUD_CLAN, EAetherPage::CLAN, ESection::TOOLS, "Cloud Clan", nullptr, s_apClanChildren, EEditorAction::NONE},
	}};

	CUIRect Header, Search;
	MainView.HSplitTop(34.0f * S, &Header, &MainView);
	Ui()->DoLabel(&Header, "Aether Settings", 24.0f * S, TEXTALIGN_ML);
	MainView.HSplitTop(8.0f * S, nullptr, &MainView);
	static CButtonContainer s_aPageButtons[7];
	CUIRect PageArea;
	const bool CompactAetherHeader = MainView.w < 880.0f * S;
	m_AetherSearchInput.SetEmptyText("Search features and settings...");
	if(CompactAetherHeader)
	{
		MainView.HSplitTop(28.0f * S, &Search, &MainView);
		Ui()->DoEditBox_Search(&m_AetherSearchInput, &Search, 14.0f * S, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive(), 9.0f * S);
		MainView.HSplitTop(8.0f * S, nullptr, &MainView);
	}
	else
	{
		MainView.HSplitTop(28.0f * S, &PageArea, &MainView);
		PageArea.VSplitRight(300.0f * S, &PageArea, &Search);
		PageArea.VSplitRight(18.0f * S, &PageArea, nullptr);
		Ui()->DoEditBox_Search(&m_AetherSearchInput, &Search, 14.0f * S, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive(), 9.0f * S);
	}
	const std::array<std::pair<EAetherPage, const char *>, 7> aPages = {{
		{EAetherPage::VISUALS, "Visuals"},
		{EAetherPage::GAMEPLAY, "Gameplay"},
		{EAetherPage::TOOLS, "Tools"},
		{EAetherPage::ASSETS, "Assets"},
		{EAetherPage::CLAN, "Clan"},
		{EAetherPage::GAMES, "Games"},
		{EAetherPage::INFO, "Info"},
	}};
	if(CompactAetherHeader)
	{
		const int TabsPerRow = MainView.w < 620.0f * S ? 3 : 4;
		const int NumRows = (int)((aPages.size() + TabsPerRow - 1) / TabsPerRow);
		MainView.HSplitTop(NumRows * 28.0f * S + (NumRows - 1) * 6.0f * S, &PageArea, &MainView);
	}
	for(size_t i = 0; i < aPages.size(); ++i)
	{
		CUIRect Button;
		if(CompactAetherHeader)
		{
			const int TabsPerRow = MainView.w < 620.0f * S ? 3 : 4;
			const float Gap = 6.0f * S;
			const float ButtonW = (PageArea.w - Gap * (TabsPerRow - 1)) / TabsPerRow;
			const int Row = (int)i / TabsPerRow;
			const int Col = (int)i % TabsPerRow;
			Button = {PageArea.x + Col * (ButtonW + Gap), PageArea.y + Row * (28.0f * S + Gap), ButtonW, 28.0f * S};
		}
		else
		{
			const float Gap = 8.0f * S;
			const float ButtonW = minimum(96.0f * S, maximum(62.0f * S, (PageArea.w - Gap * (aPages.size() - 1)) / (float)aPages.size()));
			PageArea.VSplitLeft(ButtonW, &Button, &PageArea);
			PageArea.VSplitLeft(Gap, nullptr, &PageArea);
		}
		if(DoButton_MenuTab(&s_aPageButtons[i], aPages[i].second, s_AetherActivePage == aPages[i].first, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f * S))
			s_AetherActivePage = aPages[i].first;
	}
	MainView.HSplitTop(8.0f * S, nullptr, &MainView);
	const bool EnabledFilterPage = s_AetherActivePage == EAetherPage::VISUALS || s_AetherActivePage == EAetherPage::GAMEPLAY || s_AetherActivePage == EAetherPage::TOOLS;
	if(EnabledFilterPage)
	{
		CUIRect FilterRow, FilterButton;
		MainView.HSplitTop(24.0f * S, &FilterRow, &MainView);
		FilterRow.VSplitLeft(220.0f * S, &FilterButton, nullptr);
		static CButtonContainer s_EnabledOnlyButton;
		if(DoButton_CheckBox(&s_EnabledOnlyButton, "Only enabled", m_AetherShowEnabledOnly, &FilterButton))
			m_AetherShowEnabledOnly = !m_AetherShowEnabledOnly;
		MainView.HSplitTop(8.0f * S, nullptr, &MainView);
	}

	if(s_AetherActivePage == EAetherPage::CLAN)
	{
		RenderSettingsAetherClan(MainView);
		return;
	}

	if(s_AetherActivePage == EAetherPage::ASSETS)
	{
		RenderSettingsAetherAssetsCloud(MainView);
		return;
	}

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 50.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	MainView.y += ScrollOffset.y;
	const bool SearchActive = m_AetherSearchInput.GetString()[0] != '\0';

	auto SectionHasMatches = [&](ESection Section) {
		for(const SFeature &Feature : aFeatures)
			if(AetherFeatureAllowed(Feature.m_Id) &&
				Feature.m_Section == Section &&
				(SearchActive || Feature.m_Page == s_AetherActivePage) &&
				(!EnabledFilterPage || !m_AetherShowEnabledOnly || !Feature.m_pEnabled || *Feature.m_pEnabled) &&
				AetherMusic::SearchMatches(m_AetherSearchInput.GetString(), Feature.m_pLabel, Feature.m_ChildLabels))
				return true;
		return false;
	};

	static CButtonContainer s_aExpandButtons[64];
	static CButtonContainer s_OpenEditorButton;
	static CButtonContainer s_OpenAssetsEditorButton;
	static std::array<float, 64> s_aBodyAnimations = {};
	const float AnimationStep = g_Config.m_AeOptimizer && g_Config.m_AeOptimizerDisableMenuAnimations ? 1.0f : std::clamp(Client()->RenderFrameTime() * 18.0f, 0.0f, 1.0f);

	auto BodyHeight = [&](AetherMusic::EAetherFeatureId Id) {
		switch(Id)
		{
		case AetherMusic::EAetherFeatureId::MUSIC_PLAYER: return 168.0f * S;
		case AetherMusic::EAetherFeatureId::KEYSTROKES: return 112.0f * S;
		case AetherMusic::EAetherFeatureId::INPUT_VISUALIZER:
		{
			int ColorRows = 2;
			if(g_Config.m_AeInputVisualizerShowJump)
				++ColorRows;
			if(g_Config.m_AeInputVisualizerMouse)
			{
				if(g_Config.m_AeInputVisualizerShowFire)
					++ColorRows;
				++ColorRows;
			}
			return (24.0f + 14.0f * 22.0f + 8.0f + 18.0f + ColorRows * 29.0f + 8.0f) * S;
		}
		case AetherMusic::EAetherFeatureId::STABILITY_TRAINER: return (g_Config.m_AeStabilityTrainerColorize ? 280.0f : 310.0f) * S;
		case AetherMusic::EAetherFeatureId::SESSION_STATS: return 62.0f * S;
		case AetherMusic::EAetherFeatureId::REAL_HITBOX: return 74.0f * S;
		case AetherMusic::EAetherFeatureId::NINJA_TEE_PREVIEW: return 116.0f * S;
		case AetherMusic::EAetherFeatureId::NINJA_TIMER: return 86.0f * S;
		case AetherMusic::EAetherFeatureId::SWEAT_WEAPON:
			return (430.0f +
				(g_Config.m_AeSweatWeaponShine ? 24.0f : 0.0f) +
				(g_Config.m_AeSweatWeaponElectric ? 48.0f : 0.0f) +
				(g_Config.m_AeSweatWeaponEntitiesLaser ? 118.0f : 0.0f)) *
			       S;
		case AetherMusic::EAetherFeatureId::ORBIT_AURA: return (284.0f + (g_Config.m_AeOrbitAuraIdleOnly ? 24.0f : 0.0f) + (s_AetherOrbitStyleDropdownOpen ? 76.0f : 0.0f)) * S;
		case AetherMusic::EAetherFeatureId::JELLY_TEE: return 94.0f * S;
		case AetherMusic::EAetherFeatureId::FINISH_PREDICTION: return 128.0f * S;
		case AetherMusic::EAetherFeatureId::THREE_D_PARTICLES: return (g_Config.m_Ae3DParticlesColorMode == 0 ? 266.0f : 230.0f) * S;
		case AetherMusic::EAetherFeatureId::LOADING_THEME_BACKGROUND: return 52.0f * S;
		case AetherMusic::EAetherFeatureId::CLIENT_BADGES: return 154.0f * S;
		case AetherMusic::EAetherFeatureId::PING_WHEEL: return (s_AetherPingTab == 1 ? 132.0f : s_AetherPingTab == 2 ? 88.0f : 92.0f) * S;
		case AetherMusic::EAetherFeatureId::CHAT_BUBBLES: return 238.0f * S;
		case AetherMusic::EAetherFeatureId::BLOCK_AWARENESS:
			return (s_AetherBlockAwarenessTab == 0 ? 344.0f : s_AetherBlockAwarenessTab == 1 ? 160.0f : s_AetherBlockAwarenessTab == 2 ? 150.0f : s_AetherBlockAwarenessTab == 3 ? 150.0f : 188.0f) * S;
		case AetherMusic::EAetherFeatureId::FAIL_SOUND: return 292.0f * S;
		case AetherMusic::EAetherFeatureId::SOUND: return 232.0f * S;
		case AetherMusic::EAetherFeatureId::KEYBOARD_SOUND: return 136.0f * S;
		case AetherMusic::EAetherFeatureId::AUTO_TEAM_LOCK: return 70.0f * S;
		case AetherMusic::EAetherFeatureId::ROLLBACK_DEMO: return 94.0f * S;
		case AetherMusic::EAetherFeatureId::AIM_TRAINING:
			return (229.0f +
				(g_Config.m_AeAimTrainingShrink ? 22.0f : 0.0f) +
				(g_Config.m_AeAimTrainingDespawn ? 22.0f : 0.0f)) *
			       S;
		case AetherMusic::EAetherFeatureId::PSA: return (GameClient()->m_AetherPsa.IsActive() ? 230.0f : 116.0f) * S;
		case AetherMusic::EAetherFeatureId::VAULT_CFG: return 352.0f * S;
		case AetherMusic::EAetherFeatureId::CLOUD_CLAN: return 430.0f * S;
		case AetherMusic::EAetherFeatureId::GORES_MAPS: return 430.0f * S;
		case AetherMusic::EAetherFeatureId::ASSETS_EDITOR: return 0.0f;
		case AetherMusic::EAetherFeatureId::FOCUS_MODE: return 126.0f * S;
		case AetherMusic::EAetherFeatureId::SNAP_TAP: return 46.0f * S;
		case AetherMusic::EAetherFeatureId::GORES_MODE: return 86.0f * S;
		case AetherMusic::EAetherFeatureId::DDRACE_CONFIGS: return 172.0f * S;
		case AetherMusic::EAetherFeatureId::FAST_INPUT: return 430.0f * S;
		case AetherMusic::EAetherFeatureId::FAST_SPEC: return 104.0f * S;
		case AetherMusic::EAetherFeatureId::TRANSLATOR: return 124.0f * S;
		case AetherMusic::EAetherFeatureId::SILENT_TYPING: return 52.0f * S;
		case AetherMusic::EAetherFeatureId::SAVE_UNSENT_MESSAGES: return 52.0f * S;
		case AetherMusic::EAetherFeatureId::GRADIENT_TEAM_COLORS: return 230.0f * S;
		case AetherMusic::EAetherFeatureId::BROWSER_UTILS: return 108.0f * S;
		case AetherMusic::EAetherFeatureId::CUSTOM_RESOLUTION: return 86.0f * S;
		case AetherMusic::EAetherFeatureId::OPTIMIZER: return 224.0f * S;
		default: return 0.0f;
		}
	};

	auto RenderBody = [&](AetherMusic::EAetherFeatureId Id, CUIRect Body) {
		switch(Id)
		{
		case AetherMusic::EAetherFeatureId::MUSIC_PLAYER:
			RenderSettingsAetherMusicPlayer(Body);
			break;
		case AetherMusic::EAetherFeatureId::KEYSTROKES:
			RenderSettingsAetherKeystrokes(Body);
			break;
		case AetherMusic::EAetherFeatureId::INPUT_VISUALIZER:
			RenderSettingsAetherInputVisualizer(Body);
			break;
		case AetherMusic::EAetherFeatureId::STABILITY_TRAINER:
			RenderSettingsAetherStabilityTrainer(Body);
			break;
		case AetherMusic::EAetherFeatureId::SESSION_STATS:
			RenderSettingsAetherSessionStats(Body);
			break;
		case AetherMusic::EAetherFeatureId::REAL_HITBOX:
			RenderSettingsAetherRealHitbox(Body);
			break;
		case AetherMusic::EAetherFeatureId::NINJA_TEE_PREVIEW:
			RenderSettingsAetherNinjaTeePreview(Body);
			break;
		case AetherMusic::EAetherFeatureId::NINJA_TIMER:
			RenderSettingsAetherNinjaTimer(Body);
			break;
		case AetherMusic::EAetherFeatureId::SWEAT_WEAPON:
			RenderSettingsAetherSweatWeapon(Body);
			break;
		case AetherMusic::EAetherFeatureId::ORBIT_AURA:
			RenderSettingsAetherOrbitAura(Body);
			break;
		case AetherMusic::EAetherFeatureId::JELLY_TEE:
			RenderSettingsAetherJellyTee(Body);
			break;
		case AetherMusic::EAetherFeatureId::FINISH_PREDICTION:
			RenderSettingsAetherFinishPrediction(Body);
			break;
		case AetherMusic::EAetherFeatureId::THREE_D_PARTICLES:
			RenderSettingsAetherThreeDParticles(Body);
			break;
		case AetherMusic::EAetherFeatureId::LOADING_THEME_BACKGROUND:
			RenderSettingsAetherDescription(Body, "Uses the current menu theme as the loading screen background. Turn off for the plain fallback loading background.");
			break;
		case AetherMusic::EAetherFeatureId::CLIENT_BADGES:
			RenderSettingsAetherBadges(Body);
			break;
		case AetherMusic::EAetherFeatureId::PING_WHEEL:
			RenderSettingsAetherPings(Body);
			break;
		case AetherMusic::EAetherFeatureId::CHAT_BUBBLES:
			RenderSettingsAetherChatBubbles(Body);
			break;
		case AetherMusic::EAetherFeatureId::BLOCK_AWARENESS:
			RenderSettingsAetherBlockAwareness(Body);
			break;
		case AetherMusic::EAetherFeatureId::AUTO_TEAM_LOCK:
			RenderSettingsAetherAutoTeamLock(Body);
			break;
		case AetherMusic::EAetherFeatureId::ROLLBACK_DEMO:
			RenderSettingsAetherRollbackDemo(Body);
			break;
		case AetherMusic::EAetherFeatureId::AIM_TRAINING:
			RenderSettingsAetherAimTraining(Body);
			break;
		case AetherMusic::EAetherFeatureId::PSA:
			RenderSettingsAetherPsa(Body);
			break;
		case AetherMusic::EAetherFeatureId::VAULT_CFG:
			RenderSettingsAetherVaultCfg(Body);
			break;
		case AetherMusic::EAetherFeatureId::CLOUD_CLAN:
			RenderSettingsAetherClan(Body);
			break;
		case AetherMusic::EAetherFeatureId::GORES_MAPS:
			RenderSettingsAetherGoresMaps(Body);
			break;
		case AetherMusic::EAetherFeatureId::ASSETS_EDITOR:
			RenderSettingsAetherAssetsEditor(Body);
			break;
		case AetherMusic::EAetherFeatureId::FOCUS_MODE:
			RenderSettingsAetherFocusMode(Body);
			break;
		case AetherMusic::EAetherFeatureId::SNAP_TAP:
			RenderSettingsAetherDescription(Body, "When left and right are held together, the last pressed direction stays active.");
			break;
		case AetherMusic::EAetherFeatureId::GORES_MODE:
			RenderSettingsAetherGoresMode(Body);
			break;
		case AetherMusic::EAetherFeatureId::DDRACE_CONFIGS:
			RenderSettingsAetherDdraceConfigs(Body);
			break;
		case AetherMusic::EAetherFeatureId::FAST_INPUT:
			RenderSettingsAetherFastInput(Body);
			break;
		case AetherMusic::EAetherFeatureId::FAST_SPEC:
			RenderSettingsAetherFastSpec(Body);
			break;
		case AetherMusic::EAetherFeatureId::TRANSLATOR:
			RenderSettingsAetherTranslator(Body);
			break;
		case AetherMusic::EAetherFeatureId::SILENT_TYPING:
			RenderSettingsAetherDescription(Body, "Hides the normal typing bubble flag from other players while your chat input is open.");
			break;
		case AetherMusic::EAetherFeatureId::FAIL_SOUND:
			RenderSettingsAetherFailSound(Body);
			break;
		case AetherMusic::EAetherFeatureId::SOUND:
			RenderSettingsAetherSound(Body);
			break;
		case AetherMusic::EAetherFeatureId::KEYBOARD_SOUND:
			RenderSettingsAetherKeyboardSound(Body);
			break;
		case AetherMusic::EAetherFeatureId::SAVE_UNSENT_MESSAGES:
			RenderSettingsAetherSaveUnsentMessages(Body);
			break;
		case AetherMusic::EAetherFeatureId::GRADIENT_TEAM_COLORS:
			RenderSettingsAetherGradientTeamColors(Body);
			break;
		case AetherMusic::EAetherFeatureId::BROWSER_UTILS:
			RenderSettingsAetherBrowserUtils(Body);
			break;
		case AetherMusic::EAetherFeatureId::CUSTOM_RESOLUTION:
			RenderSettingsAetherCustomResolution(Body);
			break;
		case AetherMusic::EAetherFeatureId::OPTIMIZER:
			RenderSettingsAetherOptimizer(Body);
			break;
		default:
			break;
		}
	};

	if(!SearchActive && s_AetherActivePage == EAetherPage::INFO)
	{
		auto RenderInfoCard = [&](CUIRect Card, const char *pTitle, const char *pText, ColorRGBA Accent) {
			Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 8.0f * S);
			Card.Margin(12.0f * S, &Card);
			CUIRect Title, Text;
			Card.HSplitTop(24.0f * S, &Title, &Text);
			TextRender()->TextColor(Accent);
			Ui()->DoLabel(&Title, pTitle, 17.0f * S, TEXTALIGN_ML);
			TextRender()->TextColor(0.76f, 0.82f, 0.90f, 1.0f);
			Ui()->DoLabel(&Text, pText, 12.0f * S, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		};

		CUIRect Hero;
		MainView.HSplitTop(150.0f * S, &Hero, &MainView);
		if(s_ScrollRegion.AddRect(Hero))
		{
			Hero.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 8.0f * S);
			Hero.Margin(12.0f * S, &Hero);
			const float ThirdW = Hero.w / 3.0f;
			CUIRect Silver, Heart, Mini, Rest;
			Hero.VSplitLeft(ThirdW, &Silver, &Rest);
			Rest.VSplitLeft(ThirdW, &Heart, &Mini);
			auto RenderMascot = [&](CUIRect Box, const char *pName, const char *pSkinName, bool HoverReactive = true) {
				CUIRect Label, TeeBox;
				Box.HSplitTop(26.0f * S, &Label, &TeeBox);
				Ui()->DoLabel(&Label, pName, 20.0f * S, TEXTALIGN_MC);
				TeeBox.HMargin(6.0f * S, &TeeBox);
				const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
				if(!pSkin || str_comp(pSkin->GetName(), pSkinName) != 0)
					pSkin = GameClient()->m_Skins.Find("default");
				CTeeRenderInfo SkinInfo;
				SkinInfo.Apply(pSkin);
				SkinInfo.m_CustomColoredSkin = false;
				SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				SkinInfo.m_Size = 72.0f * S;
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &SkinInfo, OffsetToMid);
				const vec2 TeePos(TeeBox.x + TeeBox.w * 0.5f, TeeBox.y + TeeBox.h * 0.5f + OffsetToMid.y);
				const vec2 Delta = Ui()->MousePos() - TeePos;
				const vec2 Dir = length(Delta) > 0.001f ? normalize(Delta) : vec2(1.0f, 0.0f);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &SkinInfo, HoverReactive && Ui()->MouseInside(&TeeBox) ? EMOTE_HAPPY : EMOTE_NORMAL, Dir, TeePos);
				return TeeBox;
			};
			RenderMascot(Silver, "silver", "Blazulite");
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->TextColor(0.96f, 0.32f, 0.48f, 1.0f);
			Ui()->DoLabel(&Heart, FontIcon::HEART, 44.0f * S, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			CUIRect MiniTeeBox = RenderMascot(Mini, "Mini silver", "G_Samoye", false);
			static CButtonContainer s_MiniSilverLoveButton;
			static int s_MiniSilverLoveClicks = 0;
			static int s_MiniSilverLoveCount = 0;
			if(Ui()->DoButtonLogic(&s_MiniSilverLoveButton, 0, &MiniTeeBox, BUTTONFLAG_LEFT))
			{
				if(s_MiniSilverLoveCount > 0)
				{
					++s_MiniSilverLoveCount;
				}
				else
				{
					++s_MiniSilverLoveClicks;
					if(s_MiniSilverLoveClicks >= 7)
						s_MiniSilverLoveCount = 1;
				}
			}
			if(s_MiniSilverLoveCount > 0)
			{
				CUIRect Love;
				Mini.HSplitBottom(20.0f * S, nullptr, &Love);
				char aLove[64];
				str_format(aLove, sizeof(aLove), "I love you x%d", s_MiniSilverLoveCount);
				TextRender()->TextColor(1.0f, 0.72f, 0.86f, 1.0f);
				Ui()->DoLabel(&Love, aLove, 13.0f * S, TEXTALIGN_MC);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		MainView.HSplitTop(10.0f * S, nullptr, &MainView);
		CUIRect Left, Right;
		MainView.HSplitTop(92.0f * S, &Hero, &MainView);
		Hero.VSplitMid(&Left, &Right, 10.0f * S);
		if(s_ScrollRegion.AddRect(Hero))
		{
			RenderInfoCard(Left, "Aether Client", "A custom DDNet client built around clean visuals, QoL tools, games and Aether-only systems.", ColorRGBA(0.55f, 0.86f, 1.0f, 1.0f));
			RenderInfoCard(Right, "Config", "Aether settings use the ae_* namespace in this clean build. Vault CFG can export a combined preset.", ColorRGBA(1.0f, 0.72f, 0.38f, 1.0f));
		}

		MainView.HSplitTop(10.0f * S, nullptr, &MainView);
		MainView.HSplitTop(82.0f * S, &Hero, &MainView);
		if(s_ScrollRegion.AddRect(Hero))
			RenderInfoCard(Hero, "Credits", "Original AetherClient info page, mascot names and presentation are preserved from the previous client branch.", ColorRGBA(0.95f, 0.56f, 0.82f, 1.0f));
		s_ScrollRegion.End();
		return;
	}

	if(!SearchActive && s_AetherActivePage == EAetherPage::GAMES)
	{
		enum class EGame
		{
			NONE,
			CHESS,
			SNAKE,
			MINESWEEPER,
			TETRIS,
		};

		struct SChessState
		{
			bool m_InGame = false;
			int m_Turn = 0; // 0 white, 1 black
			int m_Selected = -1;
			int m_Winner = -1;
			int m_LastMoveFrom = -1;
			int m_LastMoveTo = -1;
			std::array<char, 64> m_aBoard{};
		};
		struct SChessLeaderboardEntry
		{
			char m_aName[MAX_NAME_LENGTH] = "";
			int m_Rating = 1200;
			int m_Wins = 0;
			int m_Losses = 0;
			int m_Draws = 0;
		};
		struct SChessOnlinePlayer
		{
			char m_aName[MAX_NAME_LENGTH] = "";
			char m_aClient[32] = "";
			char m_aServer[96] = "";
			char m_aMap[64] = "";
			int m_Rating = 1200;
			bool m_Spectator = false;
		};
		struct SChessRecentMatch
		{
			char m_aWhite[MAX_NAME_LENGTH] = "";
			char m_aBlack[MAX_NAME_LENGTH] = "";
			char m_aWinner[MAX_NAME_LENGTH] = "";
			char m_aResult[32] = "";
			char m_aReason[48] = "";
			char m_aFinishedAt[32] = "";
			int m_WhiteRatingBefore = 1200;
			int m_WhiteRatingAfter = 1200;
			int m_BlackRatingBefore = 1200;
			int m_BlackRatingAfter = 1200;
		};
		struct SChessOnlineState
		{
			int m_Tab = 0; // 0 local, 1 online duel, 2 leaderboards, 3 recent
			bool m_HasInvite = false;
			bool m_InMatch = false;
			bool m_Rated = true;
			bool m_IsWhite = true;
			bool m_TurnWhite = true;
			bool m_MovePending = false;
			bool m_ResignPending = false;
			bool m_Check = false;
			bool m_Checkmate = false;
			bool m_Stalemate = false;
			bool m_Finished = false;
			int m_Selected = -1;
			int m_LastMoveFrom = -1;
			int m_LastMoveTo = -1;
			int m_WhiteRating = 1200;
			int m_BlackRating = 1200;
			int m_WhiteRatingBefore = 1200;
			int m_BlackRatingBefore = 1200;
			int m_WhiteRatingAfter = 1200;
			int m_BlackRatingAfter = 1200;
			int m_WhiteRatingDelta = 0;
			int m_BlackRatingDelta = 0;
			int m_OnlineCount = 0;
			int m_OverallCount = 0;
			int m_MonthlyCount = 0;
			int m_RecentCount = 0;
			int64_t m_LastOnlineRequest = 0;
			int64_t m_LastLeaderboardRequest = 0;
			int64_t m_LastRecentRequest = 0;
			char m_aInviteId[96] = "";
			char m_aInviteFrom[MAX_NAME_LENGTH] = "";
			char m_aMatchId[96] = "";
			char m_aWhite[MAX_NAME_LENGTH] = "";
			char m_aBlack[MAX_NAME_LENGTH] = "";
			char m_aWinner[MAX_NAME_LENGTH] = "";
			char m_aResult[32] = "";
			char m_aResultReason[48] = "";
			char m_aStatus[160] = "Online chess ready.";
			std::array<char, 64> m_aBoard{};
			std::vector<std::string> m_vLegalMoves;
			std::array<SChessOnlinePlayer, MAX_CLIENTS> m_aOnline{};
			std::array<SChessLeaderboardEntry, 5> m_aOverall{};
			std::array<SChessLeaderboardEntry, 5> m_aMonthly{};
			std::array<SChessRecentMatch, 20> m_aRecent{};
		};
		struct SSnakeState
		{
			bool m_InGame = false;
			bool m_GameOver = false;
			int m_Size = 16;
			int m_Dir = 1;
			int m_NextDir = 1;
			int m_Food = 0;
			int m_Length = 0;
			int m_Score = 0;
			double m_LastStep = 0.0;
			std::array<bool, 4> m_aWasDirectionDown{};
			std::array<int, 8> m_aQueuedDirs{};
			int m_QueueSize = 0;
			std::array<int, 24 * 24> m_aBody{};
		};
		struct SMinesweeperState
		{
			bool m_InGame = false;
			bool m_BombsPlaced = false;
			bool m_GameOver = false;
			bool m_Won = false;
			int m_Width = 12;
			int m_Height = 10;
			int m_Bombs = 18;
			std::array<unsigned char, 16 * 12> m_aCells{};
		};
		struct STetrisState
		{
			bool m_InGame = false;
			bool m_GameOver = false;
			int m_Piece = 0;
			int m_NextPiece = 0;
			int m_Rotation = 0;
			int m_X = 3;
			int m_Y = 0;
			int m_Score = 0;
			int m_Lines = 0;
			double m_LastFall = 0.0;
			double m_LastHorizontalMove = 0.0;
			int m_LastHorizontalDir = 0;
			std::array<bool, 6> m_aWasKeyDown{};
			std::array<int, 10 * 20> m_aBoard{};
		};

		static EGame s_ActiveGame = EGame::NONE;
		static SChessState s_Chess;
		static SChessOnlineState s_ChessOnline;
		static SSnakeState s_Snake;
		static SMinesweeperState s_Mines;
		static STetrisState s_Tetris;
		static CButtonContainer s_aGameStartButtons[4];
		static CButtonContainer s_GameBackButton;
		static CButtonContainer s_GameRestartButton;
		static CButtonContainer s_aChessTabButtons[4];
		static CButtonContainer s_ChessAcceptInviteButton;
		static CButtonContainer s_ChessDeclineInviteButton;
		static CButtonContainer s_ChessResignButton;
		static CButtonContainer s_ChessRefreshLeaderboardButton;
		static CButtonContainer s_ChessRefreshRecentButton;
		static CButtonContainer s_ChessCreateRoomButton;
		static CButtonContainer s_ChessJoinRoomButton;
		static CButtonContainer s_ChessCopyRoomButton;
		static CButtonContainer s_ChessReadyRoomButton;
		static CButtonContainer s_ChessLeaveRoomButton;
		static CLineInputBuffered<16> s_ChessRoomCodeInput;
		static std::array<CButtonContainer, MAX_CLIENTS> s_aChessChallengeButtons;
		static CButtonContainer s_SnakeWrapButton;
		static CButtonContainer s_ChessShowMovesButton;
		static std::shared_ptr<CHttpRequest> s_pChessRecentRequest;
		static CButtonContainer s_MinesDifficultyButton;
		static CButtonContainer s_aStepperMinus[8];
		static CButtonContainer s_aStepperPlus[8];

		auto IsWhitePiece = [](char Piece) { return Piece >= 'A' && Piece <= 'Z'; };
		auto IsBlackPiece = [](char Piece) { return Piece >= 'a' && Piece <= 'z'; };
		auto UpperPiece = [](char Piece) {
			return Piece >= 'a' && Piece <= 'z' ? (char)(Piece - ('a' - 'A')) : Piece;
		};
		auto OwnPiece = [&](char Piece) {
			return Piece != '\0' && (s_Chess.m_Turn == 0 ? IsWhitePiece(Piece) : IsBlackPiece(Piece));
		};
		auto OpponentPiece = [&](char Piece) {
			return Piece != '\0' && (s_Chess.m_Turn == 0 ? IsBlackPiece(Piece) : IsWhitePiece(Piece));
		};
		auto ChessIcon = [&](char Piece) {
			switch(UpperPiece(Piece))
			{
			case 'K': return FontIcon::CHESS_KING;
			case 'Q': return FontIcon::CHESS_QUEEN;
			case 'R': return FontIcon::CHESS_ROOK;
			case 'B': return FontIcon::CHESS_BISHOP;
			case 'N': return FontIcon::CHESS_KNIGHT;
			case 'P': return FontIcon::CHESS_PAWN;
			default: return "";
			}
		};
		auto ResetChess = [&]() {
			s_Chess.m_InGame = true;
			s_Chess.m_Turn = 0;
			s_Chess.m_Selected = -1;
			s_Chess.m_Winner = -1;
			s_Chess.m_LastMoveFrom = -1;
			s_Chess.m_LastMoveTo = -1;
			s_Chess.m_aBoard = {{
				'r', 'n', 'b', 'q', 'k', 'b', 'n', 'r',
				'p', 'p', 'p', 'p', 'p', 'p', 'p', 'p',
				'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
				'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
				'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
				'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
				'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P',
				'R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R',
			}};
		};
		auto ClearPath = [&](int From, int To) {
			const int Fx = From % 8;
			const int Fy = From / 8;
			const int Tx = To % 8;
			const int Ty = To / 8;
			const int StepX = (Tx > Fx) - (Tx < Fx);
			const int StepY = (Ty > Fy) - (Ty < Fy);
			int X = Fx + StepX;
			int Y = Fy + StepY;
			while(X != Tx || Y != Ty)
			{
				if(s_Chess.m_aBoard[Y * 8 + X] != '\0')
					return false;
				X += StepX;
				Y += StepY;
			}
			return true;
		};
		auto LegalMove = [&](int From, int To) {
			if(From < 0 || From >= 64 || To < 0 || To >= 64 || From == To)
				return false;
			const char Piece = s_Chess.m_aBoard[From];
			const char Target = s_Chess.m_aBoard[To];
			if(!OwnPiece(Piece) || (Target != '\0' && !OpponentPiece(Target)))
				return false;
			const int Fx = From % 8;
			const int Fy = From / 8;
			const int Tx = To % 8;
			const int Ty = To / 8;
			const int Dx = Tx - Fx;
			const int Dy = Ty - Fy;
			const int AbsDx = std::abs(Dx);
			const int AbsDy = std::abs(Dy);
			switch(UpperPiece(Piece))
			{
			case 'P':
			{
				const int Dir = IsWhitePiece(Piece) ? -1 : 1;
				const int StartRank = IsWhitePiece(Piece) ? 6 : 1;
				if(Dx == 0 && Dy == Dir && Target == '\0')
					return true;
				if(Dx == 0 && Fy == StartRank && Dy == Dir * 2 && Target == '\0' && s_Chess.m_aBoard[(Fy + Dir) * 8 + Fx] == '\0')
					return true;
				return AbsDx == 1 && Dy == Dir && Target != '\0' && OpponentPiece(Target);
			}
			case 'N': return (AbsDx == 1 && AbsDy == 2) || (AbsDx == 2 && AbsDy == 1);
			case 'B': return AbsDx == AbsDy && ClearPath(From, To);
			case 'R': return (Dx == 0 || Dy == 0) && ClearPath(From, To);
			case 'Q': return ((AbsDx == AbsDy) || Dx == 0 || Dy == 0) && ClearPath(From, To);
			case 'K': return AbsDx <= 1 && AbsDy <= 1;
			default: return false;
			}
		};
		auto LocalChessName = [&](char *pOut, int OutSize) {
			if(!pOut || OutSize <= 0)
				return false;
			pOut[0] = '\0';
			const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
			const char *pName = nullptr;
			if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
				pName = GameClient()->m_aClients[LocalId].m_aName;
			else if(g_Config.m_PlayerName[0] != '\0')
				pName = g_Config.m_PlayerName;
			if(!pName || !pName[0])
				return false;
			str_copy(pOut, pName, OutSize);
			return true;
		};
		auto ParseOnlineFen = [&](const char *pFen) {
			s_ChessOnline.m_aBoard.fill('\0');
			if(!pFen || !pFen[0])
				return;
			int Idx = 0;
			for(const char *p = pFen; *p && *p != ' ' && Idx < 64; ++p)
			{
				if(*p == '/')
					continue;
				if(*p >= '1' && *p <= '8')
				{
					for(int i = 0; i < *p - '0' && Idx < 64; ++i)
						s_ChessOnline.m_aBoard[Idx++] = '\0';
					continue;
				}
				s_ChessOnline.m_aBoard[Idx++] = *p;
			}
		};
		auto SquareName = [](int Idx, char *pOut, int OutSize) {
			if(!pOut || OutSize < 3 || Idx < 0 || Idx >= 64)
				return false;
			const int X = Idx % 8;
			const int Y = Idx / 8;
			pOut[0] = (char)('a' + X);
			pOut[1] = (char)('8' - Y);
			pOut[2] = '\0';
			return true;
		};
		auto SquareIndex = [](const char *pSquare) {
			if(!pSquare || pSquare[0] < 'a' || pSquare[0] > 'h' || pSquare[1] < '1' || pSquare[1] > '8')
				return -1;
			const int X = pSquare[0] - 'a';
			const int Y = '8' - pSquare[1];
			return Y * 8 + X;
		};
		auto OnlineOwnPiece = [&](char Piece) {
			return Piece != '\0' && (s_ChessOnline.m_IsWhite ? IsWhitePiece(Piece) : IsBlackPiece(Piece));
		};
		auto OnlineMyTurn = [&]() {
			return s_ChessOnline.m_InMatch && ((s_ChessOnline.m_TurnWhite && s_ChessOnline.m_IsWhite) || (!s_ChessOnline.m_TurnWhite && !s_ChessOnline.m_IsWhite));
		};
		auto OnlineLegalMove = [&](int From, int To) {
			char aFrom[3], aTo[3], aUci[8];
			if(!SquareName(From, aFrom, sizeof(aFrom)) || !SquareName(To, aTo, sizeof(aTo)))
				return false;
			str_format(aUci, sizeof(aUci), "%s%s", aFrom, aTo);
			for(const std::string &Move : s_ChessOnline.m_vLegalMoves)
			{
				if(Move.rfind(aUci, 0) == 0)
					return true;
			}
			return false;
		};
		auto OnlineCheckedKingSquare = [&]() {
			if(!s_ChessOnline.m_Check && !s_ChessOnline.m_Checkmate)
				return -1;
			const char King = s_ChessOnline.m_TurnWhite ? 'K' : 'k';
			for(int i = 0; i < (int)s_ChessOnline.m_aBoard.size(); ++i)
			{
				if(s_ChessOnline.m_aBoard[i] == King)
					return i;
			}
			return -1;
		};
		auto SendChessLeaderboardRequest = [&](const char *pPeriod) {
			GameClient()->m_AetherBadges.RequestChessLeaderboard(pPeriod, true);
		};
		auto RequestChessLeaderboards = [&]() {
			SendChessLeaderboardRequest("all");
			SendChessLeaderboardRequest("monthly");
			s_ChessOnline.m_LastLeaderboardRequest = time_get();
			str_copy(s_ChessOnline.m_aStatus, "Refreshing chess leaderboards...", sizeof(s_ChessOnline.m_aStatus));
		};
		auto RequestChessRecent = [&]() {
			if(s_pChessRecentRequest && s_pChessRecentRequest->State() == EHttpState::RUNNING)
				return;
			char aUrl[512];
			AetherBuildApiUrl(aUrl, sizeof(aUrl), "/v1/chess/recent?limit=20");
			s_pChessRecentRequest = std::make_shared<CHttpRequest>(aUrl);
			Http()->Run(s_pChessRecentRequest);
			s_ChessOnline.m_LastRecentRequest = time_get();
			str_copy(s_ChessOnline.m_aStatus, "Refreshing recent chess matches...", sizeof(s_ChessOnline.m_aStatus));
		};
		auto RequestChessOnline = [&]() {
			GameClient()->m_AetherBadges.RequestChessOnline(true);
			s_ChessOnline.m_LastOnlineRequest = time_get();
			str_copy(s_ChessOnline.m_aStatus, "Refreshing global chess lobby...", sizeof(s_ChessOnline.m_aStatus));
		};
		auto SendChessInvite = [&](const char *pTarget) {
			GameClient()->m_AetherBadges.SendChessInvite(pTarget);
			str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "Challenge sent to %s.", pTarget);
		};
		auto SendChessInviteReply = [&](const char *pType) {
			GameClient()->m_AetherBadges.SendChessInviteReply(pType);
			if(str_comp(pType, "chess:decline") == 0)
				s_ChessOnline.m_HasInvite = false;
		};
		auto SendChessMove = [&](const char *pUci) {
			if(!s_ChessOnline.m_InMatch || s_ChessOnline.m_aMatchId[0] == '\0' || s_ChessOnline.m_MovePending || s_ChessOnline.m_ResignPending || s_ChessOnline.m_Finished)
				return;
			GameClient()->m_AetherBadges.SendChessMove(s_ChessOnline.m_aMatchId, pUci);
			s_ChessOnline.m_MovePending = true;
			str_copy(s_ChessOnline.m_aStatus, "Sending move...", sizeof(s_ChessOnline.m_aStatus));
		};
		auto SendChessResign = [&]() {
			if(!s_ChessOnline.m_InMatch || s_ChessOnline.m_aMatchId[0] == '\0' || s_ChessOnline.m_ResignPending || s_ChessOnline.m_Finished)
				return;
			GameClient()->m_AetherBadges.SendChessResign(s_ChessOnline.m_aMatchId);
			s_ChessOnline.m_ResignPending = true;
			str_copy(s_ChessOnline.m_aStatus, "Resigning match...", sizeof(s_ChessOnline.m_aStatus));
		};
		auto ReadChessLeaderboard = [&](const json_value *pPlayers, std::array<SChessLeaderboardEntry, 5> &aEntries, int &Count) {
			Count = 0;
			if(!pPlayers || pPlayers->type != json_array)
				return;
			for(int i = 0; i < json_array_length(pPlayers) && Count < (int)aEntries.size(); ++i)
			{
				const json_value *pPlayer = json_array_get(pPlayers, i);
				if(!pPlayer || pPlayer->type != json_object)
					continue;
				SChessLeaderboardEntry &Entry = aEntries[Count++];
				str_copy(Entry.m_aName, json_string_get(json_object_get(pPlayer, "player_name")) ? json_string_get(json_object_get(pPlayer, "player_name")) : "-", sizeof(Entry.m_aName));
				Entry.m_Rating = json_int_get(json_object_get(pPlayer, "rating"));
				Entry.m_Wins = json_int_get(json_object_get(pPlayer, "wins"));
				Entry.m_Losses = json_int_get(json_object_get(pPlayer, "losses"));
				Entry.m_Draws = json_int_get(json_object_get(pPlayer, "draws"));
			}
		};
		auto JsonIntFallback = [](const json_value *pObject, const char *pName, int Fallback) {
			if(!pObject || pObject->type != json_object)
				return Fallback;
			const json_value *pValue = json_object_get(pObject, pName);
			return pValue && pValue->type == json_integer ? (int)pValue->u.integer : Fallback;
		};
		auto ReadChessRecentMatches = [&](const json_value *pMatches) {
			s_ChessOnline.m_RecentCount = 0;
			if(!pMatches || pMatches->type != json_array)
				return;
			for(int i = 0; i < json_array_length(pMatches) && s_ChessOnline.m_RecentCount < (int)s_ChessOnline.m_aRecent.size(); ++i)
			{
				const json_value *pMatch = json_array_get(pMatches, i);
				if(!pMatch || pMatch->type != json_object)
					continue;
				const json_value *pWhite = json_object_get(pMatch, "white");
				const json_value *pBlack = json_object_get(pMatch, "black");
				SChessRecentMatch &Entry = s_ChessOnline.m_aRecent[s_ChessOnline.m_RecentCount++];
				str_copy(Entry.m_aWhite, json_string_get(json_object_get(pWhite, "player_name")) ? json_string_get(json_object_get(pWhite, "player_name")) : "-", sizeof(Entry.m_aWhite));
				str_copy(Entry.m_aBlack, json_string_get(json_object_get(pBlack, "player_name")) ? json_string_get(json_object_get(pBlack, "player_name")) : "-", sizeof(Entry.m_aBlack));
				str_copy(Entry.m_aWinner, json_string_get(json_object_get(pMatch, "winner")) ? json_string_get(json_object_get(pMatch, "winner")) : "", sizeof(Entry.m_aWinner));
				str_copy(Entry.m_aResult, json_string_get(json_object_get(pMatch, "result")) ? json_string_get(json_object_get(pMatch, "result")) : "", sizeof(Entry.m_aResult));
				str_copy(Entry.m_aReason, json_string_get(json_object_get(pMatch, "result_reason")) ? json_string_get(json_object_get(pMatch, "result_reason")) : "", sizeof(Entry.m_aReason));
				str_copy(Entry.m_aFinishedAt, json_string_get(json_object_get(pMatch, "finished_at")) ? json_string_get(json_object_get(pMatch, "finished_at")) : "", sizeof(Entry.m_aFinishedAt));
				Entry.m_WhiteRatingBefore = JsonIntFallback(pWhite, "rating_before", 1200);
				Entry.m_WhiteRatingAfter = JsonIntFallback(pWhite, "rating_after", Entry.m_WhiteRatingBefore);
				Entry.m_BlackRatingBefore = JsonIntFallback(pBlack, "rating_before", 1200);
				Entry.m_BlackRatingAfter = JsonIntFallback(pBlack, "rating_after", Entry.m_BlackRatingBefore);
			}
		};
		auto PumpChessRecentRequest = [&]() {
			if(!s_pChessRecentRequest)
				return;
			const EHttpState State = s_pChessRecentRequest->State();
			if(State != EHttpState::DONE && State != EHttpState::ERROR && State != EHttpState::ABORTED)
				return;
			if(State == EHttpState::DONE && s_pChessRecentRequest->StatusCode() >= 200 && s_pChessRecentRequest->StatusCode() < 400)
			{
				json_value *pJson = s_pChessRecentRequest->ResultJson();
				ReadChessRecentMatches(pJson && pJson->type == json_object ? json_object_get(pJson, "matches") : nullptr);
				str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "Loaded %d recent match(es).", s_ChessOnline.m_RecentCount);
				if(pJson)
					json_value_free(pJson);
			}
			else
			{
				const int StatusCode = State == EHttpState::DONE ? s_pChessRecentRequest->StatusCode() : 0;
				if(StatusCode == 404)
					str_copy(s_ChessOnline.m_aStatus, "Recent matches need latest API deploy.", sizeof(s_ChessOnline.m_aStatus));
				else
					str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "Recent matches refresh failed (%d).", StatusCode);
			}
			s_pChessRecentRequest = nullptr;
		};
		auto ReadChessOnlinePlayers = [&](const json_value *pPlayers) {
			s_ChessOnline.m_OnlineCount = 0;
			if(!pPlayers || pPlayers->type != json_array)
				return;
			for(int i = 0; i < json_array_length(pPlayers) && s_ChessOnline.m_OnlineCount < (int)s_ChessOnline.m_aOnline.size(); ++i)
			{
				const json_value *pPlayer = json_array_get(pPlayers, i);
				if(!pPlayer || pPlayer->type != json_object)
					continue;
				SChessOnlinePlayer &Entry = s_ChessOnline.m_aOnline[s_ChessOnline.m_OnlineCount++];
				str_copy(Entry.m_aName, json_string_get(json_object_get(pPlayer, "player_name")) ? json_string_get(json_object_get(pPlayer, "player_name")) : "-", sizeof(Entry.m_aName));
				str_copy(Entry.m_aClient, json_string_get(json_object_get(pPlayer, "client")) ? json_string_get(json_object_get(pPlayer, "client")) : "-", sizeof(Entry.m_aClient));
				str_copy(Entry.m_aServer, json_string_get(json_object_get(pPlayer, "server_name")) ? json_string_get(json_object_get(pPlayer, "server_name")) : "-", sizeof(Entry.m_aServer));
				str_copy(Entry.m_aMap, json_string_get(json_object_get(pPlayer, "map")) ? json_string_get(json_object_get(pPlayer, "map")) : "-", sizeof(Entry.m_aMap));
				Entry.m_Rating = json_int_get(json_object_get(pPlayer, "rating"));
				const json_value *pSpectator = json_object_get(pPlayer, "spectator");
				Entry.m_Spectator = pSpectator && pSpectator->type == json_boolean && pSpectator->u.boolean;
			}
		};
		auto ApplyChessOnlineUpdate = [&](const json_value *pPlayer) {
			if(!pPlayer || pPlayer->type != json_object)
				return;
			const char *pName = json_string_get(json_object_get(pPlayer, "player_name"));
			if(!pName || !pName[0])
				return;
			int Target = -1;
			for(int i = 0; i < s_ChessOnline.m_OnlineCount; ++i)
			{
				if(str_comp_nocase(s_ChessOnline.m_aOnline[i].m_aName, pName) == 0)
				{
					Target = i;
					break;
				}
			}
			if(Target < 0 && s_ChessOnline.m_OnlineCount < (int)s_ChessOnline.m_aOnline.size())
				Target = s_ChessOnline.m_OnlineCount++;
			if(Target < 0)
				return;
			SChessOnlinePlayer &Entry = s_ChessOnline.m_aOnline[Target];
			str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));
			str_copy(Entry.m_aClient, json_string_get(json_object_get(pPlayer, "client")) ? json_string_get(json_object_get(pPlayer, "client")) : "-", sizeof(Entry.m_aClient));
			str_copy(Entry.m_aServer, json_string_get(json_object_get(pPlayer, "server_name")) ? json_string_get(json_object_get(pPlayer, "server_name")) : "-", sizeof(Entry.m_aServer));
			str_copy(Entry.m_aMap, json_string_get(json_object_get(pPlayer, "map")) ? json_string_get(json_object_get(pPlayer, "map")) : "-", sizeof(Entry.m_aMap));
			Entry.m_Rating = json_int_get(json_object_get(pPlayer, "rating"));
			const json_value *pSpectator = json_object_get(pPlayer, "spectator");
			Entry.m_Spectator = pSpectator && pSpectator->type == json_boolean && pSpectator->u.boolean;
		};
		auto ApplyChessOnlineLeft = [&](const char *pName) {
			if(!pName || !pName[0])
				return;
			for(int i = 0; i < s_ChessOnline.m_OnlineCount; ++i)
			{
				if(str_comp_nocase(s_ChessOnline.m_aOnline[i].m_aName, pName) != 0)
					continue;
				for(int j = i + 1; j < s_ChessOnline.m_OnlineCount; ++j)
					s_ChessOnline.m_aOnline[j - 1] = s_ChessOnline.m_aOnline[j];
				--s_ChessOnline.m_OnlineCount;
				return;
			}
		};
		auto JsonBool = [](const json_value *pValue) {
			return pValue && pValue->type == json_boolean && pValue->u.boolean;
		};
		auto JsonIntOr = [](const json_value *pObject, const char *pName, int Fallback) {
			if(!pObject || pObject->type != json_object)
				return Fallback;
			const int Value = json_int_get(json_object_get(pObject, pName));
			return Value > 0 || str_comp(pName, "rating_delta") == 0 ? Value : Fallback;
		};
		auto ApplyChessMatchObject = [&](const json_value *pMatch) {
			if(!pMatch || pMatch->type != json_object)
				return;
			const char *pId = json_string_get(json_object_get(pMatch, "id"));
			const char *pStatus = json_string_get(json_object_get(pMatch, "status"));
			const char *pTurn = json_string_get(json_object_get(pMatch, "turn"));
			const char *pFen = json_string_get(json_object_get(pMatch, "fen"));
			const json_value *pWhite = json_object_get(pMatch, "white");
			const json_value *pBlack = json_object_get(pMatch, "black");
			const char *pWhiteName = pWhite && pWhite->type == json_object ? json_string_get(json_object_get(pWhite, "player_name")) : "";
			const char *pBlackName = pBlack && pBlack->type == json_object ? json_string_get(json_object_get(pBlack, "player_name")) : "";
			const bool SameMatch = pId && str_comp(pId, s_ChessOnline.m_aMatchId) == 0;
			const int OldSelected = s_ChessOnline.m_Selected;
			const char OldSelectedPiece = OldSelected >= 0 && OldSelected < (int)s_ChessOnline.m_aBoard.size() ? s_ChessOnline.m_aBoard[OldSelected] : '\0';
			str_copy(s_ChessOnline.m_aMatchId, pId ? pId : "", sizeof(s_ChessOnline.m_aMatchId));
			str_copy(s_ChessOnline.m_aWhite, pWhiteName ? pWhiteName : "", sizeof(s_ChessOnline.m_aWhite));
			str_copy(s_ChessOnline.m_aBlack, pBlackName ? pBlackName : "", sizeof(s_ChessOnline.m_aBlack));
			s_ChessOnline.m_WhiteRating = JsonIntOr(pWhite, "rating", 1200);
			s_ChessOnline.m_BlackRating = JsonIntOr(pBlack, "rating", 1200);
			s_ChessOnline.m_WhiteRatingBefore = JsonIntOr(pWhite, "rating_before", s_ChessOnline.m_WhiteRating);
			s_ChessOnline.m_BlackRatingBefore = JsonIntOr(pBlack, "rating_before", s_ChessOnline.m_BlackRating);
			s_ChessOnline.m_WhiteRatingAfter = JsonIntOr(pWhite, "rating_after", s_ChessOnline.m_WhiteRating);
			s_ChessOnline.m_BlackRatingAfter = JsonIntOr(pBlack, "rating_after", s_ChessOnline.m_BlackRating);
			s_ChessOnline.m_WhiteRatingDelta = JsonIntOr(pWhite, "rating_delta", 0);
			s_ChessOnline.m_BlackRatingDelta = JsonIntOr(pBlack, "rating_delta", 0);
			s_ChessOnline.m_InMatch = true;
			s_ChessOnline.m_TurnWhite = !pTurn || str_comp(pTurn, "black") != 0;
			char aLocal[MAX_NAME_LENGTH];
			s_ChessOnline.m_IsWhite = !LocalChessName(aLocal, sizeof(aLocal)) || str_comp(aLocal, s_ChessOnline.m_aBlack) != 0;
			ParseOnlineFen(pFen);
			s_ChessOnline.m_vLegalMoves.clear();
			const json_value *pMoves = json_object_get(pMatch, "legal_moves");
			if(pMoves && pMoves->type == json_array)
			{
				for(int i = 0; i < json_array_length(pMoves); ++i)
				{
					const char *pMove = json_string_get(json_array_get(pMoves, i));
					if(pMove && pMove[0])
						s_ChessOnline.m_vLegalMoves.emplace_back(pMove);
				}
			}
			const json_value *pLastMove = json_object_get(pMatch, "last_move");
			if(pLastMove && pLastMove->type == json_object)
			{
				s_ChessOnline.m_LastMoveFrom = SquareIndex(json_string_get(json_object_get(pLastMove, "from")));
				s_ChessOnline.m_LastMoveTo = SquareIndex(json_string_get(json_object_get(pLastMove, "to")));
			}
			else
			{
				s_ChessOnline.m_LastMoveFrom = -1;
				s_ChessOnline.m_LastMoveTo = -1;
			}
			const char *pWinner = json_string_get(json_object_get(pMatch, "winner"));
			const char *pResult = json_string_get(json_object_get(pMatch, "result"));
			const char *pReason = json_string_get(json_object_get(pMatch, "result_reason"));
			const bool Finished = pStatus && str_comp(pStatus, "finished") == 0;
			const bool WasFinished = SameMatch && s_ChessOnline.m_Finished;
			s_ChessOnline.m_Check = JsonBool(json_object_get(pMatch, "check"));
			s_ChessOnline.m_Checkmate = JsonBool(json_object_get(pMatch, "checkmate"));
			s_ChessOnline.m_Stalemate = JsonBool(json_object_get(pMatch, "stalemate"));
			s_ChessOnline.m_Finished = Finished;
			str_copy(s_ChessOnline.m_aWinner, pWinner ? pWinner : "", sizeof(s_ChessOnline.m_aWinner));
			str_copy(s_ChessOnline.m_aResult, pResult ? pResult : "", sizeof(s_ChessOnline.m_aResult));
			str_copy(s_ChessOnline.m_aResultReason, pReason ? pReason : "", sizeof(s_ChessOnline.m_aResultReason));
			s_ChessOnline.m_MovePending = false;
			if(Finished)
				s_ChessOnline.m_ResignPending = false;
			if(SameMatch && OldSelected >= 0 && OldSelected < (int)s_ChessOnline.m_aBoard.size() && OldSelectedPiece != '\0' && s_ChessOnline.m_aBoard[OldSelected] == OldSelectedPiece && !Finished && OnlineMyTurn())
				s_ChessOnline.m_Selected = OldSelected;
			else
				s_ChessOnline.m_Selected = -1;
			if(Finished)
			{
				str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "Finished: %s", pWinner && pWinner[0] ? pWinner : "draw");
				if(!WasFinished)
					RequestChessLeaderboards();
			}
			else if(s_ChessOnline.m_Check)
				str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "%s is in check.", s_ChessOnline.m_TurnWhite ? "White" : "Black");
			else
				str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "%s to move.", s_ChessOnline.m_TurnWhite ? "White" : "Black");
		};
		auto PumpChessRealtimeMessages = [&]() {
			std::vector<std::string> vMessages;
			GameClient()->m_AetherBadges.PumpChessMessages(vMessages);
			auto ApplyChessPayload = [&](const json_value *pJson) {
				if(!pJson || pJson->type != json_object)
					return;
				const char *pType = json_string_get(json_object_get(pJson, "type"));
				if((!pType || !pType[0]) && json_object_get(pJson, "players"))
				{
					const char *pPeriod = json_string_get(json_object_get(pJson, "period"));
					if(pPeriod && str_comp(pPeriod, "monthly") == 0)
						ReadChessLeaderboard(json_object_get(pJson, "players"), s_ChessOnline.m_aMonthly, s_ChessOnline.m_MonthlyCount);
					else
						ReadChessLeaderboard(json_object_get(pJson, "players"), s_ChessOnline.m_aOverall, s_ChessOnline.m_OverallCount);
					str_copy(s_ChessOnline.m_aStatus, "Chess leaderboard refreshed.", sizeof(s_ChessOnline.m_aStatus));
				}
				else if(pType && str_comp(pType, "chess:invite_received") == 0)
				{
					str_copy(s_ChessOnline.m_aInviteId, json_string_get(json_object_get(pJson, "invite_id")) ? json_string_get(json_object_get(pJson, "invite_id")) : "", sizeof(s_ChessOnline.m_aInviteId));
					str_copy(s_ChessOnline.m_aInviteFrom, json_string_get(json_object_get(pJson, "from_player")) ? json_string_get(json_object_get(pJson, "from_player")) : "", sizeof(s_ChessOnline.m_aInviteFrom));
					s_ChessOnline.m_HasInvite = true;
					s_ChessOnline.m_Tab = 1;
					str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "%s challenged you.", s_ChessOnline.m_aInviteFrom);
				}
				else if(pType && (str_comp(pType, "chess:invite_expired") == 0 || str_comp(pType, "chess:invite_declined") == 0))
				{
					s_ChessOnline.m_HasInvite = false;
					str_copy(s_ChessOnline.m_aStatus, "Chess invite closed.", sizeof(s_ChessOnline.m_aStatus));
				}
				else if(pType && str_comp(pType, "chess:invite_sent") == 0)
				{
					str_copy(s_ChessOnline.m_aStatus, "Challenge sent. Waiting for response.", sizeof(s_ChessOnline.m_aStatus));
				}
				else if(pType && str_comp(pType, "chess:match_snapshot") == 0)
				{
					s_ChessOnline.m_Tab = 1;
					s_ChessOnline.m_HasInvite = false;
					ApplyChessMatchObject(json_object_get(pJson, "match"));
				}
				else if(pType && str_comp(pType, "chess:finish") == 0)
				{
					ApplyChessMatchObject(json_object_get(pJson, "match"));
				}
				else if(pType && str_comp(pType, "chess:leaderboard") == 0)
				{
					const char *pPeriod = json_string_get(json_object_get(pJson, "period"));
					if(pPeriod && str_comp(pPeriod, "monthly") == 0)
						ReadChessLeaderboard(json_object_get(pJson, "players"), s_ChessOnline.m_aMonthly, s_ChessOnline.m_MonthlyCount);
					else
						ReadChessLeaderboard(json_object_get(pJson, "players"), s_ChessOnline.m_aOverall, s_ChessOnline.m_OverallCount);
					str_copy(s_ChessOnline.m_aStatus, "Chess leaderboard refreshed.", sizeof(s_ChessOnline.m_aStatus));
				}
				else if(pType && str_comp(pType, "chess:online_snapshot") == 0)
				{
					ReadChessOnlinePlayers(json_object_get(pJson, "players"));
					str_copy(s_ChessOnline.m_aStatus, "Global chess lobby refreshed.", sizeof(s_ChessOnline.m_aStatus));
				}
				else if(pType && str_comp(pType, "chess:online_update") == 0)
				{
					ApplyChessOnlineUpdate(json_object_get(pJson, "player"));
				}
				else if(pType && str_comp(pType, "chess:online_left") == 0)
				{
					ApplyChessOnlineLeft(json_string_get(json_object_get(pJson, "player_name")));
				}
				else if(pType && str_comp(pType, "chess:error") == 0)
				{
					const char *pError = json_string_get(json_object_get(pJson, "error"));
					s_ChessOnline.m_MovePending = false;
					s_ChessOnline.m_ResignPending = false;
					str_format(s_ChessOnline.m_aStatus, sizeof(s_ChessOnline.m_aStatus), "Chess error: %s", pError ? pError : "unknown");
				}
			};
			for(const std::string &Message : vMessages)
			{
				json_settings JsonSettings;
				mem_zero(&JsonSettings, sizeof(JsonSettings));
				char aError[256];
				json_value *pJson = json_parse_ex(&JsonSettings, static_cast<const json_char *>(Message.c_str()), Message.size(), aError);
				if(!pJson)
					continue;
				const json_value *pMessages = pJson->type == json_object ? json_object_get(pJson, "messages") : nullptr;
				if(pMessages && pMessages->type == json_array)
				{
					for(int i = 0; i < json_array_length(pMessages); ++i)
						ApplyChessPayload(json_array_get(pMessages, i));
				}
				else
					ApplyChessPayload(pJson);
				json_value_free(pJson);
			}
		};
		if(s_AetherOpenChessOnline)
		{
			s_AetherOpenChessOnline = false;
			s_ActiveGame = EGame::CHESS;
			s_ChessOnline.m_Tab = 1;
			RequestChessOnline();
		}

		auto SnakeContains = [&](int Cell) {
			for(int i = 0; i < s_Snake.m_Length; ++i)
				if(s_Snake.m_aBody[i] == Cell)
					return true;
			return false;
		};
		auto SnakeSpawnFood = [&]() {
			const int CellCount = s_Snake.m_Size * s_Snake.m_Size;
			for(int Attempts = 0; Attempts < CellCount * 2; ++Attempts)
			{
				const int Candidate = rand() % CellCount;
				if(!SnakeContains(Candidate))
				{
					s_Snake.m_Food = Candidate;
					return;
				}
			}
			s_Snake.m_Food = 0;
		};
		auto ResetSnake = [&]() {
			s_Snake.m_InGame = true;
			s_Snake.m_GameOver = false;
			s_Snake.m_Size = std::clamp(g_Config.m_AeGameSnakeFieldSize, 10, 24);
			s_Snake.m_Dir = 1;
			s_Snake.m_NextDir = 1;
			s_Snake.m_Length = 3;
			s_Snake.m_Score = 0;
			s_Snake.m_LastStep = Client()->LocalTime();
			s_Snake.m_aWasDirectionDown = {};
			s_Snake.m_aQueuedDirs.fill(0);
			s_Snake.m_QueueSize = 0;
			const int Center = (s_Snake.m_Size / 2) * s_Snake.m_Size + s_Snake.m_Size / 2;
			s_Snake.m_aBody[0] = Center;
			s_Snake.m_aBody[1] = Center - 1;
			s_Snake.m_aBody[2] = Center - 2;
			SnakeSpawnFood();
		};
		auto UpdateSnake = [&]() {
			if(!s_Snake.m_InGame)
				return;
			if(s_Snake.m_GameOver)
			{
				if(Input()->KeyPress(KEY_SPACE))
					ResetSnake();
				return;
			}
			auto QueueDir = [&](int Dir) {
				const int BaseDir = s_Snake.m_QueueSize > 0 ? s_Snake.m_aQueuedDirs[s_Snake.m_QueueSize - 1] : s_Snake.m_Dir;
				if(s_Snake.m_Length > 1 && (Dir + 2) % 4 == BaseDir)
					return;
				if(s_Snake.m_QueueSize > 0 && s_Snake.m_aQueuedDirs[s_Snake.m_QueueSize - 1] == Dir)
					return;
				if(s_Snake.m_QueueSize < (int)s_Snake.m_aQueuedDirs.size())
					s_Snake.m_aQueuedDirs[s_Snake.m_QueueSize++] = Dir;
				s_Snake.m_NextDir = Dir;
			};
			const std::array<bool, 4> aDirectionDown = {{
				Input()->KeyIsPressed(KEY_UP) || Input()->KeyIsPressed(KEY_W),
				Input()->KeyIsPressed(KEY_RIGHT) || Input()->KeyIsPressed(KEY_D),
				Input()->KeyIsPressed(KEY_DOWN) || Input()->KeyIsPressed(KEY_S),
				Input()->KeyIsPressed(KEY_LEFT) || Input()->KeyIsPressed(KEY_A),
			}};
			for(int Dir = 0; Dir < 4; ++Dir)
			{
				if(aDirectionDown[Dir] && !s_Snake.m_aWasDirectionDown[Dir])
					QueueDir(Dir);
				s_Snake.m_aWasDirectionDown[Dir] = aDirectionDown[Dir];
			}
			if(Input()->KeyPress(KEY_SPACE))
				ResetSnake();

			const double Now = Client()->LocalTime();
			const double Step = std::max(0.065, 0.30 - std::clamp(g_Config.m_AeGameSnakeStartSpeed, 1, 8) * 0.025);
			if(Now - s_Snake.m_LastStep < Step)
				return;
			s_Snake.m_LastStep = Now;
			if(s_Snake.m_QueueSize > 0)
			{
				s_Snake.m_Dir = s_Snake.m_aQueuedDirs[0];
				for(int i = 1; i < s_Snake.m_QueueSize; ++i)
					s_Snake.m_aQueuedDirs[i - 1] = s_Snake.m_aQueuedDirs[i];
				--s_Snake.m_QueueSize;
				s_Snake.m_NextDir = s_Snake.m_QueueSize > 0 ? s_Snake.m_aQueuedDirs[0] : s_Snake.m_Dir;
			}
			else
				s_Snake.m_Dir = s_Snake.m_NextDir;
			int X = s_Snake.m_aBody[0] % s_Snake.m_Size;
			int Y = s_Snake.m_aBody[0] / s_Snake.m_Size;
			if(s_Snake.m_Dir == 0)
				--Y;
			else if(s_Snake.m_Dir == 1)
				++X;
			else if(s_Snake.m_Dir == 2)
				++Y;
			else
				--X;
			if(g_Config.m_AeGameSnakeWrap)
			{
				X = (X + s_Snake.m_Size) % s_Snake.m_Size;
				Y = (Y + s_Snake.m_Size) % s_Snake.m_Size;
			}
			else if(X < 0 || Y < 0 || X >= s_Snake.m_Size || Y >= s_Snake.m_Size)
			{
				s_Snake.m_GameOver = true;
				return;
			}
			const int NewHead = Y * s_Snake.m_Size + X;
			const bool Grow = NewHead == s_Snake.m_Food;
			for(int i = 0; i < s_Snake.m_Length - (Grow ? 0 : 1); ++i)
			{
				if(s_Snake.m_aBody[i] == NewHead)
				{
					s_Snake.m_GameOver = true;
					return;
				}
			}
			for(int i = std::min((int)s_Snake.m_aBody.size() - 1, s_Snake.m_Length); i > 0; --i)
				s_Snake.m_aBody[i] = s_Snake.m_aBody[i - 1];
			s_Snake.m_aBody[0] = NewHead;
			if(Grow)
			{
				s_Snake.m_Length = std::min(s_Snake.m_Length + 1, (int)s_Snake.m_aBody.size());
				++s_Snake.m_Score;
				SnakeSpawnFood();
			}
		};

		constexpr unsigned char MINE_BOMB = 1 << 0;
		constexpr unsigned char MINE_OPEN = 1 << 1;
		constexpr unsigned char MINE_FLAG = 1 << 2;
		auto MinesDifficulty = [](int Difficulty, int &W, int &H, int &Bombs) {
			if(Difficulty <= 0)
			{
				W = 9;
				H = 9;
				Bombs = 10;
			}
			else if(Difficulty == 1)
			{
				W = 12;
				H = 10;
				Bombs = 18;
			}
			else
			{
				W = 16;
				H = 12;
				Bombs = 40;
			}
		};
		auto ResetMines = [&]() {
			s_Mines.m_InGame = true;
			s_Mines.m_BombsPlaced = false;
			s_Mines.m_GameOver = false;
			s_Mines.m_Won = false;
			MinesDifficulty(g_Config.m_AeGameMinesweeperDifficulty, s_Mines.m_Width, s_Mines.m_Height, s_Mines.m_Bombs);
			s_Mines.m_aCells.fill(0);
		};
		auto MinesAdjacent = [&](int Idx) {
			const int X = Idx % s_Mines.m_Width;
			const int Y = Idx / s_Mines.m_Width;
			int Count = 0;
			for(int yy = -1; yy <= 1; ++yy)
			{
				for(int xx = -1; xx <= 1; ++xx)
				{
					if(xx == 0 && yy == 0)
						continue;
					const int NX = X + xx;
					const int NY = Y + yy;
					if(NX >= 0 && NY >= 0 && NX < s_Mines.m_Width && NY < s_Mines.m_Height && (s_Mines.m_aCells[NY * s_Mines.m_Width + NX] & MINE_BOMB))
						++Count;
				}
			}
			return Count;
		};
		auto MinesPlaceBombs = [&](int Safe) {
			const int CellCount = s_Mines.m_Width * s_Mines.m_Height;
			int Placed = 0;
			while(Placed < s_Mines.m_Bombs)
			{
				const int Candidate = rand() % CellCount;
				if(Candidate == Safe || (s_Mines.m_aCells[Candidate] & MINE_BOMB))
					continue;
				s_Mines.m_aCells[Candidate] |= MINE_BOMB;
				++Placed;
			}
			s_Mines.m_BombsPlaced = true;
		};
		auto MinesCheckWin = [&]() {
			for(int i = 0; i < s_Mines.m_Width * s_Mines.m_Height; ++i)
				if(!(s_Mines.m_aCells[i] & MINE_BOMB) && !(s_Mines.m_aCells[i] & MINE_OPEN))
					return;
			s_Mines.m_Won = true;
			s_Mines.m_GameOver = true;
		};
		auto MinesReveal = [&](int Start) {
			std::array<int, 16 * 12> aStack{};
			int StackSize = 0;
			aStack[StackSize++] = Start;
			while(StackSize > 0)
			{
				const int Idx = aStack[--StackSize];
				if(s_Mines.m_aCells[Idx] & (MINE_OPEN | MINE_FLAG))
					continue;
				s_Mines.m_aCells[Idx] |= MINE_OPEN;
				if(MinesAdjacent(Idx) != 0)
					continue;
				const int X = Idx % s_Mines.m_Width;
				const int Y = Idx / s_Mines.m_Width;
				for(int yy = -1; yy <= 1; ++yy)
				{
					for(int xx = -1; xx <= 1; ++xx)
					{
						if(xx == 0 && yy == 0)
							continue;
						const int NX = X + xx;
						const int NY = Y + yy;
						if(NX >= 0 && NY >= 0 && NX < s_Mines.m_Width && NY < s_Mines.m_Height && StackSize < (int)aStack.size())
							aStack[StackSize++] = NY * s_Mines.m_Width + NX;
					}
				}
			}
			MinesCheckWin();
		};

		auto TetrisCell = [](int Piece, int Rotation, int X, int Y) {
			static const char *s_apPieces[7][4] = {
				{"0000111100000000", "0010001000100010", "0000000011110000", "0100010001000100"},
				{"0000011001100000", "0000011001100000", "0000011001100000", "0000011001100000"},
				{"0000010011100000", "0000010001100100", "0000000011100100", "0000010011000100"},
				{"0000011011000000", "0000010001100010", "0000000001101100", "0000100011000100"},
				{"0000110001100000", "0000001001100100", "0000000011000110", "0000010011001000"},
				{"0000100011100000", "0000011001000100", "0000000011100010", "0000010001001100"},
				{"0000001011100000", "0000010001000110", "0000000011101000", "0000110001000100"},
			};
			return s_apPieces[Piece][Rotation & 3][Y * 4 + X] == '1';
		};
		auto TetrisCollides = [&](int Piece, int Rotation, int X, int Y) {
			for(int py = 0; py < 4; ++py)
			{
				for(int px = 0; px < 4; ++px)
				{
					if(!TetrisCell(Piece, Rotation, px, py))
						continue;
					const int BX = X + px;
					const int BY = Y + py;
					if(BX < 0 || BX >= 10 || BY >= 20)
						return true;
					if(BY >= 0 && s_Tetris.m_aBoard[BY * 10 + BX] != 0)
						return true;
				}
			}
			return false;
		};
		auto TetrisSpawn = [&]() {
			s_Tetris.m_Piece = s_Tetris.m_NextPiece;
			s_Tetris.m_NextPiece = rand() % 7;
			s_Tetris.m_Rotation = 0;
			s_Tetris.m_X = 3;
			s_Tetris.m_Y = 0;
			if(TetrisCollides(s_Tetris.m_Piece, s_Tetris.m_Rotation, s_Tetris.m_X, s_Tetris.m_Y))
				s_Tetris.m_GameOver = true;
		};
		auto ResetTetris = [&]() {
			s_Tetris.m_InGame = true;
			s_Tetris.m_GameOver = false;
			s_Tetris.m_Score = 0;
			s_Tetris.m_Lines = 0;
			s_Tetris.m_aBoard.fill(0);
			s_Tetris.m_NextPiece = rand() % 7;
			s_Tetris.m_LastFall = Client()->LocalTime();
			s_Tetris.m_LastHorizontalMove = 0.0;
			s_Tetris.m_LastHorizontalDir = 0;
			s_Tetris.m_aWasKeyDown = {};
			TetrisSpawn();
		};
		auto TetrisLockPiece = [&]() {
			for(int py = 0; py < 4; ++py)
			{
				for(int px = 0; px < 4; ++px)
				{
					if(!TetrisCell(s_Tetris.m_Piece, s_Tetris.m_Rotation, px, py))
						continue;
					const int BX = s_Tetris.m_X + px;
					const int BY = s_Tetris.m_Y + py;
					if(BX >= 0 && BX < 10 && BY >= 0 && BY < 20)
						s_Tetris.m_aBoard[BY * 10 + BX] = s_Tetris.m_Piece + 1;
				}
			}
			for(int y = 19; y >= 0; --y)
			{
				bool Full = true;
				for(int x = 0; x < 10; ++x)
					Full &= s_Tetris.m_aBoard[y * 10 + x] != 0;
				if(!Full)
					continue;
				for(int yy = y; yy > 0; --yy)
					for(int x = 0; x < 10; ++x)
						s_Tetris.m_aBoard[yy * 10 + x] = s_Tetris.m_aBoard[(yy - 1) * 10 + x];
				for(int x = 0; x < 10; ++x)
					s_Tetris.m_aBoard[x] = 0;
				++s_Tetris.m_Lines;
				s_Tetris.m_Score += 100;
				++y;
			}
			TetrisSpawn();
		};
		auto UpdateTetris = [&]() {
			if(!s_Tetris.m_InGame)
				return;
			if(s_Tetris.m_GameOver)
			{
				if(Input()->KeyPress(KEY_SPACE))
					ResetTetris();
				return;
			}
			const double Now = Client()->LocalTime();
			const bool LeftDown = Input()->KeyIsPressed(KEY_LEFT) || Input()->KeyIsPressed(KEY_A);
			const bool RightDown = Input()->KeyIsPressed(KEY_RIGHT) || Input()->KeyIsPressed(KEY_D);
			const bool RotateDown = Input()->KeyIsPressed(KEY_UP) || Input()->KeyIsPressed(KEY_W);
			const bool HardDropDown = Input()->KeyIsPressed(KEY_SPACE);
			const bool FreshLeft = LeftDown && !s_Tetris.m_aWasKeyDown[0];
			const bool FreshRight = RightDown && !s_Tetris.m_aWasKeyDown[1];
			const bool FreshRotate = RotateDown && !s_Tetris.m_aWasKeyDown[2];
			const bool FreshHardDrop = HardDropDown && !s_Tetris.m_aWasKeyDown[3];
			s_Tetris.m_aWasKeyDown[0] = LeftDown;
			s_Tetris.m_aWasKeyDown[1] = RightDown;
			s_Tetris.m_aWasKeyDown[2] = RotateDown;
			s_Tetris.m_aWasKeyDown[3] = HardDropDown;

			auto TryMoveHorizontal = [&](int Dir) {
				if(!TetrisCollides(s_Tetris.m_Piece, s_Tetris.m_Rotation, s_Tetris.m_X + Dir, s_Tetris.m_Y))
				{
					s_Tetris.m_X += Dir;
					s_Tetris.m_LastHorizontalMove = Now;
					s_Tetris.m_LastHorizontalDir = Dir;
				}
			};
			if(FreshLeft)
				TryMoveHorizontal(-1);
			else if(FreshRight)
				TryMoveHorizontal(1);
			else
			{
				const int HeldDir = LeftDown && !RightDown ? -1 : RightDown && !LeftDown ? 1 : 0;
				if(HeldDir != 0 && (s_Tetris.m_LastHorizontalDir != HeldDir || Now - s_Tetris.m_LastHorizontalMove >= 0.075))
					TryMoveHorizontal(HeldDir);
			}
			if(FreshRotate)
			{
				if(!TetrisCollides(s_Tetris.m_Piece, s_Tetris.m_Rotation + 1, s_Tetris.m_X, s_Tetris.m_Y))
					s_Tetris.m_Rotation = (s_Tetris.m_Rotation + 1) & 3;
			}
			if(FreshHardDrop)
			{
				while(!TetrisCollides(s_Tetris.m_Piece, s_Tetris.m_Rotation, s_Tetris.m_X, s_Tetris.m_Y + 1))
					++s_Tetris.m_Y;
				TetrisLockPiece();
				return;
			}
			const double FallStep = (Input()->KeyIsPressed(KEY_DOWN) || Input()->KeyIsPressed(KEY_S)) ? 0.05 : std::max(0.10, 0.78 - std::clamp(g_Config.m_AeGameTetrisStartSpeed, 1, 8) * 0.075);
			if(Now - s_Tetris.m_LastFall >= FallStep)
			{
				s_Tetris.m_LastFall = Now;
				if(!TetrisCollides(s_Tetris.m_Piece, s_Tetris.m_Rotation, s_Tetris.m_X, s_Tetris.m_Y + 1))
					++s_Tetris.m_Y;
				else
					TetrisLockPiece();
			}
		};

		auto DrawIntStepper = [&](CUIRect *pView, int &Value, int Min, int Max, const char *pLabel, int &ButtonIndex) {
			CUIRect Row, Label, Minus, ValueRect, Plus;
			pView->HSplitTop(20.0f * S, &Row, pView);
			Row.VSplitLeft(std::min(124.0f * S, Row.w * 0.46f), &Label, &Row);
			Ui()->DoLabel(&Label, pLabel, 12.0f * S, TEXTALIGN_ML);
			Row.VSplitRight(26.0f * S, &Row, &Plus);
			Row.VSplitRight(4.0f * S, &Row, nullptr);
			Row.VSplitRight(52.0f * S, &Row, &ValueRect);
			Row.VSplitRight(4.0f * S, &Row, nullptr);
			Row.VSplitRight(26.0f * S, &Row, &Minus);
			if(ButtonIndex + 1 < (int)std::size(s_aStepperMinus))
			{
				if(DoButton_Menu(&s_aStepperMinus[ButtonIndex], "-", 0, &Minus))
					Value = std::max(Min, Value - 1);
				char aValue[32];
				str_format(aValue, sizeof(aValue), "%d", Value);
				ValueRect.Draw(AetherPanelColor(0.30f), IGraphics::CORNER_ALL, 5.0f * S);
				Ui()->DoLabel(&ValueRect, aValue, 12.0f * S, TEXTALIGN_MC);
				if(DoButton_Menu(&s_aStepperPlus[ButtonIndex], "+", 0, &Plus))
					Value = std::min(Max, Value + 1);
				++ButtonIndex;
			}
			pView->HSplitTop(3.0f * S, nullptr, pView);
		};
		auto DrawBombIcon = [&](CUIRect IconRect, float Alpha = 1.0f) {
			CUIRect Body = IconRect;
			Body.Margin(IconRect.w * 0.18f, &Body);
			Body.y += Body.h * 0.10f;
			Body.h *= 0.82f;
			Body.w = minimum(Body.w, Body.h);
			Body.x = IconRect.x + (IconRect.w - Body.w) * 0.5f;
			Body.Draw(ColorRGBA(0.08f, 0.09f, 0.12f, 0.94f * Alpha), IGraphics::CORNER_ALL, Body.w * 0.50f);
			CUIRect Cap{Body.x + Body.w * 0.54f, Body.y - Body.h * 0.06f, Body.w * 0.22f, Body.h * 0.14f};
			Cap.Draw(ColorRGBA(0.26f, 0.27f, 0.31f, 0.95f * Alpha), IGraphics::CORNER_ALL, Cap.h * 0.35f);
			CUIRect Fuse{Cap.x + Cap.w * 0.74f, Cap.y - Cap.h * 0.60f, Body.w * 0.26f, Cap.h * 0.45f};
			Fuse.Draw(ColorRGBA(1.0f, 0.72f, 0.22f, 0.95f * Alpha), IGraphics::CORNER_ALL, Fuse.h * 0.45f);
			CUIRect Shine{Body.x + Body.w * 0.20f, Body.y + Body.h * 0.18f, Body.w * 0.18f, Body.h * 0.12f};
			Shine.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * Alpha), IGraphics::CORNER_ALL, Shine.h * 0.5f);
		};
		auto DrawSnakeIcon = [&](CUIRect IconRect) {
			CUIRect Seg = IconRect;
			Seg.Margin(IconRect.w * 0.16f, &Seg);
			const float Size = minimum(Seg.w, Seg.h) * 0.34f;
			for(int i = 0; i < 4; ++i)
			{
				const float T = (float)i / 3.0f;
				CUIRect Part{Seg.x + T * (Seg.w - Size), Seg.y + (i % 2 ? Seg.h * 0.42f : Seg.h * 0.16f), Size, Size};
				const ColorRGBA Col = i == 3 ? ColorRGBA(0.12f, 0.92f, 0.62f, 0.96f) : ColorRGBA(0.08f, 0.58f + 0.08f * i, 0.92f, 0.88f);
				Part.Draw(Col, IGraphics::CORNER_ALL, Size * 0.45f);
			}
			CUIRect Eye{Seg.x + Seg.w - Size * 0.45f, Seg.y + Seg.h * 0.24f, Size * 0.14f, Size * 0.14f};
			Eye.Draw(ColorRGBA(0.02f, 0.03f, 0.05f, 1.0f), IGraphics::CORNER_ALL, Eye.w);
		};
		auto DrawGameCard = [&](int Index, CUIRect Card, const char *pIcon, const char *pTitle, const char *pHint, EGame Game) {
			Card.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 8.0f * S);
			Card.Margin(9.0f * S, &Card);
			CUIRect Header, Icon, Title;
			Card.HSplitTop(25.0f * S, &Header, &Card);
			Header.VSplitLeft(28.0f * S, &Icon, &Title);
			if(Game == EGame::SNAKE)
				DrawSnakeIcon(Icon);
			else if(Game == EGame::MINESWEEPER)
				DrawBombIcon(Icon);
			else if(Game == EGame::CHESS || Game == EGame::TETRIS)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				Ui()->DoLabel(&Icon, pIcon, 17.0f * S, TEXTALIGN_MC);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
			else
				Ui()->DoLabel(&Icon, pIcon, 17.0f * S, TEXTALIGN_MC);
			Ui()->DoLabel(&Title, pTitle, 18.0f * S, TEXTALIGN_ML);
			CUIRect Hint;
			Card.HSplitTop(19.0f * S, &Hint, &Card);
			TextRender()->TextColor(0.78f, 0.82f, 0.90f, 1.0f);
			Ui()->DoLabel(&Hint, pHint, 11.0f * S, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Card.HSplitTop(4.0f * S, nullptr, &Card);
			CUIRect Start;
			Card.HSplitBottom(28.0f * S, &Card, &Start);
			Card.HSplitBottom(6.0f * S, &Card, nullptr);
			int StepperIndex = Index * 2;
			if(Game == EGame::CHESS)
			{
				CUIRect Row;
				Card.HSplitTop(21.0f * S, &Row, &Card);
				Ui()->DoLabel(&Row, "Mode: Local two players", 12.0f * S, TEXTALIGN_ML);
				Card.HSplitTop(4.0f * S, nullptr, &Card);
				Card.HSplitTop(21.0f * S, &Row, &Card);
				if(DoButton_CheckBox(&s_ChessShowMovesButton, "Show legal moves", g_Config.m_AeGameChessShowLegalMoves, &Row))
					g_Config.m_AeGameChessShowLegalMoves ^= 1;
			}
			else if(Game == EGame::SNAKE)
			{
				DrawIntStepper(&Card, g_Config.m_AeGameSnakeFieldSize, 10, 24, "Field size", StepperIndex);
				DrawIntStepper(&Card, g_Config.m_AeGameSnakeStartSpeed, 1, 8, "Start speed", StepperIndex);
				CUIRect Row;
				Card.HSplitTop(21.0f * S, &Row, &Card);
				if(DoButton_CheckBox(&s_SnakeWrapButton, "Wrap through walls", g_Config.m_AeGameSnakeWrap, &Row))
					g_Config.m_AeGameSnakeWrap ^= 1;
			}
			else if(Game == EGame::MINESWEEPER)
			{
				CUIRect Row, Label, Button;
				Card.HSplitTop(21.0f * S, &Row, &Card);
				Row.VSplitLeft(96.0f * S, &Label, &Button);
				Ui()->DoLabel(&Label, "Difficulty", 12.0f * S, TEXTALIGN_ML);
				const char *pDifficulty = g_Config.m_AeGameMinesweeperDifficulty == 0 ? "Easy" : g_Config.m_AeGameMinesweeperDifficulty == 1 ? "Normal" : "Hard";
				if(DoButton_Menu(&s_MinesDifficultyButton, pDifficulty, 0, &Button))
					g_Config.m_AeGameMinesweeperDifficulty = (g_Config.m_AeGameMinesweeperDifficulty + 1) % 3;
				Card.HSplitTop(4.0f * S, nullptr, &Card);
				Card.HSplitTop(21.0f * S, &Row, &Card);
				int W, H, Bombs;
				MinesDifficulty(g_Config.m_AeGameMinesweeperDifficulty, W, H, Bombs);
				char aInfo[64];
				str_format(aInfo, sizeof(aInfo), "%dx%d, %d bombs", W, H, Bombs);
				Ui()->DoLabel(&Row, aInfo, 12.0f * S, TEXTALIGN_ML);
			}
			else if(Game == EGame::TETRIS)
			{
				DrawIntStepper(&Card, g_Config.m_AeGameTetrisStartSpeed, 1, 8, "Start speed", StepperIndex);
			}
			if(DoButton_Menu(&s_aGameStartButtons[Index], "Start", 0, &Start))
			{
				s_ActiveGame = Game;
				if(Game == EGame::CHESS)
					ResetChess();
				else if(Game == EGame::SNAKE)
					ResetSnake();
				else if(Game == EGame::MINESWEEPER)
					ResetMines();
				else if(Game == EGame::TETRIS)
					ResetTetris();
			}
		};

		if(s_ActiveGame != EGame::NONE)
		{
			CUIRect Arena = MainView;
			if(s_ScrollRegion.AddRect(Arena))
			{
				Arena.Draw(AetherPanelColor(0.34f), IGraphics::CORNER_ALL, 8.0f * S);
				Arena.Margin(14.0f * S, &Arena);
				CUIRect TopBar, GameArea;
				Arena.HSplitTop(36.0f * S, &TopBar, &GameArea);
				CUIRect Title, Back, Restart;
				TopBar.VSplitLeft(320.0f * S, &Title, &TopBar);
				TopBar.VSplitRight(92.0f * S, &TopBar, &Back);
				TopBar.VSplitRight(8.0f * S, &TopBar, nullptr);
				TopBar.VSplitRight(104.0f * S, &TopBar, &Restart);
				const char *pGameName = s_ActiveGame == EGame::CHESS ? "Chess" : s_ActiveGame == EGame::SNAKE ? "Snake" : s_ActiveGame == EGame::MINESWEEPER ? "Minesweeper" : "Tetris";
				Ui()->DoLabel(&Title, pGameName, 22.0f * S, TEXTALIGN_ML);
				if(DoButton_Menu(&s_GameBackButton, "Back", 0, &Back))
					s_ActiveGame = EGame::NONE;
				if(DoButton_Menu(&s_GameRestartButton, "Restart", 0, &Restart))
				{
					if(s_ActiveGame == EGame::CHESS)
						ResetChess();
					else if(s_ActiveGame == EGame::SNAKE)
						ResetSnake();
					else if(s_ActiveGame == EGame::MINESWEEPER)
						ResetMines();
					else if(s_ActiveGame == EGame::TETRIS)
						ResetTetris();
				}
				GameArea.HSplitTop(10.0f * S, nullptr, &GameArea);

				if(s_ActiveGame == EGame::CHESS)
				{
					PumpChessRealtimeMessages();
					PumpChessRecentRequest();
					CUIRect Tabs, StatusLine;
					GameArea.HSplitTop(28.0f * S, &Tabs, &GameArea);
					const char *apTabs[] = {"Local", "Online Duel", "Leaderboards", "Recent"};
					for(int i = 0; i < 4; ++i)
					{
						CUIRect Tab;
						Tabs.VSplitLeft(Tabs.w / (4 - i), &Tab, &Tabs);
						Tab.Margin(2.0f * S, &Tab);
						if(DoButton_Menu(&s_aChessTabButtons[i], apTabs[i], s_ChessOnline.m_Tab == i, &Tab))
						{
							s_ChessOnline.m_Tab = i;
							if(i == 1)
								RequestChessOnline();
							if(i == 2 && s_ChessOnline.m_OverallCount == 0 && s_ChessOnline.m_MonthlyCount == 0)
								RequestChessLeaderboards();
							if(i == 3 && s_ChessOnline.m_RecentCount == 0)
								RequestChessRecent();
						}
					}
					GameArea.HSplitTop(4.0f * S, nullptr, &GameArea);
					GameArea.HSplitTop(22.0f * S, &StatusLine, &GameArea);
					if(s_ChessOnline.m_Tab == 0)
						Ui()->DoLabel(&StatusLine, "Local chess uses this client only.", 12.0f * S, TEXTALIGN_ML);
					else
					{
						char aRealtime[96];
						char aStatusLine[256];
						GameClient()->m_AetherBadges.ChessRealtimeStatus(aRealtime, sizeof(aRealtime));
						str_format(aStatusLine, sizeof(aStatusLine), "%s  |  %s", GameClient()->m_AetherBadges.ChessStatus(), aRealtime);
						Ui()->DoLabel(&StatusLine, aStatusLine, 11.0f * S, TEXTALIGN_ML);
					}
					GameArea.HSplitTop(8.0f * S, nullptr, &GameArea);
					if(s_ChessOnline.m_Tab == 0)
					{
				char aStatus[128];
				if(s_Chess.m_Winner >= 0)
					str_format(aStatus, sizeof(aStatus), "%s wins", s_Chess.m_Winner == 0 ? "White" : "Black");
				else
					str_format(aStatus, sizeof(aStatus), "%s to move", s_Chess.m_Turn == 0 ? "White" : "Black");
				CUIRect Status;
				GameArea.HSplitTop(24.0f * S, &Status, &GameArea);
				Ui()->DoLabel(&Status, aStatus, 14.0f * S, TEXTALIGN_ML);
				const float BoardSize = std::min(GameArea.w, GameArea.h);
				CUIRect Board;
				Board.w = BoardSize;
				Board.h = BoardSize;
				Board.x = GameArea.x + (GameArea.w - Board.w) * 0.5f;
				Board.y = GameArea.y + (GameArea.h - Board.h) * 0.5f;
				Board.Draw(AetherPanelColor(0.28f), IGraphics::CORNER_ALL, 6.0f * S);
				const float CellSize = BoardSize / 8.0f;
				int Hover = -1;
				if(Ui()->MouseInside(&Board))
				{
					const vec2 Mouse = Ui()->MousePos();
					const int X = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 7);
					const int Y = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 7);
					Hover = Y * 8 + X;
				}
				if(s_Chess.m_Winner < 0 && Hover >= 0 && Ui()->MouseButtonClicked(0))
				{
					if(s_Chess.m_Selected >= 0 && LegalMove(s_Chess.m_Selected, Hover))
					{
						char &From = s_Chess.m_aBoard[s_Chess.m_Selected];
						char &To = s_Chess.m_aBoard[Hover];
						s_Chess.m_LastMoveFrom = s_Chess.m_Selected;
						s_Chess.m_LastMoveTo = Hover;
						if(UpperPiece(To) == 'K')
							s_Chess.m_Winner = s_Chess.m_Turn;
						To = From;
						From = '\0';
						const int TargetRank = Hover / 8;
						if(To == 'P' && TargetRank == 0)
							To = 'Q';
						else if(To == 'p' && TargetRank == 7)
							To = 'q';
						s_Chess.m_Selected = -1;
						if(s_Chess.m_Winner < 0)
							s_Chess.m_Turn = 1 - s_Chess.m_Turn;
					}
					else if(OwnPiece(s_Chess.m_aBoard[Hover]))
						s_Chess.m_Selected = Hover;
					else
						s_Chess.m_Selected = -1;
				}

				for(int y = 0; y < 8; ++y)
				{
					for(int x = 0; x < 8; ++x)
					{
						const int Idx = y * 8 + x;
						CUIRect Cell{Board.x + x * CellSize, Board.y + y * CellSize, CellSize, CellSize};
						const bool Light = ((x + y) & 1) == 0;
						ColorRGBA CellColor = Light ? ColorRGBA(0.78f, 0.72f, 0.62f, 0.92f) : ColorRGBA(0.38f, 0.42f, 0.48f, 0.92f);
						if(Idx == s_Chess.m_LastMoveFrom || Idx == s_Chess.m_LastMoveTo)
							CellColor = AetherBlendColor(CellColor, ColorRGBA(1.0f, 0.78f, 0.18f, 0.95f), Idx == s_Chess.m_LastMoveTo ? 0.45f : 0.28f);
						if(Idx == Hover)
							CellColor = AetherBlendColor(CellColor, AetherThemeColor(0.92f), 0.22f);
						if(Idx == s_Chess.m_Selected)
							CellColor = ColorRGBA(0.30f, 0.55f, 1.0f, 0.82f);
						else if(g_Config.m_AeGameChessShowLegalMoves && s_Chess.m_Selected >= 0 && LegalMove(s_Chess.m_Selected, Idx))
							CellColor = AetherBlendColor(CellColor, ColorRGBA(0.20f, 0.95f, 0.50f, 0.92f), 0.30f);
						Cell.Draw(CellColor, 0, 0.0f);

						const char Piece = s_Chess.m_aBoard[Idx];
						if(Piece != '\0')
						{
							TextRender()->TextColor(IsWhitePiece(Piece) ? ColorRGBA(0.96f, 0.97f, 1.0f, 1.0f) : ColorRGBA(0.08f, 0.09f, 0.12f, 1.0f));
							TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
							Ui()->DoLabel(&Cell, ChessIcon(Piece), Cell.h * 0.54f, TEXTALIGN_MC);
							TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
					}
				}
					}
					else if(s_ChessOnline.m_Tab == 1)
					{
						CUIRect Left, Right;
						GameArea.VSplitLeft(std::min(300.0f * S, GameArea.w * 0.36f), &Left, &Right);
						Right.VSplitLeft(12.0f * S, nullptr, &Right);
						Left.Draw(AetherPanelColor(0.26f), IGraphics::CORNER_ALL, 6.0f * S);
						Left.Margin(10.0f * S, &Left);
						s_ChessRoomCodeInput.SetEmptyText("Room code");

						char aLocalName[MAX_NAME_LENGTH];
						const bool HasLocalName = LocalChessName(aLocalName, sizeof(aLocalName));

						if(GameClient()->m_AetherBadges.HasChessInvite())
						{
							CUIRect Row, Accept, Decline;
							Left.HSplitTop(24.0f * S, &Row, &Left);
							char aInvite[128];
							str_format(aInvite, sizeof(aInvite), "Invite from %s", GameClient()->m_AetherBadges.ChessInviteFrom());
							Ui()->DoLabel(&Row, aInvite, 14.0f * S, TEXTALIGN_ML);
							Left.HSplitTop(6.0f * S, nullptr, &Left);
							Left.HSplitTop(26.0f * S, &Row, &Left);
							Row.VSplitMid(&Accept, &Decline, 6.0f * S);
							if(DoButton_Menu(&s_ChessAcceptInviteButton, "Accept", 0, &Accept))
								SendChessInviteReply("chess:accept");
							if(DoButton_Menu(&s_ChessDeclineInviteButton, "Decline", 0, &Decline))
								SendChessInviteReply("chess:decline");
							Left.HSplitTop(12.0f * S, nullptr, &Left);
						}

						const auto &RoomState = GameClient()->m_AetherBadges.ChessRoom();
						CUIRect RoomCard, Row, Create, Join, Copy, CodeInput, Ready, Leave;
						Left.HSplitTop(RoomState.m_aCode[0] ? 206.0f * S : 128.0f * S, &RoomCard, &Left);
						RoomCard.Draw(AetherPanelColor(0.25f), IGraphics::CORNER_ALL, 7.0f * S);
						RoomCard.Margin(10.0f * S, &RoomCard);
						RoomCard.HSplitTop(22.0f * S, &Row, &RoomCard);
						Ui()->DoLabel(&Row, "Room duel", 15.0f * S, TEXTALIGN_ML);
						RoomCard.HSplitTop(7.0f * S, nullptr, &RoomCard);
						if(RoomState.m_aCode[0])
						{
							RoomCard.HSplitTop(38.0f * S, &Row, &RoomCard);
							Row.VSplitRight(70.0f * S, &Row, &Copy);
							TextRender()->TextColor(ColorRGBA(0.72f, 0.90f, 1.0f, 1.0f));
							Ui()->DoLabel(&Row, RoomState.m_aCode, 28.0f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
							if(DoButton_Menu(&s_ChessCopyRoomButton, "Copy", 0, &Copy))
								Input()->SetClipboardText(RoomState.m_aCode);

							RoomCard.HSplitTop(5.0f * S, nullptr, &RoomCard);
							RoomCard.HSplitTop(22.0f * S, &Row, &RoomCard);
							char aRoomLine[160];
							str_format(aRoomLine, sizeof(aRoomLine), "Status: %s%s", RoomState.m_aStatus[0] ? RoomState.m_aStatus : "waiting", RoomState.m_Rated ? "  Rated" : "");
							TextRender()->TextColor(ColorRGBA(0.78f, 0.82f, 0.90f, 1.0f));
							Ui()->DoLabel(&Row, aRoomLine, 11.0f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

							bool LocalReady = false;
							for(int i = 0; i < 2; ++i)
							{
								const bool Filled = i < RoomState.m_PlayerCount;
								const auto &Player = RoomState.m_aPlayers[i];
								if(Filled && HasLocalName && str_comp_nocase(Player.m_aName, aLocalName) == 0)
									LocalReady = Player.m_Ready;
								RoomCard.HSplitTop(24.0f * S, &Row, &RoomCard);
								char aPlayerLine[128];
								if(Filled)
									str_format(aPlayerLine, sizeof(aPlayerLine), "%s%s  %s", Player.m_Owner ? "*" : " ", Player.m_aName[0] ? Player.m_aName : "-", Player.m_Ready ? "Ready" : "Waiting");
								else
									str_copy(aPlayerLine, "- Empty slot", sizeof(aPlayerLine));
								Row.Draw(Filled ? AetherPanelColor(Player.m_Ready ? 0.34f : 0.21f) : AetherPanelColor(0.16f), IGraphics::CORNER_ALL, 5.0f * S);
								Row.Margin(7.0f * S, &Row);
								TextRender()->TextColor(Filled && Player.m_Ready ? ColorRGBA(0.58f, 0.95f, 0.70f, 1.0f) : ColorRGBA(0.78f, 0.82f, 0.90f, 1.0f));
								Ui()->DoLabel(&Row, aPlayerLine, 11.0f * S, TEXTALIGN_ML);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
								RoomCard.HSplitTop(4.0f * S, nullptr, &RoomCard);
							}
							RoomCard.HSplitTop(27.0f * S, &Row, &RoomCard);
							if(str_comp(RoomState.m_aStatus, "waiting") == 0)
							{
								Row.VSplitMid(&Ready, &Leave, 7.0f * S);
								if(DoButton_Menu(&s_ChessReadyRoomButton, LocalReady ? "Unready" : "Ready", 0, &Ready))
									GameClient()->m_AetherBadges.SendChessRoomReady(!LocalReady);
								if(DoButton_Menu(&s_ChessLeaveRoomButton, "Leave", 0, &Leave))
									GameClient()->m_AetherBadges.SendChessRoomLeave();
							}
							else
							{
								TextRender()->TextColor(0.72f, 0.78f, 0.86f, 1.0f);
								Ui()->DoLabel(&Row, "Room controls close when the match starts.", 10.5f * S, TEXTALIGN_ML);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
							}
						}
						else
						{
							RoomCard.HSplitTop(28.0f * S, &Row, &RoomCard);
							if(DoButton_Menu(&s_ChessCreateRoomButton, "Create room", 0, &Row))
								GameClient()->m_AetherBadges.SendChessRoomCreate(true);
							RoomCard.HSplitTop(8.0f * S, nullptr, &RoomCard);
							RoomCard.HSplitTop(28.0f * S, &Row, &RoomCard);
							Row.VSplitRight(76.0f * S, &CodeInput, &Join);
							CodeInput.VMargin(2.0f * S, &CodeInput);
							Ui()->DoEditBox(&s_ChessRoomCodeInput, &CodeInput, 13.0f * S, IGraphics::CORNER_ALL, {}, 5.0f * S);
							if(DoButton_Menu(&s_ChessJoinRoomButton, "Join", 0, &Join))
								GameClient()->m_AetherBadges.SendChessRoomJoin(s_ChessRoomCodeInput.GetString());
							RoomCard.HSplitTop(8.0f * S, nullptr, &RoomCard);
							RoomCard.HSplitTop(18.0f * S, &Row, &RoomCard);
							TextRender()->TextColor(0.70f, 0.76f, 0.84f, 1.0f);
							Ui()->DoLabel(&Row, "Code works without joining the same DDNet server.", 10.0f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
						Left.HSplitTop(8.0f * S, nullptr, &Left);
						const char *pChessError = GameClient()->m_AetherBadges.ChessLastError();
						if(pChessError && pChessError[0])
						{
							Left.HSplitTop(20.0f * S, &Row, &Left);
							TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
							Ui()->DoLabel(&Row, pChessError, 10.0f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
						Left.HSplitTop(8.0f * S, nullptr, &Left);

						if(s_ChessOnline.m_InMatch)
						{
							CUIRect MatchCard, Resign;
							Left.HSplitTop(s_ChessOnline.m_Finished ? 160.0f * S : 126.0f * S, &MatchCard, &Left);
							MatchCard.Draw(AetherPanelColor(s_ChessOnline.m_Finished ? 0.32f : 0.24f), IGraphics::CORNER_ALL, 7.0f * S);
							MatchCard.Margin(10.0f * S, &MatchCard);
							MatchCard.HSplitTop(22.0f * S, &Row, &MatchCard);
							Ui()->DoLabel(&Row, s_ChessOnline.m_Finished ? "Match result" : "Active match", 15.0f * S, TEXTALIGN_ML);
							MatchCard.HSplitTop(4.0f * S, nullptr, &MatchCard);
							MatchCard.HSplitTop(20.0f * S, &Row, &MatchCard);
							char aWhite[128];
							str_format(aWhite, sizeof(aWhite), "White: %s  %d%+d", s_ChessOnline.m_aWhite, s_ChessOnline.m_Finished ? s_ChessOnline.m_WhiteRatingAfter : s_ChessOnline.m_WhiteRating, s_ChessOnline.m_Finished ? s_ChessOnline.m_WhiteRatingDelta : 0);
							Ui()->DoLabel(&Row, aWhite, 12.0f * S, TEXTALIGN_ML);
							MatchCard.HSplitTop(20.0f * S, &Row, &MatchCard);
							char aBlack[128];
							str_format(aBlack, sizeof(aBlack), "Black: %s  %d%+d", s_ChessOnline.m_aBlack, s_ChessOnline.m_Finished ? s_ChessOnline.m_BlackRatingAfter : s_ChessOnline.m_BlackRating, s_ChessOnline.m_Finished ? s_ChessOnline.m_BlackRatingDelta : 0);
							Ui()->DoLabel(&Row, aBlack, 12.0f * S, TEXTALIGN_ML);
							MatchCard.HSplitTop(22.0f * S, &Row, &MatchCard);
							if(s_ChessOnline.m_Finished)
							{
								const bool LocalWon = s_ChessOnline.m_aWinner[0] && HasLocalName && str_comp_nocase(s_ChessOnline.m_aWinner, aLocalName) == 0;
								char aResult[160];
								if(!s_ChessOnline.m_aWinner[0])
									str_copy(aResult, "Draw", sizeof(aResult));
								else
									str_format(aResult, sizeof(aResult), "%s%s", LocalWon ? "You won - " : "You lost - ", s_ChessOnline.m_aWinner);
								TextRender()->TextColor(LocalWon ? ColorRGBA(0.58f, 0.95f, 0.70f, 1.0f) : ColorRGBA(1.0f, 0.55f, 0.55f, 1.0f));
								Ui()->DoLabel(&Row, aResult, 14.0f * S, TEXTALIGN_ML);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
								MatchCard.HSplitTop(20.0f * S, &Row, &MatchCard);
								char aReason[96];
								str_format(aReason, sizeof(aReason), "Reason: %s", s_ChessOnline.m_aResultReason[0] ? s_ChessOnline.m_aResultReason : "finished");
								Ui()->DoLabel(&Row, aReason, 11.0f * S, TEXTALIGN_ML);
							}
							else
							{
								char aTurn[128];
								str_format(aTurn, sizeof(aTurn), "%s%s", s_ChessOnline.m_Check ? "Check - " : "", s_ChessOnline.m_aStatus);
								TextRender()->TextColor(s_ChessOnline.m_Check ? ColorRGBA(1.0f, 0.67f, 0.22f, 1.0f) : ColorRGBA(0.78f, 0.82f, 0.90f, 1.0f));
								Ui()->DoLabel(&Row, aTurn, 12.0f * S, TEXTALIGN_ML);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
								MatchCard.HSplitTop(8.0f * S, nullptr, &MatchCard);
								MatchCard.HSplitTop(26.0f * S, &Resign, &MatchCard);
								if(DoButton_Menu(&s_ChessResignButton, s_ChessOnline.m_ResignPending ? "Resigning..." : "Resign", 0, &Resign) && !s_ChessOnline.m_ResignPending)
									SendChessResign();
							}
							Left.HSplitTop(14.0f * S, nullptr, &Left);
						}

						CUIRect ListTitle;
						Left.HSplitTop(22.0f * S, &ListTitle, &Left);
						Ui()->DoLabel(&ListTitle, "Global online players", 13.0f * S, TEXTALIGN_ML);
						Left.HSplitTop(4.0f * S, nullptr, &Left);
						if(s_ChessOnline.m_LastOnlineRequest == 0 || time_get() - s_ChessOnline.m_LastOnlineRequest > time_freq() * 12)
							RequestChessOnline();
						int VisibleOnline = 0;
						const int OnlineCount = GameClient()->m_AetherBadges.ChessOnlineCount();
						for(int i = 0; i < OnlineCount; ++i)
						{
							const auto *pPlayer = GameClient()->m_AetherBadges.ChessOnlinePlayer(i);
							if(!pPlayer)
								continue;
							const auto &Player = *pPlayer;
							if(HasLocalName && str_comp_nocase(Player.m_aName, aLocalName) == 0)
								continue;
							CUIRect Row, Name, Button, Meta;
							Left.HSplitTop(42.0f * S, &Row, &Left);
							Row.VSplitRight(88.0f * S, &Name, &Button);
							Name.HSplitTop(20.0f * S, &Name, &Meta);
							char aNameLine[128];
							str_format(aNameLine, sizeof(aNameLine), "%s  %d", Player.m_aName, Player.m_Rating);
							Ui()->DoLabel(&Name, aNameLine, 12.0f * S, TEXTALIGN_ML);
							char aMeta[192];
							if(Player.m_InServer)
								str_format(aMeta, sizeof(aMeta), "%s%s  %s / %s", Player.m_aClient, Player.m_Spectator ? " spec" : "", Player.m_aServer[0] ? Player.m_aServer : "Unknown server", Player.m_aMap[0] ? Player.m_aMap : "Unknown map");
							else
								str_format(aMeta, sizeof(aMeta), "%s  Not in server", Player.m_aClient);
							TextRender()->TextColor(0.70f, 0.76f, 0.84f, 1.0f);
							Ui()->DoLabel(&Meta, aMeta, 10.0f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
							if(DoButton_Menu(&s_aChessChallengeButtons[i], "Challenge", 0, &Button))
								SendChessInvite(Player.m_aName);
							Left.HSplitTop(5.0f * S, nullptr, &Left);
							++VisibleOnline;
							if(Left.h < 48.0f * S)
								break;
						}
						if(VisibleOnline == 0)
						{
							CUIRect Empty;
							Left.HSplitTop(36.0f * S, &Empty, &Left);
							Ui()->DoLabel(&Empty, "No other Aether players online.", 12.0f * S, TEXTALIGN_ML);
						}

						if(!s_ChessOnline.m_InMatch)
						{
							Right.Draw(AetherPanelColor(0.22f), IGraphics::CORNER_ALL, 6.0f * S);
							Right.Margin(14.0f * S, &Right);
							Ui()->DoLabel(&Right, "Challenge any online Aether player.", 14.0f * S, TEXTALIGN_MC);
						}
						else
						{
							const float BoardSize = std::min(Right.w, Right.h);
							CUIRect Board;
							Board.w = BoardSize;
							Board.h = BoardSize;
							Board.x = Right.x + (Right.w - Board.w) * 0.5f;
							Board.y = Right.y + (Right.h - Board.h) * 0.5f;
							Board.Draw(AetherPanelColor(0.28f), IGraphics::CORNER_ALL, 6.0f * S);
							const float CellSize = BoardSize / 8.0f;
							int Hover = -1;
							if(Ui()->MouseInside(&Board))
							{
								const vec2 Mouse = Ui()->MousePos();
								const int X = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 7);
								const int Y = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 7);
								Hover = Y * 8 + X;
							}
							if(OnlineMyTurn() && !s_ChessOnline.m_MovePending && !s_ChessOnline.m_ResignPending && !s_ChessOnline.m_Finished && Hover >= 0 && Ui()->MouseButtonClicked(0))
							{
								if(s_ChessOnline.m_Selected >= 0 && OnlineLegalMove(s_ChessOnline.m_Selected, Hover))
								{
									char aFrom[3], aTo[3], aUci[8];
									SquareName(s_ChessOnline.m_Selected, aFrom, sizeof(aFrom));
									SquareName(Hover, aTo, sizeof(aTo));
									str_format(aUci, sizeof(aUci), "%s%s", aFrom, aTo);
									SendChessMove(aUci);
									s_ChessOnline.m_Selected = -1;
								}
								else if(OnlineOwnPiece(s_ChessOnline.m_aBoard[Hover]))
									s_ChessOnline.m_Selected = Hover;
								else
									s_ChessOnline.m_Selected = -1;
							}
							const int CheckedKing = OnlineCheckedKingSquare();
							for(int y = 0; y < 8; ++y)
							{
								for(int x = 0; x < 8; ++x)
								{
									const int Idx = y * 8 + x;
									CUIRect Cell{Board.x + x * CellSize, Board.y + y * CellSize, CellSize, CellSize};
									const bool Light = ((x + y) & 1) == 0;
									ColorRGBA CellColor = Light ? ColorRGBA(0.78f, 0.72f, 0.62f, 0.92f) : ColorRGBA(0.38f, 0.42f, 0.48f, 0.92f);
									if(Idx == s_ChessOnline.m_LastMoveFrom || Idx == s_ChessOnline.m_LastMoveTo)
										CellColor = AetherBlendColor(CellColor, ColorRGBA(1.0f, 0.78f, 0.18f, 0.95f), Idx == s_ChessOnline.m_LastMoveTo ? 0.45f : 0.28f);
									if(Idx == Hover)
										CellColor = AetherBlendColor(CellColor, AetherThemeColor(0.92f), 0.22f);
									if(Idx == CheckedKing)
										CellColor = ColorRGBA(1.0f, 0.30f, 0.22f, 0.88f);
									else if(Idx == s_ChessOnline.m_Selected)
										CellColor = ColorRGBA(0.30f, 0.55f, 1.0f, 0.82f);
									else if(g_Config.m_AeGameChessShowLegalMoves && s_ChessOnline.m_Selected >= 0 && OnlineLegalMove(s_ChessOnline.m_Selected, Idx))
										CellColor = AetherBlendColor(CellColor, ColorRGBA(0.20f, 0.95f, 0.50f, 0.92f), 0.30f);
									Cell.Draw(CellColor, 0, 0.0f);
									const char Piece = s_ChessOnline.m_aBoard[Idx];
									if(Piece != '\0')
									{
										TextRender()->TextColor(IsWhitePiece(Piece) ? ColorRGBA(0.96f, 0.97f, 1.0f, 1.0f) : ColorRGBA(0.08f, 0.09f, 0.12f, 1.0f));
										TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
										Ui()->DoLabel(&Cell, ChessIcon(Piece), Cell.h * 0.54f, TEXTALIGN_MC);
										TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
										TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
									}
								}
							}
							if(s_ChessOnline.m_Finished)
							{
								CUIRect Result = Board;
								Result.Margin(BoardSize * 0.22f, &Result);
								Result.Draw(ColorRGBA(0.06f, 0.07f, 0.10f, 0.88f), IGraphics::CORNER_ALL, 8.0f * S);
								Result.Margin(12.0f * S, &Result);
								CUIRect Line;
								Result.HSplitTop(30.0f * S, &Line, &Result);
								const bool LocalWon = s_ChessOnline.m_aWinner[0] && HasLocalName && str_comp_nocase(s_ChessOnline.m_aWinner, aLocalName) == 0;
								const char *pTitle = !s_ChessOnline.m_aWinner[0] ? "Draw" : (LocalWon ? "You won" : "You lost");
								TextRender()->TextColor(LocalWon ? ColorRGBA(0.58f, 0.95f, 0.70f, 1.0f) : ColorRGBA(1.0f, 0.62f, 0.58f, 1.0f));
								Ui()->DoLabel(&Line, pTitle, 24.0f * S, TEXTALIGN_MC);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
								Result.HSplitTop(22.0f * S, &Line, &Result);
								char aWinner[128];
								str_format(aWinner, sizeof(aWinner), "%s", s_ChessOnline.m_aWinner[0] ? s_ChessOnline.m_aWinner : "No winner");
								Ui()->DoLabel(&Line, aWinner, 13.0f * S, TEXTALIGN_MC);
							}
						}
					}
					else if(s_ChessOnline.m_Tab == 2)
					{
						CUIRect Refresh;
						GameArea.HSplitTop(28.0f * S, &Refresh, &GameArea);
						Refresh.VSplitRight(128.0f * S, nullptr, &Refresh);
						if(DoButton_Menu(&s_ChessRefreshLeaderboardButton, "Refresh", 0, &Refresh))
							RequestChessLeaderboards();
						if(s_ChessOnline.m_OverallCount == 0 && s_ChessOnline.m_MonthlyCount == 0 && (s_ChessOnline.m_LastLeaderboardRequest == 0 || time_get() - s_ChessOnline.m_LastLeaderboardRequest > time_freq() * 8))
							RequestChessLeaderboards();
						GameArea.HSplitTop(8.0f * S, nullptr, &GameArea);
						auto DrawLeaderboard = [&](CUIRect View, const char *pTitle, const std::array<SChessLeaderboardEntry, 5> &aEntries, int Count) {
							View.Draw(AetherPanelColor(0.26f), IGraphics::CORNER_ALL, 6.0f * S);
							View.Margin(10.0f * S, &View);
							CUIRect Row;
							View.HSplitTop(24.0f * S, &Row, &View);
							Ui()->DoLabel(&Row, pTitle, 15.0f * S, TEXTALIGN_ML);
							View.HSplitTop(6.0f * S, nullptr, &View);
							for(int i = 0; i < Count; ++i)
							{
								View.HSplitTop(24.0f * S, &Row, &View);
								char aLine[160];
								str_format(aLine, sizeof(aLine), "%d. %s  %d  W%d L%d D%d", i + 1, aEntries[i].m_aName, aEntries[i].m_Rating, aEntries[i].m_Wins, aEntries[i].m_Losses, aEntries[i].m_Draws);
								Ui()->DoLabel(&Row, aLine, 12.0f * S, TEXTALIGN_ML);
							}
							if(Count == 0)
							{
								View.HSplitTop(24.0f * S, &Row, &View);
								Ui()->DoLabel(&Row, "No rated games yet.", 12.0f * S, TEXTALIGN_ML);
							}
						};
						CUIRect Overall, Monthly;
						GameArea.VSplitMid(&Overall, &Monthly, 8.0f * S);
						DrawLeaderboard(Overall, "Top 5 Overall", s_ChessOnline.m_aOverall, s_ChessOnline.m_OverallCount);
						DrawLeaderboard(Monthly, "Monthly Top 5", s_ChessOnline.m_aMonthly, s_ChessOnline.m_MonthlyCount);
					}
					else
					{
						CUIRect Refresh;
						GameArea.HSplitTop(28.0f * S, &Refresh, &GameArea);
						Refresh.VSplitRight(128.0f * S, nullptr, &Refresh);
						if(DoButton_Menu(&s_ChessRefreshRecentButton, "Refresh", 0, &Refresh))
							RequestChessRecent();
						if(s_ChessOnline.m_RecentCount == 0 && (s_ChessOnline.m_LastRecentRequest == 0 || time_get() - s_ChessOnline.m_LastRecentRequest > time_freq() * 8))
							RequestChessRecent();
						GameArea.HSplitTop(8.0f * S, nullptr, &GameArea);
						GameArea.Draw(AetherPanelColor(0.24f), IGraphics::CORNER_ALL, 6.0f * S);
						GameArea.Margin(10.0f * S, &GameArea);
						CUIRect Row;
						GameArea.HSplitTop(24.0f * S, &Row, &GameArea);
						Ui()->DoLabel(&Row, "Recent rated matches", 15.0f * S, TEXTALIGN_ML);
						GameArea.HSplitTop(6.0f * S, nullptr, &GameArea);
						for(int i = 0; i < s_ChessOnline.m_RecentCount; ++i)
						{
							GameArea.HSplitTop(52.0f * S, &Row, &GameArea);
							Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.045f), IGraphics::CORNER_ALL, 5.0f * S);
							Row.Margin(8.0f * S, &Row);
							const SChessRecentMatch &Match = s_ChessOnline.m_aRecent[i];
							CUIRect Top, Bottom;
							Row.HSplitTop(22.0f * S, &Top, &Bottom);
							char aLine[256];
							str_format(aLine, sizeof(aLine), "White %s (%d->%d)  vs  Black %s (%d->%d)", Match.m_aWhite, Match.m_WhiteRatingBefore, Match.m_WhiteRatingAfter, Match.m_aBlack, Match.m_BlackRatingBefore, Match.m_BlackRatingAfter);
							Ui()->DoLabel(&Top, aLine, 11.5f * S, TEXTALIGN_ML);
							char aResult[192];
							if(Match.m_aWinner[0])
								str_format(aResult, sizeof(aResult), "Winner: %s  -  %s", Match.m_aWinner, Match.m_aReason[0] ? Match.m_aReason : "finished");
							else
								str_format(aResult, sizeof(aResult), "Draw  -  %s", Match.m_aReason[0] ? Match.m_aReason : "finished");
							TextRender()->TextColor(Match.m_aWinner[0] ? ColorRGBA(0.72f, 0.92f, 1.0f, 1.0f) : ColorRGBA(0.82f, 0.84f, 0.88f, 1.0f));
							Ui()->DoLabel(&Bottom, aResult, 10.5f * S, TEXTALIGN_ML);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
							GameArea.HSplitTop(6.0f * S, nullptr, &GameArea);
							if(GameArea.h < 58.0f * S)
								break;
						}
						if(s_ChessOnline.m_RecentCount == 0)
						{
							GameArea.HSplitTop(42.0f * S, &Row, &GameArea);
							TextRender()->TextColor(0.66f, 0.72f, 0.82f, 1.0f);
							Ui()->DoLabel(&Row, "No finished online matches yet.", 12.0f * S, TEXTALIGN_MC);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
					}
				}
				else if(s_ActiveGame == EGame::SNAKE)
				{
					UpdateSnake();
					CUIRect Status;
					GameArea.HSplitTop(24.0f * S, &Status, &GameArea);
					char aStatus[96];
					str_format(aStatus, sizeof(aStatus), "Score: %d%s", s_Snake.m_Score, s_Snake.m_GameOver ? "  -  Game over, press Space" : "");
					Ui()->DoLabel(&Status, aStatus, 14.0f * S, TEXTALIGN_ML);
					const float BoardSize = std::min(GameArea.w, GameArea.h);
					CUIRect Board{GameArea.x + (GameArea.w - BoardSize) * 0.5f, GameArea.y + (GameArea.h - BoardSize) * 0.5f, BoardSize, BoardSize};
					Board.Draw(AetherPanelColor(0.26f), IGraphics::CORNER_ALL, 6.0f * S);
					const float CellSize = BoardSize / s_Snake.m_Size;
					for(int y = 0; y < s_Snake.m_Size; ++y)
					{
						for(int x = 0; x < s_Snake.m_Size; ++x)
						{
							CUIRect Cell{Board.x + x * CellSize, Board.y + y * CellSize, CellSize - 1.0f, CellSize - 1.0f};
							const int Idx = y * s_Snake.m_Size + x;
							ColorRGBA Color = ColorRGBA(0.08f, 0.10f, 0.13f, 0.72f);
							if(Idx == s_Snake.m_Food)
								Color = ColorRGBA(1.0f, 0.35f, 0.35f, 0.95f);
							for(int i = 0; i < s_Snake.m_Length; ++i)
							{
								if(s_Snake.m_aBody[i] == Idx)
								{
									Color = i == 0 ? ColorRGBA(0.18f, 0.95f, 0.72f, 0.96f) : ColorRGBA(0.10f, 0.62f, 0.92f, 0.88f);
									break;
								}
							}
							Cell.Draw(Color, IGraphics::CORNER_ALL, 2.0f * S);
						}
					}
				}
				else if(s_ActiveGame == EGame::MINESWEEPER)
				{
					CUIRect Status;
					GameArea.HSplitTop(24.0f * S, &Status, &GameArea);
					char aStatus[96];
					str_format(aStatus, sizeof(aStatus), "%dx%d, %d bombs%s", s_Mines.m_Width, s_Mines.m_Height, s_Mines.m_Bombs, s_Mines.m_Won ? "  -  Cleared!" : s_Mines.m_GameOver ? "  -  Boom!" : "");
					Ui()->DoLabel(&Status, aStatus, 14.0f * S, TEXTALIGN_ML);
					const float BoardSize = std::min(GameArea.w, GameArea.h);
					const float CellSize = std::min(BoardSize / s_Mines.m_Width, BoardSize / s_Mines.m_Height);
					CUIRect Board{GameArea.x + (GameArea.w - CellSize * s_Mines.m_Width) * 0.5f, GameArea.y + (GameArea.h - CellSize * s_Mines.m_Height) * 0.5f, CellSize * s_Mines.m_Width, CellSize * s_Mines.m_Height};
					int Hover = -1;
					if(Ui()->MouseInside(&Board))
					{
						const vec2 Mouse = Ui()->MousePos();
						const int X = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, s_Mines.m_Width - 1);
						const int Y = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, s_Mines.m_Height - 1);
						Hover = Y * s_Mines.m_Width + X;
					}
					if(!s_Mines.m_GameOver && Hover >= 0)
					{
						if(Ui()->MouseButtonClicked(1) && !(s_Mines.m_aCells[Hover] & MINE_OPEN))
							s_Mines.m_aCells[Hover] ^= MINE_FLAG;
						if(Ui()->MouseButtonClicked(0) && !(s_Mines.m_aCells[Hover] & MINE_FLAG))
						{
							if(!s_Mines.m_BombsPlaced)
								MinesPlaceBombs(Hover);
							if(s_Mines.m_aCells[Hover] & MINE_BOMB)
							{
								s_Mines.m_aCells[Hover] |= MINE_OPEN;
								s_Mines.m_GameOver = true;
							}
							else
								MinesReveal(Hover);
						}
					}
					for(int y = 0; y < s_Mines.m_Height; ++y)
					{
						for(int x = 0; x < s_Mines.m_Width; ++x)
						{
							const int Idx = y * s_Mines.m_Width + x;
							CUIRect Cell{Board.x + x * CellSize, Board.y + y * CellSize, CellSize - 1.0f, CellSize - 1.0f};
							const bool Open = s_Mines.m_aCells[Idx] & MINE_OPEN;
							ColorRGBA Color = Open ? ColorRGBA(0.22f, 0.25f, 0.30f, 0.86f) : ColorRGBA(0.38f, 0.43f, 0.50f, 0.88f);
							if(Idx == Hover && !s_Mines.m_GameOver)
								Color = AetherBlendColor(Color, AetherThemeColor(0.90f), 0.20f);
							Cell.Draw(Color, IGraphics::CORNER_ALL, 2.0f * S);
							if((s_Mines.m_GameOver || Open) && (s_Mines.m_aCells[Idx] & MINE_BOMB))
							{
								DrawBombIcon(Cell, 0.95f);
							}
							else if(s_Mines.m_aCells[Idx] & MINE_FLAG)
							{
								TextRender()->TextColor(1.0f, 0.86f, 0.25f, 1.0f);
								Ui()->DoLabel(&Cell, FontIcon::FLAG_CHECKERED, Cell.h * 0.40f, TEXTALIGN_MC);
								TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
							}
							else if(Open)
							{
								const int Adjacent = MinesAdjacent(Idx);
								if(Adjacent > 0)
								{
									char aNum[2] = {(char)('0' + Adjacent), '\0'};
									Ui()->DoLabel(&Cell, aNum, Cell.h * 0.42f, TEXTALIGN_MC);
								}
							}
						}
					}
				}
				else if(s_ActiveGame == EGame::TETRIS)
				{
					UpdateTetris();
					CUIRect Status;
					GameArea.HSplitTop(24.0f * S, &Status, &GameArea);
					char aStatus[128];
					str_format(aStatus, sizeof(aStatus), "Score: %d  Lines: %d  Next: %d%s", s_Tetris.m_Score, s_Tetris.m_Lines, s_Tetris.m_NextPiece + 1, s_Tetris.m_GameOver ? "  -  Game over, press Space" : "");
					Ui()->DoLabel(&Status, aStatus, 14.0f * S, TEXTALIGN_ML);
					const float CellSize = std::min(GameArea.w / 12.0f, GameArea.h / 22.0f);
					CUIRect Board{GameArea.x + (GameArea.w - CellSize * 10.0f) * 0.5f, GameArea.y + (GameArea.h - CellSize * 20.0f) * 0.5f, CellSize * 10.0f, CellSize * 20.0f};
					static const ColorRGBA s_aPieceColors[8] = {
						ColorRGBA(0.10f, 0.12f, 0.16f, 0.74f),
						ColorRGBA(0.34f, 0.88f, 1.0f, 0.95f),
						ColorRGBA(1.0f, 0.86f, 0.28f, 0.95f),
						ColorRGBA(0.72f, 0.44f, 1.0f, 0.95f),
						ColorRGBA(0.45f, 0.95f, 0.45f, 0.95f),
						ColorRGBA(1.0f, 0.35f, 0.35f, 0.95f),
						ColorRGBA(0.35f, 0.55f, 1.0f, 0.95f),
						ColorRGBA(1.0f, 0.58f, 0.24f, 0.95f),
					};
					for(int y = 0; y < 20; ++y)
					{
						for(int x = 0; x < 10; ++x)
						{
							int CellValue = s_Tetris.m_aBoard[y * 10 + x];
							for(int py = 0; py < 4; ++py)
								for(int px = 0; px < 4; ++px)
									if(TetrisCell(s_Tetris.m_Piece, s_Tetris.m_Rotation, px, py) && s_Tetris.m_X + px == x && s_Tetris.m_Y + py == y)
										CellValue = s_Tetris.m_Piece + 1;
							CUIRect Cell{Board.x + x * CellSize, Board.y + y * CellSize, CellSize - 1.0f, CellSize - 1.0f};
							Cell.Draw(s_aPieceColors[std::clamp(CellValue, 0, 7)], IGraphics::CORNER_ALL, 2.0f * S);
						}
					}
				}
			}
			s_ScrollRegion.End();
			return;
		}

		const int Columns = MainView.w >= 760.0f * S ? 2 : 1;
		const float Gap = 10.0f * S;
		const float CardWidth = (MainView.w - Gap * (Columns - 1)) / Columns;
		struct SGameCard
		{
			const char *m_pIcon;
			const char *m_pTitle;
			const char *m_pHint;
			EGame m_Game;
		};
		const std::array<SGameCard, 4> aCards = {{
			{FontIcon::CHESS_KING, "Chess", "Local two-player now; duel mode can reuse this board later.", EGame::CHESS},
			{"", "Snake", "Arrows/WASD move, Space restart.", EGame::SNAKE},
			{"", "Minesweeper", "LMB open, RMB flag, hover hints.", EGame::MINESWEEPER},
			{FontIcon::BORDER_ALL, "Tetris", "Arrows/WASD move, Up rotate, Space hard drop.", EGame::TETRIS},
		}};
		const int Rows = (int)std::ceil(aCards.size() / (float)Columns);
		const float AvailableCardHeight = (MainView.h - Gap * (Rows - 1)) / maximum(1, Rows);
		const float CardHeight = std::max(174.0f * S, std::min(208.0f * S, AvailableCardHeight));
		for(size_t i = 0; i < aCards.size(); ++i)
		{
			const int Row = i / Columns;
			const int Col = i % Columns;
			CUIRect Card{MainView.x + Col * (CardWidth + Gap), MainView.y + Row * (CardHeight + Gap), CardWidth, CardHeight};
			if(s_ScrollRegion.AddRect(Card))
				DrawGameCard((int)i, Card, aCards[i].m_pIcon, aCards[i].m_pTitle, aCards[i].m_pHint, aCards[i].m_Game);
		}
		MainView.HSplitTop(Rows * CardHeight + (Rows - 1) * Gap, nullptr, &MainView);
		s_ScrollRegion.End();
		return;
	}

	for(ESection Section : {ESection::VISUALS, ESection::GAMEPLAY, ESection::TOOLS, ESection::EDITORS})
	{
		if(!SectionHasMatches(Section))
			continue;
		CUIRect SectionHeader;
		MainView.HSplitTop(28.0f * S, &SectionHeader, &MainView);
		if(s_ScrollRegion.AddRect(SectionHeader))
		{
			TextRender()->TextColor(0.53f, 0.72f, 1.0f, 1.0f);
			const char *pSectionLabel = Section == ESection::VISUALS ? "VISUALS" : Section == ESection::GAMEPLAY ? "GAMEPLAY" : Section == ESection::TOOLS ? "TOOLS" : "EDITORS";
			Ui()->DoLabel(&SectionHeader, pSectionLabel, 15.0f * S, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		for(size_t Index = 0; Index < aFeatures.size(); ++Index)
		{
			const SFeature &Feature = aFeatures[Index];
			if(!AetherFeatureAllowed(Feature.m_Id) ||
				Feature.m_Section != Section ||
				(!SearchActive && Feature.m_Page != s_AetherActivePage) ||
				(EnabledFilterPage && m_AetherShowEnabledOnly && Feature.m_pEnabled && !*Feature.m_pEnabled) ||
				!AetherMusic::SearchMatches(m_AetherSearchInput.GetString(), Feature.m_pLabel, Feature.m_ChildLabels))
				continue;

			CUIRect Row;
			MainView.HSplitTop(42.0f * S, &Row, &MainView);
			MainView.HSplitTop(6.0f * S, nullptr, &MainView);
			if(s_ScrollRegion.AddRect(Row))
			{
				Row.Draw(AetherThemeColor(0.18f), IGraphics::CORNER_ALL, 6.0f * S);
				if(Feature.m_EditorAction != EEditorAction::NONE)
				{
					CUIRect Label, OpenButton;
					Row.Margin(8.0f * S, &Row);
					Row.VSplitRight(90.0f * S, &Label, &OpenButton);
					Ui()->DoLabel(&Label, Feature.m_pLabel, 16.0f * S, TEXTALIGN_ML);
					if(Feature.m_EditorAction == EEditorAction::OPEN_HUD_EDITOR &&
						DoButton_Menu(&s_OpenEditorButton, Localize("Open"), 0, &OpenButton))
					{
						bool EditorOpened = false;
						EditorOpened |= GameClient()->m_AetherMusicPlayer.OpenEditor();
						EditorOpened |= GameClient()->m_AetherKeystrokes.OpenEditor();
						EditorOpened |= GameClient()->m_AetherInputVisualizer.OpenEditor();
						EditorOpened |= GameClient()->m_AetherSessionStats.OpenEditor();
						EditorOpened |= GameClient()->m_AetherFinishPrediction.OpenEditor();
						EditorOpened |= GameClient()->m_AetherStabilityTrainer.OpenEditor();
						EditorOpened |= GameClient()->m_Hud.OpenTClientFrozenTextEditor();
						if(EditorOpened)
						{
							SetActive(false);
						}
						else
							PopupWarning("HUD Editor", "Connect to a server or open a demo before editing the HUD.", Localize("Ok"), std::chrono::seconds(4));
					}
					else if(Feature.m_EditorAction == EEditorAction::OPEN_ASSETS_EDITOR &&
						DoButton_Menu(&s_OpenAssetsEditorButton, Localize("Open"), 0, &OpenButton))
					{
						s_AetherAssetsEditorOpen = true;
					}
				}
				else
				{
					CUIRect CheckBox, ExpandArea, Arrow;
					if(Feature.m_pEnabled)
						Row.VSplitLeft(34.0f * S, &CheckBox, &ExpandArea);
					else
						ExpandArea = Row;
					const CUIRect ExpandButton = ExpandArea;
					ExpandArea.VSplitRight(38.0f * S, &ExpandArea, &Arrow);
					if(Feature.m_pEnabled)
					{
						CheckBox.Margin(7.0f * S, &CheckBox);
					}
					if(Feature.m_pEnabled && DoButton_CheckBox(Feature.m_pEnabled, "", *Feature.m_pEnabled, &CheckBox))
					{
						*Feature.m_pEnabled ^= 1;
						if(*Feature.m_pEnabled)
							m_AetherExpandedFeature = Feature.m_Id;
						else if(m_AetherExpandedFeature == Feature.m_Id)
							m_AetherExpandedFeature = AetherMusic::EAetherFeatureId::NONE;
					}
					if(Ui()->DoButtonLogic(&s_aExpandButtons[Index], m_AetherExpandedFeature == Feature.m_Id, &ExpandButton, BUTTONFLAG_LEFT))
						m_AetherExpandedFeature = AetherMusic::ToggleAccordion(m_AetherExpandedFeature, Feature.m_Id);
					ExpandArea.VSplitLeft(8.0f * S, nullptr, &ExpandArea);
					Ui()->DoLabel(&ExpandArea, Feature.m_pLabel, 16.0f * S, TEXTALIGN_ML);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Ui()->DoLabel(&Arrow, m_AetherExpandedFeature == Feature.m_Id ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_RIGHT, 13.0f * S, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}
			}

			const float TargetAnimation = m_AetherExpandedFeature == Feature.m_Id ? 1.0f : 0.0f;
			s_aBodyAnimations[Index] += (TargetAnimation - s_aBodyAnimations[Index]) * AnimationStep;
			if(std::abs(TargetAnimation - s_aBodyAnimations[Index]) < 0.003f)
				s_aBodyAnimations[Index] = TargetAnimation;
			const float FullBodyHeight = BodyHeight(Feature.m_Id);
			if(FullBodyHeight > 0.0f && s_aBodyAnimations[Index] > 0.01f)
			{
				CUIRect Body;
				MainView.HSplitTop(FullBodyHeight * s_aBodyAnimations[Index], &Body, &MainView);
				MainView.HSplitTop(8.0f * S * s_aBodyAnimations[Index], nullptr, &MainView);
				if(s_ScrollRegion.AddRect(Body) && s_aBodyAnimations[Index] > 0.12f)
				{
					CUIRect RenderArea = Body;
					RenderArea.h = FullBodyHeight;
					Ui()->ClipEnable(&Body);
					RenderBody(Feature.m_Id, RenderArea);
					Ui()->ClipDisable();
				}
			}
		}
	}
	s_ScrollRegion.End();
}
