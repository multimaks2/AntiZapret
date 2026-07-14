#pragma once

#include <Windows.h>
#include "imgui.h"

extern "C" {
#include "lauxlib.h"
}

#include <string>

struct lua_State;
class FontManager;
class ThemeManager;
class LuaRuntime;

class LuaApi
{
public:
	void Bind(LuaRuntime* runtime, FontManager* fonts, ThemeManager* theme);

	void Register(lua_State* L);
	static LuaApi* FromState(lua_State* L);
	void SetWindow(HWND hwnd);
	void BeginFrame(HWND hwnd);
	void BeginContent(float y, float width, float height);
	void EndContent();
	void EndFrame();

	std::string GetTitleBarText(lua_State* L) const;
	void ClearTitleBar(lua_State* L);
	bool HandleWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
	struct FrameState
	{
		HWND hwnd = nullptr;
		ImDrawList* drawList = nullptr;
		ImVec2 drawOrigin = {};
		ImVec2 contentOrigin = {};
		float frameWidth = 0.f;
		float frameHeight = 0.f;
	};

	static int LuaGetWindowSize(lua_State* L);
	static int LuaAddEventHandler(lua_State* L);
	static int LuaSetTitleBarText(lua_State* L);
	static int LuaGetTitleBarHeight(lua_State* L);
	static int LuaDrawRectangle(lua_State* L);
	static int LuaPushClipRect(lua_State* L);
	static int LuaPopClipRect(lua_State* L);
	static int LuaDrawPolyLine(lua_State* L);
	static int LuaDrawText(lua_State* L);
	static int LuaGetTextWidth(lua_State* L);
	static int LuaApplyColorAlpha(lua_State* L);
	static int LuaGetCursorPosition(lua_State* L);
	static int LuaIsMouseInArea(lua_State* L);
	static int LuaIsMouseClicked(lua_State* L);
	static int LuaIsMouseDown(lua_State* L);
	static int LuaGetMouseWheel(lua_State* L);
	static int LuaGetDeltaTime(lua_State* L);
	static int LuaGetThemeColors(lua_State* L);
	static int LuaGetThemeMix(lua_State* L);
	static int LuaIsThemeLight(lua_State* L);
	static int LuaSetThemeLight(lua_State* L);
	static int LuaSetClipboardText(lua_State* L);
	static int LuaSetTextInputFocus(lua_State* L);
	static int LuaGetTextInput(lua_State* L);
	static int LuaClearTextInput(lua_State* L);
	static int LuaIsTextInputFocused(lua_State* L);
	static int LuaToColor(lua_State* L);

	ImVec2 ToDrawPos(float x, float y) const;
	POINT ContentCursorPos() const;
	ImFont* ResolveFont(const char* name, float scale) const;
	bool CopyStringToClipboardUtf8(const std::string& utf8) const;
	void AppendTextInputUtf8(const std::string& text);

	LuaRuntime* m_runtime = nullptr;
	FontManager* m_fonts = nullptr;
	ThemeManager* m_theme = nullptr;
	FrameState m_frame;
	std::string m_titleBarStatic = "AntiZapret";
	int m_titleBarProviderRef = LUA_NOREF;
	std::string m_textInput;
	bool m_textInputFocus = false;

	static constexpr size_t kTextInputMaxLen = 253;
	static constexpr const char* kRegistryKey = "AntiZapretLuaApi";
};
