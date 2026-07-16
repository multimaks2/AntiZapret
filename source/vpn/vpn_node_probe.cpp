#include "vpn/vpn_node_probe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <WinInet.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
	void EnsureWinsock()
	{
		static bool ready = false;
		if (ready)
			return;
		WSADATA data = {};
		ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}

	bool SetSocketTimeouts(SOCKET sock, int timeoutMs)
	{
		const DWORD t = static_cast<DWORD>((std::max)(timeoutMs, 1));
		return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0
			&& setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0;
	}

	bool ParseIpv4(const std::string& text, unsigned& a, unsigned& b, unsigned& c, unsigned& d)
	{
		a = b = c = d = 0;
		int n = 0;
		unsigned aa = 0, bb = 0, cc = 0, dd = 0;
		if (sscanf(text.c_str(), "%u.%u.%u.%u%n", &aa, &bb, &cc, &dd, &n) != 4)
			return false;
		if (n <= 0 || static_cast<size_t>(n) != text.size())
			return false;
		if (aa > 255 || bb > 255 || cc > 255 || dd > 255)
			return false;
		a = aa;
		b = bb;
		c = cc;
		d = dd;
		return true;
	}

	bool IsClashFakeIp(const std::string& ip)
	{
		unsigned a = 0, b = 0, c = 0, d = 0;
		if (!ParseIpv4(ip, a, b, c, d))
			return false;
		// Clash/mihomo fake-ip default: 198.18.0.0/16 (sometimes documented as /15).
		return a == 198 && (b == 18 || b == 19);
	}

	std::string HttpGetUtf8(const char* url, const char* headers, int timeoutMs)
	{
		HINTERNET internet = InternetOpenA(
			"AntiZapretDnsResolve",
			INTERNET_OPEN_TYPE_PRECONFIG,
			nullptr,
			nullptr,
			0);
		if (!internet)
			return {};

		DWORD timeout = static_cast<DWORD>((std::max)(timeoutMs, 1000));
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url,
			headers,
			headers ? static_cast<DWORD>(strlen(headers)) : 0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI
				| INTERNET_FLAG_SECURE | INTERNET_FLAG_KEEP_CONNECTION,
			0);
		if (!request)
		{
			InternetCloseHandle(internet);
			return {};
		}

		std::string body;
		char buffer[4096];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof buffer, &read) && read > 0)
		{
			body.append(buffer, buffer + read);
			if (body.size() > 256 * 1024)
				break;
		}

		InternetCloseHandle(request);
		InternetCloseHandle(internet);
		return body;
	}

	void AppendUniqueIpv4(std::vector<std::string>& out, const std::string& ip)
	{
		if (ip.empty() || IsClashFakeIp(ip))
			return;
		unsigned a = 0, b = 0, c = 0, d = 0;
		if (!ParseIpv4(ip, a, b, c, d))
			return;
		if (std::find(out.begin(), out.end(), ip) != out.end())
			return;
		out.push_back(ip);
	}

	void CollectIpv4FromDnsJson(const std::string& json, std::vector<std::string>& out)
	{
		// Google/Cloudflare DNS JSON: "data":"a.b.c.d" for A records.
		size_t pos = 0;
		while (pos < json.size())
		{
			const size_t key = json.find("\"data\"", pos);
			if (key == std::string::npos)
				break;
			const size_t colon = json.find(':', key + 6);
			if (colon == std::string::npos)
				break;
			const size_t q1 = json.find('"', colon + 1);
			if (q1 == std::string::npos)
				break;
			const size_t q2 = json.find('"', q1 + 1);
			if (q2 == std::string::npos)
				break;
			AppendUniqueIpv4(out, json.substr(q1 + 1, q2 - q1 - 1));
			pos = q2 + 1;
		}
	}

	std::string JoinIps(const std::vector<std::string>& ips)
	{
		std::string out;
		for (const auto& ip : ips)
		{
			if (!out.empty())
				out += ", ";
			out += ip;
		}
		return out;
	}

	std::string ResolveViaDoh(const std::string& host)
	{
		std::vector<std::string> ips;

		// Prefer Google JSON API (no special Accept header).
		{
			char url[512];
			snprintf(url, sizeof url, "https://dns.google/resolve?name=%s&type=A", host.c_str());
			CollectIpv4FromDnsJson(HttpGetUtf8(url, nullptr, 4000), ips);
		}
		if (!ips.empty())
			return JoinIps(ips);

		// Cloudflare DoH JSON fallback.
		{
			char url[512];
			snprintf(url, sizeof url, "https://cloudflare-dns.com/dns-query?name=%s&type=A", host.c_str());
			CollectIpv4FromDnsJson(
				HttpGetUtf8(url, "Accept: application/dns-json\r\n", 4000),
				ips);
		}
		return JoinIps(ips);
	}

	std::string ResolveViaGetAddrInfo(const std::string& host)
	{
		EnsureWinsock();

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* result = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
			return {};

		std::vector<std::string> ips;
		for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next)
		{
			if (!entry->ai_addr || entry->ai_family != AF_INET)
				continue;
			char ipBuf[INET_ADDRSTRLEN] = {};
			const auto* addr = reinterpret_cast<sockaddr_in*>(entry->ai_addr);
			if (inet_ntop(AF_INET, &addr->sin_addr, ipBuf, sizeof(ipBuf)))
				AppendUniqueIpv4(ips, ipBuf);
		}
		freeaddrinfo(result);
		return JoinIps(ips);
	}
}

std::string VpnNodeProbe::ResolveHostIpv4(const std::string& host)
{
	if (host.empty())
		return {};

	// System getaddrinfo often returns Clash fake-ip (198.18.x.x) while VPN runs.
	// DoH over HTTPS bypasses DNS hijack and returns real A records.
	std::string resolved = ResolveViaDoh(host);
	if (!resolved.empty())
		return resolved;
	return ResolveViaGetAddrInfo(host);
}

int VpnNodeProbe::TcpPingMs(const std::string& host, int port, int timeoutMs)
{
	if (host.empty() || port <= 0 || port > 65535)
		return -1;

	EnsureWinsock();

	// With mihomo TUN + fake-ip, system DNS returns 198.18.x.x and connect()
	// completes locally against Clash (~10–20 ms) — not real RTT from the PC.
	// Resolve a real public IPv4 (DoH) and TCP-connect to that address directly.
	std::string connectIp;
	unsigned a = 0, b = 0, c = 0, d = 0;
	if (ParseIpv4(host, a, b, c, d) && !IsClashFakeIp(host))
	{
		connectIp = host;
	}
	else
	{
		std::string resolved = ResolveHostIpv4(host);
		const size_t comma = resolved.find(',');
		if (comma != std::string::npos)
			resolved = resolved.substr(0, comma);
		// trim spaces
		while (!resolved.empty() && (resolved.front() == ' ' || resolved.front() == '\t'))
			resolved.erase(resolved.begin());
		while (!resolved.empty() && (resolved.back() == ' ' || resolved.back() == '\t'))
			resolved.pop_back();
		if (!resolved.empty() && !IsClashFakeIp(resolved) && ParseIpv4(resolved, a, b, c, d))
			connectIp = resolved;
	}

	if (connectIp.empty())
		return -1;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(port));
	if (inet_pton(AF_INET, connectIp.c_str(), &addr.sin_addr) != 1)
		return -1;

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
		return -1;

	SetSocketTimeouts(sock, timeoutMs);
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);

	int responseMs = -1;
	const auto started = std::chrono::steady_clock::now();
	const int connectResult = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	if (connectResult == 0)
	{
		responseMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count());
	}
	else if (WSAGetLastError() == WSAEWOULDBLOCK)
	{
		fd_set writeSet;
		FD_ZERO(&writeSet);
		FD_SET(sock, &writeSet);
		timeval tv = {};
		tv.tv_sec = timeoutMs / 1000;
		tv.tv_usec = (timeoutMs % 1000) * 1000;
		if (select(0, nullptr, &writeSet, nullptr, &tv) > 0)
		{
			int soError = 0;
			int soLen = sizeof(soError);
			getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLen);
			if (soError == 0)
			{
				responseMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started).count());
			}
		}
	}

	closesocket(sock);
	return responseMs;
}

int VpnNodeProbe::HttpRealPingMs(
	const std::string& proxyHost,
	int proxyPort,
	const char* url,
	int timeoutMs)
{
	if (proxyHost.empty() || proxyPort <= 0 || !url || !url[0])
		return -1;

	// v2rayN ConnectionHandler.GetRealPingTime: HTTP GET via proxy, 2 samples, keep min.
	char proxyBuf[128];
	snprintf(proxyBuf, sizeof proxyBuf, "%s:%d", proxyHost.c_str(), proxyPort);

	HINTERNET internet = InternetOpenA(
		"AntiZapretRealPing",
		INTERNET_OPEN_TYPE_PROXY,
		proxyBuf,
		nullptr,
		0);
	if (!internet)
		return -1;

	DWORD connectTimeout = 3000; // v2rayN ConnectTimeout = 3s
	DWORD overallTimeout = static_cast<DWORD>((std::max)(timeoutMs, 1000));
	InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
	InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &overallTimeout, sizeof(overallTimeout));
	InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &connectTimeout, sizeof(connectTimeout));

	int bestMs = -1;
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		const auto started = std::chrono::steady_clock::now();
		HINTERNET request = InternetOpenUrlA(
			internet,
			url,
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI
				| INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_COOKIES,
			0);
		if (request)
		{
			char discard[512];
			DWORD read = 0;
			InternetReadFile(request, discard, sizeof discard, &read);
			InternetCloseHandle(request);

			const int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count());
			if (elapsedMs > 0 && (bestMs < 0 || elapsedMs < bestMs))
				bestMs = elapsedMs;
		}

		if (attempt == 0)
			Sleep(100); // v2rayN delay between samples
	}

	InternetCloseHandle(internet);
	return bestMs;
}

float VpnNodeProbe::MeasureDownloadPeakMBps(
	const std::string& proxyHost,
	int proxyPort,
	const char* url,
	int timeoutMs,
	std::atomic_bool* cancelFlag)
{
	if (proxyHost.empty() || proxyPort <= 0 || !url || !url[0])
		return -1.f;

	// v2rayN: download via local proxy, track peak BytesPerSecondSpeed, report MB/s (/1e6).
	char proxyBuf[128];
	snprintf(proxyBuf, sizeof proxyBuf, "%s:%d", proxyHost.c_str(), proxyPort);

	HINTERNET internet = InternetOpenA(
		"AntiZapretVpnProbe",
		INTERNET_OPEN_TYPE_PROXY,
		proxyBuf,
		nullptr,
		0);
	if (!internet)
		return -1.f;

	// ConnectTimeout ≈ Clamp(timeout/5, 2, 5) seconds like DownloaderHelper.
	const int connectSec = (std::max)(2, (std::min)(5, timeoutMs / 5000));
	DWORD connectTimeout = static_cast<DWORD>(connectSec * 1000);
	DWORD blockTimeout = static_cast<DWORD>((std::max)(timeoutMs, 10000));
	InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
	InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &blockTimeout, sizeof(blockTimeout));
	InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &connectTimeout, sizeof(connectTimeout));

	HINTERNET request = InternetOpenUrlA(
		internet,
		url,
		nullptr,
		0,
		INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION,
		0);
	if (!request)
	{
		InternetCloseHandle(internet);
		return -1.f;
	}

	const auto started = std::chrono::steady_clock::now();
	auto sampleStart = started;
	unsigned long long total = 0;
	unsigned long long sampleBytes = 0;
	double maxBytesPerSec = 0.0;

	char buffer[16384];
	DWORD read = 0;
	while (InternetReadFile(request, buffer, sizeof buffer, &read) && read > 0)
	{
		if (cancelFlag && cancelFlag->load())
		{
			maxBytesPerSec = 0.0;
			break;
		}

		total += read;
		sampleBytes += read;

		const auto now = std::chrono::steady_clock::now();
		const auto sampleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sampleStart).count();
		if (sampleMs >= 200)
		{
			const double bps = (static_cast<double>(sampleBytes) * 1000.0) / static_cast<double>(sampleMs);
			if (bps > maxBytesPerSec)
				maxBytesPerSec = bps;
			sampleBytes = 0;
			sampleStart = now;
		}

		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
		if (elapsedMs >= timeoutMs)
			break;
	}

	// Flush last partial sample window.
	{
		const auto now = std::chrono::steady_clock::now();
		const auto sampleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sampleStart).count();
		if (sampleMs > 0 && sampleBytes > 0)
		{
			const double bps = (static_cast<double>(sampleBytes) * 1000.0) / static_cast<double>(sampleMs);
			if (bps > maxBytesPerSec)
				maxBytesPerSec = bps;
		}
	}

	InternetCloseHandle(request);
	InternetCloseHandle(internet);

	if (cancelFlag && cancelFlag->load())
		return -1.f;
	if (total < 64 * 1024 || maxBytesPerSec <= 0.0)
		return -1.f;

	// v2rayN: maxSpeed / 1000 / 1000 → MB/s
	return static_cast<float>(maxBytesPerSec / 1000.0 / 1000.0);
}

bool VpnNodeProbe::CopyUtf8ToClipboard(const std::string& text)
{
	if (text.empty() || !OpenClipboard(nullptr))
		return false;

	EmptyClipboard();
	const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wideLen <= 0)
	{
		CloseClipboard();
		return false;
	}

	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLen) * sizeof(wchar_t));
	if (!mem)
	{
		CloseClipboard();
		return false;
	}

	wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
	if (!dst)
	{
		GlobalFree(mem);
		CloseClipboard();
		return false;
	}

	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dst, wideLen);
	GlobalUnlock(mem);
	const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
	if (!ok)
		GlobalFree(mem);
	CloseClipboard();
	return ok;
}

std::string VpnNodeProbe::BuildOutboundJson(const VpnNode& node)
{
	auto escape = [](const std::string& value)
	{
		std::string out;
		out.reserve(value.size() + 8);
		for (char ch : value)
		{
			if (ch == '\\' || ch == '"')
				out.push_back('\\');
			out.push_back(ch);
		}
		return out;
	};

	std::ostringstream json;
	json << "{\n"
		<< "  \"name\": \"" << escape(node.name) << "\",\n"
		<< "  \"scheme\": \"" << escape(node.scheme) << "\",\n"
		<< "  \"server\": \"" << escape(node.server) << "\",\n"
		<< "  \"port\": " << node.port << ",\n"
		<< "  \"tls\": " << (node.tls ? "true" : "false") << ",\n"
		<< "  \"uri\": \"" << escape(node.originalUri) << "\"\n"
		<< "}\n";
	return json.str();
}

std::string VpnNodeProbe::NowTimeLabel()
{
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	char buf[32];
	snprintf(buf, sizeof buf, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
	return buf;
}
