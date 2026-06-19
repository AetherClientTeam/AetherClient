/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_VERSION_H
#define GAME_VERSION_H

// ddnet
#define GAME_NAME "DDNet"
#define DDNET_VERSION_NUMBER 19082
extern const char *GIT_SHORTREV_HASH;
#ifndef GAME_RELEASE_VERSION_INTERNAL
#define GAME_RELEASE_VERSION_INTERNAL 19.8.2
#endif
#define GAME_RELEASE_VERSION STRINGIFY(GAME_RELEASE_VERSION_INTERNAL)

// Some legacy community servers reject patch-level DDNet revisions even though
// the protocol is compatible. Keep the source baseline at 19.8.2 while using
// the last accepted 19.8 handshake identity.
#define DDNET_NETWORK_VERSION_NUMBER 19080
#define DDNET_NETWORK_VERSION "19.8"

// teeworlds
#define CLIENT_VERSION7 0x0705
#define GAME_VERSION "0.6.4, " GAME_RELEASE_VERSION
#define GAME_NETVERSION "0.6 626fce9a778df4d4"
#define GAME_NETVERSION7 "0.7 802f1be60a05665f"

// TClient
#ifndef TCLIENT_VERSION
#define TCLIENT_VERSION "10.8.7"
#endif
#define TCLIENT_SOURCE_REVISION "4e4269396b97d06879c11ae3b9696c3d"

// client branding
#ifndef AETHER_VERSION
#define AETHER_VERSION "1.0.1"
#endif
#define CLIENT_NAME "Aether"
#define CLIENT_RELEASE_VERSION AETHER_VERSION
#ifndef AETHERCLIENT_VERSION
#define AETHERCLIENT_VERSION CLIENT_RELEASE_VERSION
#endif
#ifndef AETHERCLIENT_UPDATE_RELEASE_API_URL
#define AETHERCLIENT_UPDATE_RELEASE_API_URL "https://api.github.com/repos/AetherClientTeam/AetherClient/releases"
#endif

#endif
