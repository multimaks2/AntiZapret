#pragma once

#include <atomic>
#include <mutex>
#include <string>

class ZapretManager;
class TgWsProxyManager;

class ZapretUpdateApply
{
public:
	static ZapretUpdateApply& Instance();

	void RequestApply(ZapretManager* zapret, TgWsProxyManager* tgProxy);
	void RequestApplyTg(TgWsProxyManager* tgProxy);

	bool IsApplying() const { return m_applying.load(); }
	bool IsApplyingTg() const { return m_applyingTg.load(); }
	std::string GetStatusMessage() const;
	std::string GetTgStatusMessage() const;

private:
	ZapretUpdateApply() = default;
	void RunApply(ZapretManager* zapret, TgWsProxyManager* tgProxy);
	void RunApplyTg(TgWsProxyManager* tgProxy);

	mutable std::mutex m_mutex;
	std::string m_statusMessage;
	std::string m_tgStatusMessage;
	std::atomic<bool> m_applying { false };
	std::atomic<bool> m_applyingTg { false };
};
