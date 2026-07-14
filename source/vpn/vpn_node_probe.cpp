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
#include <sstream>

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
}

int VpnNodeProbe::TcpPingMs(const std::string& host, int port, int timeoutMs)
{
	if (host.empty() || port <= 0 || port > 65535)
		return -1;

	EnsureWinsock();

	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* resolved = nullptr;
	const std::string portText = std::to_string(port);
	if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &resolved) != 0 || !resolved)
		return -1;

	int bestMs = -1;
	for (addrinfo* entry = resolved; entry != nullptr; entry = entry->ai_next)
	{
		SOCKET sock = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if (sock == INVALID_SOCKET)
			continue;

		SetSocketTimeouts(sock, timeoutMs);
		u_long nonBlocking = 1;
		ioctlsocket(sock, FIONBIO, &nonBlocking);

		const auto started = std::chrono::steady_clock::now();
		const int connectResult = connect(sock, entry->ai_addr, static_cast<int>(entry->ai_addrlen));
		if (connectResult == 0)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			bestMs = static_cast<int>(elapsed);
			closesocket(sock);
			break;
		}

		if (WSAGetLastError() == WSAEWOULDBLOCK)
		{
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(sock, &writeSet);
			timeval tv = {};
			tv.tv_sec = timeoutMs / 1000;
			tv.tv_usec = (timeoutMs % 1000) * 1000;
			const int ready = select(0, nullptr, &writeSet, nullptr, &tv);
			if (ready > 0)
			{
				int soError = 0;
				int soLen = sizeof(soError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLen);
				if (soError == 0)
				{
					const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - started).count();
					bestMs = static_cast<int>(elapsed);
					closesocket(sock);
					break;
				}
			}
		}

		closesocket(sock);
	}

	freeaddrinfo(resolved);
	return bestMs;
}

float VpnNodeProbe::MeasureDownloadMbps(
	const std::string& proxyHost,
	int proxyPort,
	const char* url,
	int timeoutMs,
	std::atomic_bool* cancelFlag)
{
	if (proxyHost.empty() || proxyPort <= 0 || !url || !url[0])
		return -1.f;

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

	DWORD connectTimeout = static_cast<DWORD>((std::max)(timeoutMs, 1000));
	InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
	InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
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
	char buffer[16384];
	DWORD read = 0;
	unsigned long long total = 0;
	while (InternetReadFile(request, buffer, sizeof buffer, &read) && read > 0)
	{
		if (cancelFlag && cancelFlag->load())
		{
			total = 0;
			break;
		}
		total += read;
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		if (elapsedMs >= timeoutMs || total >= 12ull * 1024ull * 1024ull)
			break;
	}

	InternetCloseHandle(request);
	InternetCloseHandle(internet);

	if (cancelFlag && cancelFlag->load())
		return -1.f;

	const auto elapsed = std::chrono::steady_clock::now() - started;
	const double seconds = std::chrono::duration<double>(elapsed).count();
	if (total < 64 * 1024 || seconds < 0.05)
		return -1.f;

	return static_cast<float>((static_cast<double>(total) * 8.0) / (seconds * 1000.0 * 1000.0));
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
