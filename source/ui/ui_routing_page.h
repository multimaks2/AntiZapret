#pragma once

#include "vpn/vpn_domain_routes.h"
#include "vpn/vpn_service_routes.h"
#include "vpn/vpn_store.h"

#include <unordered_map>
#include <vector>

class AppSettings;
class FontManager;
class ThemeManager;
class VpnManager;
struct UiThemeColors;
struct UiAccentColors;

class UiRoutingPage
{
public:
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);
	void SetVpnManager(VpnManager* manager) { m_vpnManager = manager; }
	void SetAppSettings(AppSettings* settings) { m_appSettings = settings; }

private:
	struct ProcessRule
	{
		char label[128];
		int action = 1;
	};

	VpnStore m_store;
	VpnManager* m_vpnManager = nullptr;
	AppSettings* m_appSettings = nullptr;
	bool m_loaded = false;
	bool m_domainRulesLoaded = false;
	bool m_serviceRoutesLoaded = false;
	bool m_serviceRoutesLoading = false;
	bool m_bypassLan = true;
	float m_applyDebounce = 0.f;
	int m_transportMode = 1;
	int m_dnsMode = 1;
	int m_bootstrapDns = 2;
	int m_bootstrapType = 0;
	int m_proxyDns = 0;
	int m_proxyType = 0;

	float m_bypassLanMix = 1.f;
	std::vector<float> m_serviceMix;
	std::vector<ServiceRouteEntry> m_serviceRoutes;
	std::vector<VpnDomainRule> m_domainRules;
	std::vector<ProcessRule> m_processRules;

	float m_applySuccessTimer = 0.f;
	int m_selectedProcess = -1;
	int m_selectedDomain = -1;
	char m_serviceSearch[128] = {};
	bool m_gamesExpanded = false;
	std::unordered_map<int, bool> m_sectionExpanded;

	void EnsureLoaded();
	void EnsureServiceRoutesLoaded();
	void EnsureDomainRulesLoaded();
	bool MatchesServiceSearch(const ServiceRouteEntry& service) const;
	bool MatchesTextSearch(const char* text) const;
	void DrawServiceRoutes(FontManager& fonts, float width, const UiThemeColors& colors);
	void DrawAdvancedRules(
		FontManager& fonts,
		float width,
		const UiThemeColors& colors,
		const UiAccentColors& accents);
	void ScheduleApply();
	void FlushApplyIfDue(float deltaTime);
	void ApplyRouting();
};
