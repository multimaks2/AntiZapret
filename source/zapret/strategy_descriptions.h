#pragma once

struct StrategyDescription
{
	const char* summary;
	const char* const* keyPoints;
	const char* tspuDetail;
};

namespace StrategyDescriptions
{
	const StrategyDescription* GetByIndex(int strategyIndex);
	const StrategyDescription* GetById(const char* strategyId);
	const StrategyDescription* GetSmartStrategy();
}
