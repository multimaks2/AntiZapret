#include "gfx/font_manager.h"

#include "imgui.h"

#include <Windows.h>

void FontManager::Initialize()
{
	ImGuiIO& io = ImGui::GetIO();
	const ImWchar* textRanges = io.Fonts->GetGlyphRangesCyrillic();
	wchar_t winDir[MAX_PATH] = {};
	const bool hasWinDir = GetWindowsDirectoryW(winDir, MAX_PATH) != 0;
	char pathUtf8[MAX_PATH * 2] = {};
	bool loaded = false;

	if (hasWinDir)
	{
		const wchar_t* names[] = { L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf", L"\\Fonts\\arial.ttf" };
		for (const wchar_t* name : names)
		{
			wchar_t path[MAX_PATH] = {};
			if (wcscpy_s(path, winDir) || wcscat_s(path, name))
				continue;
			if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
				continue;
			if (WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) <= 0)
				continue;
			if (io.Fonts->AddFontFromFileTTF(pathUtf8, 14.f, nullptr, textRanges))
			{
				loaded = true;
				break;
			}
		}
	}
	if (!loaded)
		io.Fonts->AddFontDefault();

	if (hasWinDir)
	{
		wchar_t iconPath[MAX_PATH] = {};
		if (!wcscpy_s(iconPath, winDir) && !wcscat_s(iconPath, L"\\Fonts\\segmdl2.ttf") && GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
		{
			if (WideCharToMultiByte(CP_UTF8, 0, iconPath, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) > 0)
			{
				static const ImWchar iconRanges[] = { 0xE700, 0xF8FF, 0 };
				ImFontConfig cfg;
				cfg.PixelSnapH = true;
				cfg.MergeMode = true;
				io.Fonts->AddFontFromFileTTF(pathUtf8, 14.f, &cfg, iconRanges);
			}
		}
	}

	if (hasWinDir)
	{
		const wchar_t* names[] = { L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf", L"\\Fonts\\arial.ttf" };
		for (const wchar_t* name : names)
		{
			wchar_t path[MAX_PATH] = {};
			if (wcscpy_s(path, winDir) || wcscat_s(path, name))
				continue;
			if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
				continue;
			if (WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) <= 0)
				continue;
			if (ImFont* font = io.Fonts->AddFontFromFileTTF(pathUtf8, 15.f, nullptr, textRanges))
			{
				m_tileFont = font;
				break;
			}
		}
	}
	if (!m_tileFont)
		m_tileFont = io.Fonts->Fonts.empty() ? io.Fonts->AddFontDefault() : io.Fonts->Fonts[0];

	if (hasWinDir)
	{
		wchar_t iconPath[MAX_PATH] = {};
		if (!wcscpy_s(iconPath, winDir) && !wcscat_s(iconPath, L"\\Fonts\\segmdl2.ttf") && GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
		{
			if (WideCharToMultiByte(CP_UTF8, 0, iconPath, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) > 0)
			{
				static const ImWchar iconRanges[] = { 0xE700, 0xF8FF, 0 };
				ImFontConfig iconCfg;
				iconCfg.PixelSnapH = true;
				m_iconFont = io.Fonts->AddFontFromFileTTF(pathUtf8, 16.f, &iconCfg, iconRanges);
			}
		}
	}
}

ImFont* FontManager::GetTileFont() const
{
	return m_tileFont ? m_tileFont : ImGui::GetFont();
}
