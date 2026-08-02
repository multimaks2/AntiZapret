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
	char m_customAppName[96] = {};
	char m_customAppTargets[192] = {};
	char m_customSiteName[96] = {};
	char m_customSiteTargets[192] = {};
	bool m_openProcessPicker = false;
	char m_processPickerFilter[128] = {};
	std::vector<std::string> m_processPickerList;
	float m_processPickerScrollY = 0.f;
	float m_processPickerScrollDisplay = 0.f;
	float m_processPickerScrollVel = 0.f;
	bool m_showDuplicateWarning = false;
	std::string m_duplicateWarningAttempt;
	std::string m_duplicateWarningExisting;

	void EnsureLoaded();
	void EnsureServiceRoutesLoaded();
	void SyncDiscordRouteFromFixDiscord();
	void WriteFixDiscordFromDiscordRoute();
	void EnsureDomainRulesLoaded();
	bool MatchesServiceSearch(const ServiceRouteEntry& service) const;
	bool MatchesTextSearch(const char* text) const;
	void RefreshProcessPickerList();
	bool IsProcessAlreadyCovered(const std::string& exeName) const;
	const ServiceRouteEntry* FindCoveringApp(const std::string& exeOrName) const;
	const ServiceRouteEntry* FindCoveringSite(const std::string& domainOrName) const;
	bool TryAddCustomEntry(ServiceCatalogKind kind, const std::string& name, const std::string& targets);
	void ShowDuplicateWarning(const std::string& attempted, const std::string& existing);
	void AddCustomAppFromProcess(const std::string& exeName);
	void DrawProcessPickerModal(
		FontManager& fonts,
		const UiThemeColors& colors,
		const UiAccentColors& accents);
	void DrawDuplicateWarningModal(
		const UiThemeColors& colors,
		const UiAccentColors& accents);
	void DrawServiceRoutes(
		FontManager& fonts,
		float width,
		const UiThemeColors& colors,
		const UiAccentColors& accents);
	void DrawAdvancedRules(
		FontManager& fonts,
		float width,
		const UiThemeColors& colors,
		const UiAccentColors& accents);
	void ScheduleApply();
	void FlushApplyIfDue(float deltaTime);
	void ApplyRouting();
};
