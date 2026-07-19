#include "app/protocol_handler.h"

#include "app/app_log.h"

#include <shellapi.h>

#include <mutex>
#include <queue>
#include <algorithm>
#include <cctype>

namespace
{
	std::mutex g_queueMutex;
	std::queue<ProtocolCommand> g_queue;
	HANDLE g_mutex = nullptr;
	bool g_startupFromAutostart = false;

	void BringExistingWindowToFront()
	{
		HWND existing = FindWindowW(ProtocolHandler::kWindowClassName, nullptr);
		if (!existing)
			return;
		if (!IsWindowVisible(existing))
			ShowWindow(existing, SW_SHOW);
		if (IsIconic(existing))
			ShowWindow(existing, SW_RESTORE);
		else
			ShowWindow(existing, SW_SHOW);
		SetForegroundWindow(existing);
	}

	std::string ToLower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return s;
	}

	std::string UrlDecode(const std::string& in, bool plusAsSpace = true)
	{
		std::string out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size(); ++i)
		{
			if (in[i] == '%' && i + 2 < in.size())
			{
				auto hex = [](char c) -> int {
					if (c >= '0' && c <= '9') return c - '0';
					if (c >= 'a' && c <= 'f') return c - 'a' + 10;
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					return -1;
				};
				const int hi = hex(in[i + 1]);
				const int lo = hex(in[i + 2]);
				if (hi >= 0 && lo >= 0)
				{
					out.push_back(static_cast<char>((hi << 4) | lo));
					i += 2;
					continue;
				}
			}
			else if (plusAsSpace && in[i] == '+')
			{
				out.push_back(' ');
				continue;
			}
			out.push_back(in[i]);
		}
		return out;
	}

	void ParseQuery(const std::string& query, ProtocolCommand& cmd)
	{
		size_t start = 0;
		while (start < query.size())
		{
			size_t amp = query.find('&', start);
			if (amp == std::string::npos)
				amp = query.size();
			const std::string pair = query.substr(start, amp - start);
			const size_t eq = pair.find('=');
			std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
			std::string val = eq == std::string::npos ? std::string() : pair.substr(eq + 1);
			key = UrlDecode(key);
			val = UrlDecode(val);
			cmd.params.emplace_back(key, val);
			if (key == "url" && cmd.importUrl.empty())
				cmd.importUrl = val;
			if (key == "tab" && cmd.openTab.empty())
				cmd.openTab = ToLower(val);
			start = amp + 1;
		}
	}

	bool WriteRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* data)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
			return false;
		const LONG ok = RegSetValueExW(
			key,
			valueName,
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(data),
			static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
		RegCloseKey(key);
		return ok == ERROR_SUCCESS;
	}
}

namespace ProtocolHandler
{
	std::string WideToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};
		std::string out(static_cast<size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
		return out;
	}

	std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty())
			return {};
		const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		if (size <= 1)
			return {};
		std::wstring out(static_cast<size_t>(size - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), size);
		return out;
	}

	bool RegisterUrlProtocol()
	{
		wchar_t exePath[MAX_PATH] = {};
		if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
			return false;

		std::wstring command = L"\"";
		command += exePath;
		command += L"\" \"%1\"";

		bool ok = true;
		ok = WriteRegString(HKEY_CURRENT_USER, L"Software\\Classes\\antizapret", nullptr, L"URL:AntiZapret Protocol") && ok;
		ok = WriteRegString(HKEY_CURRENT_USER, L"Software\\Classes\\antizapret", L"URL Protocol", L"") && ok;
		ok = WriteRegString(HKEY_CURRENT_USER, L"Software\\Classes\\antizapret\\DefaultIcon", nullptr, exePath) && ok;
		ok = WriteRegString(HKEY_CURRENT_USER, L"Software\\Classes\\antizapret\\shell\\open\\command", nullptr, command.c_str()) && ok;
		if (ok)
			AppLog::Instance().Append(LogSource::VpnRouting, "Protocol: registered antizapret://");
		else
			AppLog::Instance().Append(LogSource::VpnRouting, "Protocol: failed to register antizapret://");
		return ok;
	}

	bool CommandLineHasAutostartFlag(const std::wstring& cmdLine)
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
		if (!argv)
			return false;
		bool found = false;
		for (int i = 1; i < argc; ++i)
		{
			if (_wcsicmp(argv[i], L"--autostart") == 0 || _wcsicmp(argv[i], L"-autostart") == 0)
			{
				found = true;
				break;
			}
		}
		LocalFree(argv);
		return found;
	}

	void SetStartupFromAutostart(bool value)
	{
		g_startupFromAutostart = value;
	}

	bool IsStartupFromAutostart()
	{
		return g_startupFromAutostart;
	}

	bool ForwardToExistingInstanceAndShouldExit()
	{
		g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
		if (!g_mutex)
			return false;

		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return false; // we own the instance

		const std::wstring cmd = GetCommandLineW() ? GetCommandLineW() : L"";
		const ProtocolCommand parsed = ParseCommandLine(cmd);
		if (!parsed.valid || parsed.raw.empty())
		{
			BringExistingWindowToFront();
			CloseHandle(g_mutex);
			g_mutex = nullptr;
			return true;
		}

		HWND existing = nullptr;
		for (int i = 0; i < 50 && !existing; ++i)
		{
			existing = FindWindowW(kWindowClassName, nullptr);
			if (!existing)
				Sleep(20);
		}
		if (existing)
		{
			const std::string payload = parsed.raw;
			COPYDATASTRUCT cds {};
			cds.dwData = kCopyDataProtocol;
			cds.cbData = static_cast<DWORD>(payload.size() + 1);
			cds.lpData = const_cast<char*>(payload.c_str());
			SendMessageW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
			BringExistingWindowToFront();
		}

		CloseHandle(g_mutex);
		g_mutex = nullptr;
		return true;
	}

	ProtocolCommand ParseUri(const std::string& uriUtf8)
	{
		ProtocolCommand cmd;
		cmd.raw = uriUtf8;
		std::string uri = uriUtf8;
		// trim quotes/spaces
		while (!uri.empty() && (uri.front() == '"' || uri.front() == '\'' || std::isspace(static_cast<unsigned char>(uri.front()))))
			uri.erase(uri.begin());
		while (!uri.empty() && (uri.back() == '"' || uri.back() == '\'' || std::isspace(static_cast<unsigned char>(uri.back()))))
			uri.pop_back();

		const std::string lower = ToLower(uri);
		if (lower.rfind("antizapret:", 0) != 0)
			return cmd;

		size_t pos = 11; // after antizapret:
		if (pos < uri.size() && uri[pos] == '/')
			++pos;
		if (pos < uri.size() && uri[pos] == '/')
			++pos;

		std::string rest = uri.substr(pos);
		if (rest.empty())
			return cmd;

		// Optional version prefix: v1/...
		if (rest.size() >= 3 && (rest[0] == 'v' || rest[0] == 'V') && std::isdigit(static_cast<unsigned char>(rest[1])) && rest[2] == '/')
		{
			cmd.version = rest.substr(0, 2);
			rest = rest.substr(3);
		}

		const size_t q = rest.find('?');
		std::string path = q == std::string::npos ? rest : rest.substr(0, q);
		std::string query = q == std::string::npos ? std::string() : rest.substr(q + 1);
		if (!query.empty())
			ParseQuery(query, cmd);

		const std::string pathLower = ToLower(path);

		// antizapret://add/<url>  (Happ-style; url may contain ://)
		if (pathLower.rfind("add/", 0) == 0)
		{
			cmd.action = "import";
			cmd.importUrl = UrlDecode(path.substr(4), false);
			if (cmd.importUrl.empty())
			{
				// keep query url if present
			}
			cmd.valid = !cmd.importUrl.empty();
			return cmd;
		}
		if (pathLower == "add" || pathLower == "import")
		{
			cmd.action = "import";
			cmd.valid = !cmd.importUrl.empty();
			return cmd;
		}
		if (pathLower.rfind("import/", 0) == 0)
		{
			cmd.action = "import";
			cmd.importUrl = UrlDecode(path.substr(7), false);
			cmd.valid = !cmd.importUrl.empty();
			return cmd;
		}
		if (pathLower == "open" || pathLower.rfind("open/", 0) == 0)
		{
			cmd.action = "open";
			if (pathLower.rfind("open/", 0) == 0 && cmd.openTab.empty())
				cmd.openTab = ToLower(path.substr(5));
			cmd.valid = !cmd.openTab.empty();
			return cmd;
		}
		if (pathLower.rfind("control/", 0) == 0)
		{
			cmd.action = "control";
			std::string cpath = path.substr(8);
			const size_t slash = cpath.find('/');
			if (slash == std::string::npos)
			{
				cmd.target = ToLower(cpath);
			}
			else
			{
				cmd.target = ToLower(cpath.substr(0, slash));
				cmd.controlAction = ToLower(cpath.substr(slash + 1));
			}
			cmd.valid = !cmd.target.empty();
			return cmd;
		}

		cmd.action = "unknown";
		cmd.valid = true; // keep for future routing / logging
		return cmd;
	}

	ProtocolCommand ParseCommandLine(const std::wstring& cmdLine)
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
		ProtocolCommand result;
		if (!argv)
			return result;

		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = WideToUtf8(argv[i]);
			ProtocolCommand parsed = ParseUri(arg);
			if (parsed.valid || ToLower(arg).rfind("antizapret:", 0) == 0)
			{
				result = parsed;
				if (!result.valid && !arg.empty())
				{
					result.raw = arg;
					result.action = "unknown";
					result.valid = true;
				}
				break;
			}
		}
		LocalFree(argv);
		return result;
	}

	void Enqueue(const ProtocolCommand& cmd)
	{
		if (!cmd.valid && cmd.raw.empty())
			return;
		std::lock_guard<std::mutex> lock(g_queueMutex);
		g_queue.push(cmd);
	}

	bool TryDequeue(ProtocolCommand& outCmd)
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (g_queue.empty())
			return false;
		outCmd = std::move(g_queue.front());
		g_queue.pop();
		return true;
	}

	bool HandleCopyData(const COPYDATASTRUCT* cds)
	{
		if (!cds || cds->dwData != kCopyDataProtocol || !cds->lpData || cds->cbData == 0)
			return false;
		const char* bytes = static_cast<const char*>(cds->lpData);
		size_t len = cds->cbData;
		if (len > 0 && bytes[len - 1] == '\0')
			--len;
		const std::string uri(bytes, bytes + len);
		ProtocolCommand cmd = ParseUri(uri);
		if (!cmd.valid && !uri.empty())
		{
			cmd.raw = uri;
			cmd.action = "unknown";
			cmd.valid = true;
		}
		Enqueue(cmd);
		AppLog::Instance().Append(LogSource::VpnRouting, "Protocol: received via WM_COPYDATA: " + uri);
		return true;
	}
}
