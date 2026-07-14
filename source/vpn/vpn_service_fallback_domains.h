#pragma once

#include <string>
#include <vector>

namespace VpnServiceFallbackDomains
{
	void Collect(const std::string& serviceId, std::vector<std::string>& outDomains);
}
