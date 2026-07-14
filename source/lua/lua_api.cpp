#include "lua/lua_api.h"

#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "lua/lua_runtime.h"
#include "ui/ui_layout.h"
#include "imgui.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstring>

void LuaApi::Bind(LuaRuntime* runtime, FontManager* fonts, ThemeManager* theme)
{
	m_runtime = runtime;
	m_fonts = fonts;
	m_theme = theme;
	m_titleBarProviderRef = LUA_NOREF;
}

LuaApi* LuaApi::FromState(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, kRegistryKey);
	LuaApi* api = static_cast<LuaApi*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return api;
}

void LuaApi::Register(lua_State* L)
{
	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_REGISTRYINDEX, kRegistryKey);

	static const luaL_Reg kBindings[] = {
		{ "getWindowSize", LuaGetWindowSize },
		{ "addEventHandler", LuaAddEventHandler },
		{ "setTitleBarText", LuaSetTitleBarText },
		{ "getTitleBarHeight", LuaGetTitleBarHeight },
		{ "dxDrawRectangle", LuaDrawRectangle },
		{ "pushClipRect", LuaPushClipRect },
		{ "popClipRect", LuaPopClipRect },
		{ "dxDrawPolyLine", LuaDrawPolyLine },
		{ "dxDrawText", LuaDrawText },
		{ "dxGetTextWidth", LuaGetTextWidth },
		{ "applyColorAlpha", LuaApplyColorAlpha },
		{ "getCursorPosition", LuaGetCursorPosition },
		{ "isMouseInArea", LuaIsMouseInArea },
		{ "isMouseClicked", LuaIsMouseClicked },
		{ "isMouseDown", LuaIsMouseDown },
		{ "getMouseWheel", LuaGetMouseWheel },
		{ "getDeltaTime", LuaGetDeltaTime },
		{ "getThemeColors", LuaGetThemeColors },
		{ "getThemeMix", LuaGetThemeMix },
		{ "isThemeLight", LuaIsThemeLight },
		{ "setThemeLight", LuaSetThemeLight },
		{ "setClipboardText", LuaSetClipboardText },
		{ "setTextInputFocus", LuaSetTextInputFocus },
		{ "getTextInput", LuaGetTextInput },
		{ "clearTextInput", LuaClearTextInput },
		{ "isTextInputFocused", LuaIsTextInputFocused },
		{ "tocolor", LuaToColor },
		{ nullptr, nullptr },
	};

	for (const luaL_Reg* binding = kBindings; binding->name; ++binding)
	{
		lua_pushcfunction(L, binding->func);
		lua_setglobal(L, binding->name);
	}
}

void LuaApi::SetWindow(HWND hwnd)
{
	m_frame.hwnd = hwnd;
}

void LuaApi::BeginFrame(HWND hwnd)
{
	m_frame.hwnd = hwnd;
	RECT clientRect = {};
	if (hwnd)
		GetClientRect(hwnd, &clientRect);
	m_frame.frameWidth = float(clientRect.right - clientRect.left);
	m_frame.frameHeight = float(clientRect.bottom - clientRect.top);

	const UiThemeColors theme = m_theme ? m_theme->GetColors() : UiThemeColors{};
	ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ m_frame.frameWidth, m_frame.frameHeight }, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.bg);
	ImGui::Begin(
		"##LuaUi",
		nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
	m_frame.drawList = ImGui::GetWindowDrawList();
}

void LuaApi::BeginContent(float y, float width, float height)
{
	ImGui::SetCursorPos({ 0.f, y });
	ImGui::BeginChild("##LuaContent", { width, height }, false, ImGuiWindowFlags_NoScrollbar);
	m_frame.drawList = ImGui::GetWindowDrawList();
	m_frame.drawOrigin = ImGui::GetWindowPos();
	m_frame.contentOrigin = { 0.f, y };
}

void LuaApi::EndContent()
{
	ImGui::EndChild();
	m_frame.contentOrigin = {};
}

void LuaApi::EndFrame()
{
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	m_frame.drawList = nullptr;
}

std::string LuaApi::GetTitleBarText(lua_State* L) const
{
	if (m_titleBarProviderRef != LUA_NOREF)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_titleBarProviderRef);
		if (lua_pcall(L, 0, 1, 0) == 0 && lua_isstring(L, -1))
		{
			const char* text = lua_tostring(L, -1);
			std::string result = text ? text : m_titleBarStatic;
			lua_pop(L, 1);
			return result;
		}
		lua_pop(L, 1);
	}
	return m_titleBarStatic;
}

void LuaApi::ClearTitleBar(lua_State* L)
{
	if (m_titleBarProviderRef != LUA_NOREF)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, m_titleBarProviderRef);
		m_titleBarProviderRef = LUA_NOREF;
	}
	m_titleBarStatic = "AntiZapret";
}

bool LuaApi::HandleWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	(void)lParam;
	if (!m_textInputFocus)
		return false;

	if (msg == WM_CHAR)
	{
		const wchar_t ch = static_cast<wchar_t>(wParam);
		if (ch < 32 || ch == 127)
			return false;
		if (m_textInput.size() >= kTextInputMaxLen)
			return true;

		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, (int)sizeof(utf8), nullptr, nullptr);
		if (len > 0)
			m_textInput.append(utf8, static_cast<size_t>(len));
		return true;
	}

	if (msg == WM_KEYDOWN)
	{
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (ctrl && (wParam == 'V' || wParam == 'v'))
		{
			if (OpenClipboard(nullptr))
			{
				const HANDLE data = GetClipboardData(CF_UNICODETEXT);
				if (data)
				{
					const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
					if (text)
					{
						const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
						if (bytes > 1)
						{
							std::string utf8(static_cast<size_t>(bytes - 1), '\0');
							WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), bytes, nullptr, nullptr);
							AppendTextInputUtf8(utf8);
						}
						GlobalUnlock(data);
					}
				}
				CloseClipboard();
			}
			return true;
		}

		if (wParam == VK_BACK || wParam == VK_DELETE)
		{
			if (!m_textInput.empty())
				m_textInput.pop_back();
			return true;
		}
		if (wParam == VK_ESCAPE)
		{
			m_textInputFocus = false;
			return true;
		}
	}

	return false;
}

ImVec2 LuaApi::ToDrawPos(float x, float y) const
{
	return { m_frame.drawOrigin.x + x, m_frame.drawOrigin.y + y };
}

POINT LuaApi::ContentCursorPos() const
{
	POINT pt = {};
	GetCursorPos(&pt);
	if (m_frame.hwnd)
		ScreenToClient(m_frame.hwnd, &pt);
	pt.x -= (LONG)m_frame.contentOrigin.x;
	pt.y -= (LONG)m_frame.contentOrigin.y;
	return pt;
}

ImFont* LuaApi::ResolveFont(const char* name, float scale) const
{
	(void)scale;
	if (!m_fonts)
		return ImGui::GetFont();
	if (name && strcmp(name, "tile") == 0)
		return m_fonts->GetTileFont();
	if (name && strcmp(name, "icon") == 0)
	{
		ImFont* iconFont = m_fonts->GetIconFont();
		return iconFont ? iconFont : ImGui::GetFont();
	}
	return ImGui::GetFont();
}

bool LuaApi::CopyStringToClipboardUtf8(const std::string& utf8) const
{
	if (utf8.empty() || !OpenClipboard(nullptr))
		return false;
	EmptyClipboard();

	const int wchars = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (wchars <= 0)
	{
		CloseClipboard();
		return false;
	}

	const SIZE_T byteCount = static_cast<SIZE_T>(wchars) * sizeof(WCHAR);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
	if (!memory)
	{
		CloseClipboard();
		return false;
	}

	LPWSTR dst = static_cast<LPWSTR>(GlobalLock(memory));
	if (!dst)
	{
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, dst, wchars);
	GlobalUnlock(memory);

	if (!SetClipboardData(CF_UNICODETEXT, memory))
	{
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}

	CloseClipboard();
	return true;
}

void LuaApi::AppendTextInputUtf8(const std::string& text)
{
	if (text.empty())
		return;
	const size_t space = kTextInputMaxLen - m_textInput.size();
	if (space == 0)
		return;
	m_textInput.append(text, 0, min(space, text.size()));
}

namespace
{
	ImU32 ColorToImU32(lua_Number color)
	{
		const unsigned int c = (unsigned int)color;
		const unsigned char b = (unsigned char)(c & 0xFF);
		const unsigned char g = (unsigned char)((c >> 8) & 0xFF);
		const unsigned char r = (unsigned char)((c >> 16) & 0xFF);
		const unsigned char a = (unsigned char)((c >> 24) & 0xFF);
		return IM_COL32(r, g, b, a);
	}

	bool IsColorByte(lua_State* L, int index)
	{
		return lua_isnumber(L, index) && lua_tonumber(L, index) >= 0.0 && lua_tonumber(L, index) <= 255.0;
	}

	ImU32 ReadColor(lua_State* L, int index, ImU32 defaultColor, int* nextIndex)
	{
		if (lua_isnoneornil(L, index))
		{
			if (nextIndex)
				*nextIndex = index + 1;
			return defaultColor;
		}

		if (IsColorByte(L, index) && IsColorByte(L, index + 1) && IsColorByte(L, index + 2))
		{
			const int r = (int)lua_tointeger(L, index);
			const int g = (int)lua_tointeger(L, index + 1);
			const int b = (int)lua_tointeger(L, index + 2);
			int consumed = 3;
			int a = 255;
			if (IsColorByte(L, index + 3))
			{
				a = (int)lua_tointeger(L, index + 3);
				consumed = 4;
			}
			if (nextIndex)
				*nextIndex = index + consumed;
			return IM_COL32(r, g, b, a);
		}

		if (nextIndex)
			*nextIndex = index + 1;
		return ColorToImU32(luaL_checknumber(L, index));
	}
}

int LuaApi::LuaGetWindowSize(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (!api || !api->m_frame.hwnd)
	{
		lua_pushinteger(L, 0);
		lua_pushinteger(L, 0);
		return 2;
	}
	RECT rc = {};
	GetClientRect(api->m_frame.hwnd, &rc);
	lua_pushinteger(L, rc.right - rc.left);
	lua_pushinteger(L, rc.bottom - rc.top);
	return 2;
}

int LuaApi::LuaAddEventHandler(lua_State* L)
{
	LuaApi* api = FromState(L);
	const char* event = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	if (api && api->m_runtime)
		api->m_runtime->AddEventHandler(event, ref);
	return 0;
}

int LuaApi::LuaSetTitleBarText(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (!api)
		return 0;

	if (lua_isstring(L, 1))
	{
		const char* text = lua_tostring(L, 1);
		api->m_titleBarStatic = text ? text : "";
		if (api->m_titleBarProviderRef != LUA_NOREF)
		{
			luaL_unref(L, LUA_REGISTRYINDEX, api->m_titleBarProviderRef);
			api->m_titleBarProviderRef = LUA_NOREF;
		}
		return 0;
	}

	luaL_checktype(L, 1, LUA_TFUNCTION);
	lua_pushvalue(L, 1);
	if (api->m_titleBarProviderRef != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, api->m_titleBarProviderRef);
	api->m_titleBarProviderRef = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int LuaApi::LuaGetTitleBarHeight(lua_State* L)
{
	lua_pushnumber(L, UiLayout::TitleBarHeight());
	return 1;
}

int LuaApi::LuaDrawRectangle(lua_State* L)
{
	LuaApi* api = FromState(L);
	const float x = (float)luaL_checknumber(L, 1);
	const float y = (float)luaL_checknumber(L, 2);
	const float w = (float)luaL_checknumber(L, 3);
	const float h = (float)luaL_checknumber(L, 4);
	int colorIndex = 5;
	const ImU32 color = ReadColor(L, colorIndex, IM_COL32_WHITE, &colorIndex);
	(void)lua_toboolean(L, colorIndex);
	(void)lua_toboolean(L, colorIndex + 1);
	const float rounding = (float)luaL_optnumber(L, colorIndex + 2, 0.0);

	if (!api || !api->m_frame.drawList)
		return 0;
	const ImVec2 pMin = api->ToDrawPos(x, y);
	const ImVec2 pMax = { pMin.x + w, pMin.y + h };
	api->m_frame.drawList->AddRectFilled(pMin, pMax, color, rounding);
	return 0;
}

int LuaApi::LuaPushClipRect(lua_State* L)
{
	LuaApi* api = FromState(L);
	const float x = (float)luaL_checknumber(L, 1);
	const float y = (float)luaL_checknumber(L, 2);
	const float w = (float)luaL_checknumber(L, 3);
	const float h = (float)luaL_checknumber(L, 4);
	if (!api || !api->m_frame.drawList)
		return 0;
	const ImVec2 pMin = api->ToDrawPos(x, y);
	const ImVec2 pMax = { pMin.x + w, pMin.y + h };
	api->m_frame.drawList->PushClipRect(pMin, pMax, true);
	return 0;
}

int LuaApi::LuaPopClipRect(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (api && api->m_frame.drawList)
		api->m_frame.drawList->PopClipRect();
	return 0;
}

int LuaApi::LuaDrawPolyLine(lua_State* L)
{
	LuaApi* api = FromState(L);
	luaL_checktype(L, 1, LUA_TTABLE);
	int colorIndex = 2;
	const ImU32 color = ReadColor(L, colorIndex, IM_COL32_WHITE, &colorIndex);
	const float width = (float)luaL_optnumber(L, colorIndex, 1.0);
	if (!api || !api->m_frame.drawList)
		return 0;

	api->m_frame.drawList->PathClear();
	const int count = (int)lua_objlen(L, 1);
	for (int i = 1; i <= count; ++i)
	{
		lua_rawgeti(L, 1, i);
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			continue;
		}
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		const float x = (float)lua_tonumber(L, -2);
		const float y = (float)lua_tonumber(L, -1);
		lua_pop(L, 3);
		api->m_frame.drawList->PathLineTo(api->ToDrawPos(x, y));
	}
	api->m_frame.drawList->PathStroke(color, 0, width);
	return 0;
}

int LuaApi::LuaDrawText(lua_State* L)
{
	LuaApi* api = FromState(L);
	const char* text = luaL_checkstring(L, 1);
	const float left = (float)luaL_checknumber(L, 2);
	const float top = (float)luaL_checknumber(L, 3);
	const float right = (float)luaL_checknumber(L, 4);
	const float bottom = (float)luaL_checknumber(L, 5);
	int nextArg = 6;
	const ImU32 color = ReadColor(L, nextArg, IM_COL32_WHITE, &nextArg);
	const float scale = (float)luaL_optnumber(L, nextArg++, 1.0);
	const char* fontName = luaL_optstring(L, nextArg++, "default");
	const char* alignX = luaL_optstring(L, nextArg++, "left");
	const char* alignY = luaL_optstring(L, nextArg++, "top");

	if (!api || !api->m_frame.drawList || !text)
		return 0;

	ImFont* font = api->ResolveFont(fontName, scale);
	float fontSize = font->LegacySize * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi * scale;
	if (fontName && strcmp(fontName, "icon") == 0)
		fontSize = 14.f * scale;
	const ImVec2 textSize = font->CalcTextSizeA(fontSize, 1e4f, 0.f, text);

	float drawX = left;
	float drawY = top;
	const float boxW = right - left;
	const float boxH = bottom - top;
	if (strcmp(alignX, "center") == 0)
		drawX = left + (boxW - textSize.x) * 0.5f;
	else if (strcmp(alignX, "right") == 0)
		drawX = right - textSize.x;
	if (strcmp(alignY, "center") == 0)
		drawY = top + (boxH - textSize.y) * 0.5f;
	else if (strcmp(alignY, "bottom") == 0)
		drawY = bottom - textSize.y;

	api->m_frame.drawList->AddText(font, fontSize, api->ToDrawPos(drawX, drawY), color, text);
	return 0;
}

int LuaApi::LuaGetTextWidth(lua_State* L)
{
	LuaApi* api = FromState(L);
	const char* text = luaL_checkstring(L, 1);
	const float scale = (float)luaL_optnumber(L, 2, 1.0);
	const char* fontName = luaL_optstring(L, 3, "default");
	ImFont* font = api ? api->ResolveFont(fontName, scale) : ImGui::GetFont();
	float fontSize = font->LegacySize * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi * scale;
	if (fontName && strcmp(fontName, "icon") == 0)
		fontSize = 14.f * scale;
	const ImVec2 size = font->CalcTextSizeA(fontSize, 1e4f, 0.f, text);
	lua_pushnumber(L, size.x);
	lua_pushnumber(L, size.y);
	return 2;
}

int LuaApi::LuaApplyColorAlpha(lua_State* L)
{
	const ImU32 col = ColorToImU32(luaL_checknumber(L, 1));
	float alpha = (float)luaL_checknumber(L, 2);
	if (alpha < 0.f)
		alpha = 0.f;
	if (alpha > 1.f)
		alpha = 1.f;

	ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(col);
	rgba.w *= alpha;

	const int r = (int)(rgba.x * 255.f + 0.5f);
	const int g = (int)(rgba.y * 255.f + 0.5f);
	const int b = (int)(rgba.z * 255.f + 0.5f);
	const int a = (int)(rgba.w * 255.f + 0.5f);
	lua_pushnumber(L, (a * 16777216) + (r * 65536) + (g * 256) + b);
	return 1;
}

int LuaApi::LuaGetCursorPosition(lua_State* L)
{
	LuaApi* api = FromState(L);
	const POINT pt = api ? api->ContentCursorPos() : POINT{};
	lua_pushinteger(L, pt.x);
	lua_pushinteger(L, pt.y);
	return 2;
}

int LuaApi::LuaIsMouseInArea(lua_State* L)
{
	LuaApi* api = FromState(L);
	const float x = (float)luaL_checknumber(L, 1);
	const float y = (float)luaL_checknumber(L, 2);
	const float w = (float)luaL_checknumber(L, 3);
	const float h = (float)luaL_checknumber(L, 4);
	const POINT pt = api ? api->ContentCursorPos() : POINT{};
	const bool inside = pt.x >= x && pt.x <= x + w && pt.y >= y && pt.y <= y + h;
	lua_pushboolean(L, inside);
	return 1;
}

int LuaApi::LuaIsMouseClicked(lua_State* L)
{
	const int button = (int)luaL_optinteger(L, 1, 1);
	ImGuiMouseButton btn = ImGuiMouseButton_Left;
	if (button == 2)
		btn = ImGuiMouseButton_Right;
	else if (button == 3)
		btn = ImGuiMouseButton_Middle;
	lua_pushboolean(L, ImGui::GetIO().MouseClicked[btn]);
	return 1;
}

int LuaApi::LuaIsMouseDown(lua_State* L)
{
	const int button = (int)luaL_optinteger(L, 1, 1);
	int vk = VK_LBUTTON;
	if (button == 2)
		vk = VK_RBUTTON;
	else if (button == 3)
		vk = VK_MBUTTON;
	lua_pushboolean(L, (GetAsyncKeyState(vk) & 0x8000) != 0);
	return 1;
}

int LuaApi::LuaGetMouseWheel(lua_State* L)
{
	lua_pushnumber(L, ImGui::GetIO().MouseWheel);
	return 1;
}

int LuaApi::LuaGetDeltaTime(lua_State* L)
{
	lua_pushnumber(L, ImGui::GetIO().DeltaTime);
	return 1;
}

int LuaApi::LuaGetThemeColors(lua_State* L)
{
	LuaApi* api = FromState(L);
	const UiThemeColors theme = api && api->m_theme ? api->m_theme->GetColors() : UiThemeColors{};
	lua_createtable(L, 0, 9);
	auto pushColor = [&](const char* key, const ImVec4& c) {
		lua_pushnumber(L, ((int)(c.w * 255) << 24) | ((int)(c.x * 255) << 16) | ((int)(c.y * 255) << 8) | (int)(c.z * 255));
		lua_setfield(L, -2, key);
	};
	pushColor("bg", theme.bg);
	pushColor("sidebarBg", theme.sidebarBg);
	pushColor("navActive", theme.navActive);
	pushColor("navHover", theme.navHover);
	pushColor("textPrimary", theme.textPrimary);
	pushColor("textMuted", theme.textMuted);
	pushColor("tileBg", theme.tileBg);
	pushColor("tileBorder", theme.tileBorder);
	pushColor("badgeBg", theme.badgeBg);
	return 1;
}

int LuaApi::LuaGetThemeMix(lua_State* L)
{
	LuaApi* api = FromState(L);
	lua_pushnumber(L, api && api->m_theme ? api->m_theme->GetMix() : 0.0);
	return 1;
}

int LuaApi::LuaIsThemeLight(lua_State* L)
{
	LuaApi* api = FromState(L);
	lua_pushboolean(L, api && api->m_theme ? api->m_theme->IsLight() : 0);
	return 1;
}

int LuaApi::LuaSetThemeLight(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (api && api->m_theme)
		api->m_theme->SetLight(lua_toboolean(L, 1) != 0);
	return 0;
}

int LuaApi::LuaSetClipboardText(lua_State* L)
{
	LuaApi* api = FromState(L);
	size_t len = 0;
	const char* text = luaL_checklstring(L, 1, &len);
	const bool ok = api ? api->CopyStringToClipboardUtf8(std::string(text, len)) : false;
	lua_pushboolean(L, ok);
	return 1;
}

int LuaApi::LuaSetTextInputFocus(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (!api)
		return 0;
	api->m_textInputFocus = lua_toboolean(L, 1) != 0;
	if (api->m_textInputFocus && api->m_frame.hwnd)
		SetFocus(api->m_frame.hwnd);
	return 0;
}

int LuaApi::LuaGetTextInput(lua_State* L)
{
	LuaApi* api = FromState(L);
	lua_pushstring(L, api ? api->m_textInput.c_str() : "");
	return 1;
}

int LuaApi::LuaClearTextInput(lua_State* L)
{
	LuaApi* api = FromState(L);
	if (api)
		api->m_textInput.clear();
	return 0;
}

int LuaApi::LuaIsTextInputFocused(lua_State* L)
{
	LuaApi* api = FromState(L);
	lua_pushboolean(L, api ? api->m_textInputFocus : 0);
	return 1;
}

int LuaApi::LuaToColor(lua_State* L)
{
	const int r = (int)luaL_checkinteger(L, 1);
	const int g = (int)luaL_checkinteger(L, 2);
	const int b = (int)luaL_checkinteger(L, 3);
	const int a = (int)luaL_optinteger(L, 4, 255);
	lua_pushnumber(L, (a * 16777216) + (r * 65536) + (g * 256) + b);
	return 1;
}
