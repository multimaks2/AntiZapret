#pragma once

#include <string>
#include <vector>
#include <Windows.h>

// Browser / second-instance deep links:
//   antizapret://add/<subscription-url>
//   antizapret://import?url=<urlencoded-url>
//   antizapret://strategy?id=<strategy-id>&start=1
//   antizapret://open?tab=vpn|home|antizapret|tg|routing|console|settings|about
//   antizapret://v1/control/<target>/<action>?key=value
struct ProtocolCommand
{
	std::string raw;
	std::string version;   // "" or "v1"
	std::string action;    // import | open | control | strategy | unknown
	std::string target;    // vpn | zapret | ...
	std::string controlAction;
	std::string importUrl;
	std::string strategyId;
	std::string openTab;   // vpn, home, ...
	std::vector<std::pair<std::string, std::string>> params;
	bool startStrategy = true;
	bool valid = false;
};

namespace ProtocolHandler
{
	constexpr wchar_t kWindowClassName[] = L"AntiZapretWindowClass";
	constexpr wchar_t kMutexName[] = L"AntiZapret_SingleInstance_7F83B2E1";
	constexpr UINT kCopyDataProtocol = 0xA201; // COPYDATASTRUCT::dwData

	// HKCU URL protocol -> current exe "%1"
	bool RegisterUrlProtocol();

	// If another instance owns the mutex, forward cmdline URI and return true (caller should exit).
	bool ForwardToExistingInstanceAndShouldExit();

	bool CommandLineHasAutostartFlag(const std::wstring& cmdLine);
	// True when argv contains an antizapret:// deep link (Discord import, browser open.html, …).
	bool CommandLineHasProtocolUri(const std::wstring& cmdLine);
	void SetStartupFromAutostart(bool value);
	bool IsStartupFromAutostart();

	ProtocolCommand ParseUri(const std::string& uriUtf8);
	ProtocolCommand ParseCommandLine(const std::wstring& cmdLine);

	void Enqueue(const ProtocolCommand& cmd);
	bool TryDequeue(ProtocolCommand& outCmd);

	bool HandleCopyData(const COPYDATASTRUCT* cds);
	std::string WideToUtf8(const std::wstring& wide);
	std::wstring Utf8ToWide(const std::string& utf8);
	std::string UrlEncode(const std::string& value);
}
