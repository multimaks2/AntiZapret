#include "vpn/vpn_mihomo_api.h"

#include <Windows.h>
#include <WinInet.h>

#include <filesystem>
#include <sstream>
#include <string>

namespace
{
	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty())
			return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (length <= 1)
			return {};
		std::string result(static_cast<size_t>(length - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
		return result;
	}

	std::string JsonEscape(const std::string& value)
	{
		std::string escaped;
		escaped.reserve(value.size() + 8);
		for (char ch : value)
		{
			if (ch == '\\' || ch == '"')
				escaped.push_back('\\');
			escaped.push_back(ch);
		}
		return escaped;
	}

	bool HttpPutJson(int port, const char* path, const std::string& jsonBody, std::string& outError)
	{
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "Не удалось открыть HTTP-сессию.";
			return false;
		}

		HINTERNET connection = InternetConnectA(
			internet,
			"127.0.0.1",
			static_cast<INTERNET_PORT>(port),
			nullptr,
			nullptr,
			INTERNET_SERVICE_HTTP,
			0,
			0);
		if (!connection)
		{
			InternetCloseHandle(internet);
			outError = "Не удалось подключиться к API mihomo.";
			return false;
		}

		HINTERNET request = HttpOpenRequestA(
			connection,
			"PUT",
			path,
			nullptr,
			nullptr,
			nullptr,
			INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
			0);
		if (!request)
		{
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			outError = "Не удалось создать HTTP-запрос.";
			return false;
		}

		const char* headers = "Content-Type: application/json\r\n";
		const BOOL sent = HttpSendRequestA(
			request,
			headers,
			static_cast<DWORD>(-1),
			const_cast<char*>(jsonBody.data()),
			static_cast<DWORD>(jsonBody.size()));

		if (!sent)
		{
			InternetCloseHandle(request);
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			outError = "Не удалось отправить запрос hot reload.";
			return false;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		if (!HttpQueryInfoA(
				request,
				HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
				&statusCode,
				&statusSize,
				nullptr))
		{
			InternetCloseHandle(request);
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			outError = "Не удалось прочитать ответ API mihomo.";
			return false;
		}

		InternetCloseHandle(request);
		InternetCloseHandle(connection);
		InternetCloseHandle(internet);

		if (statusCode < 200 || statusCode >= 300)
		{
			std::ostringstream message;
			message << "API mihomo вернул код " << statusCode << ".";
			outError = message.str();
			return false;
		}

		outError.clear();
		return true;
	}

	bool HttpGet(
		int port,
		const char* path,
		std::string& outBody,
		std::string& outError,
		DWORD timeoutMs)
	{
		outBody.clear();
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "Не удалось открыть HTTP-сессию.";
			return false;
		}

		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		HINTERNET connection = InternetConnectA(
			internet,
			"127.0.0.1",
			static_cast<INTERNET_PORT>(port),
			nullptr,
			nullptr,
			INTERNET_SERVICE_HTTP,
			0,
			0);
		if (!connection)
		{
			InternetCloseHandle(internet);
			outError = "Не удалось подключиться к API mihomo.";
			return false;
		}

		HINTERNET request = HttpOpenRequestA(
			connection,
			"GET",
			path,
			nullptr,
			nullptr,
			nullptr,
			INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_UI,
			0);
		if (!request)
		{
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			outError = "Не удалось создать HTTP-запрос.";
			return false;
		}

		if (!HttpSendRequestA(request, nullptr, 0, nullptr, 0))
		{
			InternetCloseHandle(request);
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			outError = "Не удалось отправить запрос к API mihomo.";
			return false;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		HttpQueryInfoA(
			request,
			HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
			&statusCode,
			&statusSize,
			nullptr);

		char buffer[512];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer) - 1, &read) && read > 0)
		{
			buffer[read] = '\0';
			outBody.append(buffer, buffer + read);
		}

		InternetCloseHandle(request);
		InternetCloseHandle(connection);
		InternetCloseHandle(internet);

		if (statusCode < 200 || statusCode >= 300)
		{
			outError = "API mihomo вернул код " + std::to_string(statusCode) + ".";
			return false;
		}

		outError.clear();
		return true;
	}

	std::string UrlEncode(const std::string& value)
	{
		static const char* hex = "0123456789ABCDEF";
		std::string out;
		out.reserve(value.size() * 3);
		for (unsigned char ch : value)
		{
			if ((ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '-' || ch == '_' || ch == '.' || ch == '~')
			{
				out.push_back(static_cast<char>(ch));
			}
			else
			{
				out.push_back('%');
				out.push_back(hex[(ch >> 4) & 0xF]);
				out.push_back(hex[ch & 0xF]);
			}
		}
		return out;
	}

	int ParseDelayJson(const std::string& body)
	{
		const size_t key = body.find("\"delay\"");
		if (key == std::string::npos)
			return -1;
		const size_t colon = body.find(':', key);
		if (colon == std::string::npos)
			return -1;
		size_t i = colon + 1;
		while (i < body.size() && (body[i] == ' ' || body[i] == '\t'))
			++i;
		if (i >= body.size() || body[i] < '0' || body[i] > '9')
			return -1;
		int value = 0;
		while (i < body.size() && body[i] >= '0' && body[i] <= '9')
		{
			value = value * 10 + (body[i] - '0');
			++i;
		}
		return value > 0 ? value : -1;
	}

	bool HttpDelete(int port, const char* path)
	{
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
		if (!internet)
			return false;

		HINTERNET connection = InternetConnectA(
			internet,
			"127.0.0.1",
			static_cast<INTERNET_PORT>(port),
			nullptr,
			nullptr,
			INTERNET_SERVICE_HTTP,
			0,
			0);
		if (!connection)
		{
			InternetCloseHandle(internet);
			return false;
		}

		HINTERNET request = HttpOpenRequestA(
			connection,
			"DELETE",
			path,
			nullptr,
			nullptr,
			nullptr,
			INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
			0);
		if (!request)
		{
			InternetCloseHandle(connection);
			InternetCloseHandle(internet);
			return false;
		}

		const BOOL sent = HttpSendRequestA(request, nullptr, 0, nullptr, 0);
		InternetCloseHandle(request);
		InternetCloseHandle(connection);
		InternetCloseHandle(internet);
		return sent == TRUE;
	}
}

bool MihomoApi::ReloadConfig(const std::wstring& configPath, int apiPort, std::string& outError)
{
	if (configPath.empty())
	{
		outError = "Путь к config.yaml пуст.";
		return false;
	}

	std::error_code ec;
	const std::filesystem::path absolutePath = std::filesystem::absolute(configPath, ec);
	const std::string pathUtf8 = JsonEscape(WideToUtf8(absolutePath.wstring()));
	const std::string body = std::string("{\"path\":\"") + pathUtf8 + "\"}";

	return HttpPutJson(apiPort, "/configs?force=true", body, outError);
}

void MihomoApi::FlushConnections(int apiPort)
{
	HttpDelete(apiPort, "/connections");
}

int MihomoApi::GetProxyDelayMs(
	int apiPort,
	const std::string& proxyName,
	const char* testUrl,
	int timeoutMs)
{
	if (proxyName.empty() || !testUrl || !testUrl[0] || timeoutMs <= 0)
		return -1;

	const std::string path =
		"/proxies/" + UrlEncode(proxyName)
		+ "/delay?timeout=" + std::to_string(timeoutMs)
		+ "&url=" + UrlEncode(testUrl);

	// Two samples, keep the best — same idea as v2rayN RealPing.
	int bestMs = -1;
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		std::string body;
		std::string error;
		const DWORD httpTimeout = static_cast<DWORD>(timeoutMs + 2000);
		if (HttpGet(apiPort, path.c_str(), body, error, httpTimeout))
		{
			const int delayMs = ParseDelayJson(body);
			if (delayMs > 0 && (bestMs < 0 || delayMs < bestMs))
				bestMs = delayMs;
		}
		if (attempt == 0)
			Sleep(80);
	}
	return bestMs;
}
