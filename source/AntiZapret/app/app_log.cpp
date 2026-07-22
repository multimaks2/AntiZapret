#include "app/app_log.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
	std::string TrimCopy(std::string value)
	{
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
			value.erase(value.begin());
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
			value.pop_back();
		return value;
	}

	size_t FindSplitColon(const std::string& text)
	{
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] != ':')
				continue;
			// Skip URL schemes: https:// http://
			if (i + 2 < text.size() && text[i + 1] == '/' && text[i + 2] == '/')
				continue;
			return i;
		}
		return std::string::npos;
	}
}

AppLog& AppLog::Instance()
{
	static AppLog instance;
	return instance;
}

void AppLog::ParseActionResult(const std::string& text, std::string& outAction, std::string& outResult)
{
	outAction.clear();
	outResult.clear();
	const std::string trimmed = TrimCopy(text);
	if (trimmed.empty())
		return;

	std::string body = trimmed;
	if (body.front() == '[')
	{
		const size_t close = body.find(']');
		if (close != std::string::npos)
		{
			const std::string bracket = TrimCopy(body.substr(1, close - 1));
			const std::string rest = TrimCopy(body.substr(close + 1));
			if (!bracket.empty() && !rest.empty())
			{
				outAction = bracket;
				const size_t colon = FindSplitColon(rest);
				if (colon != std::string::npos)
				{
					const std::string left = TrimCopy(rest.substr(0, colon));
					const std::string right = TrimCopy(rest.substr(colon + 1));
					if (!left.empty() && !right.empty())
						outResult = left + ": " + right;
					else
						outResult = rest;
				}
				else
				{
					outResult = rest;
				}
				return;
			}
			if (!bracket.empty())
			{
				outAction = bracket;
				return;
			}
		}
	}

	const size_t colon = FindSplitColon(body);
	if (colon != std::string::npos)
	{
		outAction = TrimCopy(body.substr(0, colon));
		outResult = TrimCopy(body.substr(colon + 1));
		if (outAction.empty())
		{
			outAction = body;
			outResult.clear();
		}
		return;
	}

	outAction = body;
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

void AppLog::Append(LogSource source, const std::string& action, const std::string& result)
{
	const std::string trimmedAction = TrimCopy(action);
	if (trimmedAction.empty())
		return;

	const std::string trimmedResult = TrimCopy(result);
	const std::string dedupeKey = trimmedResult.empty()
		? trimmedAction
		: (trimmedAction + " : " + trimmedResult);

	std::lock_guard<std::mutex> lock(m_mutex);
	if (source == m_lastSource && dedupeKey == m_lastText)
		return;

	m_lastSource = source;
	m_lastText = dedupeKey;
	AppendStructuredLocked(source, trimmedAction, trimmedResult);
}

void AppLog::AppendLineLocked(LogSource source, const std::string& line)
{
	LogEntry entry {};
	entry.source = source;
	entry.time = std::chrono::system_clock::now();
	entry.text = line;
	ParseActionResult(line, entry.action, entry.result);
	if (entry.action.empty())
		entry.action = line;
	m_entries.push_back(std::move(entry));
	if (m_entries.size() > kMaxEntries)
		m_entries.erase(m_entries.begin(), m_entries.begin() + (m_entries.size() - kMaxEntries));
}

void AppLog::AppendStructuredLocked(LogSource source, const std::string& action, const std::string& result)
{
	LogEntry entry {};
	entry.source = source;
	entry.time = std::chrono::system_clock::now();
	entry.action = action;
	entry.result = result;
	entry.text = result.empty() ? action : (action + " : " + result);
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
