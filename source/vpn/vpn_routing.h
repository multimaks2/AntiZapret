#pragma once

#include <string>
#include <vector>

#include "vpn/vpn_domain_routes.h"
#include "vpn/vpn_service_routes.h"

enum class VpnRoutingPreset
{
	Ruv1Blocked = 1,
	Ruv1ExceptRu = 2,
	Ruv1All = 3,
	Custom = 4
};

struct VpnCustomRoutingInput
{
	std::vector<ServiceRouteEntry> services;
	std::vector<VpnDomainRule> domains;
	bool includeAdultServices = false;
};

namespace VpnRouting
{
	VpnRoutingPreset PresetFromWorkMode(int workMode);
	void AppendRuleProviders(
		std::string& yaml,
		VpnRoutingPreset preset,
		const std::wstring& vpnDirectory,
		const VpnCustomRoutingInput* custom = nullptr);
	void AppendRules(
		std::string& yaml,
		VpnRoutingPreset preset,
		const std::wstring& vpnDirectory,
		bool hasRuInsideDomainRules,
		bool hasGoogleDomainRules,
		bool hasGoogleIpRules,
		const VpnCustomRoutingInput* custom = nullptr,
		bool fixDiscord = false);
}
