#include "discord/discord_presence.h"

#include "version.h"
#include "discord_rpc.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace
{
	constexpr char kApplicationId[] = "1526697979879231658";
	constexpr float kCallbackIntervalSec = 0.5f;
	constexpr float kForceRefreshSec = 12.f;

	void BuildServicesState(bool zapret, bool tg, bool vpn, char* out, size_t outSize)
	{
		if (!zapret && !tg && !vpn)
		{
			strncpy_s(out, outSize, "не использует сервисы", _TRUNCATE);
			return;
		}

		strncpy_s(out, outSize, "использует — ", _TRUNCATE);
		bool first = true;
		auto append = [&](const char* label) {
			if (!first)
				strncat_s(out, outSize, ", ", _TRUNCATE);
			strncat_s(out, outSize, label, _TRUNCATE);
			first = false;
		};

		if (vpn)
			append("VPN");
		if (zapret)
			append("антизапрет");
		if (tg)
			append("tg-ws-proxy");
	}
}

void DiscordPresence::Initialize()
{
	if (m_initialized)
		return;

	DiscordEventHandlers handlers = {};
	Discord_Initialize(kApplicationId, &handlers, 0, nullptr);
	m_startTimestamp = static_cast<long long>(std::time(nullptr));
	m_initialized = true;
	m_hasPresence = false;
	m_callbackAge = 0.f;
	m_forceAge = kForceRefreshSec; // push soon after init
}

void DiscordPresence::Shutdown()
{
	if (!m_initialized)
		return;

	Discord_ClearPresence();
	Discord_Shutdown();
	m_initialized = false;
	m_hasPresence = false;
}

void DiscordPresence::Update(
	UiTab activeTab,
	bool zapretRunning,
	bool tgRunning,
	bool vpnRunning,
	bool enabled,
	float deltaTime)
{
	if (!enabled)
	{
		if (m_initialized)
		{
			m_callbackAge += deltaTime;
			if (m_callbackAge >= kCallbackIntervalSec)
			{
				Discord_RunCallbacks();
				m_callbackAge = 0.f;
			}
			if (m_hasPresence || m_last.enabled)
			{
				Discord_ClearPresence();
				m_hasPresence = false;
				m_last.enabled = false;
			}
		}
		return;
	}

	if (!m_initialized)
		Initialize();

	m_callbackAge += deltaTime;
	if (m_callbackAge >= kCallbackIntervalSec)
	{
		Discord_RunCallbacks();
		m_callbackAge = 0.f;
	}

	const Snapshot snap{ activeTab, zapretRunning, tgRunning, vpnRunning, true };
	m_forceAge += deltaTime;

	const bool changed = !m_hasPresence
		|| !m_last.enabled
		|| snap.tab != m_last.tab
		|| snap.zapret != m_last.zapret
		|| snap.tg != m_last.tg
		|| snap.vpn != m_last.vpn;

	if (!changed && m_forceAge < kForceRefreshSec)
		return;

	PushPresence(snap);
	m_last = snap;
	m_hasPresence = true;
	m_forceAge = 0.f;
}

void DiscordPresence::PushPresence(const Snapshot& snap) const
{
	char details[128] = {};
	char state[128] = {};
	char largeText[128] = {};

	snprintf(details, sizeof details, "%s", TabLabel(snap.tab));
	BuildServicesState(snap.zapret, snap.tg, snap.vpn, state, sizeof state);
	snprintf(largeText, sizeof largeText, "AntiZapret %s", ANTIZAPRET_VERSION);

	DiscordRichPresence presence = {};
	presence.details = details;
	presence.state = state;
	presence.startTimestamp = m_startTimestamp;
	presence.largeImageKey = "main";
	presence.largeImageText = largeText;
	presence.smallImageKey = TabImageKey(snap.tab);
	presence.smallImageText = TabLabel(snap.tab);
	presence.instance = 0;

	Discord_UpdatePresence(&presence);
}

const char* DiscordPresence::TabImageKey(UiTab tab)
{
	switch (tab)
	{
	case UiTab::Home: return "icon-home";
	case UiTab::AntiZapret: return "icon-antizapret";
	case UiTab::TgWsProxy: return "icon-tgfix";
	case UiTab::Vpn: return "icon-vpn";
	case UiTab::Routing: return "icon-routing";
	case UiTab::Console: return "icon-console";
	case UiTab::Settings: return "icon-settings";
	case UiTab::About: return "icon-about";
	}
	return "icon-home";
}

const char* DiscordPresence::TabLabel(UiTab tab)
{
	switch (tab)
	{
	case UiTab::Home: return "Главная";
	case UiTab::AntiZapret: return "Антизапрет";
	case UiTab::TgWsProxy: return "TG Fix";
	case UiTab::Vpn: return "VPN";
	case UiTab::Routing: return "Маршрутизация";
	case UiTab::Console: return "Консоль";
	case UiTab::Settings: return "Настройки";
	case UiTab::About: return "О приложении";
	}
	return "AntiZapret";
}
