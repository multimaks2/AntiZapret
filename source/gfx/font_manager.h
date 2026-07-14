#pragma once

#include "imgui.h"

class FontManager
{
public:
	void Initialize();
	ImFont* GetIconFont() const { return m_iconFont; }
	ImFont* GetTileFont() const;

private:
	ImFont* m_iconFont = nullptr;
	ImFont* m_tileFont = nullptr;
};
