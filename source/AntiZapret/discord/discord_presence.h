#pragma once

#include "ui/ui_types.h"

#include <string>

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
		bool shareButtonEnabled,
		bool downloadButtonEnabled,
		const std::string& downloadUrl,
		float deltaTime);

private:
	void PushPresence(
		UiTab tab,
		bool zapret,
		bool tg,
		bool vpn,
		const std::string& detailsText,
		bool shareButton,
		bool downloadButton,
		const std::string& downloadUrl) const;

	static const char* TabImageKey(UiTab tab);
	static const char* TabLabel(UiTab tab);

	bool m_initialized;
	bool m_hasPresence;
	UiTab m_lastTab;
	bool m_lastZapret;
	bool m_lastTg;
	bool m_lastVpn;
	bool m_lastEnabled;
	bool m_lastShareButton;
	bool m_lastDownloadButton;
	std::string m_lastDetails;
	std::string m_lastDownloadUrl;
	// Kept alive for Discord_UpdatePresence string pointers.
	mutable std::string m_pushDownloadUrl;
	float m_callbackAge;
	float m_refreshAge;
	long long m_sessionStartedAt;
};
