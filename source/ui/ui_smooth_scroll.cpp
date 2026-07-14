#include "ui/ui_smooth_scroll.h"

#include "imgui.h"

#include <cmath>

namespace
{
	float Clamp(float value, float minValue, float maxValue)
	{
		if (value < minValue)
			return minValue;
		if (value > maxValue)
			return maxValue;
		return value;
	}
}

void UiSmoothScroll::Draw(
	const char* id,
	ImVec2 viewportSize,
	float deltaTime,
	const std::function<void(float width)>& drawContent,
	float wheelMultiplier)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.f);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, { 0, 0, 0, 0 });

	const bool open = ImGui::BeginChild(
		id,
		viewportSize,
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (open)
	{
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.f)
				m_scrollVelocity -= wheel * 220.f * wheelMultiplier;
		}

		ImGui::SetCursorPos({ 0.f, -m_scrollDisplay });
		const float width = ImGui::GetContentRegionAvail().x;
		const float startY = ImGui::GetCursorPosY();
		drawContent(width);
		const float contentHeight = ImGui::GetCursorPosY() - startY;

		const float maxScroll = contentHeight > viewportSize.y ? contentHeight - viewportSize.y : 0.f;

		if (std::fabs(m_scrollVelocity) > 0.5f)
		{
			m_scrollY += m_scrollVelocity * deltaTime;
			m_scrollVelocity *= expf(-deltaTime * 7.f);
		}
		m_scrollY = Clamp(m_scrollY, 0.f, maxScroll);
		if (m_scrollY <= 0.f || m_scrollY >= maxScroll)
			m_scrollVelocity = 0.f;

		const float smoothK = 1.f - expf(-deltaTime * 14.f);
		m_scrollDisplay += (m_scrollY - m_scrollDisplay) * smoothK;
		if (std::fabs(m_scrollY - m_scrollDisplay) < 0.25f)
			m_scrollDisplay = m_scrollY;
	}

	ImGui::EndChild();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
}
