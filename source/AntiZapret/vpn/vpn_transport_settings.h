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
	std::string lastSubscriptionUrl;
	// Unix seconds from subscription-userinfo `expire=`; 0 = unknown.
	long long subscriptionExpireUnix = 0;
	// From subscription-userinfo upload/download/total (bytes). total<=0 => unlimited.
	long long subscriptionUploadBytes = 0;
	long long subscriptionDownloadBytes = 0;
	long long subscriptionTotalBytes = 0;
	std::string subscriptionSupportUrl;
	std::string subscriptionProfileTitle;
	std::string subscriptionAnnounce;
	std::string subscriptionProviderId;
	std::string subscriptionUserId;
	std::string subscriptionIconUrl;
};

namespace VpnTransportSettings
{
	std::string ResolveBootstrapEndpoint(int providerIndex, int typeIndex);
	std::string ResolveProxyEndpoint(int providerIndex, int typeIndex);
}
