#ifndef GAME_CLIENT_COMPONENTS_AETHER_VAULT_CFG_H
#define GAME_CLIENT_COMPONENTS_AETHER_VAULT_CFG_H

#include <base/system.h>

#include <game/client/component.h>

#include <string>
#include <vector>

class CAetherVaultCfg : public CComponent
{
	char m_aLastPath[IO_MAX_PATH_LENGTH] = "";
	char m_aLastError[128] = "";
	int m_LastSavedLines = 0;
	std::vector<std::string> m_vCfgs;

public:
	int Sizeof() const override { return sizeof(*this); }

	bool Save(const char *pName);
	bool Load(const char *pFilename);
	bool Rename(const char *pOldFilename, const char *pNewName);
	bool Delete(const char *pFilename);
	void RefreshList();
	const std::vector<std::string> &Files() const { return m_vCfgs; }
	const char *LastPath() const { return m_aLastPath; }
	const char *LastError() const { return m_aLastError; }
	int LastSavedLines() const { return m_LastSavedLines; }
};

#endif
