#pragma once

#include "zapret/zapret_update_check.h"

#include <atomic>
#include <mutex>
#include <string>

// Mihomo: local `mihomo.exe -v` vs MetaCubeX GitHub latest tag
// Wintun: local FileVersion of wintun.dll vs version on wintun.net
class VpnModuleUpdateCheck
{
public:
	static VpnModuleUpdateCheck& Instance();

	void StartBackgroundCheck();
	void RequestCheck();

	ComponentUpdateStatus GetMihomoStatus() const;
	std::string GetMihomoLocalVersion() const;
	std::string GetMihomoRemoteVersion() const;
	std::string GetMihomoRemoteTag() const;

	ComponentUpdateStatus GetWintunStatus() const;
	std::string GetWintunLocalVersion() const;
	std::string GetWintunRemoteVersion() const;

private:
	VpnModuleUpdateCheck() = default;

	void SeedLocalVersions();
	void RunCheck();

	mutable std::mutex m_mutex;
	ComponentUpdateStatus m_mihomoStatus = ComponentUpdateStatus::Unknown;
	std::string m_mihomoLocalVersion;
	std::string m_mihomoRemoteVersion;
	std::string m_mihomoRemoteTag;
	ComponentUpdateStatus m_wintunStatus = ComponentUpdateStatus::Unknown;
	std::string m_wintunLocalVersion;
	std::string m_wintunRemoteVersion;

	std::atomic<bool> m_checkStarted { false };
	std::atomic<bool> m_checkInProgress { false };
};
