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
	const StrategyDescription* GetSmartStrategy();
}
