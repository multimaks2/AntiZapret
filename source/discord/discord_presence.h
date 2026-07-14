#pragma once

#include "ui/ui_types.h"

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
		bool enabled,
		bool shareButtonEnabled,
		float deltaTime);

private:
	void PushPresence(
		UiTab tab,
		bool zapret,
		bool tg,
		bool vpn,
		bool shareButton) const;

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
	float m_callbackAge;
	float m_refreshAge;
	long long m_sessionStartedAt;
};
