#include "gfx/font_manager.h"

#include "imgui.h"

#include <Windows.h>

#include <string>

namespace
{
	std::wstring ExeDirectoryW()
	{
		wchar_t modulePath[MAX_PATH] = {};
		const DWORD len = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		if (len == 0 || len >= MAX_PATH)
			return {};
		std::wstring path(modulePath, len);
		const size_t slash = path.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return {};
		return path.substr(0, slash + 1);
	}

	bool WideToUtf8(const std::wstring& wide, char* out, int outBytes)
	{
		return WideCharToMultiByte(
				   CP_UTF8,
				   0,
				   wide.c_str(),
				   -1,
				   out,
				   outBytes,
				   nullptr,
				   nullptr)
			> 0;
	}

	ImFont* TryAddFontFile(
		ImFontAtlas* atlas,
		const std::wstring& path,
		float size,
		const ImFontConfig* cfg,
		const ImWchar* ranges)
	{
		if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			return nullptr;
		char pathUtf8[MAX_PATH * 2] = {};
		if (!WideToUtf8(path, pathUtf8, static_cast<int>(sizeof pathUtf8)))
			return nullptr;
		return atlas->AddFontFromFileTTF(pathUtf8, size, cfg, ranges);
	}

	std::wstring FontsDirCandidate(const std::wstring& exeDir, const wchar_t* relative)
	{
		return exeDir + relative;
	}
}

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
		if (!wcscpy_s(iconPath, winDir) && !wcscat_s(iconPath, L"\\Fonts\\segmdl2.ttf")
			&& GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
		{
			if (WideCharToMultiByte(CP_UTF8, 0, iconPath, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) > 0)
			{
				static const ImWchar iconRanges[] = { 0xE700, 0xF8FF, 0 };
				ImFontConfig cfg;
				cfg.PixelSnapH = true;
				cfg.MergeMode = true;
				// Segoe MDL2 sits optically high in its metrics box (console uses merged glyphs).
				cfg.GlyphOffset.y = 14.f * 0.12f;
				io.Fonts->AddFontFromFileTTF(pathUtf8, 14.f, &cfg, iconRanges);
			}
		}
	}

	// Merge a tiny FA set into the UI font so console log lines can draw brand/solid icons
	// (logs are one text buffer — separate FA fonts would not apply).
	{
		const std::wstring exeDir = ExeDirectoryW();
		static const ImWchar kFaSolidLogRanges[] = {
			0xf0ac, 0xf0ac, // globe — VPN
			0xf120, 0xf120, // terminal — fallback
			0xf3ed, 0xf3ed, // shield-halved — Антизапрет
			0,
		};
		static const ImWchar kFaBrandLogRanges[] = {
			0xf2c6, 0xf2c6, // telegram — TG WS Proxy
			0,
		};

		ImFontConfig mergeCfg;
		mergeCfg.MergeMode = true;
		mergeCfg.PixelSnapH = true;
		mergeCfg.GlyphMinAdvanceX = 13.f;

		const std::wstring solidCandidates[] = {
			FontsDirCandidate(exeDir, L"fonts\\fa-solid-900.ttf"),
			FontsDirCandidate(exeDir, L"..\\..\\vendor\\fontawesome\\fa-solid-900.ttf"),
		};
		for (const std::wstring& candidate : solidCandidates)
		{
			if (TryAddFontFile(io.Fonts, candidate, 14.f, &mergeCfg, kFaSolidLogRanges))
				break;
		}

		const std::wstring brandCandidates[] = {
			FontsDirCandidate(exeDir, L"fonts\\fa-brands-400.ttf"),
			FontsDirCandidate(exeDir, L"..\\..\\vendor\\fontawesome\\fa-brands-400.ttf"),
		};
		for (const std::wstring& candidate : brandCandidates)
		{
			if (TryAddFontFile(io.Fonts, candidate, 14.f, &mergeCfg, kFaBrandLogRanges))
				break;
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
		if (!wcscpy_s(iconPath, winDir) && !wcscat_s(iconPath, L"\\Fonts\\segmdl2.ttf")
			&& GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
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

	const std::wstring exeDir = ExeDirectoryW();
	// FA Brands / Solid PUA spans roughly E000–F8FF.
	static const ImWchar kFaRanges[] = { 0xE000, 0xF8FF, 0 };

	ImFontConfig faCfg;
	faCfg.PixelSnapH = true;
	faCfg.GlyphMinAdvanceX = 14.f;

	const std::wstring brandCandidates[] = {
		FontsDirCandidate(exeDir, L"fonts\\fa-brands-400.ttf"),
		FontsDirCandidate(exeDir, L"..\\..\\vendor\\fontawesome\\fa-brands-400.ttf"),
	};
	for (const std::wstring& candidate : brandCandidates)
	{
		m_brandFont = TryAddFontFile(io.Fonts, candidate, 16.f, &faCfg, kFaRanges);
		if (m_brandFont)
			break;
	}

	ImFontConfig solidCfg = faCfg;
	const std::wstring solidCandidates[] = {
		FontsDirCandidate(exeDir, L"fonts\\fa-solid-900.ttf"),
		FontsDirCandidate(exeDir, L"..\\..\\vendor\\fontawesome\\fa-solid-900.ttf"),
	};
	for (const std::wstring& candidate : solidCandidates)
	{
		m_solidFont = TryAddFontFile(io.Fonts, candidate, 15.f, &solidCfg, kFaRanges);
		if (m_solidFont)
			break;
	}
}

ImFont* FontManager::GetTileFont() const
{
	return m_tileFont ? m_tileFont : ImGui::GetFont();
}
