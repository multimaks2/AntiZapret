#pragma once

#include <string>

struct VpnStoreSettings
{
	std::string activeUri;
	int workMode = 1;

	// 0 = Proxy (system proxy + mixed-port), 1 = Tunnel (TUN)
	int transportMode = 1;
	// 0 = system DNS, 1 = built-in DNS (mihomo)
	int dnsMode = 1;
	int bootstrapDns = 2;
	int bootstrapType = 0;
	int proxyDns = 0;
	int proxyType = 0;
	int routingRevision = 0;
	bool fixDiscord = false;
};

namespace VpnTransportSettings
{
	std::string ResolveBootstrapEndpoint(int providerIndex, int typeIndex);
	std::string ResolveProxyEndpoint(int providerIndex, int typeIndex);
}
