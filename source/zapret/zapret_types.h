#pragma once

enum class ZapretRunStatus
{
	Running,
	Starting,
	Stopped
};

enum class StrategyTestState
{
	Idle,
	Running,
	Paused,
	Completed
};

enum class SmartStrategyTuneState
{
	Idle,
	Running,
	Completed
};
