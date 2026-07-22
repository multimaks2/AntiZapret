#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

enum class ComponentUpdateStatus
{
	Unknown,
	Checking,
	UpToDate,
	UpdateAvailable,
	Error,
};

// Zapret: local .service/version.txt vs Flowseal version.txt
// TG WS Proxy: local proxy/__init__.py __version__ vs GitHub Releases latest tag
class ZapretUpdateCheck
{
public:
	static ZapretUpdateCheck& Instance();

	void StartBackgroundCheck();
	void RequestCheck();

	void SetOnZapretUpdateAvailable(std::function<void()> callback);
	void SetOnTgUpdateAvailable(std::function<void()> callback);

	ComponentUpdateStatus GetZapretStatus() const;
	std::string GetZapretLocalVersion() const;
	std::string GetZapretRemoteVersion() const;

	ComponentUpdateStatus GetTgProxyStatus() const;
	std::string GetTgProxyLocalVersion() const;
	std::string GetTgProxyRemoteVersion() const;
	// Release tag including leading 'v' when present (e.g. v1.8.1), for download URL.
	std::string GetTgProxyRemoteTag() const;

	static const char* GetStatusMessage(ComponentUpdateStatus status);

private:
	ZapretUpdateCheck() = default;

	void SeedLocalVersions();
	void RunCheck();
	void NotifyZapretUpdateIfNeeded(ComponentUpdateStatus status);
	void NotifyTgUpdateIfNeeded(ComponentUpdateStatus status);

	mutable std::mutex m_mutex;
	ComponentUpdateStatus m_zapretStatus = ComponentUpdateStatus::Unknown;
	std::string m_zapretLocalVersion;
	std::string m_zapretRemoteVersion = "Unknown";
	ComponentUpdateStatus m_tgProxyStatus = ComponentUpdateStatus::Unknown;
	std::string m_tgProxyLocalVersion;
	std::string m_tgProxyRemoteVersion;
	std::string m_tgProxyRemoteTag;
	std::function<void()> m_onZapretUpdateAvailable;
	std::function<void()> m_onTgUpdateAvailable;

	std::atomic<bool> m_checkStarted { false };
	std::atomic<bool> m_checkInProgress { false };
	std::atomic<bool> m_zapretAutoUpdateNotified { false };
	std::atomic<bool> m_tgAutoUpdateNotified { false };
};
