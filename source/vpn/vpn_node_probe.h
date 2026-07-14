#pragma once

#include "vpn/vpn_node.h"

#include <atomic>
#include <string>
#include <vector>

namespace VpnNodeProbe
{
	// TCP connect RTT to server:port (v2rayN Tcping-style). -1 on failure.
	int TcpPingMs(const std::string& host, int port, int timeoutMs = 4000);

	// Download via HTTP proxy (mihomo mixed-port). Returns Mbps or -1.
	float MeasureDownloadMbps(
		const std::string& proxyHost,
		int proxyPort,
		const char* url,
		int timeoutMs,
		std::atomic_bool* cancelFlag);

	bool CopyUtf8ToClipboard(const std::string& text);
	std::string BuildOutboundJson(const VpnNode& node);
	std::string NowTimeLabel();
}
