#pragma once

#include "ui/ui_types.h"

class DiscordPresence
{
public:
	DiscordPresence();

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
	struct Snapshot
	{
		UiTab tab;
		bool zapret;
		bool tg;
		bool vpn;
		bool enabled;
		bool shareButton;

		Snapshot();
	};

	void PushPresence(const Snapshot& snap) const;
	static const char* TabImageKey(UiTab tab);
	static const char* TabLabel(UiTab tab);

	bool m_initialized;
	bool m_hasPresence;
	Snapshot m_last;
	float m_callbackAge;
	float m_refreshAge;
	long long m_sessionStartedAt;
};
