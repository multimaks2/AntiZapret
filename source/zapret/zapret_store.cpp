#include "zapret/zapret_store.h"

#include "app/settings_document.h"
#include "zapret/strategies.hpp"

#include <Windows.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	std::string TrimLine(std::string line)
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();

		const size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		const size_t end = line.find_last_not_of(" \t");
		return line.substr(start, end - start + 1);
	}

	std::vector<std::string> SplitPipeFields(const std::string& line)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= line.size())
		{
			const size_t pos = line.find('|', start);
			if (pos == std::string::npos)
			{
				parts.push_back(line.substr(start));
				break;
			}
			parts.push_back(line.substr(start, pos - start));
			start = pos + 1;
		}
		return parts;
	}

	bool ParseResultValue(const std::string& name, const std::string& value, StrategyTestEntry& outEntry)
	{
		if (name.empty())
			return false;

		const std::vector<std::string> parts = SplitPipeFields(value);
		if (parts.size() < 3)
			return false;

		// r.<id>=D|Y|T|ping|runtime|dual  OR legacy name|D|Y|T...
		size_t offset = 0;
		if (parts.size() >= 4 && parts[0].size() == 1 && (parts[0][0] == '0' || parts[0][0] == '1'))
			offset = 0;
		else if (parts.size() >= 4)
			offset = 1; // ignore embedded name if present

		if (parts.size() < offset + 3)
			return false;
		if (parts[offset].size() != 1 || parts[offset + 1].size() != 1 || parts[offset + 2].size() != 1)
			return false;

		outEntry.discordOk = parts[offset][0] == '1';
		outEntry.youtubeOk = parts[offset + 1][0] == '1';
		outEntry.telegramOk = parts[offset + 2][0] == '1';
		if (parts.size() >= offset + 4)
			outEntry.pingMs = std::atoi(parts[offset + 3].c_str());
		if (parts.size() >= offset + 5)
			outEntry.runtimeSec = std::atoi(parts[offset + 4].c_str());
		if (parts.size() >= offset + 6)
			outEntry.providerDualOutageCount = std::atoi(parts[offset + 5].c_str());
		return true;
	}
}

void ZapretStore::Load()
{
	m_results.clear();
	m_lastStrategy.clear();

	SettingsDocument::Doc doc;
	{
		std::lock_guard<std::mutex> lock(SettingsDocument::Mutex());
		SettingsDocument::Load(doc);
	}

	const SettingsDocument::KeyMap keys = SettingsDocument::GetSection(doc, "zapret_results");
	for (const auto& kv : keys)
	{
		if (kv.first == "last_strategy")
		{
			m_lastStrategy = kv.second;
			continue;
		}

		std::string name = kv.first;
		std::string value = kv.second;
		if (name.rfind("r.", 0) == 0)
			name = name.substr(2);

		StrategyTestEntry entry;
		if (!ParseResultValue(name, value, entry))
			continue;
		m_results[name] = entry;
	}
}

void ZapretStore::Save()
{
	SettingsDocument::KeyMap keys;
	if (!m_lastStrategy.empty())
		keys["last_strategy"] = m_lastStrategy;

	for (const auto& pair : m_results)
	{
		const StrategyTestEntry& entry = pair.second;
		keys["r." + pair.first] =
			std::string(entry.discordOk ? "1" : "0") + "|"
			+ (entry.youtubeOk ? "1" : "0") + "|"
			+ (entry.telegramOk ? "1" : "0") + "|"
			+ std::to_string(entry.pingMs) + "|"
			+ std::to_string(entry.runtimeSec) + "|"
			+ std::to_string(entry.providerDualOutageCount);
	}

	SettingsDocument::UpsertSection("zapret_results", keys);
}

void ZapretStore::SetLastStrategy(const std::string& strategyName)
{
	m_lastStrategy = strategyName;
}

void ZapretStore::ClearResults()
{
	m_results.clear();
}

void ZapretStore::SetResult(const std::string& strategyName, const StrategyTestEntry& entry)
{
	m_results[strategyName] = entry;
}

const StrategyTestEntry* ZapretStore::GetResult(const std::string& strategyName) const
{
	const auto it = m_results.find(strategyName);
	if (it == m_results.end())
		return nullptr;
	return &it->second;
}

int ZapretStore::FindStrategyIndex(const std::string& strategyName) const
{
	for (std::size_t i = 0; i < ZapretStrategies::kStrategyCount; ++i)
	{
		if (strategyName == ZapretStrategies::kStrategies[i].id)
			return static_cast<int>(i);
	}
	return -1;
}

std::string ZapretStore::GetBestStrategyName() const
{
	std::string best;
	int maxScore = -1;
	int bestPingMs = -1;

	for (const auto& pair : m_results)
	{
		const StrategyTestEntry& entry = pair.second;
		int score = 0;
		if (entry.discordOk)
			++score;
		if (entry.youtubeOk)
			++score;
		if (entry.telegramOk)
			++score;

		bool wins = false;
		if (score > maxScore)
			wins = true;
		else if (score == maxScore)
		{
			if (entry.pingMs >= 0 && bestPingMs < 0)
				wins = true;
			else if (entry.pingMs >= 0 && bestPingMs >= 0 && entry.pingMs < bestPingMs)
				wins = true;
		}

		if (wins)
		{
			maxScore = score;
			bestPingMs = entry.pingMs;
			best = pair.first;
		}
	}

	return best;
}

int ZapretStore::GetPreferredStrategyIndex(bool autoSelectBest) const
{
	if (autoSelectBest)
	{
		const std::string best = GetBestStrategyName();
		const int bestIndex = FindStrategyIndex(best);
		if (bestIndex >= 0)
			return bestIndex;
	}

	const int lastIndex = FindStrategyIndex(m_lastStrategy);
	if (lastIndex >= 0)
		return lastIndex;

	return 0;
}
