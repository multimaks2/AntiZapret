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
		float wheelMultiplier = 1.f);

private:
	float m_scrollY = 0.f;
	float m_scrollDisplay = 0.f;
	float m_scrollVelocity = 0.f;
};
