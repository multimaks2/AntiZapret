#include "zapret/smart_strategy_engine.h"

#include "app/settings_document.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>
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

	bool ParseBool(const std::string& value)
	{
		return value == "1" || value == "true" || value == "yes" || value == "on";
	}

	bool StartsWith(const std::string& value, const char* prefix)
	{
		const size_t prefixLen = strlen(prefix);
		return value.size() >= prefixLen && value.compare(0, prefixLen, prefix) == 0;
	}

	bool IsDesyncModeArg(const std::string& arg)
	{
		return StartsWith(arg, "--dpi-desync=")
			&& !StartsWith(arg, "--dpi-desync-repeats=")
			&& !StartsWith(arg, "--dpi-desync-fooling=")
			&& !StartsWith(arg, "--dpi-desync-fake-")
			&& !StartsWith(arg, "--dpi-desync-fakedsplit-")
			&& !StartsWith(arg, "--dpi-desync-split-")
			&& !StartsWith(arg, "--dpi-desync-any-protocol=")
			&& !StartsWith(arg, "--dpi-desync-cutoff=");
	}

	int ParseRepeats(const std::string& arg)
	{
		const char* prefix = "--dpi-desync-repeats=";
		if (!StartsWith(arg, prefix))
			return -1;
		return std::atoi(arg.c_str() + strlen(prefix));
	}

	std::string SetRepeats(const std::string& arg, int value)
	{
		return std::string("--dpi-desync-repeats=") + std::to_string(value);
	}

	std::vector<std::string> SplitModes(const std::string& value)
	{
		std::vector<std::string> modes;
		size_t start = 0;
		while (start <= value.size())
		{
			const size_t pos = value.find(',', start);
			const std::string part = TrimLine(value.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
			if (!part.empty())
				modes.push_back(part);
			if (pos == std::string::npos)
				break;
			start = pos + 1;
		}
		return modes;
	}

	std::string JoinModes(const std::vector<std::string>& modes)
	{
		std::ostringstream output;
		for (size_t i = 0; i < modes.size(); ++i)
		{
			if (i != 0)
				output << ',';
			output << modes[i];
		}
		return output.str();
	}
}

void SmartStrategyGenome::LoadFromStrategy(int strategyIndex)
{
	args.clear();
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return;

	const auto& strategy = ZapretStrategies::kStrategies[static_cast<std::size_t>(strategyIndex)];
	args.reserve(strategy.argCount);
	for (std::size_t i = 0; i < strategy.argCount; ++i)
		args.emplace_back(strategy.args[i]);
}

void SmartStrategyGenome::Mutate(std::mt19937& rng)
{
	if (args.empty())
		return;

	std::vector<size_t> repeatIndices;
	std::vector<size_t> desyncIndices;
	std::vector<size_t> foolingIndices;
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (StartsWith(args[i], "--dpi-desync-repeats="))
			repeatIndices.push_back(i);
		else if (IsDesyncModeArg(args[i]))
			desyncIndices.push_back(i);
		else if (StartsWith(args[i], "--dpi-desync-fooling="))
			foolingIndices.push_back(i);
	}

	std::uniform_int_distribution<int> mutationKind(0, 2);
	switch (mutationKind(rng))
	{
	case 0:
		if (!repeatIndices.empty())
		{
			const size_t index = repeatIndices[static_cast<size_t>(rng() % repeatIndices.size())];
			int repeats = ParseRepeats(args[index]);
			if (repeats < 0)
				repeats = 6;
			std::uniform_int_distribution<int> delta(-1, 1);
			repeats = (std::max)(4, (std::min)(12, repeats + delta(rng)));
			args[index] = SetRepeats(args[index], repeats);
		}
		break;
	case 1:
		if (!desyncIndices.empty())
		{
			static const char* kPool[] = { "fake", "fakedsplit", "multisplit", "disorder" };
			const size_t index = desyncIndices[static_cast<size_t>(rng() % desyncIndices.size())];
			std::vector<std::string> modes = SplitModes(args[index].substr(strlen("--dpi-desync=")));
			if (modes.empty())
				modes.push_back("fake");

			const char* mode = kPool[rng() % 4];
			const auto existing = std::find(modes.begin(), modes.end(), mode);
			if (existing != modes.end())
				modes.erase(existing);
			else
				modes.push_back(mode);

			if (modes.empty())
				modes.push_back("fake");

			args[index] = std::string("--dpi-desync=") + JoinModes(modes);
		}
		break;
	default:
		if (!foolingIndices.empty())
		{
			static const char* kPool[] = { "ts", "badseq", "md5sig" };
			const size_t index = foolingIndices[static_cast<size_t>(rng() % foolingIndices.size())];
			if ((rng() & 1u) == 0u)
			{
				args.erase(args.begin() + static_cast<std::ptrdiff_t>(index));
				break;
			}
			args[index] = std::string("--dpi-desync-fooling=") + kPool[rng() % 3];
		}
		break;
	}
}

std::string SmartStrategyGenome::BuildSummary() const
{
	int minRepeats = -1;
	int maxRepeats = -1;
	bool hasFakedsplit = false;
	bool hasMultisplit = false;
	bool hasFake = false;
	bool hasFooling = false;

	for (const std::string& arg : args)
	{
		if (StartsWith(arg, "--dpi-desync-repeats="))
		{
			const int repeats = ParseRepeats(arg);
			if (repeats >= 0)
			{
				if (minRepeats < 0 || repeats < minRepeats)
					minRepeats = repeats;
				if (maxRepeats < 0 || repeats > maxRepeats)
					maxRepeats = repeats;
			}
		}
		else if (IsDesyncModeArg(arg))
		{
			const std::string modes = arg.substr(strlen("--dpi-desync="));
			hasFake = hasFake || modes.find("fake") != std::string::npos;
			hasFakedsplit = hasFakedsplit || modes.find("fakedsplit") != std::string::npos;
			hasMultisplit = hasMultisplit || modes.find("multisplit") != std::string::npos;
		}
		else if (StartsWith(arg, "--dpi-desync-fooling="))
		{
			hasFooling = true;
		}
	}

	std::ostringstream output;
	output << "general";
	if (hasFake)
		output << "+fake";
	if (hasFakedsplit)
		output << "+split";
	if (hasMultisplit)
		output << "+multi";
	if (minRepeats >= 0)
	{
		if (minRepeats == maxRepeats)
			output << " r" << minRepeats;
		else
			output << " r" << minRepeats << "-" << maxRepeats;
	}
	if (hasFooling)
		output << " fool";
	return output.str();
}

void SmartStrategyEngine::Load()
{
	m_activeGenome.args.clear();
	m_bestScore = 0.f;
	m_enabled = false;
	m_lastExplanation.clear();

	SettingsDocument::Doc doc;
	{
		std::lock_guard<std::mutex> lock(SettingsDocument::Mutex());
		SettingsDocument::Load(doc);
	}

	const SettingsDocument::KeyMap keys = SettingsDocument::GetSection(doc, "smart_strategy");
	if (keys.empty())
	{
		ResetFromTemplate();
		return;
	}

	std::vector<std::pair<int, std::string>> indexedArgs;
	for (const auto& kv : keys)
	{
		if (kv.first == "enabled")
			m_enabled = ParseBool(kv.second);
		else if (kv.first == "best_score")
			m_bestScore = static_cast<float>(std::strtod(kv.second.c_str(), nullptr));
		else if (kv.first == "explanation")
			m_lastExplanation = kv.second;
		else if (StartsWith(kv.first, "arg_"))
			indexedArgs.emplace_back(std::atoi(kv.first.c_str() + 4), kv.second);
	}

	std::sort(indexedArgs.begin(), indexedArgs.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	std::vector<std::string> loadedArgs;
	loadedArgs.reserve(indexedArgs.size());
	for (const auto& pair : indexedArgs)
		loadedArgs.push_back(pair.second);

	if (!loadedArgs.empty())
		m_activeGenome.args = std::move(loadedArgs);
	else
		ResetFromTemplate();
}

void SmartStrategyEngine::Save()
{
	SettingsDocument::KeyMap keys;
	keys["enabled"] = m_enabled ? "1" : "0";
	keys["best_score"] = std::to_string(m_bestScore);
	keys["explanation"] = m_lastExplanation;
	for (size_t i = 0; i < m_activeGenome.args.size(); ++i)
		keys["arg_" + std::to_string(i)] = m_activeGenome.args[i];
	SettingsDocument::UpsertSection("smart_strategy", keys);
}

void SmartStrategyEngine::SetEnabled(bool enabled)
{
	m_enabled = enabled;
	if (enabled && m_activeGenome.args.empty())
		ResetFromTemplate();
	Save();
}

void SmartStrategyEngine::ResetFromTemplate()
{
	m_activeGenome.LoadFromStrategy(kTemplateStrategyIndex);
	m_bestScore = 0.f;
	m_lastExplanation = "Шаблон general, параметры winws будут подбираться автоматически.";
}

SmartStrategyGenome SmartStrategyEngine::CreateMutation(std::mt19937& rng) const
{
	SmartStrategyGenome genome = m_activeGenome;
	genome.Mutate(rng);
	return genome;
}

void SmartStrategyEngine::CommitGenome(const SmartStrategyGenome& genome, float score)
{
	m_activeGenome = genome;
	m_bestScore = score;
	m_lastExplanation = BuildExplanation(genome, score);
	Save();
}

bool SmartStrategyEngine::ConsiderCandidate(const SmartStrategyGenome& genome, float score)
{
	if (score > m_bestScore + 0.001f)
	{
		CommitGenome(genome, score);
		return true;
	}
	return false;
}

float SmartStrategyEngine::ComputeReward(const StrategyTestEntry& entry, bool telegramViaMtproto)
{
	float reward = 0.f;
	if (entry.discordOk)
		reward += 3.f;
	if (entry.youtubeOk)
		reward += 3.f;
	if (entry.telegramOk || telegramViaMtproto)
		reward += 2.f;
	if (entry.pingMs >= 0)
		reward += (std::max)(0.f, 2.f - static_cast<float>(entry.pingMs) / 100.f);
	return reward / 10.f;
}

float SmartStrategyEngine::ScoreFromProbe(
	bool discord,
	bool youtube,
	bool telegram,
	int pingMs,
	bool telegramViaMtproto)
{
	StrategyTestEntry entry;
	entry.discordOk = discord;
	entry.youtubeOk = youtube;
	entry.telegramOk = telegram;
	entry.pingMs = pingMs;
	return ComputeReward(entry, telegramViaMtproto) * 10.f;
}

std::string SmartStrategyEngine::BuildExplanation(const SmartStrategyGenome& genome, float score) const
{
	std::ostringstream output;
	output << "Конфиг «" << genome.BuildSummary() << "», score " << static_cast<int>(score * 10) / 10.f;
	return output.str();
}

SmartStrategyStatus SmartStrategyEngine::BuildStatus(
	SmartStrategyTuneState tuneState,
	int tuneCurrent,
	int tuneTotal) const
{
	SmartStrategyStatus status;
	status.bestScore = m_bestScore;
	status.summary = m_activeGenome.BuildSummary();
	status.explanation = m_lastExplanation.empty()
		? BuildExplanation(m_activeGenome, m_bestScore)
		: m_lastExplanation;
	status.tuneState = tuneState;
	status.tuneCurrent = tuneCurrent;
	status.tuneTotal = tuneTotal;
	return status;
}
