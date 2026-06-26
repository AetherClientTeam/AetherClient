#include <base/net.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/discord.h>

#if defined(CONF_DISCORD)
#if defined(_WIN32)
#define AETHER_DISCORD_RENAMED_ISTORAGE
#define IStorage DiscordWindowsIStorage
#endif
#include <discord_game_sdk.h>
#if defined(AETHER_DISCORD_RENAMED_ISTORAGE)
#undef IStorage
#undef AETHER_DISCORD_RENAMED_ISTORAGE
#endif

typedef enum EDiscordResult(DISCORD_API *FDiscordCreate)(DiscordVersion, struct DiscordCreateParams *, struct IDiscordCore **);

const char *AetherDiscordClientName()
{
	return "Aether Client";
}

const char *AetherDiscordLargeImage()
{
	return "vera_1024";
}

void AetherApplyDiscordAssets(DiscordActivity &Activity)
{
	str_copy(Activity.assets.large_image, AetherDiscordLargeImage(), sizeof(Activity.assets.large_image));
	str_copy(Activity.assets.large_text, AetherDiscordClientName(), sizeof(Activity.assets.large_text));
	str_copy(Activity.assets.small_image, AetherDiscordLargeImage(), sizeof(Activity.assets.small_image));
	str_copy(Activity.assets.small_text, AetherDiscordClientName(), sizeof(Activity.assets.small_text));
}

#if defined(CONF_DISCORD_DYNAMIC)
#include <dlfcn.h>
FDiscordCreate GetDiscordCreate()
{
	void *pSdk = dlopen("discord_game_sdk.so", RTLD_NOW);
	if(!pSdk)
	{
		return nullptr;
	}
	return (FDiscordCreate)dlsym(pSdk, "DiscordCreate");
}
#else
FDiscordCreate GetDiscordCreate()
{
	return DiscordCreate;
}
#endif

class CDiscord : public IDiscord
{
	DiscordActivity m_Activity;
	bool m_UpdateActivity = false;
	int64_t m_LastActivityUpdate = 0;

	IDiscordCore *m_pCore = nullptr;
	IDiscordActivityEvents m_ActivityEvents;
	IDiscordActivityManager *m_pActivityManager = nullptr;

	FDiscordCreate m_pfnDiscordCreate = nullptr;
	bool m_Enabled = false;

public:
	bool Init(FDiscordCreate pfnDiscordCreate)
	{
		m_pfnDiscordCreate = pfnDiscordCreate;
		m_Enabled = false;
		return InitDiscord();
	}
	bool InitDiscord()
	{
		if(m_pCore)
		{
			m_pCore->destroy(m_pCore);
			m_pCore = 0;
			m_pActivityManager = 0;
		}
		if(!m_Enabled)
			return false;

		mem_zero(&m_ActivityEvents, sizeof(m_ActivityEvents));

		m_ActivityEvents.on_activity_join = &CDiscord::OnActivityJoin;
		m_pActivityManager = 0;

		DiscordCreateParams Params;
		DiscordCreateParamsSetDefault(&Params);

		Params.client_id = 1517583519919112433; // Aether Client
		Params.flags = EDiscordCreateFlags::DiscordCreateFlags_NoRequireDiscord;
		Params.event_data = this;
		Params.activity_events = &m_ActivityEvents;

		int Error = m_pfnDiscordCreate(DISCORD_VERSION, &Params, &m_pCore);

		if(Error != DiscordResult_Ok)
		{
			dbg_msg("discord", "error initializing discord instance, error=%d", Error);
			return true;
		}

		m_pActivityManager = m_pCore->get_activity_manager(m_pCore);

		// which application to launch when joining activity
		m_pActivityManager->register_command(m_pActivityManager, CONNECTLINK_DOUBLE_SLASH);
		m_pActivityManager->register_steam(m_pActivityManager, 412220); // steam id

		ClearGameInfo();

		return false;
	}

	void Update(bool Enabled) override
	{
		bool NeedsUpdate = m_Enabled != Enabled;
		m_Enabled = Enabled;

		if(NeedsUpdate)
			InitDiscord();

		if(m_pCore && m_Enabled)
		{
			// update every 5 seconds, rate limit is 5 updates per 20 seconds
			if(m_UpdateActivity && time_get() > m_LastActivityUpdate + time_freq() * 5)
			{
				m_UpdateActivity = false;
				m_LastActivityUpdate = time_get();

				m_pActivityManager->update_activity(m_pActivityManager, &m_Activity, 0, 0);
			}

			m_pCore->run_callbacks(m_pCore);
		}
	}

	void ClearGameInfo() override
	{
		mem_zero(&m_Activity, sizeof(DiscordActivity));

		AetherApplyDiscordAssets(m_Activity);
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.details, "In menus", sizeof(m_Activity.details));
		str_copy(m_Activity.state, AetherDiscordClientName(), sizeof(m_Activity.state));
		m_Activity.instance = false;

		m_UpdateActivity = true;
	}

	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override
	{
		(void)Registered;
		mem_zero(&m_Activity, sizeof(DiscordActivity));

		AetherApplyDiscordAssets(m_Activity);
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.name, AetherDiscordClientName(), sizeof(m_Activity.name));
		m_Activity.instance = false;

		str_format(m_Activity.details, sizeof(m_Activity.details), "Map: %s", ServerInfo.m_aMap);
		str_copy(m_Activity.state, AetherDiscordClientName(), sizeof(m_Activity.state));

		m_UpdateActivity = true;
	}

	void UpdateServerInfo(const CServerInfo &ServerInfo) override
	{
		if(!m_Activity.instance)
		{
			str_format(m_Activity.details, sizeof(m_Activity.details), "Map: %s", ServerInfo.m_aMap);
			str_copy(m_Activity.state, AetherDiscordClientName(), sizeof(m_Activity.state));
			m_UpdateActivity = true;
			return;
		}

		str_format(m_Activity.details, sizeof(m_Activity.details), "Map: %s", ServerInfo.m_aMap);
		str_copy(m_Activity.state, AetherDiscordClientName(), sizeof(m_Activity.state));
		m_UpdateActivity = true;
	}

	void UpdatePlayerCount(int Count) override
	{
		(void)Count;
	}

	void UpdateServerIp(const CServerInfo &ServerInfo)
	{
		(void)ServerInfo;
	}

	static void DISCORD_CALLBACK OnActivityJoin(void *pEventData, const char *pSecret)
	{
		CDiscord *pSelf = static_cast<CDiscord *>(pEventData);
		IClient *m_pClient = pSelf->Kernel()->RequestInterface<IClient>();
		m_pClient->Connect(pSecret);
	}

	~CDiscord()
	{
		if(m_pCore)
			m_pCore->destroy(m_pCore);
	}
};

static IDiscord *CreateDiscordImpl()
{
	FDiscordCreate pfnDiscordCreate = GetDiscordCreate();
	if(!pfnDiscordCreate)
	{
		return 0;
	}
	CDiscord *pDiscord = new CDiscord();
	if(pDiscord->Init(pfnDiscordCreate))
	{
		delete pDiscord;
		return 0;
	}
	return pDiscord;
}
#else
static IDiscord *CreateDiscordImpl()
{
	return nullptr;
}
#endif

class CDiscordStub : public IDiscord
{
	void Update(bool Enabled) override {}
	void ClearGameInfo() override {}
	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override {}
	void UpdateServerInfo(const CServerInfo &ServerInfo) override {}
	void UpdatePlayerCount(int Count) override {}
};

IDiscord *CreateDiscord()
{
	IDiscord *pDiscord = CreateDiscordImpl();
	if(pDiscord)
	{
		return pDiscord;
	}
	return new CDiscordStub();
}
