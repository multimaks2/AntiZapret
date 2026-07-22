#pragma once

#include "zapret/strategies.hpp"
#include "zapret/zapret_store.h"
#include "zapret/zapret_types.h"

#include <random>
#include <string>
#include <vector>

struct SmartStrategyGenome
{
	std::vector<std::string> args;

	void LoadFromStrategy(int strategyIndex);
	void Mutate(std::mt19937& rng);
	std::string BuildSummary() const;
};

struct SmartStrategyStatus
{
	float bestScore = 0.f;
	std::string summary;
	std::string explanation;
	SmartStrategyTuneState tuneState = SmartStrategyTuneState::Idle;
	int tuneCurrent = 0;
	int tuneTotal = 0;
};

class SmartStrategyEngine
{
public:
	static constexpr const char* kLabel = "Умная стратегия";
	static constexpr int kTemplateStrategyIndex = ZapretStrategies::kGeneralStrategyIndex;
	static constexpr int kDefaultTuneIterations = 12;

	void Load();
	void Save();

	bool IsEnabled() const { return m_enabled; }
	void SetEnabled(bool enabled);

	const SmartStrategyGenome& GetActiveGenome() const { return m_activeGenome; }
	float GetBestScore() const { return m_bestScore; }
	const std::string& GetLastExplanation() const { return m_lastExplanation; }

	void ResetFromTemplate();
	SmartStrategyGenome CreateMutation(std::mt19937& rng) const;
	void CommitGenome(const SmartStrategyGenome& genome, float score);
	bool ConsiderCandidate(const SmartStrategyGenome& genome, float score);

	SmartStrategyStatus BuildStatus(SmartStrategyTuneState tuneState, int tuneCurrent, int tuneTotal) const;

	static float ComputeReward(const StrategyTestEntry& entry, bool telegramViaMtproto);
	static float ScoreFromProbe(bool discord, bool youtube, bool telegram, int pingMs, bool telegramViaMtproto);

private:
	std::string BuildExplanation(const SmartStrategyGenome& genome, float score) const;

	SmartStrategyGenome m_activeGenome;
	float m_bestScore = 0.f;
	bool m_enabled = false;
	std::string m_lastExplanation;
};
