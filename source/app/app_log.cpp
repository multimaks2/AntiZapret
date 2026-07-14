#include "app/app_log.h"

#include <algorithm>
#include <sstream>

AppLog& AppLog::Instance()
{
	static AppLog instance;
	return instance;
}

void AppLog::Append(LogSource source, const std::string& text)
{
	if (text.empty())
		return;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (source == m_lastSource && text == m_lastText)
		return;

	m_lastSource = source;
	m_lastText = text;

	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		AppendLineLocked(source, line);
	}
}

void AppLog::AppendLineLocked(LogSource source, const std::string& line)
{
	LogEntry entry {};
	entry.source = source;
	entry.time = std::chrono::system_clock::now();
	entry.text = line;
	m_entries.push_back(std::move(entry));
	if (m_entries.size() > kMaxEntries)
		m_entries.erase(m_entries.begin(), m_entries.begin() + (m_entries.size() - kMaxEntries));
}

void AppLog::Clear(LogFilter filter)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (filter == LogFilter::All)
	{
		m_entries.clear();
		m_lastText.clear();
		return;
	}

	m_entries.erase(
		std::remove_if(
			m_entries.begin(),
			m_entries.end(),
			[filter](const LogEntry& entry) { return MatchesFilter(entry.source, filter); }),
		m_entries.end());
	m_lastText.clear();
}

std::vector<LogEntry> AppLog::Snapshot(LogFilter filter) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<LogEntry> result;
	result.reserve(m_entries.size());
	for (const LogEntry& entry : m_entries)
	{
		if (MatchesFilter(entry.source, filter))
			result.push_back(entry);
	}
	return result;
}

bool AppLog::MatchesFilter(LogSource source, LogFilter filter)
{
	switch (filter)
	{
	case LogFilter::All: return true;
	case LogFilter::Zapret: return source == LogSource::Zapret;
	case LogFilter::Telegram: return source == LogSource::Telegram;
	case LogFilter::VpnRouting: return source == LogSource::VpnRouting;
	}
	return true;
}
