#include "discord/discord_presence.h"

#include "version.h"
#include "discord_rpc.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace
{
	constexpr char kApplicationId[] = "1526697979879231658";
	constexpr char kShareButtonLabel[] = "Импорт";
	constexpr char kShareButtonUrl[] = "https://github.com/multimaks2/AntiZapret/releases/latest";
	constexpr char kDownloadButtonLabel[] = "Скачать AntiZapret";
	constexpr char kDefaultDownloadUrl[] = "https://github.com/multimaks2/AntiZapret/releases/latest";
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

	bool LooksLikeHttpUrl(const std::string& url)
	{
		return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
	}
}

AppRichPresence::AppRichPresence()
	: m_initialized(false)
	, m_hasPresence(false)
	, m_lastTab(UiTab::Home)
	, m_lastZapret(false)
	, m_lastTg(false)
	, m_lastVpn(false)
	, m_lastEnabled(false)
	, m_lastShareButton(true)
	, m_lastDownloadButton(true)
	, m_callbackAge(0.f)
	, m_refreshAge(0.f)
	, m_sessionStartedAt(0)
{
}

void AppRichPresence::Initialize()
{
	if (m_initialized)
		return;

	DiscordEventHandlers handlers;
	std::memset(&handlers, 0, sizeof(handlers));
	Discord_Initialize(kApplicationId, &handlers, 0, nullptr);
	m_sessionStartedAt = static_cast<long long>(std::time(nullptr));
	m_initialized = true;
	m_hasPresence = false;
	m_callbackAge = 0.f;
	m_refreshAge = kForceRefreshSec;
}

void AppRichPresence::Shutdown()
{
	if (!m_initialized)
		return;

	Discord_ClearPresence();
	Discord_Shutdown();
	m_initialized = false;
	m_hasPresence = false;
}

void AppRichPresence::Update(
	UiTab activeTab,
	bool zapretRunning,
	bool tgRunning,
	bool vpnRunning,
	const std::string& detailsText,
	bool enabled,
	bool shareButtonEnabled,
	bool downloadButtonEnabled,
	const std::string& downloadUrl,
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
			if (m_hasPresence || m_lastEnabled)
			{
				Discord_ClearPresence();
				m_hasPresence = false;
				m_lastEnabled = false;
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

	m_refreshAge += deltaTime;

	const bool changed = !m_hasPresence
		|| !m_lastEnabled
		|| activeTab != m_lastTab
		|| zapretRunning != m_lastZapret
		|| tgRunning != m_lastTg
		|| vpnRunning != m_lastVpn
		|| shareButtonEnabled != m_lastShareButton
		|| downloadButtonEnabled != m_lastDownloadButton
		|| detailsText != m_lastDetails
		|| downloadUrl != m_lastDownloadUrl;

	if (!changed && m_refreshAge < kForceRefreshSec)
		return;

	PushPresence(
		activeTab,
		zapretRunning,
		tgRunning,
		vpnRunning,
		detailsText,
		shareButtonEnabled,
		downloadButtonEnabled,
		downloadUrl);
	m_lastTab = activeTab;
	m_lastZapret = zapretRunning;
	m_lastTg = tgRunning;
	m_lastVpn = vpnRunning;
	m_lastEnabled = true;
	m_lastShareButton = shareButtonEnabled;
	m_lastDownloadButton = downloadButtonEnabled;
	m_lastDetails = detailsText;
	m_lastDownloadUrl = downloadUrl;
	m_hasPresence = true;
	m_refreshAge = 0.f;
}

void AppRichPresence::PushPresence(
	UiTab tab,
	bool zapret,
	bool tg,
	bool vpn,
	const std::string& detailsText,
	bool shareButton,
	bool downloadButton,
	const std::string& downloadUrl) const
{
	char details[128] = {};
	char state[128] = {};
	char largeText[128] = {};

	strncpy_s(details, sizeof details, detailsText.c_str(), _TRUNCATE);
	BuildServicesState(zapret, tg, vpn, state, sizeof state);
	snprintf(largeText, sizeof largeText, "AntiZapret %s", ANTIZAPRET_VERSION);

	DiscordRichPresence presence;
	std::memset(&presence, 0, sizeof(presence));
	presence.details = details;
	presence.state = state;
	presence.startTimestamp = m_sessionStartedAt;
	presence.largeImageKey = "main";
	presence.largeImageText = largeText;
	presence.smallImageKey = TabImageKey(tab);
	presence.smallImageText = TabLabel(tab);

	if (shareButton)
	{
		presence.button1Label = kShareButtonLabel;
		presence.button1Url = kShareButtonUrl;
	}

	if (downloadButton)
	{
		m_pushDownloadUrl = LooksLikeHttpUrl(downloadUrl) ? downloadUrl : kDefaultDownloadUrl;
		presence.button2Label = kDownloadButtonLabel;
		presence.button2Url = m_pushDownloadUrl.c_str();
	}

	presence.instance = 0;
	Discord_UpdatePresence(&presence);
}

const char* AppRichPresence::TabImageKey(UiTab tab)
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

const char* AppRichPresence::TabLabel(UiTab tab)
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
