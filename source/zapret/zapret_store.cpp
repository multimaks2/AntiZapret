#include "zapret/zapret_store.h"

#include "zapret/strategies.hpp"
#include "zapret/zapret_paths.h"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	std::filesystem::path GetStorePath()
	{
		return std::filesystem::path(ZapretPaths::GetAppDirectory()) / L"result.ini";
	}

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
}

void ZapretStore::Load()
{
	m_results.clear();
	m_lastStrategy.clear();

	const std::filesystem::path iniPath = GetStorePath();
	std::ifstream input(iniPath, std::ios::binary);
	if (!input)
		return;

	std::string line;
	while (std::getline(input, line))
	{
		line = TrimLine(line);
		if (line.empty())
			continue;

		if (line.rfind("; last_strategy=", 0) == 0)
		{
			m_lastStrategy = TrimLine(line.substr(16));
			continue;
		}

		if (line[0] == ';' || line[0] == '#')
			continue;

		const std::vector<std::string> parts = SplitPipeFields(line);
		if (parts.size() < 4)
			continue;

		const std::string& name = parts[0];
		if (name.empty() || parts[1].size() != 1 || parts[2].size() != 1 || parts[3].size() != 1)
			continue;

		StrategyTestEntry entry;
		entry.discordOk = parts[1][0] == '1';
		entry.youtubeOk = parts[2][0] == '1';
		entry.telegramOk = parts[3][0] == '1';
		if (parts.size() >= 5)
			entry.pingMs = std::atoi(parts[4].c_str());
		if (parts.size() >= 6)
			entry.runtimeSec = std::atoi(parts[5].c_str());
		if (parts.size() >= 7)
			entry.providerDualOutageCount = std::atoi(parts[6].c_str());

		m_results[name] = entry;
	}
}

void ZapretStore::Save()
{
	const std::filesystem::path iniPath = GetStorePath();
	const std::filesystem::path tmpPath = iniPath.string() + ".tmp";

	std::error_code ec;
	std::filesystem::create_directories(iniPath.parent_path(), ec);
	std::filesystem::remove(tmpPath, ec);

	{
		std::ofstream output(tmpPath, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!output)
			return;

		output << "; AntiZapret strategy test results: name|D|Y|T|pingMs|runtimeSec|dualOutages\r\n";
		output << "; D/Y/T: 1=ok; pingMs=-1 если ICMP недоступен; runtimeSec=время работы; dualOutages=Discord+YouTube одновременно недоступны\r\n";
		if (!m_lastStrategy.empty())
			output << "; last_strategy=" << m_lastStrategy << "\r\n";

		for (std::size_t i = 0; i < ZapretStrategies::kStrategyCount; ++i)
		{
			const std::string name(ZapretStrategies::kStrategies[i].id);
			const auto it = m_results.find(name);
			if (it == m_results.end())
				continue;

			const StrategyTestEntry& entry = it->second;
			output << name << '|'
				<< (entry.discordOk ? '1' : '0') << '|'
				<< (entry.youtubeOk ? '1' : '0') << '|'
				<< (entry.telegramOk ? '1' : '0') << '|'
				<< entry.pingMs << '|'
				<< entry.runtimeSec << '|'
				<< entry.providerDualOutageCount << "\r\n";
		}
	}

	ec.clear();
	std::filesystem::remove(iniPath, ec);
	ec.clear();
	std::filesystem::rename(tmpPath, iniPath, ec);
	if (ec)
	{
		std::error_code ec2;
		std::filesystem::remove(iniPath, ec2);
		std::filesystem::rename(tmpPath, iniPath, ec2);
	}
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
