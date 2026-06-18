#include "vault_cfg.h"

#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/storage.h>

#include <game/client/components/binds.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{
constexpr const char *VAULT_DIR = "vault";
constexpr const char *DEFAULT_VAULT_NAME = "aether_vault.cfg";

std::string LowerAscii(const char *pText)
{
	std::string Result;
	for(unsigned char C : std::string(pText ? pText : ""))
		Result.push_back((char)std::tolower(C));
	return Result;
}

bool IsSensitiveLine(const char *pLine)
{
	const std::string Lower = LowerAscii(pLine);
	return Lower.find("/login") != std::string::npos ||
	       Lower.find(" login ") != std::string::npos ||
	       Lower.find("\"login ") != std::string::npos ||
	       Lower.find("password") != std::string::npos ||
	       Lower.find("token") != std::string::npos ||
	       Lower.find("auth") != std::string::npos;
}

bool WriteLine(IOHANDLE File, const char *pLine)
{
	return io_write(File, pLine, str_length(pLine)) == (unsigned)str_length(pLine) && io_write_newline(File);
}

std::string SanitizeCfgName(const char *pName)
{
	std::string Name;
	for(unsigned char C : std::string(pName && pName[0] ? pName : DEFAULT_VAULT_NAME))
	{
		if(std::isalnum(C) || C == '_' || C == '-' || C == ' ' || C == '.')
			Name.push_back((char)C);
		else
			Name.push_back('_');
	}
	while(!Name.empty() && (Name.front() == ' ' || Name.front() == '.'))
		Name.erase(Name.begin());
	while(!Name.empty() && Name.back() == ' ')
		Name.pop_back();
	if(Name.empty())
		Name = DEFAULT_VAULT_NAME;
	if(Name.size() < 4 || str_comp_nocase(Name.c_str() + Name.size() - 4, ".cfg") != 0)
		Name += ".cfg";
	return Name;
}

std::string VaultPathFor(const char *pName)
{
	return std::string(VAULT_DIR) + "/" + SanitizeCfgName(pName);
}

int ListVaultCfgCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	if(IsDir || !pName || pName[0] == '.')
		return 0;
	const int Len = str_length(pName);
	if(Len > 4 && str_comp_nocase(pName + Len - 4, ".cfg") == 0)
		static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
	return 0;
}

struct SCollectContext
{
	std::vector<std::string> m_vLines;
};

void CollectVariable(const SConfigVariable *pVariable, void *pUserData)
{
	if(!pVariable || (pVariable->m_Flags & CFGFLAG_SAVE) == 0 || pVariable->IsDefault())
		return;
	char aLine[2048];
	pVariable->Serialize(aLine, sizeof(aLine));
	if(IsSensitiveLine(aLine))
		return;
	static_cast<SCollectContext *>(pUserData)->m_vLines.emplace_back(aLine);
}

void AppendEscapedBindLine(std::vector<std::string> &vLines, IInput *pInput, const char *pKeyName, const char *pCommand)
{
	if(!pCommand || pCommand[0] == '\0' || IsSensitiveLine(pCommand))
		return;
	const int Size = str_length(pCommand) * 2 + str_length(pKeyName) + 16;
	std::vector<char> vBuffer(Size);
	str_format(vBuffer.data(), vBuffer.size(), "bind %s \"", pKeyName);
	char *pDst = vBuffer.data() + str_length(vBuffer.data());
	str_escape(&pDst, pCommand, vBuffer.data() + vBuffer.size());
	str_append(vBuffer.data(), "\"", vBuffer.size());
	if(!IsSensitiveLine(vBuffer.data()))
		vLines.emplace_back(vBuffer.data());
}
} // namespace

bool CAetherVaultCfg::Save(const char *pName)
{
	m_aLastError[0] = '\0';
	m_aLastPath[0] = '\0';
	m_LastSavedLines = 0;

	Storage()->CreateFolder(VAULT_DIR, IStorage::TYPE_SAVE);
	const std::string VaultPath = VaultPathFor(pName);
	char aFullPath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = Storage()->OpenFile(VaultPath.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE, aFullPath, sizeof(aFullPath));
	if(!File)
	{
		str_copy(m_aLastError, "Could not open the vault cfg for writing.", sizeof(m_aLastError));
		return false;
	}

	SCollectContext Context;
	ConfigManager()->PossibleConfigVariables("", CFGFLAG_SAVE, CollectVariable, &Context);

	std::vector<std::string> vBindLines;
	vBindLines.emplace_back("unbindall");
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
		{
			const char *pCommand = GameClient()->m_Binds.Get(Key, Modifier);
			if(!pCommand || pCommand[0] == '\0')
				continue;
			char aBindName[128];
			GameClient()->m_Binds.GetKeyBindName(Key, Modifier, aBindName, sizeof(aBindName));
			AppendEscapedBindLine(vBindLines, Input(), aBindName, pCommand);
		}
	}

	bool Success = true;
	Success &= WriteLine(File, "# Aether Vault CFG");
	Success &= WriteLine(File, "# Sanitized export: KoG /login, password, token and auth lines are intentionally skipped.");
	Success &= WriteLine(File, "");
	for(const std::string &Line : Context.m_vLines)
		Success &= WriteLine(File, Line.c_str());
	if(!Context.m_vLines.empty())
		Success &= WriteLine(File, "");
	for(const std::string &Line : vBindLines)
		Success &= WriteLine(File, Line.c_str());

	if(io_sync(File) != 0 || io_close(File) != 0)
		Success = false;
	if(!Success)
	{
		str_copy(m_aLastError, "Could not finish writing the vault cfg.", sizeof(m_aLastError));
		return false;
	}

	str_copy(m_aLastPath, aFullPath, sizeof(m_aLastPath));
	m_LastSavedLines = (int)Context.m_vLines.size() + (int)vBindLines.size();
	RefreshList();
	return true;
}

bool CAetherVaultCfg::Load(const char *pFilename)
{
	m_aLastError[0] = '\0';
	const std::string VaultPath = VaultPathFor(pFilename);
	if(!Storage()->FileExists(VaultPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Vault cfg not found.", sizeof(m_aLastError));
		return false;
	}
	if(!Console()->ExecuteFile(VaultPath.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Could not load the selected vault cfg.", sizeof(m_aLastError));
		return false;
	}
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, VaultPath.c_str(), m_aLastPath, sizeof(m_aLastPath));
	return true;
}

bool CAetherVaultCfg::Rename(const char *pOldFilename, const char *pNewName)
{
	m_aLastError[0] = '\0';
	const std::string OldPath = VaultPathFor(pOldFilename);
	const std::string NewPath = VaultPathFor(pNewName);
	if(OldPath == NewPath)
		return true;
	if(!Storage()->FileExists(OldPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Vault cfg not found.", sizeof(m_aLastError));
		return false;
	}
	if(Storage()->FileExists(NewPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "A vault cfg with that name already exists.", sizeof(m_aLastError));
		return false;
	}
	if(!Storage()->RenameFile(OldPath.c_str(), NewPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Could not rename the vault cfg.", sizeof(m_aLastError));
		return false;
	}
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, NewPath.c_str(), m_aLastPath, sizeof(m_aLastPath));
	RefreshList();
	return true;
}

bool CAetherVaultCfg::Delete(const char *pFilename)
{
	m_aLastError[0] = '\0';
	const std::string VaultPath = VaultPathFor(pFilename);
	if(!Storage()->FileExists(VaultPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Vault cfg not found.", sizeof(m_aLastError));
		return false;
	}
	if(!Storage()->RemoveFile(VaultPath.c_str(), IStorage::TYPE_SAVE))
	{
		str_copy(m_aLastError, "Could not delete the vault cfg.", sizeof(m_aLastError));
		return false;
	}
	m_aLastPath[0] = '\0';
	RefreshList();
	return true;
}

void CAetherVaultCfg::RefreshList()
{
	Storage()->CreateFolder(VAULT_DIR, IStorage::TYPE_SAVE);
	m_vCfgs.clear();
	Storage()->ListDirectory(IStorage::TYPE_SAVE, VAULT_DIR, ListVaultCfgCallback, &m_vCfgs);
	std::sort(m_vCfgs.begin(), m_vCfgs.end());
}
