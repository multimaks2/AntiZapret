#include "app/settings_document.h"

#include "zapret/zapret_paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t");
		return value.substr(start, end - start + 1);
	}

	std::vector<std::string> g_sectionOrder = {
		"tg_proxy",
		"antizapret",
		"ui",
		"scroll",
		"vpn",
		"smart_strategy",
		"zapret_results",
	};
}

namespace SettingsDocument
{
	std::mutex& Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	bool Load(Doc& out)
	{
		out.clear();
		const std::filesystem::path path = ZapretPaths::GetSettingsPath();
		std::ifstream input(path, std::ios::binary);
		if (!input)
			return false;

		std::string currentSection;
		std::string line;
		while (std::getline(input, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;

			if (line.front() == '[' && line.back() == ']')
			{
				currentSection = line.substr(1, line.size() - 2);
				continue;
			}

			const size_t eq = line.find('=');
			if (eq == std::string::npos || currentSection.empty())
				continue;

			out[currentSection][Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
		}
		return true;
	}

	bool Save(const Doc& doc)
	{
		const std::filesystem::path path = ZapretPaths::GetSettingsPath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		const std::filesystem::path tmpPath = path.wstring() + L".tmp";
		std::filesystem::remove(tmpPath, ec);

		{
			std::ofstream output(tmpPath, std::ios::binary | std::ios::trunc);
			if (!output)
				return false;

			output << "; AntiZapret settings\r\n";

			std::vector<std::string> written;
			auto writeSection = [&](const std::string& section)
			{
				const auto it = doc.find(section);
				if (it == doc.end() || it->second.empty())
					return;
				if (std::find(written.begin(), written.end(), section) != written.end())
					return;
				written.push_back(section);
				output << "[" << section << "]\r\n";
				for (const auto& kv : it->second)
					output << kv.first << "=" << kv.second << "\r\n";
			};

			for (const std::string& section : g_sectionOrder)
				writeSection(section);
			for (const auto& pair : doc)
				writeSection(pair.first);
		}

		ec.clear();
		std::filesystem::remove(path, ec);
		ec.clear();
		std::filesystem::rename(tmpPath, path, ec);
		if (ec)
		{
			std::error_code ec2;
			std::filesystem::remove(path, ec2);
			std::filesystem::rename(tmpPath, path, ec2);
			return !ec2;
		}
		return true;
	}

	KeyMap GetSection(const Doc& doc, const std::string& section)
	{
		const auto it = doc.find(section);
		if (it == doc.end())
			return {};
		return it->second;
	}

	void SetSection(Doc& doc, const std::string& section, const KeyMap& keys)
	{
		if (keys.empty())
			doc.erase(section);
		else
			doc[section] = keys;
	}

	void UpsertSection(const std::string& section, const KeyMap& keys)
	{
		std::lock_guard<std::mutex> lock(Mutex());
		Doc doc;
		Load(doc);
		SetSection(doc, section, keys);
		Save(doc);
	}
}
