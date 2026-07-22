#include "ui/ui_smooth_scroll.h"

#include "imgui.h"
#include "imgui_internal.h"

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

	// Nested BeginChild / InputTextMultiline with their own vertical scroll must keep the wheel.
	bool IsHoveringNestedVerticalScroll(ImGuiWindow* root)
	{
		ImGuiContext& g = *GImGui;
		ImGuiWindow* hovered = g.HoveredWindow;
		if (!hovered || hovered == root)
			return false;

		for (ImGuiWindow* w = hovered; w != nullptr; w = w->ParentWindow)
		{
			if (w == root)
				return false;
			if (w->ScrollMax.y > 1.f)
				return true;
			if ((w->Flags & ImGuiWindowFlags_AlwaysVerticalScrollbar) != 0)
				return true;
		}
		return false;
	}
}

void UiSmoothScroll::JumpToBottom()
{
	m_jumpToBottom = true;
}

void UiSmoothScroll::Draw(
	const char* id,
	ImVec2 viewportSize,
	float deltaTime,
	const std::function<void(float width)>& drawContent,
	float wheelMultiplier,
	bool* stickToBottom,
	bool enablePageScroll)
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
		ImGuiWindow* self = ImGui::GetCurrentWindow();

		if (!enablePageScroll)
		{
			m_scrollY = 0.f;
			m_scrollDisplay = 0.f;
			m_scrollVelocity = 0.f;
			m_jumpToBottom = false;
		}

		ImGui::SetCursorPos({ 0.f, -m_scrollDisplay });
		const float width = ImGui::GetContentRegionAvail().x;
		const float startY = ImGui::GetCursorPosY();
		drawContent(width);
		const float contentHeight = ImGui::GetCursorPosY() - startY;

		// Tell ImGui the full virtual content height (required after SetCursorPos scroll offset).
		ImGui::SetCursorPos({ 0.f, contentHeight });
		ImGui::Dummy({ width, 0.01f });

		if (enablePageScroll)
		{
			// Apply wheel after content so nested scrollables (console, tables) win when hovered.
			if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
				&& !IsHoveringNestedVerticalScroll(self))
			{
				const float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.f)
				{
					m_scrollVelocity -= wheel * 220.f * wheelMultiplier;
					if (stickToBottom)
						*stickToBottom = false;
				}
			}

			const float maxScroll = contentHeight > viewportSize.y ? contentHeight - viewportSize.y : 0.f;

			if (m_jumpToBottom || (stickToBottom && *stickToBottom))
			{
				m_scrollY = maxScroll;
				m_scrollDisplay = maxScroll;
				m_scrollVelocity = 0.f;
				m_jumpToBottom = false;
				if (stickToBottom)
					*stickToBottom = true;
			}

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
			m_scrollDisplay = Clamp(m_scrollDisplay, 0.f, maxScroll);

			if (stickToBottom && maxScroll > 0.f && m_scrollY >= maxScroll - 1.f)
				*stickToBottom = true;
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
}
