#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_routing.h"
#include "vpn/vpn_transport_settings.h"

#include <string>
#include <vector>

namespace VpnConfigBuilder
{
	struct ParallelProbeEndpoint
	{
		int nodeIndex = -1;
		int port = 0;
		std::string proxyTag;
	};

	bool WriteRuntimeConfig(
		const VpnNode& node,
		VpnRoutingPreset preset,
		const VpnStoreSettings& transport,
		const std::wstring& mihomoHome,
		int mixedPort,
		int apiPort,
		std::string& outError,
		bool fetchGeosites = true);

	// v2rayN-style RealPing batch: all selected proxies + one mixed listener per node.
	// Always Proxy-only (no TUN) so probing does not hijack system routing.
	bool WriteParallelProbeConfig(
		const std::vector<VpnNode>& nodes,
		const std::vector<int>& indices,
		int activeIndex,
		VpnRoutingPreset preset,
		const VpnStoreSettings& transport,
		const std::wstring& mihomoHome,
		int mixedPort,
		int apiPort,
		int portBase,
		std::vector<ParallelProbeEndpoint>& outEndpoints,
		std::string& outError);
}
