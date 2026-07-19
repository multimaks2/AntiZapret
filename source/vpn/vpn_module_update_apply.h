#pragma once

#include <atomic>
#include <mutex>
#include <string>

class VpnManager;

class VpnModuleUpdateApply
{
public:
	static VpnModuleUpdateApply& Instance();

	void RequestApplyMihomo(VpnManager* vpn);
	void RequestApplyWintun(VpnManager* vpn);

	bool IsApplyingMihomo() const { return m_applyingMihomo.load(); }
	bool IsApplyingWintun() const { return m_applyingWintun.load(); }
	bool IsApplyingAny() const { return IsApplyingMihomo() || IsApplyingWintun(); }

	std::string GetMihomoStatusMessage() const;
	std::string GetWintunStatusMessage() const;

private:
	VpnModuleUpdateApply() = default;
	void RunApplyMihomo(VpnManager* vpn);
	void RunApplyWintun(VpnManager* vpn);

	mutable std::mutex m_mutex;
	std::string m_mihomoStatusMessage;
	std::string m_wintunStatusMessage;
	std::atomic<bool> m_applyingMihomo { false };
	std::atomic<bool> m_applyingWintun { false };
};
