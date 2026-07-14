#pragma once

#include <map>
#include <string>

struct StrategyTestEntry
{
	bool discordOk = false;
	bool youtubeOk = false;
	bool telegramOk = false;
	int pingMs = -1;
	int runtimeSec = 0;
	int providerDualOutageCount = 0;
};

class ZapretStore
{
public:
	void Load();
	void Save();

	void SetLastStrategy(const std::string& strategyName);
	const std::string& GetLastStrategy() const { return m_lastStrategy; }

	void ClearResults();
	void SetResult(const std::string& strategyName, const StrategyTestEntry& entry);
	const StrategyTestEntry* GetResult(const std::string& strategyName) const;
	const std::map<std::string, StrategyTestEntry>& GetResults() const { return m_results; }

	std::string GetBestStrategyName() const;
	int FindStrategyIndex(const std::string& strategyName) const;
	int GetPreferredStrategyIndex(bool autoSelectBest) const;

private:
	std::map<std::string, StrategyTestEntry> m_results;
	std::string m_lastStrategy;
};
