#pragma once

#include "imgui.h"

class FontManager
{
public:
	void Initialize();
	// Segoe MDL2 — chevrons, toolbar glyphs, fallback service icons.
	ImFont* GetIconFont() const { return m_iconFont; }
	// Font Awesome Solid — sidebar / generic UI.
	ImFont* GetSolidFont() const { return m_solidFont ? m_solidFont : m_iconFont; }
	// Font Awesome Brands — app/service logos.
	ImFont* GetBrandFont() const { return m_brandFont ? m_brandFont : m_iconFont; }
	ImFont* GetTileFont() const;

private:
	ImFont* m_iconFont = nullptr;
	ImFont* m_solidFont = nullptr;
	ImFont* m_brandFont = nullptr;
	ImFont* m_tileFont = nullptr;
};
