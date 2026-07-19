#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

// Thread-safe sectioned settings.ini document (preserves unrelated sections on save).
namespace SettingsDocument
{
	using KeyMap = std::map<std::string, std::string>;
	using Doc = std::map<std::string, KeyMap>;

	std::mutex& Mutex();
	bool Load(Doc& out);
	bool Save(const Doc& doc);
	KeyMap GetSection(const Doc& doc, const std::string& section);
	void SetSection(Doc& doc, const std::string& section, const KeyMap& keys);
	void UpsertSection(const std::string& section, const KeyMap& keys);
}
