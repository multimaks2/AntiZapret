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

		// r.<id>=D|Y|T|ping|runtime|dual[|httpOk|httpErr|pingOk|pingFail|full]
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
		if (parts.size() >= offset + 7)
			outEntry.httpOk = std::atoi(parts[offset + 6].c_str());
		if (parts.size() >= offset + 8)
			outEntry.httpErr = std::atoi(parts[offset + 7].c_str());
		if (parts.size() >= offset + 9)
			outEntry.pingOk = std::atoi(parts[offset + 8].c_str());
		if (parts.size() >= offset + 10)
			outEntry.pingFail = std::atoi(parts[offset + 9].c_str());
		if (parts.size() >= offset + 11)
			outEntry.fullTest = parts[offset + 10] == "1";
		return true;
	}

	int EntryScore(const StrategyTestEntry& entry)
	{
		if (entry.fullTest || entry.httpOk > 0 || entry.httpErr > 0)
			return entry.httpOk;
		int score = 0;
		if (entry.discordOk)
			++score;
		if (entry.youtubeOk)
			++score;
		if (entry.telegramOk)
			++score;
		return score;
	}

	int EntryPingScore(const StrategyTestEntry& entry)
	{
		if (entry.fullTest || entry.pingOk > 0 || entry.pingFail > 0)
			return entry.pingOk;
		return entry.pingMs >= 0 ? 1 : 0;
	}

	std::string SanitizeTargetField(std::string value)
	{
		for (char& ch : value)
		{
			if (ch == '^' || ch == ';' || ch == '\r' || ch == '\n' || ch == '|')
				ch = ' ';
		}
		return value;
	}

	std::string EncodeTargets(const std::vector<StrategyTargetResultView>& targets)
	{
		std::string out;
		for (size_t i = 0; i < targets.size(); ++i)
		{
			if (i > 0)
				out += ';';
			const StrategyTargetResultView& t = targets[i];
			out += SanitizeTargetField(t.name);
			out += '^';
			out += SanitizeTargetField(t.detail);
			out += '^';
			out += (t.ok ? '1' : '0');
			out += '^';
			out += (t.isPing ? '1' : '0');
			out += '^';
			out += std::to_string(t.pingMs);
		}
		return out;
	}

	std::vector<StrategyTargetResultView> DecodeTargets(const std::string& value)
	{
		std::vector<StrategyTargetResultView> out;
		if (value.empty())
			return out;

		size_t rowStart = 0;
		while (rowStart <= value.size())
		{
			size_t rowEnd = value.find(';', rowStart);
			if (rowEnd == std::string::npos)
				rowEnd = value.size();
			const std::string row = value.substr(rowStart, rowEnd - rowStart);
			if (!row.empty())
			{
				std::vector<std::string> fields;
				size_t fieldStart = 0;
				while (fieldStart <= row.size())
				{
					const size_t fieldEnd = row.find('^', fieldStart);
					if (fieldEnd == std::string::npos)
					{
						fields.push_back(row.substr(fieldStart));
						break;
					}
					fields.push_back(row.substr(fieldStart, fieldEnd - fieldStart));
					fieldStart = fieldEnd + 1;
				}
				if (fields.size() >= 5)
				{
					StrategyTargetResultView target;
					target.name = fields[0];
					target.detail = fields[1];
					target.ok = fields[2] == "1";
					target.isPing = fields[3] == "1";
					target.pingMs = std::atoi(fields[4].c_str());
					out.push_back(std::move(target));
				}
			}
			if (rowEnd >= value.size())
				break;
			rowStart = rowEnd + 1;
		}
		return out;
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
		if (name.rfind("t.", 0) == 0)
		{
			name = name.substr(2);
			m_results[name].targets = DecodeTargets(value);
			continue;
		}
		if (name.rfind("r.", 0) == 0)
			name = name.substr(2);

		StrategyTestEntry entry;
		if (!ParseResultValue(name, value, entry))
			continue;
		entry.targets = m_results[name].targets;
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
			+ std::to_string(entry.providerDualOutageCount) + "|"
			+ std::to_string(entry.httpOk) + "|"
			+ std::to_string(entry.httpErr) + "|"
			+ std::to_string(entry.pingOk) + "|"
			+ std::to_string(entry.pingFail) + "|"
			+ (entry.fullTest ? "1" : "0");
		if (!entry.targets.empty())
			keys["t." + pair.first] = EncodeTargets(entry.targets);
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
	int bestPingScore = -1;
	int bestPingMs = -1;

	for (const auto& pair : m_results)
	{
		const StrategyTestEntry& entry = pair.second;
		const int score = EntryScore(entry);
		const int pingScore = EntryPingScore(entry);

		bool wins = false;
		if (score > maxScore)
			wins = true;
		else if (score == maxScore)
		{
			if (pingScore > bestPingScore)
				wins = true;
			else if (pingScore == bestPingScore)
			{
				if (entry.pingMs >= 0 && bestPingMs < 0)
					wins = true;
				else if (entry.pingMs >= 0 && bestPingMs >= 0 && entry.pingMs < bestPingMs)
					wins = true;
			}
		}

		if (wins)
		{
			maxScore = score;
			bestPingScore = pingScore;
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
