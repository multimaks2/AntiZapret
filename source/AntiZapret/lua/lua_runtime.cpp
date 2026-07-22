#include "lua/lua_runtime.h"

#include "lua/lua_api.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <filesystem>

bool LuaRuntime::Init(HWND hwnd, LuaApi& api)
{
	if (m_state)
		return true;

	m_api = &api;
	m_loadError.clear();
	m_loadScriptPath.clear();
	m_scriptLoaded = false;
	m_hwnd = hwnd;
	m_state = luaL_newstate(nullptr);
	if (!m_state)
	{
		m_loadError = "Не удалось создать виртуальную машину Lua.";
		return false;
	}

	luaL_openlibs(m_state);
	luaopen_utf8(m_state);
	api.Register(m_state);
	api.SetWindow(hwnd);
	return true;
}

void LuaRuntime::Shutdown()
{
	if (!m_state)
		return;

	if (m_scriptLoaded)
	{
		FireEvent("onStop");
		FireEvent("Stop");
	}

	if (m_api)
		m_api->ClearTitleBar(m_state);

	ClearEventHandlers();
	lua_close(m_state);
	m_state = nullptr;
	m_hwnd = nullptr;
	m_api = nullptr;
	m_scriptLoaded = false;
}

bool LuaRuntime::LoadClientScript(const char* path)
{
	m_loadError.clear();
	m_loadScriptPath.clear();
	m_scriptLoaded = false;

	if (!m_state)
	{
		m_loadError = "Lua не инициализирован.";
		return false;
	}
	if (!path || !path[0])
	{
		m_loadError = "Путь к скрипту не указан.";
		return false;
	}

	const std::string resolved = ResolveScriptPath(path);
	m_loadScriptPath = resolved;
	if (!std::filesystem::exists(resolved))
	{
		m_loadError = "Файл не найден: " + resolved;
		return false;
	}

	ClearEventHandlers();
	if (luaL_dofile(m_state, resolved.c_str()) != 0)
	{
		const char* err = lua_tostring(m_state, -1);
		m_loadError = err ? err : "Неизвестная ошибка загрузки Lua-скрипта.";
		lua_pop(m_state, 1);
		return false;
	}

	m_scriptLoaded = true;
	FireEvent("onStart");
	FireEvent("Start");
	return true;
}

void LuaRuntime::FireEvent(const char* eventName)
{
	if (!m_state || !eventName)
		return;

	const auto it = m_eventHandlers.find(eventName);
	if (it == m_eventHandlers.end())
		return;

	for (int ref : it->second)
	{
		lua_rawgeti(m_state, LUA_REGISTRYINDEX, ref);
		if (lua_pcall(m_state, 0, 0, 0) != 0)
		{
			const char* err = lua_tostring(m_state, -1);
			if (err && err[0])
				m_loadError = std::string("Ошибка в обработчике \"") + eventName + "\": " + err;
			else
				m_loadError = std::string("Ошибка в обработчике \"") + eventName + "\".";
			lua_pop(m_state, 1);
		}
	}
}

void LuaRuntime::AddEventHandler(const char* event, int handlerRef)
{
	m_eventHandlers[event].push_back(handlerRef);
}

void LuaRuntime::ClearEventHandlers()
{
	if (!m_state)
		return;
	for (auto& pair : m_eventHandlers)
	{
		for (int ref : pair.second)
			luaL_unref(m_state, LUA_REGISTRYINDEX, ref);
	}
	m_eventHandlers.clear();
}

std::string LuaRuntime::ResolveScriptPath(const char* path)
{
	if (!path || !path[0])
		return {};

	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
		return path;

	const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
	return (exeDir / path).string();
}
