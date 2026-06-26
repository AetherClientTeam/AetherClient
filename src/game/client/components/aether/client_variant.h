#ifndef GAME_CLIENT_COMPONENTS_AETHER_CLIENT_VARIANT_H
#define GAME_CLIENT_COMPONENTS_AETHER_CLIENT_VARIANT_H

#include <base/str.h>

#ifndef AETHER_CLIENT_VARIANT
#define AETHER_CLIENT_VARIANT "aether"
#endif

#ifndef AETHER_CLIENT_EXECUTABLE
#define AETHER_CLIENT_EXECUTABLE "Aether"
#endif

namespace AetherVariant
{
inline const char *Key()
{
	return AETHER_CLIENT_VARIANT;
}

inline const char *Executable()
{
	return AETHER_CLIENT_EXECUTABLE;
}

inline bool IsAether()
{
	return str_comp_nocase(Key(), "aether") == 0;
}

inline bool IsVera()
{
	return str_comp_nocase(Key(), "vera") == 0;
}

inline bool IsVia()
{
	return str_comp_nocase(Key(), "via") == 0;
}

inline bool IsVex()
{
	return str_comp_nocase(Key(), "vex") == 0;
}

inline bool WarlistEnabled()
{
	return IsAether() || IsVex();
}

inline int Index()
{
	if(IsVera())
		return 1;
	if(IsVia())
		return 2;
	if(IsVex())
		return 3;
	return 0;
}

inline const char *DisplayName()
{
	if(IsVera())
		return "Vera";
	if(IsVia())
		return "Via";
	if(IsVex())
		return "Vex";
	return "Aether";
}

inline const char *Description()
{
	if(IsVera())
		return "Gores";
	if(IsVia())
		return "DDRace";
	if(IsVex())
		return "Block";
	return "All-in-one";
}

inline const char *LogoLockupPath()
{
	if(IsVera())
		return "core/logos/vera_lockup_1024.png";
	if(IsVia())
		return "core/logos/via_lockup_1024.png";
	if(IsVex())
		return "core/logos/vex_lockup_1024.png";
	return "core/logos/vera_lockup_1024.png";
}

inline const char *IconPath(int Index)
{
	static constexpr const char *s_apPaths[] = {
		"core/logos/aether_512.png",
		"core/logos/vera_512.png",
		"core/logos/via_512.png",
		"core/logos/vex_512.png",
	};
	return Index >= 0 && Index < 4 ? s_apPaths[Index] : s_apPaths[0];
}

inline const char *ExecutableName(int Index)
{
#if defined(CONF_PLATFORM_WINDOWS) || defined(_WIN32)
	static constexpr const char *s_apNames[] = {"Aether.exe", "Vera.exe", "Via.exe", "Vex.exe"};
#else
	static constexpr const char *s_apNames[] = {"Aether", "Vera", "Via", "Vex"};
#endif
	return Index >= 0 && Index < 4 ? s_apNames[Index] : s_apNames[0];
}
} // namespace AetherVariant

#endif
