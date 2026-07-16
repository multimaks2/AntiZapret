#include "vpn/vpn_import.h"

#include <Windows.h>
#include <WinInet.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	std::string ToLower(std::string value)
	{
		for (char& ch : value)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return value;
	}

	bool StartsWithIgnoreCase(const std::string& value, const char* prefix)
	{
		const std::string lower = ToLower(value);
		const std::string prefixLower = ToLower(std::string(prefix));
		return lower.rfind(prefixLower, 0) == 0;
	}

	bool Base64Decode(const std::string& input, std::string& out)
	{
		std::string normalized;
		normalized.reserve(input.size());
		for (char ch : input)
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
			return false;

		std::vector<BYTE> buffer(required);
		if (!CryptStringToBinaryA(
				normalized.c_str(),
				static_cast<DWORD>(normalized.size()),
				CRYPT_STRING_BASE64,
				buffer.data(),
				&required,
				nullptr,
				nullptr))
			return false;

		out.assign(reinterpret_cast<const char*>(buffer.data()), required);
		return true;
	}

	size_t SkipJsonWhitespace(const std::string& json, size_t index)
	{
		while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])))
			++index;
		return index;
	}

	std::string UrlDecode(const std::string& value)
	{
		std::string result;
		result.reserve(value.size());
		for (size_t i = 0; i < value.size(); ++i)
		{
			if (value[i] == '%' && i + 2 < value.size())
			{
				const char hex[] = { value[i + 1], value[i + 2], '\0' };
				result.push_back(static_cast<char>(strtoul(hex, nullptr, 16)));
				i += 2;
			}
			else if (value[i] == '+')
				result.push_back(' ');
			else
				result.push_back(value[i]);
		}
		return result;
	}

	std::string JsonStringField(const std::string& json, const char* key)
	{
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return {};

		size_t start = SkipJsonWhitespace(json, pos + pattern.size());
		if (start >= json.size() || json[start] != '"')
			return {};
		++start;

		std::string result;
		while (start < json.size())
		{
			const char ch = json[start++];
			if (ch == '\\' && start < json.size())
			{
				result.push_back(json[start++]);
				continue;
			}
			if (ch == '"')
				break;
			result.push_back(ch);
		}
		return result;
	}

	int JsonIntField(const std::string& json, const char* key, int fallback = 0)
	{
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return fallback;

		size_t start = SkipJsonWhitespace(json, pos + pattern.size());
		if (start < json.size() && json[start] == '"')
		{
			++start;
			return std::atoi(json.c_str() + start);
		}

		return std::atoi(json.c_str() + start);
	}

	void AssignNodeIdentity(VpnNode& node, int nodeIndex)
	{
		if (node.id.empty())
		{
			char buffer[32] = {};
			snprintf(buffer, sizeof buffer, "node-%d", nodeIndex);
			node.id = buffer;
		}
		if (node.group.empty())
			node.group = "Imported";
		if (node.name.empty())
			node.name = node.server.empty() ? node.id : node.server;
	}

	bool ParseHostPort(const std::string& hostPort, std::string& host, int& port, int defaultPort)
	{
		if (hostPort.empty())
			return false;

		if (hostPort.front() == '[')
		{
			const size_t end = hostPort.find(']');
			if (end == std::string::npos)
				return false;
			host = hostPort.substr(1, end - 1);
			if (end + 1 < hostPort.size() && hostPort[end + 1] == ':')
				port = std::atoi(hostPort.c_str() + end + 2);
			else
				port = defaultPort;
			return true;
		}

		const size_t colon = hostPort.rfind(':');
		if (colon == std::string::npos)
		{
			host = hostPort;
			port = defaultPort;
			return true;
		}

		host = hostPort.substr(0, colon);
		port = std::atoi(hostPort.c_str() + colon + 1);
		return !host.empty() && port > 0;
	}

	bool QueryContainsTls(const std::string& query)
	{
		const std::string lower = ToLower(query);
		return lower.find("security=tls") != std::string::npos
			|| lower.find("security=reality") != std::string::npos
			|| lower.find("security=xtls") != std::string::npos
			|| lower.find("allowinsecure=0") != std::string::npos;
	}

	bool ParseStandardUri(const std::string& scheme, const std::string& rest, VpnNode& node, int nodeIndex, std::string& error)
	{
		std::string body = rest;
		std::string fragment;
		const size_t hashPos = body.find('#');
		if (hashPos != std::string::npos)
		{
			fragment = UrlDecode(body.substr(hashPos + 1));
			body = body.substr(0, hashPos);
		}

		std::string query;
		const size_t queryPos = body.find('?');
		if (queryPos != std::string::npos)
		{
			query = body.substr(queryPos + 1);
			body = body.substr(0, queryPos);
		}

		const size_t atPos = body.rfind('@');
		std::string hostPort = atPos == std::string::npos ? body : body.substr(atPos + 1);
		if (!ParseHostPort(hostPort, node.server, node.port, node.port > 0 ? node.port : 443))
		{
			error = "Не удалось разобрать host:port.";
			return false;
		}

		node.scheme = scheme;
		node.tls = QueryContainsTls(query) || node.port == 443 || node.port == 8443;
		if (!fragment.empty())
			node.name = fragment;
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool ParseVmessJson(const std::string& json, VpnNode& node, int nodeIndex, std::string& error)
	{
		node.scheme = "vmess";
		node.name = JsonStringField(json, "ps");
		node.server = JsonStringField(json, "add");
		node.port = JsonIntField(json, "port", 443);
		const std::string net = JsonStringField(json, "net");
		const std::string tls = JsonStringField(json, "tls");
		node.tls = tls == "tls" || tls == "xtls" || net == "ws" || net == "grpc";
		if (node.server.empty() || node.port <= 0)
		{
			error = "Некорректный vmess JSON.";
			return false;
		}
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool ParseShadowsocksUri(const std::string& rest, VpnNode& node, int nodeIndex, std::string& error)
	{
		node.scheme = "ss";
		std::string body = rest;
		std::string fragment;
		const size_t hashPos = body.find('#');
		if (hashPos != std::string::npos)
		{
			fragment = UrlDecode(body.substr(hashPos + 1));
			body = body.substr(0, hashPos);
		}

		std::string userPart;
		std::string hostPort = body;
		const size_t atPos = body.rfind('@');
		if (atPos != std::string::npos)
		{
			userPart = body.substr(0, atPos);
			hostPort = body.substr(atPos + 1);
		}
		else
		{
			std::string decoded;
			if (Base64Decode(body, decoded))
			{
				const size_t decodedAt = decoded.rfind('@');
				if (decodedAt != std::string::npos)
				{
					userPart = decoded.substr(0, decodedAt);
					hostPort = decoded.substr(decodedAt + 1);
				}
			}
		}

		if (!ParseHostPort(hostPort, node.server, node.port, 8388))
		{
			error = "Некорректный ss:// URI.";
			return false;
		}

		node.tls = false;
		if (!fragment.empty())
			node.name = fragment;
		else if (!userPart.empty())
			node.name = userPart;
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool LooksLikeHtml(const std::string& text)
	{
		const std::string trimmed = Trim(text);
		return StartsWithIgnoreCase(trimmed, "<!doctype")
			|| StartsWithIgnoreCase(trimmed, "<html")
			|| StartsWithIgnoreCase(trimmed, "<head")
			|| trimmed.find("<!doctype html") != std::string::npos;
	}

	bool FetchSubscriptionText(const std::string& url, std::string& outText, std::string& outError)
	{
		// Panels like Capybara return an install HTML page for unknown UAs
		// (e.g. "AntiZapret"), and the real base64/share-link feed for VPN clients.
		HINTERNET internet = InternetOpenA("v2rayN/6.40", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "WinInet недоступен.";
			return false;
		}

		DWORD timeoutMs = 20000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char* headers =
			"Accept: text/plain, text/yaml, application/json, */*\r\n"
			"Accept-Language: en-US,en;q=0.9\r\n";

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			headers,
			static_cast<DWORD>(-1),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS,
			0);

		if (!request)
		{
			InternetCloseHandle(internet);
			outError = "Не удалось скачать подписку.";
			return false;
		}

		char buffer[4096];
		DWORD read = 0;
		outText.clear();
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			outText.append(buffer, buffer + read);

		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (outText.empty())
		{
			outError = "Подписка пустая.";
			return false;
		}

		if (LooksLikeHtml(outText))
		{
			outError =
				"Сервер вернул HTML-страницу установки вместо списка серверов. "
				"Скопируйте сырую ссылку подписки (иконка «Получить ссылку» на сайте), "
				"а не страницу Happ/INCY.";
			return false;
		}

		std::string decoded;
		if (Base64Decode(Trim(outText), decoded) && decoded.find("://") != std::string::npos)
			outText = decoded;

		return true;
	}

	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			line = Trim(line);
			if (!line.empty())
				lines.push_back(line);
		}
		return lines;
	}

	bool IsDuplicate(const std::vector<VpnNode>& existing, const VpnNode& candidate)
	{
		for (const VpnNode& node : existing)
		{
			if (!candidate.originalUri.empty() && candidate.originalUri == node.originalUri)
				return true;
			if (candidate.scheme == node.scheme
				&& candidate.server == node.server
				&& candidate.port == node.port
				&& candidate.name == node.name)
			{
				return true;
			}
		}
		return false;
	}
}

bool VpnImport::ReadClipboardUtf8(std::string& outText)
{
	outText.clear();
	if (!OpenClipboard(nullptr))
		return false;

	HANDLE memory = GetClipboardData(CF_UNICODETEXT);
	if (!memory)
	{
		CloseClipboard();
		return false;
	}

	const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(memory));
	if (!text)
	{
		CloseClipboard();
		return false;
	}

	const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (length > 1)
	{
		outText.resize(static_cast<size_t>(length - 1));
		WideCharToMultiByte(CP_UTF8, 0, text, -1, outText.data(), length, nullptr, nullptr);
	}

	GlobalUnlock(memory);
	CloseClipboard();
	return !outText.empty();
}

bool VpnImport::ParseShareLink(const std::string& line, VpnNode& outNode, int nodeIndex, std::string& outError)
{
	const std::string trimmed = Trim(line);
	if (trimmed.empty())
	{
		outError = "Пустая строка.";
		return false;
	}

	outNode = VpnNode {};
	outNode.originalUri = trimmed;
	outNode.pingMs = -1;
	outNode.speedMbps = -1.f;
	outNode.alive = -1;

	if (StartsWithIgnoreCase(trimmed, "vless://"))
		return ParseStandardUri("vless", trimmed.substr(8), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "vmess://"))
	{
		const std::string payload = trimmed.substr(8);
		if (!payload.empty() && payload.front() == '{')
			return ParseVmessJson(payload, outNode, nodeIndex, outError);

		std::string decoded;
		if (!Base64Decode(payload, decoded))
		{
			outError = "Не удалось декодировать vmess://";
			return false;
		}
		return ParseVmessJson(decoded, outNode, nodeIndex, outError);
	}
	if (StartsWithIgnoreCase(trimmed, "trojan://"))
		return ParseStandardUri("trojan", trimmed.substr(9), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "ss://"))
		return ParseShadowsocksUri(trimmed.substr(5), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "hysteria2://") || StartsWithIgnoreCase(trimmed, "hy2://"))
	{
		const size_t prefixLen = StartsWithIgnoreCase(trimmed, "hysteria2://") ? 12 : 5;
		if (!ParseStandardUri("hysteria2", trimmed.substr(prefixLen), outNode, nodeIndex, outError))
			return false;
		outNode.tls = true;
		return true;
	}

	outError = "Неподдерживаемый формат ссылки.";
	return false;
}

VpnImportResult VpnImport::ImportFromText(const std::string& text, int nextNodeIndex)
{
	VpnImportResult result;
	std::vector<std::string> pendingLines = SplitLines(text);
	std::vector<std::string> expanded;

	for (const std::string& line : pendingLines)
	{
		if (StartsWithIgnoreCase(line, "http://") || StartsWithIgnoreCase(line, "https://"))
		{
			std::string subscriptionText;
			std::string error;
			if (!FetchSubscriptionText(line, subscriptionText, error))
			{
				result.errors.push_back(error);
				continue;
			}

			for (const std::string& subLine : SplitLines(subscriptionText))
				expanded.push_back(subLine);
			continue;
		}

		expanded.push_back(line);
	}

	int nodeIndex = nextNodeIndex;
	for (const std::string& line : expanded)
	{
		VpnNode node;
		std::string error;
		if (!ParseShareLink(line, node, nodeIndex, error))
		{
			if (!error.empty())
				result.errors.push_back(error + " (" + line.substr(0, std::min<size_t>(line.size(), 48)) + ")");
			continue;
		}

		if (IsDuplicate(result.nodes, node))
		{
			++result.duplicatesSkipped;
			continue;
		}

		result.nodes.push_back(std::move(node));
		++nodeIndex;
	}

	return result;
}
