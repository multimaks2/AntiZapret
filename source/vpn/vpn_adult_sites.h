#pragma once

#include "vpn/vpn_service_routes.h"

#include <string>
#include <vector>

namespace VpnAdultSites
{
	bool IsAdultServiceId(const std::string& serviceId);
	const std::vector<ServiceCatalogEntry>& Catalog();
	void CollectFallbackDomains(const std::string& serviceId, std::vector<std::string>& outDomains);
}
