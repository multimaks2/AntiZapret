#pragma once

#include "vpn/vpn_node.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace VpnNodeProbe
{
	// ICMP echo like `ping` in cmd. Resolves host to real IPv4 (DoH). -1 on failure.
	int IcmpPingMs(const std::string& host, int timeoutMs = 4000);

	// TCP connect RTT to server:port (v2rayN Tcping). Default timeout 5s. -1 on failure.
	int TcpPingMs(const std::string& host, int port, int timeoutMs = 5000);

	// Resolve hostname to IPv4 address list (comma-separated). Uses DoH to avoid
	// Clash/mihomo fake-ip (198.18.0.0/15) from system DNS. Empty on failure.
	std::string ResolveHostIpv4(const std::string& host);

	// v2rayN Realping: HTTP GET via local HTTP/SOCKS mixed-port. Best of 2 samples. -1 on failure.
	int HttpRealPingMs(
		const std::string& proxyHost,
		int proxyPort,
		const char* url = "https://www.google.com/generate_204",
		int timeoutMs = 9000);

	// Peak download speed in MB/s via HTTP proxy (v2rayN Speedtest-style). -1 on failure.
	// Optional onProgress is called as the running peak updates (for live UI).
	float MeasureDownloadPeakMBps(
		const std::string& proxyHost,
		int proxyPort,
		const char* url,
		int timeoutMs,
		std::atomic_bool* cancelFlag,
		const std::function<void(float peakMBps)>& onProgress = {});

	bool CopyUtf8ToClipboard(const std::string& text);
	std::string BuildOutboundJson(const VpnNode& node);
	std::string NowTimeLabel();
}
