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
	std::string text;   // original line (compat)
	std::string action; // [действие]
	std::string result; // результат after " : "
};

class AppLog
{
public:
	static AppLog& Instance();

	void Append(LogSource source, const std::string& text);
	// Structured: ICON [tag] [time] [action] : result
	void Append(LogSource source, const std::string& action, const std::string& result);
	void Clear(LogFilter filter);
	std::vector<LogEntry> Snapshot(LogFilter filter) const;

	static void ParseActionResult(const std::string& text, std::string& outAction, std::string& outResult);

private:
	AppLog() = default;

	void AppendLineLocked(LogSource source, const std::string& line);
	void AppendStructuredLocked(LogSource source, const std::string& action, const std::string& result);
	static bool MatchesFilter(LogSource source, LogFilter filter);

	static constexpr size_t kMaxEntries = 3000;

	mutable std::mutex m_mutex;
	std::vector<LogEntry> m_entries;
	LogSource m_lastSource = LogSource::Zapret;
	std::string m_lastText;
};
