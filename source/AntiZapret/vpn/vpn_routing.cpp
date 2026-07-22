#include "vpn/vpn_routing.h"

#include "vpn/vpn_discord_voice_rules.h"
#include "vpn/vpn_service_routes.h"

#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace
{
	bool LooksLikeIpAddress(const std::string& value)
	{
		if (value.empty())
			return false;

		bool hasDot = false;
		for (char ch : value)
		{
			if (ch == '.')
				hasDot = true;
			else if (ch == ':')
				return true;
			else if (!std::isdigit(static_cast<unsigned char>(ch)))
				return false;
		}
		return hasDot;
	}

	std::string NormalizeIpCidr(const std::string& value)
	{
		if (value.find('/') != std::string::npos)
			return value;
		if (value.find(':') != std::string::npos)
			return value + "/128";
		return value + "/32";
	}

	const char* ActionLabel(VpnDomainRuleAction action)
	{
		switch (action)
		{
		case VpnDomainRuleAction::Direct: return "DIRECT";
		case VpnDomainRuleAction::Reject: return "REJECT";
		case VpnDomainRuleAction::Proxy:
		default: return "PROXY";
		}
	}

	const char* ServiceModeAction(ServiceRouteMode mode)
	{
		switch (mode)
		{
		case ServiceRouteMode::Antizapret:
		case ServiceRouteMode::None:
			return "DIRECT";
		case ServiceRouteMode::VpnTunnel:
		case ServiceRouteMode::VpnProxy:
		default:
			return "PROXY";
		}
	}

	std::string ProviderNameForService(const std::string& serviceId)
	{
		return "svc-" + serviceId;
	}

	bool GeositeFileIsUsable(const std::wstring& vpnDirectory, const std::string& geositeName)
	{
		const std::filesystem::path path =
			std::filesystem::path(vpnDirectory) / L"srss" / ("geosite-" + geositeName + ".srs");
		std::error_code ec;
		const auto size = std::filesystem::file_size(path, ec);
		return !ec && size >= 64;
	}

	void AppendDomainRule(std::string& yaml, const VpnDomainRule& rule)
	{
		if (rule.address.empty())
			return;

		if (LooksLikeIpAddress(rule.address))
		{
			yaml += "  - IP-CIDR,";
			yaml += NormalizeIpCidr(rule.address);
			yaml += ",";
			yaml += ActionLabel(rule.action);
			yaml += "\n";
			return;
		}

		yaml += "  - DOMAIN-SUFFIX,";
		yaml += rule.address;
		yaml += ",";
		yaml += ActionLabel(rule.action);
		yaml += "\n";
	}
}

VpnRoutingPreset VpnRouting::PresetFromWorkMode(int workMode)
{
	switch (workMode)
	{
	case 2: return VpnRoutingPreset::Ruv1ExceptRu;
	case 3: return VpnRoutingPreset::Ruv1All;
	case 4: return VpnRoutingPreset::Custom;
	default: return VpnRoutingPreset::Ruv1Blocked; // 0 (legacy) и 1
	}
}

void VpnRouting::AppendRuleProviders(
	std::string& yaml,
	VpnRoutingPreset preset,
	const std::wstring& vpnDirectory,
	const VpnCustomRoutingInput* custom)
{
	yaml +=
		"rule-providers:\n"
		"  ads-all:\n"
		"    type: file\n"
		"    behavior: domain\n"
		"    format: mrs\n"
		"    path: ./srss/geosite-category-ads-all.srs\n"
		"  private-ip:\n"
		"    type: file\n"
		"    behavior: ipcidr\n"
		"    format: mrs\n"
		"    path: ./srss/geoip-private.srs\n"
		"  private-domain:\n"
		"    type: file\n"
		"    behavior: domain\n"
		"    format: mrs\n"
		"    path: ./srss/geosite-private.srs\n";

	if (preset == VpnRoutingPreset::Custom && custom != nullptr)
	{
		std::unordered_set<std::string> addedProviders;
		for (const ServiceRouteEntry& service : custom->services)
		{
			if (!service.enabled || service.mode == ServiceRouteMode::None)
				continue;
			if (VpnServiceRoutes::IsAdultSection(service.section) && !custom->includeAdultServices)
				continue;
			if (VpnServiceRoutes::PreferFallbackOnly(service.id))
				continue;

			const std::string geositeName = VpnServiceRoutes::GeositeNameForService(service.id);
			if (geositeName.empty())
				continue;

			const std::string providerKey = geositeName + ":" + service.id;
			if (!addedProviders.insert(providerKey).second)
				continue;
			if (!GeositeFileIsUsable(vpnDirectory, geositeName))
				continue;

			const std::string providerName = ProviderNameForService(service.id);
			yaml += "  " + providerName + ":\n";
			yaml += "    type: file\n";
			yaml += "    behavior: domain\n";
			yaml += "    format: mrs\n";
			yaml += "    path: ./srss/geosite-" + geositeName + ".srs\n";
		}
		return;
	}

	yaml +=
		"  ru-blocked-ip:\n"
		"    type: file\n"
		"    behavior: ipcidr\n"
		"    format: mrs\n"
		"    path: ./srss/geoip-ru-blocked.srs\n"
		"  ru-blocked-domain:\n"
		"    type: file\n"
		"    behavior: domain\n"
		"    format: mrs\n"
		"    path: ./srss/geosite-ru-blocked.srs\n"
		"  ru-ip:\n"
		"    type: file\n"
		"    behavior: ipcidr\n"
		"    format: mrs\n"
		"    path: ./srss/geoip-ru.srs\n";

	if (preset == VpnRoutingPreset::Ruv1Blocked)
	{
		const std::filesystem::path root(vpnDirectory);
		if (std::filesystem::exists(root / L"srss" / L"geosite-google.srs"))
		{
			yaml +=
				"  google-domain:\n"
				"    type: file\n"
				"    behavior: domain\n"
				"    format: mrs\n"
				"    path: ./srss/geosite-google.srs\n";
		}
		if (std::filesystem::exists(root / L"srss" / L"geoip-google.srs"))
		{
			yaml +=
				"  google-ip:\n"
				"    type: file\n"
				"    behavior: ipcidr\n"
				"    format: mrs\n"
				"    path: ./srss/geoip-google.srs\n";
		}
	}

	if (preset == VpnRoutingPreset::Ruv1ExceptRu)
	{
		const std::filesystem::path ruInsideRules =
			std::filesystem::path(vpnDirectory) / L"srss" / L"geosite-ru-available-only-inside.srs";
		if (std::filesystem::exists(ruInsideRules))
		{
			yaml +=
				"  ru-inside-domain:\n"
				"    type: file\n"
				"    behavior: domain\n"
				"    format: mrs\n"
				"    path: ./srss/geosite-ru-available-only-inside.srs\n";
		}
	}
}

void VpnRouting::AppendRules(
	std::string& yaml,
	VpnRoutingPreset preset,
	const std::wstring& vpnDirectory,
	bool hasRuInsideDomainRules,
	bool hasGoogleDomainRules,
	bool hasGoogleIpRules,
	const VpnCustomRoutingInput* custom,
	bool fixDiscord)
{
	yaml += "rules:\n";

	if (preset == VpnRoutingPreset::Custom && custom != nullptr)
	{
		yaml += "  - RULE-SET,ads-all,REJECT\n";
		yaml += "  - RULE-SET,private-ip,DIRECT\n";
		yaml += "  - RULE-SET,private-domain,DIRECT\n";

		if (fixDiscord)
			VpnDiscordVoiceRules::AppendDomainAndVoiceRules(yaml, "PROXY");

		for (const ServiceRouteEntry& service : custom->services)
		{
			if (!service.enabled || service.mode == ServiceRouteMode::None)
				continue;
			if (VpnServiceRoutes::IsAdultSection(service.section) && !custom->includeAdultServices)
				continue;
			// Already forced via Fix Discord — skip duplicate discord catalogue rules.
			if (fixDiscord && service.id == "discord")
				continue;

			const std::string geositeName = VpnServiceRoutes::GeositeNameForService(service.id);
			const char* action = ServiceModeAction(service.mode);
			std::vector<std::string> fallbackDomains;
			VpnServiceRoutes::CollectFallbackDomains(service.id, fallbackDomains);

			if (!fallbackDomains.empty())
			{
				for (const std::string& domain : fallbackDomains)
				{
					yaml += "  - DOMAIN-SUFFIX,";
					yaml += domain;
					yaml += ",";
					yaml += action;
					yaml += "\n";
				}
			}

			if (!VpnServiceRoutes::PreferFallbackOnly(service.id)
				&& !geositeName.empty()
				&& GeositeFileIsUsable(vpnDirectory, geositeName))
			{
				yaml += "  - RULE-SET,";
				yaml += ProviderNameForService(service.id);
				yaml += ",";
				yaml += action;
				yaml += "\n";
			}

			if (VpnServiceRoutes::NeedsVoiceRouting(service.id)
				&& (service.mode == ServiceRouteMode::VpnTunnel
					|| service.mode == ServiceRouteMode::VpnProxy))
			{
				VpnDiscordVoiceRules::AppendRules(yaml, action);
			}
		}

		for (const VpnDomainRule& rule : custom->domains)
			AppendDomainRule(yaml, rule);

		yaml += "  - MATCH,DIRECT\n";
		return;
	}

	yaml += "  - PROCESS-NAME,uTorrent.exe,DIRECT\n";
	yaml += "  - PROCESS-NAME,BitComet.exe,DIRECT\n";
	yaml += "  - PROCESS-NAME,Transmission.exe,DIRECT\n";
	yaml += "  - PROCESS-NAME,qBittorrent.exe,DIRECT\n";
	yaml += "  - RULE-SET,ads-all,REJECT\n";
	yaml += "  - RULE-SET,private-ip,DIRECT\n";
	yaml += "  - RULE-SET,private-domain,DIRECT\n";

	if (fixDiscord)
		VpnDiscordVoiceRules::AppendDomainAndVoiceRules(yaml, "PROXY");

	if (preset == VpnRoutingPreset::Ruv1Blocked)
	{
		yaml += "  - IP-CIDR,1.0.0.1/32,PROXY\n";
		yaml += "  - IP-CIDR,1.1.1.1/32,PROXY\n";
		yaml += "  - IP-CIDR,8.8.8.8/32,PROXY\n";
		yaml += "  - IP-CIDR,8.8.4.4/32,PROXY\n";
		yaml += "  - AND,((DST-PORT,50000-65535),(NETWORK,UDP)),PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,youtube.com,PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,googlevideo.com,PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,ytimg.com,PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,ggpht.com,PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,googleapis.com,PROXY\n";
		yaml += "  - DOMAIN-SUFFIX,gstatic.com,PROXY\n";
		if (hasGoogleDomainRules)
			yaml += "  - RULE-SET,google-domain,PROXY\n";
		if (hasGoogleIpRules)
			yaml += "  - RULE-SET,google-ip,PROXY\n";
		yaml += "  - RULE-SET,ru-blocked-ip,PROXY\n";
		yaml += "  - RULE-SET,ru-blocked-domain,PROXY\n";
		yaml += "  - MATCH,DIRECT\n";
		return;
	}

	if (preset == VpnRoutingPreset::Ruv1ExceptRu)
	{
		if (hasRuInsideDomainRules)
			yaml += "  - RULE-SET,ru-inside-domain,DIRECT\n";
		yaml += "  - DOMAIN-SUFFIX,ru,DIRECT\n";
		yaml += "  - DOMAIN-SUFFIX,su,DIRECT\n";
		yaml += "  - DOMAIN-SUFFIX,xn--p1ai,DIRECT\n";
		yaml += "  - RULE-SET,ru-ip,DIRECT\n";
		yaml += "  - MATCH,PROXY\n";
		return;
	}

	yaml += "  - MATCH,PROXY\n";
}
