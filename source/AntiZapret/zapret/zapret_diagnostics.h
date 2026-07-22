#pragma once

#include <string>
#include <vector>

namespace ZapretDiagnostics
{
	enum class Severity
	{
		Ok,
		Warn,
		Error,
		Info
	};

	struct Line
	{
		Severity severity = Severity::Info;
		std::string text;
	};

	struct Report
	{
		std::vector<Line> lines;
		std::vector<std::string> conflictingServices;
		bool askClearDiscordCache = true;
		int errorCount = 0;
		int warnCount = 0;
	};

	// Mirrors service.bat → Run Diagnostics (item 10).
	Report Run();

	bool RemoveServices(const std::vector<std::string>& serviceNames);
	bool ClearDiscordCache();
}
