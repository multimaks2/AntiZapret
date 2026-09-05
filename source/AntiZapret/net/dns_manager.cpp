#include "net/dns_manager.h"

#include "zapret/zapret_connectivity.h"
#include "zapret/zapret_paths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace DnsManager
{
namespace
{
	constexpr PublicServer kPublicServers[] = {
#include "dns_public_servers.inc"
	};

	std::string SockaddrToIp(const SOCKET_ADDRESS& address)
	{
		if (!address.lpSockaddr)
			return {};

		char buffer[INET6_ADDRSTRLEN] = {};
		if (address.lpSockaddr->sa_family == AF_INET)
		{
			const auto* sa = reinterpret_cast<const sockaddr_in*>(address.lpSockaddr);
			if (InetNtopA(AF_INET, &sa->sin_addr, buffer, sizeof buffer))
				return buffer;
		}
		return {};
	}

	bool RunHiddenNetsh(const std::wstring& args, std::string& errorOut)
	{
		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};

		std::wstring cmdLine = L"cmd.exe /C netsh ";
		cmdLine += args;
		std::vector<wchar_t> buffer(cmdLine.begin(), cmdLine.end());
		buffer.push_back(L'\0');

		if (!CreateProcessW(
			nullptr,
			buffer.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&si,
			&pi))
		{
			errorOut = "Не удалось запустить netsh";
			return false;
		}

		WaitForSingleObject(pi.hProcess, 20000);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (exitCode != 0)
		{
			char msg[96] = {};
			snprintf(msg, sizeof msg, "netsh вернул код %lu", static_cast<unsigned long>(exitCode));
			errorOut = msg;
			return false;
		}
		return true;
	}

	std::wstring QuoteName(const std::wstring& name)
	{
		std::wstring out = L"\"";
		for (wchar_t ch : name)
		{
			if (ch == L'"')
				out += L"\\\"";
			else
				out += ch;
		}
		out += L'"';
		return out;
	}

	bool IsIpv4Literal(const char* value)
	{
		if (!value || !value[0])
			return false;
		IN_ADDR addr = {};
		return InetPtonA(AF_INET, value, &addr) == 1;
	}
}

const PublicServer* GetPublicServers(size_t& outCount)
{
	static const std::vector<PublicServer> kFiltered = []() {
		std::vector<PublicServer> list;
		static std::vector<std::string> tagStorage;
		std::unordered_map<std::string, size_t> indexByIp;
		list.reserve(std::size(kPublicServers));
		tagStorage.reserve(std::size(kPublicServers));

		for (const PublicServer& server : kPublicServers)
		{
			if (!IsIpv4Literal(server.ipv4))
				continue;

			const auto found = indexByIp.find(server.ipv4);
			if (found == indexByIp.end())
			{
				indexByIp.emplace(server.ipv4, list.size());
				list.push_back(server);
				tagStorage.emplace_back(server.tags ? server.tags : "");
				continue;
			}

			// Merge search tags from aliases (Xbox/PSN/…) into the first unique IP entry.
			std::string& tags = tagStorage[found->second];
			if (!server.tags || !server.tags[0])
				continue;
			std::unordered_set<std::string> parts;
			auto addParts = [&](const std::string& src) {
				size_t start = 0;
				while (start < src.size())
				{
					const size_t comma = src.find(',', start);
					std::string part = src.substr(
						start,
						comma == std::string::npos ? std::string::npos : comma - start);
					while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
						part.erase(part.begin());
					while (!part.empty() && (part.back() == ' ' || part.back() == '\t'))
						part.pop_back();
					if (!part.empty())
						parts.insert(part);
					if (comma == std::string::npos)
						break;
					start = comma + 1;
				}
			};
			addParts(tags);
			addParts(server.tags);
			tags.clear();
			for (const std::string& part : parts)
			{
				if (!tags.empty())
					tags += ',';
				tags += part;
			}
		}

		for (size_t i = 0; i < list.size(); ++i)
			list[i].tags = tagStorage[i].c_str();

		return list;
	}();

	outCount = kFiltered.size();
	return kFiltered.data();
}

bool RefreshAdapters(std::vector<AdapterInfo>& outAdapters)
{
	outAdapters.clear();

	const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;

	ULONG size = 0;
	DWORD status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
	if (status != ERROR_BUFFER_OVERFLOW || size == 0)
		return false;

	std::vector<BYTE> buffer(size);
	auto* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
	status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &size);
	if (status != NO_ERROR)
		return false;

	for (PIP_ADAPTER_ADDRESSES adapter = addresses; adapter; adapter = adapter->Next)
	{
		if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;

		AdapterInfo info;
		info.ifIndex = adapter->IfIndex;
		info.isUp = adapter->OperStatus == IfOperStatusUp;
		info.isLoopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
		info.isTunnel = adapter->IfType == IF_TYPE_TUNNEL
			|| (adapter->TunnelType != TUNNEL_TYPE_NONE && adapter->TunnelType != 0);
		info.nameWide = adapter->FriendlyName ? adapter->FriendlyName : L"";
		info.name = ZapretPaths::WideToUtf8(info.nameWide);
		if (adapter->Description)
			info.description = ZapretPaths::WideToUtf8(adapter->Description);

		int dnsIndex = 0;
		for (PIP_ADAPTER_DNS_SERVER_ADDRESS dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next)
		{
			const std::string ip = SockaddrToIp(dns->Address);
			if (ip.empty())
				continue;
			if (dnsIndex == 0)
				info.primaryDns = ip;
			else if (dnsIndex == 1)
			{
				info.alternateDns = ip;
				break;
			}
			++dnsIndex;
		}

		if (info.name.empty())
			continue;
		outAdapters.push_back(std::move(info));
	}

	std::stable_sort(outAdapters.begin(), outAdapters.end(), [](const AdapterInfo& a, const AdapterInfo& b) {
		if (a.isUp != b.isUp)
			return a.isUp && !b.isUp;
		if (a.isTunnel != b.isTunnel)
			return !a.isTunnel && b.isTunnel;
		return a.name < b.name;
	});

	return !outAdapters.empty();
}

int FindPreferredAdapterIndex(const std::vector<AdapterInfo>& adapters)
{
	DWORD preferred = 0;
	GetBestInterface(INADDR_ANY, &preferred);

	for (int i = 0; i < static_cast<int>(adapters.size()); ++i)
	{
		if (preferred != 0 && adapters[static_cast<size_t>(i)].ifIndex == preferred)
			return i;
	}

	for (int i = 0; i < static_cast<int>(adapters.size()); ++i)
	{
		const AdapterInfo& a = adapters[static_cast<size_t>(i)];
		if (a.isUp && !a.isTunnel && !a.isLoopback)
			return i;
	}

	return adapters.empty() ? -1 : 0;
}

int MeasureServerLatencyMs(const char* ipv4, int timeoutMs)
{
	return ZapretConnectivity::MeasureIcmpPingMs(ipv4, timeoutMs);
}

bool ApplyStaticDns(
	const std::wstring& adapterName,
	const char* primaryIpv4,
	const char* alternateIpv4,
	std::string& errorOut)
{
	errorOut.clear();
	if (adapterName.empty())
	{
		errorOut = "Адаптер не выбран";
		return false;
	}
	if (!IsIpv4Literal(primaryIpv4))
	{
		errorOut = "Некорректный основной DNS";
		return false;
	}

	const std::wstring quoted = QuoteName(adapterName);
	const std::wstring primaryWide = ZapretPaths::Utf8ToWide(primaryIpv4);

	std::wstring setCmd = L"interface ipv4 set dnsservers name=";
	setCmd += quoted;
	setCmd += L" source=static address=";
	setCmd += primaryWide;
	setCmd += L" register=none validate=no";

	if (!RunHiddenNetsh(setCmd, errorOut))
		return false;

	if (alternateIpv4 && alternateIpv4[0] && IsIpv4Literal(alternateIpv4)
		&& strcmp(alternateIpv4, primaryIpv4) != 0)
	{
		const std::wstring altWide = ZapretPaths::Utf8ToWide(alternateIpv4);
		std::wstring addCmd = L"interface ipv4 add dnsservers name=";
		addCmd += quoted;
		addCmd += L" address=";
		addCmd += altWide;
		addCmd += L" index=2 validate=no";
		if (!RunHiddenNetsh(addCmd, errorOut))
			return false;
	}

	return true;
}

bool ApplyDhcpDns(const std::wstring& adapterName, std::string& errorOut)
{
	errorOut.clear();
	if (adapterName.empty())
	{
		errorOut = "Адаптер не выбран";
		return false;
	}

	std::wstring cmd = L"interface ipv4 set dnsservers name=";
	cmd += QuoteName(adapterName);
	cmd += L" source=dhcp";
	return RunHiddenNetsh(cmd, errorOut);
}

}  // namespace DnsManager
