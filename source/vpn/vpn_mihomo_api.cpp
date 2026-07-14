#include "vpn/vpn_mihomo_api.h"

#include <Windows.h>
#include <WinInet.h>

#include <filesystem>
#include <sstream>

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
