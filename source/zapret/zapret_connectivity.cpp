#include "zapret/zapret_connectivity.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <WinInet.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace ZapretConnectivity
{
namespace
{
	constexpr int kMtProxyHandshakeLen = 64;
	constexpr int kMtProxySkipLen = 8;
	constexpr int kMtProxyPrekeyLen = 32;
	constexpr int kMtProxyIvLen = 16;
	constexpr int kMtProxyProtoTagPos = 56;
	constexpr int kMtProxyDcPos = 60;
	constexpr uint32_t kMtProxyProtoPaddedIntermediate = 0xDDDDDDDD;

	void EnsureWinsock()
	{
		static std::once_flag once;
		std::call_once(once, []()
		{
			WSADATA wsa = {};
			WSAStartup(MAKEWORD(2, 2), &wsa);
		});
	}

	bool Sha256(const uint8_t* data, size_t size, uint8_t out[32])
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
			return false;

		BCRYPT_HASH_HANDLE hash = nullptr;
		NTSTATUS status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status != 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return false;
		}

		status = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
		if (status == 0)
			status = BCryptFinishHash(hash, out, 32, 0);

		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return status == 0;
	}

	bool Aes256Ctr(uint8_t* data, size_t size, const uint8_t key[32], uint8_t iv[16])
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
			return false;

		static const wchar_t kCtrMode[] = L"ChainingModeCTR";
		if (BCryptSetProperty(
			algorithm,
			BCRYPT_CHAINING_MODE,
			reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(kCtrMode)),
			static_cast<ULONG>((wcslen(kCtrMode) + 1) * sizeof(wchar_t)),
			0) != 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return false;
		}

		BCRYPT_KEY_HANDLE keyHandle = nullptr;
		if (BCryptGenerateSymmetricKey(algorithm, &keyHandle, nullptr, 0, const_cast<PUCHAR>(key), 32, 0) != 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return false;
		}

		ULONG resultSize = 0;
		const NTSTATUS status = BCryptEncrypt(
			keyHandle,
			data,
			static_cast<ULONG>(size),
			nullptr,
			iv,
			16,
			data,
			static_cast<ULONG>(size),
			&resultSize,
			0);

		BCryptDestroyKey(keyHandle);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return status == 0;
	}

	bool IsReservedHandshake(const uint8_t* header)
	{
		if (header[0] == 0xEF)
			return true;

		const uint32_t first = *reinterpret_cast<const uint32_t*>(header);
		if (first == 0x44414548u || first == 0x54534F50u || first == 0x20544547u || first == 0x4954504Fu
			|| first == 0xDDDDDDDDu || first == 0xEEEEEEEEu || first == 0x02010316u)
		{
			return true;
		}

		const uint32_t second = *reinterpret_cast<const uint32_t*>(header + 4);
		return second == 0;
	}

	bool BuildMtProxyHandshake(uint8_t out[64], const uint8_t secret[16])
	{
		uint8_t header[kMtProxyHandshakeLen] = {};
		for (int attempt = 0; attempt < 32; ++attempt)
		{
			if (BCryptGenRandom(nullptr, header, sizeof(header), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
				return false;
			if (!IsReservedHandshake(header))
				break;
		}

		*reinterpret_cast<uint32_t*>(header + kMtProxyProtoTagPos) = kMtProxyProtoPaddedIntermediate;
		*reinterpret_cast<int16_t*>(header + kMtProxyDcPos) = 2;

		uint8_t hashInput[kMtProxyPrekeyLen + 16] = {};
		memcpy(hashInput, header + kMtProxySkipLen, kMtProxyPrekeyLen);
		memcpy(hashInput + kMtProxyPrekeyLen, secret, 16);

		uint8_t key[kMtProxyPrekeyLen] = {};
		if (!Sha256(hashInput, sizeof(hashInput), key))
			return false;

		uint8_t iv[kMtProxyIvLen] = {};
		memcpy(iv, header + kMtProxySkipLen + kMtProxyPrekeyLen, kMtProxyIvLen);

		uint8_t encrypted[kMtProxyHandshakeLen] = {};
		memcpy(encrypted, header, kMtProxyHandshakeLen);
		if (!Aes256Ctr(encrypted, kMtProxyHandshakeLen, key, iv))
			return false;

		memcpy(out, header, kMtProxyProtoTagPos);
		memcpy(out + kMtProxyProtoTagPos, encrypted + kMtProxyProtoTagPos, kMtProxyHandshakeLen - kMtProxyProtoTagPos);
		return true;
	}

	std::string NormalizeProbeHost(const char* host)
	{
		if (!host || host[0] == '\0' || strcmp(host, "0.0.0.0") == 0)
			return "127.0.0.1";
		return host;
	}

	bool WaitForSocketReady(SOCKET socket, int timeoutMs)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socket, &readSet);

		fd_set exceptSet;
		FD_ZERO(&exceptSet);
		FD_SET(socket, &exceptSet);

		timeval timeout = {};
		timeout.tv_sec = timeoutMs / 1000;
		timeout.tv_usec = (timeoutMs % 1000) * 1000;

		const int ready = select(0, &readSet, nullptr, &exceptSet, &timeout);
		if (ready < 0)
			return false;
		if (FD_ISSET(socket, &exceptSet))
			return false;
		if (!FD_ISSET(socket, &readSet))
			return false;

		char buffer[1] = {};
		const int received = recv(socket, buffer, sizeof(buffer), MSG_PEEK);
		return received > 0;
	}

	bool ProbeUrl(const char* url, DWORD timeoutMs)
	{
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
			return false;

		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url,
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_AUTO_REDIRECT,
			0);

		if (!request)
		{
			InternetCloseHandle(internet);
			return false;
		}

		DWORD statusCode = 0;
		DWORD statusLen = sizeof(statusCode);
		const bool hasStatus = HttpQueryInfoA(
			request,
			HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
			&statusCode,
			&statusLen,
			nullptr);

		bool ok = false;
		if (hasStatus && statusCode >= 200 && statusCode < 500)
			ok = true;
		else
		{
			char buffer[4] = {};
			DWORD read = 0;
			ok = InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0;
		}

		InternetCloseHandle(request);
		InternetCloseHandle(internet);
		return ok;
	}
}

bool ProbeDiscord()
{
	return ProbeUrl("https://discord.com", 2500);
}

bool ProbeYouTube()
{
	return ProbeUrl("https://i.ytimg.com", 2500);
}

bool ProbeTelegram()
{
	if (ProbeUrl("https://web.telegram.org", 2500))
		return true;
	return ProbeUrl("https://kws2.web.telegram.org", 2500);
}

bool ProbeTelegramMtProxy(const char* host, int port, const uint8_t secret[16], int timeoutMs)
{
	if (!host || !secret || port <= 0 || port > 65535)
		return false;

	EnsureWinsock();

	uint8_t handshake[kMtProxyHandshakeLen] = {};
	if (!BuildMtProxyHandshake(handshake, secret))
		return false;

	const std::string probeHost = NormalizeProbeHost(host);
	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* resolved = nullptr;
	if (getaddrinfo(probeHost.c_str(), std::to_string(port).c_str(), &hints, &resolved) != 0 || !resolved)
		return false;

	SOCKET socketHandle = INVALID_SOCKET;
	for (addrinfo* entry = resolved; entry != nullptr; entry = entry->ai_next)
	{
		socketHandle = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if (socketHandle == INVALID_SOCKET)
			continue;

		const DWORD connectTimeout = static_cast<DWORD>(timeoutMs);
		setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));
		setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));

		if (connect(socketHandle, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) == 0)
			break;

		closesocket(socketHandle);
		socketHandle = INVALID_SOCKET;
	}
	freeaddrinfo(resolved);

	if (socketHandle == INVALID_SOCKET)
		return false;

	const bool sent = send(socketHandle, reinterpret_cast<const char*>(handshake), kMtProxyHandshakeLen, 0) == kMtProxyHandshakeLen;
	if (!sent)
	{
		closesocket(socketHandle);
		return false;
	}

	const bool proxyAccepted = WaitForSocketReady(socketHandle, timeoutMs);
	closesocket(socketHandle);
	return proxyAccepted;
}

int MeasureIcmpPingMs()
{
	HANDLE icmp = IcmpCreateFile();
	if (icmp == INVALID_HANDLE_VALUE)
		return -1;

	IN_ADDR destinationAddr = {};
	if (InetPtonA(AF_INET, "1.1.1.1", &destinationAddr) != 1)
	{
		IcmpCloseHandle(icmp);
		return -1;
	}

	const IPAddr destination = destinationAddr.S_un.S_addr;
	char sendData[4] = {};
	unsigned char replyBuffer[sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 16] = {};
	const DWORD timeoutMs = 1200;
	const DWORD replies = IcmpSendEcho(
		icmp,
		destination,
		sendData,
		sizeof(sendData),
		nullptr,
		replyBuffer,
		static_cast<DWORD>(sizeof(replyBuffer)),
		timeoutMs);
	IcmpCloseHandle(icmp);

	if (replies == 0)
		return -1;

	const ICMP_ECHO_REPLY* reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuffer);
	if (reply->Status != 0)
		return -1;

	return static_cast<int>(reply->RoundTripTime);
}

}  // namespace ZapretConnectivity
