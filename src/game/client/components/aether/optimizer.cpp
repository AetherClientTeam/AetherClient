#include "optimizer.h"

#include <base/system.h>

#include <engine/shared/config.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <tlhelp32.h>

#include <cwchar>
#endif

#if defined(CONF_FAMILY_WINDOWS)
namespace
{
bool AetherIsDiscordProcess(const wchar_t *pName)
{
	return _wcsicmp(pName, L"Discord.exe") == 0 ||
	       _wcsicmp(pName, L"DiscordPTB.exe") == 0 ||
	       _wcsicmp(pName, L"DiscordCanary.exe") == 0;
}

void AetherApplyDiscordPriority()
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hSnapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32W Entry;
	Entry.dwSize = sizeof(Entry);
	if(Process32FirstW(hSnapshot, &Entry))
	{
		do
		{
			if(!AetherIsDiscordProcess(Entry.szExeFile))
				continue;
			HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, Entry.th32ProcessID);
			if(hProcess)
			{
				SetPriorityClass(hProcess, BELOW_NORMAL_PRIORITY_CLASS);
				CloseHandle(hProcess);
			}
		} while(Process32NextW(hSnapshot, &Entry));
	}
	CloseHandle(hSnapshot);
}
}
#endif

void CAetherOptimizer::OnRender()
{
	const int WantedHighPriority = g_Config.m_AeOptimizer && g_Config.m_AeOptimizerHighPriority;
#if defined(CONF_FAMILY_WINDOWS)
	if(WantedHighPriority != m_LastHighPriority)
	{
		m_LastHighPriority = WantedHighPriority;
		SetPriorityClass(GetCurrentProcess(), WantedHighPriority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
	}

	const int WantedDiscordBelowNormal = g_Config.m_AeOptimizer && g_Config.m_AeOptimizerDiscordBelowNormal;
	const int64_t Now = time_get();
	if(WantedDiscordBelowNormal && (WantedDiscordBelowNormal != m_LastDiscordBelowNormal || Now - m_LastDiscordScanTime > time_freq() * 5))
	{
		AetherApplyDiscordPriority();
		m_LastDiscordScanTime = Now;
	}
	m_LastDiscordBelowNormal = WantedDiscordBelowNormal;
#else
	m_LastHighPriority = WantedHighPriority;
	m_LastDiscordBelowNormal = g_Config.m_AeOptimizer && g_Config.m_AeOptimizerDiscordBelowNormal;
#endif
}
