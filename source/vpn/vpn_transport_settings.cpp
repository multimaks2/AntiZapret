#include "vpn/vpn_transport_settings.h"

namespace
{
	struct DnsProvider
	{
		const char* udp;
		const char* tcp;
		const char* dot;
		const char* doh;
	};

	const DnsProvider kBootstrapProviders[] = {
		{ "1.1.1.1", "tcp://1.1.1.1", "tls://1.1.1.1", "https://cloudflare-dns.com/dns-query" },
		{ "8.8.8.8", "tcp://8.8.8.8", "tls://8.8.8.8", "https://dns.google/dns-query" },
		{ "9.9.9.9", "tcp://9.9.9.9", "tls://dns.quad9.net", "https://dns.quad9.net/dns-query" },
		{ "77.88.8.8", "tcp://77.88.8.8", "tls://common.dot.dns.yandex.net", "https://common.dot.dns.yandex.net/dns-query" },
		{ "208.67.222.222", "tcp://208.67.222.222", "tls://dns.opendns.com", "https://doh.opendns.com/dns-query" },
	};

	const DnsProvider kProxyProviders[] = {
		{ "8.8.8.8", "tcp://8.8.8.8", "tls://8.8.8.8", "https://dns.google/dns-query" },
		{ "1.1.1.1", "tcp://1.1.1.1", "tls://1.1.1.1", "https://cloudflare-dns.com/dns-query" },
		{ "9.9.9.9", "tcp://9.9.9.9", "tls://dns.quad9.net", "https://dns.quad9.net/dns-query" },
		{ "208.67.222.222", "tcp://208.67.222.222", "tls://dns.opendns.com", "https://doh.opendns.com/dns-query" },
	};

	int ClampIndex(int value, int count)
	{
		if (count <= 0)
			return 0;
		if (value < 0)
			return 0;
		if (value >= count)
			return count - 1;
		return value;
	}

	std::string EndpointForType(const DnsProvider& provider, int typeIndex)
	{
		switch (typeIndex)
		{
		case 1: return provider.tcp;
		case 2: return provider.dot;
		case 3: return provider.doh;
		default: return provider.udp;
		}
	}
}

std::string VpnTransportSettings::ResolveBootstrapEndpoint(int providerIndex, int typeIndex)
{
	const int provider = ClampIndex(providerIndex, static_cast<int>(sizeof kBootstrapProviders / sizeof kBootstrapProviders[0]));
	const int type = ClampIndex(typeIndex, 4);
	return EndpointForType(kBootstrapProviders[provider], type);
}

std::string VpnTransportSettings::ResolveProxyEndpoint(int providerIndex, int typeIndex)
{
	const int provider = ClampIndex(providerIndex, static_cast<int>(sizeof kProxyProviders / sizeof kProxyProviders[0]));
	const int type = ClampIndex(typeIndex, 3);
	return EndpointForType(kProxyProviders[provider], type);
}
