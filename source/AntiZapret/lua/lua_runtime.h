#pragma once

#include <Windows.h>

#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

class LuaApi;

class LuaRuntime
{
public:
	bool Init(HWND hwnd, LuaApi& api);
	void Shutdown();

	bool LoadClientScript(const char* path);
	void FireEvent(const char* eventName);
	void AddEventHandler(const char* event, int handlerRef);

	bool IsScriptLoaded() const { return m_scriptLoaded; }
	bool HasLoadError() const { return !m_loadError.empty(); }
	const char* GetLoadError() const { return m_loadError.c_str(); }
	const char* GetLoadScriptPath() const { return m_loadScriptPath.c_str(); }
	bool IsReady() const { return m_state != nullptr; }
	lua_State* State() const { return m_state; }
	HWND Window() const { return m_hwnd; }

private:
	void ClearEventHandlers();
	static std::string ResolveScriptPath(const char* path);

	lua_State* m_state = nullptr;
	HWND m_hwnd = nullptr;
	LuaApi* m_api = nullptr;
	bool m_scriptLoaded = false;
	std::string m_loadError;
	std::string m_loadScriptPath;
	std::unordered_map<std::string, std::vector<int>> m_eventHandlers;
};
