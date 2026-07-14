#pragma once

#include "ui/ui_types.h"

class DiscordPresence
{
public:
	void Initialize();
	void Shutdown();
	void Update(UiTab activeTab, bool zapretRunning, bool tgRunning, bool vpnRunning, float deltaTime);

private:
	struct Snapshot
	{
		UiTab tab = UiTab::Home;
		bool zapret = false;
		bool tg = false;
		bool vpn = false;
	};

	void PushPresence(const Snapshot& snap) const;
	static const char* TabImageKey(UiTab tab);
	static const char* TabLabel(UiTab tab);

	bool m_initialized = false;
	bool m_hasPresence = false;
	Snapshot m_last = {};
	float m_callbackAge = 0.f;
	float m_forceAge = 0.f;
	long long m_startTimestamp = 0;
};
