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
	ForeignMisc,
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
};

struct ServiceCatalogEntry
{
	const char* id;
	uint32_t icon;
	const char* name;
	const char* description;
	ServiceCatalogRegion region;
	ServiceCatalogSection section;
};

struct ServiceRouteEntry
{
	std::string id;
	uint32_t icon = 0;
	std::string name;
	std::string description;
	ServiceCatalogRegion region = ServiceCatalogRegion::Foreign;
	ServiceCatalogSection section = ServiceCatalogSection::ForeignSocial;
	bool enabled = true;
	ServiceRouteMode mode = ServiceRouteMode::None;
};

namespace VpnServiceRoutes
{
	const char* ModeLabel(ServiceRouteMode mode);
	const char* SectionLabel(ServiceCatalogSection section);
	std::string GeositeNameForService(const std::string& serviceId);
	void CollectRequiredGeosites(const std::vector<ServiceRouteEntry>& routes, std::vector<std::string>& outNames);
	void CollectFallbackDomains(const std::string& serviceId, std::vector<std::string>& outDomains);
	bool HasFallbackDomains(const std::string& serviceId);
	bool PreferFallbackOnly(const std::string& serviceId);
	bool IsAdultSection(ServiceCatalogSection section);
	bool NeedsVoiceRouting(const std::string& serviceId);
	const std::vector<ServiceCatalogEntry>& Catalog();
	void BuildDefaultRoutes(std::vector<ServiceRouteEntry>& outRoutes);
	bool Load(std::vector<ServiceRouteEntry>& outRoutes);
	void Save(const std::vector<ServiceRouteEntry>& routes);
}
