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

	constexpr float kScrollbarWidth = 10.f;
	// Gap between content and the bar (left of the track).
	constexpr float kScrollbarGapFromContent = 8.f;
	// Keep the bar almost flush to the viewport's right edge.
	constexpr float kScrollbarEdgePad = 4.f;
	constexpr float kScrollbarMinGrab = 28.f;
	constexpr float kScrollbarStrip = kScrollbarWidth + kScrollbarGapFromContent + kScrollbarEdgePad;
	// When the bar is hidden, keep the same right inset as the page pad.
	constexpr float kHiddenRightPad = 12.f;
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
	bool enablePageScroll,
	bool showScrollbar)
{
	// VPN scrollbar mode: viewport already extends to the MainArea right edge.
	// Reserve either page-pad (hidden) or gap+bar+edge (shown), animated by slide.
	float contentW = viewportSize.x;
	if (showScrollbar && enablePageScroll)
	{
		const float reserve =
			kHiddenRightPad + (kScrollbarStrip - kHiddenRightPad) * m_scrollbarSlide;
		contentW = (std::max)(1.f, viewportSize.x - reserve);
	}
	const ImVec2 contentViewport = { contentW, viewportSize.y };

	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.f);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, { 0, 0, 0, 0 });
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, { 0, 0, 0, 0 });

	const ImVec2 viewportScreenPos = ImGui::GetCursorScreenPos();
	const bool open = ImGui::BeginChild(
		id,
		contentViewport,
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	float maxScroll = 0.f;
	if (open)
	{
		ImGuiWindow* self = ImGui::GetCurrentWindow();

		if (!enablePageScroll)
		{
			m_scrollY = 0.f;
			m_scrollDisplay = 0.f;
			m_scrollVelocity = 0.f;
			m_jumpToBottom = false;
			m_scrollbarDragging = false;
			m_layoutNeedsScrollbar = false;
			m_scrollbarAlpha = 0.f;
			m_scrollbarSlide = 0.f;
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
			maxScroll = contentHeight > contentViewport.y ? contentHeight - contentViewport.y : 0.f;

			// Apply wheel after content so nested scrollables (console, tables) win when hovered.
			if (!m_scrollbarDragging
				&& ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
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

	const bool needsScrollbar = showScrollbar && enablePageScroll && maxScroll > 1.f;
	m_layoutNeedsScrollbar = needsScrollbar;

	const float slideTarget = needsScrollbar ? 1.f : 0.f;
	// Hide a bit faster so the bar doesn't vanish before content finishes moving.
	const float slideK = 1.f - expf(-deltaTime * (needsScrollbar ? 14.f : 18.f));
	m_scrollbarSlide += (slideTarget - m_scrollbarSlide) * slideK;
	// Kill exponential tail: otherwise the bar is already gone while content still eases ~last pixels.
	if (!needsScrollbar && m_scrollbarSlide < 0.08f)
		m_scrollbarSlide = 0.f;
	if (needsScrollbar && m_scrollbarSlide > 0.999f)
		m_scrollbarSlide = 1.f;

	m_scrollbarAlpha = m_scrollbarSlide;
	if (m_scrollbarAlpha < 0.01f && !needsScrollbar)
	{
		m_scrollbarAlpha = 0.f;
		m_scrollbarSlide = 0.f;
		m_scrollbarDragging = false;
		return;
	}

	const int trackA = static_cast<int>(28.f * m_scrollbarAlpha + 0.5f);
	const int grabIdleA = static_cast<int>(150.f * m_scrollbarAlpha + 0.5f);
	const int grabActiveA = static_cast<int>(200.f * m_scrollbarAlpha + 0.5f);

	// Slide distance == content expansion (strip - hiddenPad), so when the bar is gone
	// the layout has already finished — no post-hide content jump.
	const float restRight = viewportScreenPos.x + viewportSize.x - kScrollbarEdgePad;
	const float restLeft = restRight - kScrollbarWidth;
	const float slidePx = (1.f - m_scrollbarSlide) * (kScrollbarStrip - kHiddenRightPad);
	const ImVec2 trackMin = {
		restLeft + slidePx,
		viewportScreenPos.y + kScrollbarEdgePad
	};
	const ImVec2 trackMax = {
		restRight + slidePx,
		viewportScreenPos.y + viewportSize.y - kScrollbarEdgePad
	};
	const float trackH = (std::max)(1.f, trackMax.y - trackMin.y);
	const float grabH = Clamp(
		trackH * (contentViewport.y / (contentViewport.y + (std::max)(maxScroll, 1.f))),
		kScrollbarMinGrab,
		trackH);
	const float grabTravel = (std::max)(0.f, trackH - grabH);
	const float grabT = maxScroll > 0.f ? (m_scrollDisplay / maxScroll) : 0.f;
	const float grabY = trackMin.y + grabTravel * grabT;
	const ImVec2 grabMin = { trackMin.x, grabY };
	const ImVec2 grabMax = { trackMax.x, grabY + grabH };

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(trackMin, trackMax, IM_COL32(255, 255, 255, trackA), 4.f);

	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 mouse = io.MousePos;
	const bool hoveredGrab =
		mouse.x >= grabMin.x && mouse.x <= grabMax.x
		&& mouse.y >= grabMin.y && mouse.y <= grabMax.y;
	const bool hoveredTrack =
		mouse.x >= trackMin.x && mouse.x <= trackMax.x
		&& mouse.y >= trackMin.y && mouse.y <= trackMax.y;

	if (needsScrollbar && hoveredTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hoveredGrab)
		{
			m_scrollbarDragging = true;
			m_scrollbarDragGrabOffset = mouse.y - grabMin.y;
		}
		else if (grabTravel > 0.f)
		{
			const float clickedT = Clamp((mouse.y - trackMin.y - grabH * 0.5f) / grabTravel, 0.f, 1.f);
			m_scrollY = clickedT * maxScroll;
			m_scrollDisplay = m_scrollY;
			m_scrollVelocity = 0.f;
			m_scrollbarDragging = true;
			m_scrollbarDragGrabOffset = grabH * 0.5f;
			if (stickToBottom)
				*stickToBottom = false;
		}
	}

	if (m_scrollbarDragging)
	{
		if (needsScrollbar && ImGui::IsMouseDown(ImGuiMouseButton_Left) && grabTravel > 0.f)
		{
			const float newGrabY = mouse.y - m_scrollbarDragGrabOffset;
			const float newT = Clamp((newGrabY - trackMin.y) / grabTravel, 0.f, 1.f);
			m_scrollY = newT * maxScroll;
			m_scrollDisplay = m_scrollY;
			m_scrollVelocity = 0.f;
			if (stickToBottom)
				*stickToBottom = false;
		}
		else
		{
			m_scrollbarDragging = false;
		}
	}

	const bool active = m_scrollbarDragging || hoveredGrab;
	const ImU32 grabCol = active
		? IM_COL32(220, 220, 230, grabActiveA)
		: IM_COL32(190, 190, 200, grabIdleA);
	drawList->AddRectFilled(grabMin, grabMax, grabCol, 4.f);
}
