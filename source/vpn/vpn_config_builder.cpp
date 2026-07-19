#include "vpn/vpn_config_builder.h"

#include "app/app_settings.h"
#include "vpn/vpn_domain_routes.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_routing.h"
#include "vpn/vpn_rules_updater.h"
#include "vpn/vpn_service_routes.h"
#include "vpn/vpn_transport_settings.h"

#include <Windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "crypt32.lib")

namespace
{
	std::string YamlQuote(const std::string& value)
	{
		std::string escaped;
		escaped.reserve(value.size() + 8);
		for (char ch : value)
		{
			if (ch == '\\' || ch == '"')
				escaped.push_back('\\');
			escaped.push_back(ch);
		}
		return "\"" + escaped + "\"";
	}

	std::string GetQueryParam(const std::string& query, const char* key)
	{
		const std::string pattern = std::string(key) + "=";
		const size_t pos = query.find(pattern);
		if (pos == std::string::npos)
			return {};

		size_t start = pos + pattern.size();
		const size_t end = query.find('&', start);
		const std::string raw = end == std::string::npos ? query.substr(start) : query.substr(start, end - start);

		std::string result;
		result.reserve(raw.size());
		for (size_t i = 0; i < raw.size(); ++i)
		{
			if (raw[i] == '%' && i + 2 < raw.size())
			{
				const char hex[] = { raw[i + 1], raw[i + 2], '\0' };
				result.push_back(static_cast<char>(strtoul(hex, nullptr, 16)));
				i += 2;
			}
			else if (raw[i] == '+')
				result.push_back(' ');
			else
				result.push_back(raw[i]);
		}
		return result;
	}

	std::string JsonStringFieldLoose(const std::string& json, const char* key)
	{
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return {};

		size_t start = pos + pattern.size();
		while (start < json.size() && (json[start] == ' ' || json[start] == '\t'))
			++start;

		if (start < json.size() && json[start] == '"')
		{
			++start;
			std::string result;
			while (start < json.size() && json[start] != '"')
				result.push_back(json[start++]);
			return result;
		}

		std::string result;
		while (start < json.size() && json[start] != ',' && json[start] != '}' && json[start] != '\n' && json[start] != '\r')
			result.push_back(json[start++]);
		return result;
	}

	std::string DecodeVmessPayload(const std::string& uri)
	{
		const size_t schemePos = uri.find("://");
		if (schemePos == std::string::npos)
			return {};

		std::string payload = uri.substr(schemePos + 3);
		if (payload.empty())
			return {};

		if (payload.front() == '{')
			return payload;

		VpnNode temp;
		std::string error;
		if (VpnImport::ParseShareLink(uri, temp, 1, error))
		{
			// ParseShareLink already validated payload; re-read by asking import path indirectly.
		}

		std::string normalized;
		normalized.reserve(payload.size());
		for (char ch : payload)
		{
			if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
				continue;
			if (ch == '-')
				normalized.push_back('+');
			else if (ch == '_')
				normalized.push_back('/');
			else
				normalized.push_back(ch);
		}
		while (normalized.size() % 4 != 0)
			normalized.push_back('=');

		DWORD required = 0;
		if (!CryptStringToBinaryA(
				normalized.c_str(),
				static_cast<DWORD>(normalized.size()),
				CRYPT_STRING_BASE64,
				nullptr,
				&required,
				nullptr,
				nullptr))
			return {};

		std::string decoded(required, '\0');
		if (!CryptStringToBinaryA(
				normalized.c_str(),
				static_cast<DWORD>(normalized.size()),
				CRYPT_STRING_BASE64,
				reinterpret_cast<BYTE*>(decoded.data()),
				&required,
				nullptr,
				nullptr))
			return {};

		decoded.resize(required);
		return decoded;
	}

	bool BuildTrojanProxyYaml(const VpnNode& node, const std::string& proxyName, std::string& outYaml, std::string& outError)
	{
		const std::string uri = node.originalUri;
		const size_t schemePos = uri.find("://");
		if (schemePos == std::string::npos)
		{
			outError = "Некорректная trojan:// ссылка.";
			return false;
		}

		std::string rest = uri.substr(schemePos + 3);
		const size_t hashPos = rest.find('#');
		if (hashPos != std::string::npos)
			rest = rest.substr(0, hashPos);

		std::string query;
		const size_t queryPos = rest.find('?');
		if (queryPos != std::string::npos)
		{
			query = rest.substr(queryPos + 1);
			rest = rest.substr(0, queryPos);
		}

		const size_t atPos = rest.rfind('@');
		if (atPos == std::string::npos)
		{
			outError = "Некорректная trojan:// ссылка.";
			return false;
		}

		const std::string password = rest.substr(0, atPos);
		const std::string security = GetQueryParam(query, "security");
		const std::string sni = GetQueryParam(query, "sni");
		const std::string fp = GetQueryParam(query, "fp");
		const std::string pbk = GetQueryParam(query, "pbk");
		const std::string sid = GetQueryParam(query, "sid");

		std::ostringstream yaml;
		yaml << "  - name: " << YamlQuote(proxyName) << "\n";
		yaml << "    type: trojan\n";
		yaml << "    server: " << YamlQuote(node.server) << "\n";
		yaml << "    port: " << node.port << "\n";
		yaml << "    password: " << YamlQuote(password) << "\n";
		yaml << "    udp: true\n";

		if (!sni.empty())
			yaml << "    sni: " << YamlQuote(sni) << "\n";
		if (!fp.empty())
			yaml << "    client-fingerprint: " << YamlQuote(fp) << "\n";
		if (security == "reality")
		{
			yaml << "    network: tcp\n";
			yaml << "    reality-opts:\n";
			if (!pbk.empty())
				yaml << "      public-key: " << YamlQuote(pbk) << "\n";
			if (!sid.empty())
				yaml << "      short-id: " << YamlQuote(sid) << "\n";
		}
		else if (node.tls)
		{
			yaml << "    skip-cert-verify: true\n";
		}

		outYaml = yaml.str();
		return true;
	}

	bool BuildVmessProxyYaml(const VpnNode& node, const std::string& proxyName, std::string& outYaml, std::string& outError)
	{
		const std::string json = DecodeVmessPayload(node.originalUri);
		if (json.empty())
		{
			outError = "Не удалось декодировать vmess://.";
			return false;
		}

		const std::string uuid = JsonStringFieldLoose(json, "id");
		const std::string net = JsonStringFieldLoose(json, "net");
		const std::string host = JsonStringFieldLoose(json, "host");
		const std::string path = JsonStringFieldLoose(json, "path");
		const std::string tls = JsonStringFieldLoose(json, "tls");
		const std::string cipher = JsonStringFieldLoose(json, "scy");
		const std::string headerType = JsonStringFieldLoose(json, "type");

		if (uuid.empty())
		{
			outError = "vmess:// не содержит id.";
			return false;
		}

		std::ostringstream yaml;
		yaml << "  - name: " << YamlQuote(proxyName) << "\n";
		yaml << "    type: vmess\n";
		yaml << "    server: " << YamlQuote(node.server) << "\n";
		yaml << "    port: " << node.port << "\n";
		yaml << "    uuid: " << YamlQuote(uuid) << "\n";
		yaml << "    alterId: 0\n";
		yaml << "    cipher: " << YamlQuote(cipher.empty() ? "auto" : cipher) << "\n";
		yaml << "    udp: true\n";

		if (net == "ws")
		{
			yaml << "    network: ws\n";
			yaml << "    ws-opts:\n";
			yaml << "      path: " << YamlQuote(path.empty() ? "/" : path) << "\n";
			if (!host.empty())
				yaml << "      headers:\n        Host: " << YamlQuote(host) << "\n";
		}
		else if (headerType == "http" || net == "http")
		{
			yaml << "    network: http\n";
			yaml << "    http-opts:\n";
			yaml << "      method: GET\n";
			yaml << "      path:\n        - " << YamlQuote(path.empty() ? "/" : path) << "\n";
			yaml << "      headers:\n";
			yaml << "        Connection:\n          - keep-alive\n";
			if (!host.empty())
				yaml << "        Host:\n          - " << YamlQuote(host) << "\n";
		}
		else
		{
			yaml << "    network: " << YamlQuote(net.empty() ? "tcp" : net) << "\n";
		}

		if (tls == "tls")
		{
			yaml << "    tls: true\n";
			yaml << "    skip-cert-verify: true\n";
			if (!host.empty())
				yaml << "    servername: " << YamlQuote(host) << "\n";
		}

		outYaml = yaml.str();
		return true;
	}

	bool BuildVlessProxyYaml(const VpnNode& node, const std::string& proxyName, std::string& outYaml, std::string& outError)
	{
		const std::string uri = node.originalUri;
		const size_t schemePos = uri.find("://");
		if (schemePos == std::string::npos)
		{
			outError = "Некорректная vless:// ссылка.";
			return false;
		}

		std::string rest = uri.substr(schemePos + 3);
		const size_t hashPos = rest.find('#');
		if (hashPos != std::string::npos)
			rest = rest.substr(0, hashPos);

		std::string query;
		const size_t queryPos = rest.find('?');
		if (queryPos != std::string::npos)
		{
			query = rest.substr(queryPos + 1);
			rest = rest.substr(0, queryPos);
		}

		const size_t atPos = rest.rfind('@');
		if (atPos == std::string::npos)
		{
			outError = "Некорректная vless:// ссылка (нет uuid@host).";
			return false;
		}

		const std::string uuid = rest.substr(0, atPos);
		if (uuid.empty() || node.server.empty() || node.port <= 0)
		{
			outError = "vless://: пустой uuid/server/port.";
			return false;
		}

		const std::string security = GetQueryParam(query, "security");
		const std::string network = GetQueryParam(query, "type");
		const std::string flow = GetQueryParam(query, "flow");
		const std::string sni = GetQueryParam(query, "sni");
		const std::string fp = GetQueryParam(query, "fp");
		const std::string pbk = GetQueryParam(query, "pbk");
		const std::string sid = GetQueryParam(query, "sid");
		const std::string spx = GetQueryParam(query, "spx");
		const std::string path = GetQueryParam(query, "path");
		const std::string hostHeader = GetQueryParam(query, "host");
		const std::string alpn = GetQueryParam(query, "alpn");
		const std::string packetEncoding = GetQueryParam(query, "packetEncoding");
		const std::string encryption = GetQueryParam(query, "encryption");

		std::ostringstream yaml;
		yaml << "  - name: " << YamlQuote(proxyName) << "\n";
		yaml << "    type: vless\n";
		yaml << "    server: " << YamlQuote(node.server) << "\n";
		yaml << "    port: " << node.port << "\n";
		yaml << "    uuid: " << YamlQuote(uuid) << "\n";
		yaml << "    udp: true\n";
		if (!encryption.empty() && encryption != "none")
			yaml << "    encryption: " << YamlQuote(encryption) << "\n";
		if (!flow.empty())
			yaml << "    flow: " << YamlQuote(flow) << "\n";
		if (!packetEncoding.empty())
			yaml << "    packet-encoding: " << YamlQuote(packetEncoding) << "\n";
		else if (!flow.empty())
			yaml << "    packet-encoding: xudp\n";

		const std::string net = network.empty() ? "tcp" : network;
		yaml << "    network: " << YamlQuote(net) << "\n";

		if (net == "ws")
		{
			yaml << "    ws-opts:\n";
			yaml << "      path: " << YamlQuote(path.empty() ? "/" : path) << "\n";
			if (!hostHeader.empty())
				yaml << "      headers:\n        Host: " << YamlQuote(hostHeader) << "\n";
		}
		else if (net == "grpc")
		{
			yaml << "    grpc-opts:\n";
			yaml << "      grpc-service-name: " << YamlQuote(path) << "\n";
		}

		const bool useTls = security == "tls" || security == "reality" || node.tls || node.port == 443;
		if (useTls)
		{
			yaml << "    tls: true\n";
			if (!sni.empty())
				yaml << "    servername: " << YamlQuote(sni) << "\n";
			else if (!hostHeader.empty())
				yaml << "    servername: " << YamlQuote(hostHeader) << "\n";
			if (!fp.empty())
				yaml << "    client-fingerprint: " << YamlQuote(fp) << "\n";
			if (!alpn.empty())
			{
				yaml << "    alpn:\n";
				yaml << "      - " << YamlQuote(alpn) << "\n";
			}
			if (security == "reality")
			{
				yaml << "    reality-opts:\n";
				if (!pbk.empty())
					yaml << "      public-key: " << YamlQuote(pbk) << "\n";
				if (!sid.empty())
					yaml << "      short-id: " << YamlQuote(sid) << "\n";
			}
			else
			{
				yaml << "    skip-cert-verify: true\n";
			}
		}

		(void)spx;
		outYaml = yaml.str();
		return true;
	}

	bool BuildHysteria2ProxyYaml(const VpnNode& node, const std::string& proxyName, std::string& outYaml, std::string& outError)
	{
		const std::string uri = node.originalUri;
		const size_t schemePos = uri.find("://");
		if (schemePos == std::string::npos)
		{
			outError = "Некорректная hysteria2:// ссылка.";
			return false;
		}

		std::string rest = uri.substr(schemePos + 3);
		const size_t hashPos = rest.find('#');
		if (hashPos != std::string::npos)
			rest = rest.substr(0, hashPos);

		std::string query;
		const size_t queryPos = rest.find('?');
		if (queryPos != std::string::npos)
		{
			query = rest.substr(queryPos + 1);
			rest = rest.substr(0, queryPos);
		}

		const size_t atPos = rest.rfind('@');
		if (atPos == std::string::npos)
		{
			outError = "Некорректная hysteria2:// ссылка (нет password@host).";
			return false;
		}

		std::string password = rest.substr(0, atPos);
		// Url-decode password (import may encode it).
		{
			std::string decoded;
			decoded.reserve(password.size());
			for (size_t i = 0; i < password.size(); ++i)
			{
				if (password[i] == '%' && i + 2 < password.size())
				{
					const char hex[] = { password[i + 1], password[i + 2], '\0' };
					decoded.push_back(static_cast<char>(strtoul(hex, nullptr, 16)));
					i += 2;
				}
				else if (password[i] == '+')
					decoded.push_back(' ');
				else
					decoded.push_back(password[i]);
			}
			password = decoded;
		}

		if (password.empty() || node.server.empty() || node.port <= 0)
		{
			outError = "hysteria2://: пустой password/server/port.";
			return false;
		}

		const std::string sni = GetQueryParam(query, "sni");
		const std::string fp = GetQueryParam(query, "fp");
		const std::string obfs = GetQueryParam(query, "obfs");
		const std::string obfsPassword = GetQueryParam(query, "obfs-password");

		std::ostringstream yaml;
		yaml << "  - name: " << YamlQuote(proxyName) << "\n";
		yaml << "    type: hysteria2\n";
		yaml << "    server: " << YamlQuote(node.server) << "\n";
		yaml << "    port: " << node.port << "\n";
		yaml << "    password: " << YamlQuote(password) << "\n";
		if (!sni.empty())
			yaml << "    sni: " << YamlQuote(sni) << "\n";
		if (!fp.empty())
			yaml << "    client-fingerprint: " << YamlQuote(fp) << "\n";
		if (!obfs.empty())
			yaml << "    obfs: " << YamlQuote(obfs) << "\n";
		if (!obfsPassword.empty())
			yaml << "    obfs-password: " << YamlQuote(obfsPassword) << "\n";
		yaml << "    skip-cert-verify: true\n";

		outYaml = yaml.str();
		return true;
	}

	bool BuildProxyYaml(const VpnNode& node, const std::string& proxyName, std::string& outYaml, std::string& outError)
	{
		if (node.scheme == "trojan")
			return BuildTrojanProxyYaml(node, proxyName, outYaml, outError);
		if (node.scheme == "vmess")
			return BuildVmessProxyYaml(node, proxyName, outYaml, outError);
		if (node.scheme == "vless")
			return BuildVlessProxyYaml(node, proxyName, outYaml, outError);
		if (node.scheme == "hysteria2")
			return BuildHysteria2ProxyYaml(node, proxyName, outYaml, outError);

		outError = "Неподдерживаемый протокол для запуска VPN: " + node.scheme
			+ ". Поддерживаются trojan, vmess, vless, hysteria2.";
		return false;
	}

	void AppendDnsBlock(
		std::ostringstream& yaml,
		const VpnStoreSettings& transport,
		VpnRoutingPreset preset,
		bool useTun)
	{
		const std::string bootstrap = VpnTransportSettings::ResolveBootstrapEndpoint(
			transport.bootstrapDns,
			transport.bootstrapType);
		const std::string proxyDns = VpnTransportSettings::ResolveProxyEndpoint(
			transport.proxyDns,
			transport.proxyType);

		const bool forceFakeIpForTun = useTun
			&& (preset == VpnRoutingPreset::Ruv1All || preset == VpnRoutingPreset::Custom);
		const bool useFakeIp = transport.dnsMode != 0 || forceFakeIpForTun;
		const bool skipRuDnsFallback = preset == VpnRoutingPreset::Ruv1All
			|| preset == VpnRoutingPreset::Custom;

		if (!useFakeIp)
		{
			yaml << "dns:\n";
			yaml << "  enable: true\n";
			yaml << "  ipv6: false\n";
			yaml << "  enhanced-mode: redir-host\n";
			yaml << "  use-system-hosts: true\n";
			yaml << "  nameserver:\n";
			yaml << "    - " << bootstrap << "\n";
			yaml << "\n";
			return;
		}

		yaml << "dns:\n";
		yaml << "  enable: true\n";
		yaml << "  ipv6: false\n";
		yaml << "  enhanced-mode: fake-ip\n";
		yaml << "  fake-ip-range: 198.18.0.1/16\n";
		yaml << "  default-nameserver:\n";
		yaml << "    - " << bootstrap << "\n";
		yaml << "  nameserver:\n";
		yaml << "    - " << bootstrap << "\n";
		if (!skipRuDnsFallback)
		{
			yaml << "  fallback:\n";
			yaml << "    - " << proxyDns << "\n";
			yaml << "  fallback-filter:\n";
			yaml << "    geoip: true\n";
			yaml << "    geoip-code: RU\n";
		}
		yaml << "  proxy-server-nameserver:\n";
		yaml << "    - " << proxyDns << "\n";
		yaml << "  fake-ip-filter:\n";
		yaml << "    - \"+.stun.*.*\"\n";
		yaml << "    - \"+.stun.*.*.*\"\n";
		yaml << "    - \"*.msftconnecttest.com\"\n";
		yaml << "    - \"*.msftncsi.com\"\n";
		yaml << "    - \"WORKGROUP\"\n";
		yaml << "\n";
	}
}

bool VpnConfigBuilder::WriteRuntimeConfig(
	const VpnNode& node,
	VpnRoutingPreset preset,
	const VpnStoreSettings& transport,
	const std::wstring& mihomoHome,
	int mixedPort,
	int apiPort,
	std::string& outError,
	bool fetchGeosites)
{
	const std::string proxyName = node.name.empty() ? "selected" : node.name;

	std::string proxyYaml;
	if (!BuildProxyYaml(node, proxyName, proxyYaml, outError))
		return false;

	std::ostringstream yaml;
	const bool useTun = transport.transportMode != 0;
	const bool strictRoute = useTun && preset == VpnRoutingPreset::Ruv1All;

	yaml << "mixed-port: " << mixedPort << "\n";
	yaml << "external-controller: 127.0.0.1:" << apiPort << "\n";
	yaml << "allow-lan: false\n";
	yaml << "mode: rule\n";
	yaml << "log-level: warning\n";
	yaml << "ipv6: false\n";
	yaml << "geodata-mode: true\n";
	yaml << "geo-auto-update: false\n";
	yaml << "find-process-mode: " << (useTun ? "strict" : "off") << "\n";
	yaml << "\n";

	yaml << "sniffer:\n";
	yaml << "  enable: true\n";
	if (useTun)
		yaml << "  override-destination: true\n";
	yaml << "  sniff:\n";
	yaml << "    HTTP:\n";
	yaml << "      ports: [80, 8080-8880]\n";
	yaml << "    TLS:\n";
	yaml << "      ports: [443, 8443]\n";
	yaml << "    QUIC:\n";
	yaml << "      ports: [443]\n";
	yaml << "\n";

	yaml << "tun:\n";
	yaml << "  enable: " << (useTun ? "true" : "false") << "\n";
	if (useTun)
	{
		yaml << "  stack: gvisor\n";
		yaml << "  device: AntiZapret\n";
		yaml << "  auto-route: true\n";
		yaml << "  auto-detect-interface: true\n";
		yaml << "  strict-route: " << (strictRoute ? "true" : "false") << "\n";
		yaml << "  dns-hijack:\n";
		yaml << "    - any:53\n";
		yaml << "    - tcp://any:53\n";
	}
	yaml << "\n";

	AppendDnsBlock(yaml, transport, preset, useTun);
	yaml << "proxies:\n";
	yaml << proxyYaml;
	yaml << "\n";
	yaml << "proxy-groups:\n";
	yaml << "  - name: PROXY\n";
	yaml << "    type: select\n";
	yaml << "    proxies:\n";
	yaml << "      - " << YamlQuote(proxyName) << "\n";
	yaml << "\n";

	std::string configBody = yaml.str();
	const std::filesystem::path srssDirectory = std::filesystem::path(mihomoHome) / L"srss";

	VpnCustomRoutingInput customRouting;
	const VpnCustomRoutingInput* customRoutingPtr = nullptr;
	if (preset == VpnRoutingPreset::Custom)
	{
		VpnServiceRoutes::Load(customRouting.services);
		VpnDomainRoutes::Load(customRouting.domains);

		AppSettings appSettings;
		appSettings.Load();
		customRouting.includeAdultServices = appSettings.GetConfirmAdult();

		if (fetchGeosites)
		{
			std::vector<std::string> requiredGeosites;
			VpnServiceRoutes::CollectRequiredGeosites(customRouting.services, requiredGeosites);
			VpnRulesUpdater::EnsureGeositeFiles(requiredGeosites);
		}

		customRoutingPtr = &customRouting;
	}

	const bool hasRuInsideDomainRules = preset == VpnRoutingPreset::Ruv1ExceptRu
		&& std::filesystem::exists(srssDirectory / L"geosite-ru-available-only-inside.srs");
	const bool hasGoogleDomainRules = preset == VpnRoutingPreset::Ruv1Blocked
		&& std::filesystem::exists(srssDirectory / L"geosite-google.srs");
	const bool hasGoogleIpRules = preset == VpnRoutingPreset::Ruv1Blocked
		&& std::filesystem::exists(srssDirectory / L"geoip-google.srs");
	VpnRouting::AppendRuleProviders(configBody, preset, mihomoHome, customRoutingPtr);
	configBody += "\n";
	VpnRouting::AppendRules(
		configBody,
		preset,
		mihomoHome,
		hasRuInsideDomainRules,
		hasGoogleDomainRules,
		hasGoogleIpRules,
		customRoutingPtr,
		transport.fixDiscord);
	configBody += "\n";

	const std::filesystem::path configPath = std::filesystem::path(mihomoHome) / L"config.yaml";
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(mihomoHome), ec);

	std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
	if (!output)
	{
		outError = "Не удалось записать config.yaml.";
		return false;
	}

	output << configBody;
	return output.good();
}

bool VpnConfigBuilder::WriteParallelProbeConfig(
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
	std::string& outError)
{
	outEndpoints.clear();
	if (indices.empty())
	{
		outError = "Нет серверов для параллельного пинга.";
		return false;
	}

	std::ostringstream proxiesYaml;
	std::ostringstream listenersYaml;
	std::ostringstream groupProxiesYaml;
	std::string activeTag;

	for (size_t n = 0; n < indices.size(); ++n)
	{
		const int index = indices[n];
		if (index < 0 || index >= static_cast<int>(nodes.size()))
			continue;

		const VpnNode& node = nodes[static_cast<size_t>(index)];
		char tagBuf[32];
		snprintf(tagBuf, sizeof tagBuf, "az-%d", index);
		const std::string tag = tagBuf;

		std::string proxyYaml;
		std::string buildError;
		if (!BuildProxyYaml(node, tag, proxyYaml, buildError))
			continue;

		proxiesYaml << proxyYaml;
		groupProxiesYaml << "      - " << YamlQuote(tag) << "\n";

		const int port = portBase + static_cast<int>(outEndpoints.size());
		listenersYaml << "  - name: L" << index << "\n";
		listenersYaml << "    type: mixed\n";
		listenersYaml << "    port: " << port << "\n";
		listenersYaml << "    listen: 127.0.0.1\n";
		listenersYaml << "    proxy: " << YamlQuote(tag) << "\n";

		ParallelProbeEndpoint endpoint;
		endpoint.nodeIndex = index;
		endpoint.port = port;
		endpoint.proxyTag = tag;
		outEndpoints.push_back(endpoint);

		if (index == activeIndex)
			activeTag = tag;
	}

	if (outEndpoints.empty())
	{
		outError = "Не удалось собрать прокси для параллельного пинга.";
		return false;
	}

	if (activeTag.empty())
		activeTag = outEndpoints.front().proxyTag;

	std::ostringstream yaml;
	// Probe session must never enable TUN — only local mixed listeners for RealPing.
	constexpr bool useTun = false;

	yaml << "mixed-port: " << mixedPort << "\n";
	yaml << "external-controller: 127.0.0.1:" << apiPort << "\n";
	yaml << "allow-lan: false\n";
	yaml << "mode: rule\n";
	yaml << "log-level: warning\n";
	yaml << "ipv6: false\n";
	yaml << "geodata-mode: true\n";
	yaml << "geo-auto-update: false\n";
	yaml << "find-process-mode: off\n";
	yaml << "\n";

	yaml << "sniffer:\n";
	yaml << "  enable: true\n";
	yaml << "  sniff:\n";
	yaml << "    HTTP:\n";
	yaml << "      ports: [80, 8080-8880]\n";
	yaml << "    TLS:\n";
	yaml << "      ports: [443, 8443]\n";
	yaml << "    QUIC:\n";
	yaml << "      ports: [443]\n";
	yaml << "\n";

	yaml << "tun:\n";
	yaml << "  enable: false\n";
	yaml << "\n";

	AppendDnsBlock(yaml, transport, preset, useTun);
	yaml << "proxies:\n";
	yaml << proxiesYaml.str();
	yaml << "\n";
	yaml << "proxy-groups:\n";
	yaml << "  - name: PROXY\n";
	yaml << "    type: select\n";
	yaml << "    proxies:\n";
	yaml << "      - " << YamlQuote(activeTag) << "\n";
	yaml << groupProxiesYaml.str();
	yaml << "\n";
	yaml << "listeners:\n";
	yaml << listenersYaml.str();
	yaml << "\n";

	std::string configBody = yaml.str();
	const std::filesystem::path srssDirectory = std::filesystem::path(mihomoHome) / L"srss";

	VpnCustomRoutingInput customRouting;
	const VpnCustomRoutingInput* customRoutingPtr = nullptr;
	if (preset == VpnRoutingPreset::Custom)
	{
		VpnServiceRoutes::Load(customRouting.services);
		VpnDomainRoutes::Load(customRouting.domains);
		AppSettings appSettings;
		appSettings.Load();
		customRouting.includeAdultServices = appSettings.GetConfirmAdult();
		customRoutingPtr = &customRouting;
	}

	const bool hasRuInsideDomainRules = preset == VpnRoutingPreset::Ruv1ExceptRu
		&& std::filesystem::exists(srssDirectory / L"geosite-ru-available-only-inside.srs");
	const bool hasGoogleDomainRules = preset == VpnRoutingPreset::Ruv1Blocked
		&& std::filesystem::exists(srssDirectory / L"geosite-google.srs");
	const bool hasGoogleIpRules = preset == VpnRoutingPreset::Ruv1Blocked
		&& std::filesystem::exists(srssDirectory / L"geoip-google.srs");
	VpnRouting::AppendRuleProviders(configBody, preset, mihomoHome, customRoutingPtr);
	configBody += "\n";
	VpnRouting::AppendRules(
		configBody,
		preset,
		mihomoHome,
		hasRuInsideDomainRules,
		hasGoogleDomainRules,
		hasGoogleIpRules,
		customRoutingPtr,
		transport.fixDiscord);
	configBody += "\n";

	const std::filesystem::path configPath = std::filesystem::path(mihomoHome) / L"config.yaml";
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(mihomoHome), ec);
	std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
	if (!output)
	{
		outError = "Не удалось записать config.yaml для параллельного пинга.";
		return false;
	}
	output << configBody;
	return output.good();
}
