#pragma once

#include "ui/ui_types.h"

#include <string>

struct DiscordPresenceButtons
{
	bool downloadEnabled = false;
	std::string downloadUrl;
	bool importEnabled = false;
	std::string importLabel;
	std::string importUrl; // must be http(s) for Discord buttons
};

// Application-side Rich Presence controller (not a Discord SDK type).
class AppRichPresence
{
public:
	AppRichPresence();

	void Initialize();
	void Shutdown();
	void Update(
		UiTab activeTab,
		bool zapretRunning,
		bool tgRunning,
		bool vpnRunning,
		const std::string& detailsText,
		bool enabled,
		const DiscordPresenceButtons& buttons,
		float deltaTime);

private:
	void PushPresence(
		UiTab tab,
		bool zapret,
		bool tg,
		bool vpn,
		const std::string& detailsText,
		const DiscordPresenceButtons& buttons) const;

	static const char* TabImageKey(UiTab tab);
	static const char* TabLabel(UiTab tab);

	bool m_initialized;
	bool m_hasPresence;
	UiTab m_lastTab;
	bool m_lastZapret;
	bool m_lastTg;
	bool m_lastVpn;
	bool m_lastEnabled;
	DiscordPresenceButtons m_lastButtons;
	std::string m_lastDetails;
	// Kept alive for Discord_UpdatePresence string pointers.
	mutable std::string m_pushImportUrl;
	mutable std::string m_pushDownloadUrl;
	mutable std::string m_pushImportLabel;
	float m_callbackAge;
	float m_refreshAge;
	long long m_sessionStartedAt;
};
