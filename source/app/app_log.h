#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

enum class LogSource
{
	Zapret,
	Telegram,
	VpnRouting
};

enum class LogFilter
{
	All,
	Zapret,
	Telegram,
	VpnRouting
};

struct LogEntry
{
	LogSource source = LogSource::Zapret;
	std::chrono::system_clock::time_point time {};
	std::string text;
};

class AppLog
{
public:
	static AppLog& Instance();

	void Append(LogSource source, const std::string& text);
	void Clear(LogFilter filter);
	std::vector<LogEntry> Snapshot(LogFilter filter) const;

private:
	AppLog() = default;

	void AppendLineLocked(LogSource source, const std::string& line);
	static bool MatchesFilter(LogSource source, LogFilter filter);

	static constexpr size_t kMaxEntries = 3000;

	mutable std::mutex m_mutex;
	std::vector<LogEntry> m_entries;
	LogSource m_lastSource = LogSource::Zapret;
	std::string m_lastText;
};
