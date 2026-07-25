#pragma once

#include "imgui.h"

#include <functional>

class UiSmoothScroll
{
public:
	void Draw(
		const char* id,
		ImVec2 viewportSize,
		float deltaTime,
		const std::function<void(float width)>& drawContent,
		float wheelMultiplier = 1.f,
		bool* stickToBottom = nullptr,
		bool enablePageScroll = true,
		bool showScrollbar = false);

	void JumpToBottom();

private:
	float m_scrollY = 0.f;
	float m_scrollDisplay = 0.f;
	float m_scrollVelocity = 0.f;
	bool m_jumpToBottom = false;
	bool m_scrollbarDragging = false;
	float m_scrollbarDragGrabOffset = 0.f;
	// Previous-frame layout flag: reserve right strip when scrollbar is needed.
	bool m_layoutNeedsScrollbar = false;
	float m_scrollbarAlpha = 0.f;
	// 0 = hidden off the right edge, 1 = fully slid into place (also drives content reserve).
	float m_scrollbarSlide = 0.f;
};
