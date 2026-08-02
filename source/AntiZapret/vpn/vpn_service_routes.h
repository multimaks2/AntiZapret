#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ServiceRouteMode : int
{
	Antizapret = 0,
	VpnTunnel = 1,
	VpnProxy = 2,
	None = 3,
};

enum class ServiceCatalogRegion : int
{
	Foreign = 0,
	Russian = 1,
};

enum class ServiceCatalogSection : int
{
	ForeignTools = 0,
	ForeignSocial,
	ForeignStreaming,
	ForeignBrowser,
	ForeignAI,
	ForeignDev,
	ForeignLaunchers,
	ForeignGames,
	ForeignSteamNew,
	ForeignAdult,
	ForeignMisc, // torrent clients
	ForeignStandalone, // end of apps list, no fold group (e.g. Windows)
	RussianBrowser,
	RussianEco,
	RussianBank,
	RussianGov,
	RussianShop,
	RussianDelivery,
	RussianTelecom,
	RussianStreaming,
	RussianTravel,
	RussianProperty,
	RussianWorkHealth,
	RussianMisc,
	CustomApps,
	CustomSites,
};

enum class ServiceCatalogKind : int
{
	Site = 0,
	App = 1,
};

struct ServiceCatalogEntry
{
	const char* id;
	uint32_t icon;
	bool brandIcon; // true = Font Awesome Brands, false = Segoe MDL2
	const char* name;
	const char* description;
	ServiceCatalogRegion region;
	ServiceCatalogSection section;
};

struct ServiceRouteEntry
{
	std::string id;
	uint32_t icon = 0;
	bool brandIcon = false;
	std::string name;
	std::string description;
	ServiceCatalogRegion region = ServiceCatalogRegion::Foreign;
	ServiceCatalogSection section = ServiceCatalogSection::ForeignSocial;
	ServiceCatalogKind kind = ServiceCatalogKind::Site;
	bool custom = false;
	bool enabled = true;
	ServiceRouteMode mode = ServiceRouteMode::None;
};

namespace VpnServiceRoutes
{
	const char* ModeLabel(ServiceRouteMode mode);
	const char* SectionLabel(ServiceCatalogSection section);
	uint32_t SectionIcon(ServiceCatalogSection section);
	ServiceCatalogKind InferKind(const ServiceCatalogEntry& item);
	ServiceCatalogKind InferKind(const ServiceRouteEntry& entry);
	std::string GeositeNameForService(const std::string& serviceId);
	void CollectRequiredGeosites(const std::vector<ServiceRouteEntry>& routes, std::vector<std::string>& outNames);
	void CollectFallbackDomains(const std::string& serviceId, std::vector<std::string>& outDomains);
	void CollectRouteTargets(
		const ServiceRouteEntry& service,
		std::vector<std::string>& outDomains,
		std::vector<std::string>& outProcesses);
	bool HasFallbackDomains(const std::string& serviceId);
	bool PreferFallbackOnly(const std::string& serviceId);
	bool IsAdultSection(ServiceCatalogSection section);
	bool NeedsVoiceRouting(const std::string& serviceId);
	const std::vector<ServiceCatalogEntry>& Catalog();
	void BuildDefaultRoutes(std::vector<ServiceRouteEntry>& outRoutes);
	bool Load(std::vector<ServiceRouteEntry>& outRoutes);
	void Save(const std::vector<ServiceRouteEntry>& routes);
	ServiceRouteEntry MakeCustomEntry(
		ServiceCatalogKind kind,
		const std::string& name,
		const std::string& targets);

	// Keep Discord catalogue row aligned with VPN "Fix Discord" checkbox.
	bool IsFixDiscordEffective(const ServiceRouteEntry& entry);
	bool ApplyFixDiscordToEntry(ServiceRouteEntry& entry, bool fixDiscord);
	bool ApplyFixDiscordToRoutes(std::vector<ServiceRouteEntry>& routes, bool fixDiscord);
}
