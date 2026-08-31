#include "ui/ui_snake_game.h"

#include "gfx/theme_manager.h"
#include "ui/ui_common.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

void UiSnakeGame::Reset()
{
	static bool seeded = false;
	if (!seeded)
	{
		std::srand(static_cast<unsigned>(std::time(nullptr)));
		seeded = true;
	}

	m_length = 3;
	m_body[0] = { kGridW / 2, kGridH / 2 };
	m_body[1] = { kGridW / 2 - 1, kGridH / 2 };
	m_body[2] = { kGridW / 2 - 2, kGridH / 2 };
	m_dir = Dir::Right;
	m_pendingDir = Dir::Right;
	m_stepAge = 0.f;
	m_score = 0;
	m_alive = true;
	m_paused = false;
	m_swipeActive = false;
	SpawnFood();
}

void UiSnakeGame::SpawnFood()
{
	if (m_length >= kMaxLen)
		return;

	for (int attempt = 0; attempt < 200; ++attempt)
	{
		const int x = std::rand() % kGridW;
		const int y = std::rand() % kGridH;
		if (!Occupied(x, y))
		{
			m_food = { x, y };
			return;
		}
	}

	for (int y = 0; y < kGridH; ++y)
	{
		for (int x = 0; x < kGridW; ++x)
		{
			if (!Occupied(x, y))
			{
				m_food = { x, y };
				return;
			}
		}
	}
}

bool UiSnakeGame::Occupied(int x, int y, int skipTail) const
{
	const int limit = m_length - skipTail;
	for (int i = 0; i < limit; ++i)
	{
		if (m_body[i].x == x && m_body[i].y == y)
			return true;
	}
	return false;
}

void UiSnakeGame::SetDir(Dir next)
{
	if (!m_alive || m_paused)
		return;

	const int cur = static_cast<int>(m_dir);
	const int nxt = static_cast<int>(next);
	if ((cur + 2) % 4 == nxt)
		return;
	m_pendingDir = next;
}

void UiSnakeGame::Step()
{
	if (!m_alive)
		return;

	m_dir = m_pendingDir;
	Cell head = m_body[0];
	switch (m_dir)
	{
	case Dir::Up: --head.y; break;
	case Dir::Right: ++head.x; break;
	case Dir::Down: ++head.y; break;
	case Dir::Left: --head.x; break;
	}

	if (head.x < 0 || head.y < 0 || head.x >= kGridW || head.y >= kGridH)
	{
		m_alive = false;
		if (m_score > m_best)
			m_best = m_score;
		return;
	}

	const bool eat = head.x == m_food.x && head.y == m_food.y;
	if (Occupied(head.x, head.y, eat ? 0 : 1))
	{
		m_alive = false;
		if (m_score > m_best)
			m_best = m_score;
		return;
	}

	for (int i = m_length - (eat ? 0 : 1); i > 0; --i)
		m_body[i] = m_body[i - 1];
	m_body[0] = head;

	if (eat)
	{
		if (m_length < kMaxLen)
			++m_length;
		++m_score;
		SpawnFood();
	}
}

void UiSnakeGame::Update(float deltaTime)
{
	if (!m_alive || m_paused)
		return;

	m_stepAge += deltaTime;
	while (m_stepAge >= kStepSec)
	{
		m_stepAge -= kStepSec;
		Step();
	}
}

bool UiSnakeGame::Draw(
	const ImVec2& areaMin,
	const ImVec2& areaMax,
	const UiThemeColors& colors,
	const UiAccentColors& accents)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float width = areaMax.x - areaMin.x;
	const float height = areaMax.y - areaMin.y;
	if (width < 40.f || height < 40.f)
		return false;

	constexpr float kHudH = 36.f;
	constexpr float kPad = 10.f;
	const ImVec2 boardMin = { areaMin.x + kPad, areaMin.y + kHudH };
	const ImVec2 boardMax = { areaMax.x - kPad, areaMax.y - kPad };
	const float boardW = boardMax.x - boardMin.x;
	const float boardH = boardMax.y - boardMin.y;
	const float cell = (boardW / kGridW < boardH / kGridH) ? (boardW / kGridW) : (boardH / kGridH);
	const float gridW = cell * kGridW;
	const float gridH = cell * kGridH;
	const ImVec2 gridMin = {
		boardMin.x + (boardW - gridW) * 0.5f,
		boardMin.y + (boardH - gridH) * 0.5f
	};
	const ImVec2 gridMax = { gridMin.x + gridW, gridMin.y + gridH };

	char hud[96] = {};
	if (m_alive)
		snprintf(hud, sizeof hud, "Змейка  ·  %d  ·  рекорд %d", m_score, m_best);
	else
		snprintf(hud, sizeof hud, "Итог %d  ·  рекорд %d  ·  R — заново", m_score, m_best);
	dl->AddText({ areaMin.x + kPad, areaMin.y + 10.f }, ImGui::GetColorU32(colors.textPrimary), hud);

	const char* hint = "Стрелки / WASD · свайп по полю · Esc — выход";
	const ImVec2 hintSize = ImGui::CalcTextSize(hint);
	dl->AddText(
		{ areaMax.x - kPad - hintSize.x, areaMin.y + 10.f },
		ImGui::GetColorU32(colors.textMuted),
		hint);

	dl->AddRectFilled(gridMin, gridMax, ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBg, 0.96f)), 8.f);
	dl->AddRect(gridMin, gridMax, ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.7f)), 8.f, 0, 1.2f);

	const ImU32 gridLine = ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.22f));
	for (int x = 1; x < kGridW; ++x)
	{
		const float px = gridMin.x + x * cell;
		dl->AddLine({ px, gridMin.y }, { px, gridMax.y }, gridLine);
	}
	for (int y = 1; y < kGridH; ++y)
	{
		const float py = gridMin.y + y * cell;
		dl->AddLine({ gridMin.x, py }, { gridMax.x, py }, gridLine);
	}

	const ImU32 foodCol = ImGui::GetColorU32(accents.fail);
	const ImVec2 foodMin = { gridMin.x + m_food.x * cell + 2.f, gridMin.y + m_food.y * cell + 2.f };
	const ImVec2 foodMax = { foodMin.x + cell - 4.f, foodMin.y + cell - 4.f };
	dl->AddRectFilled(foodMin, foodMax, foodCol, 3.f);

	for (int i = 0; i < m_length; ++i)
	{
		const float t = m_length <= 1 ? 1.f : 1.f - (static_cast<float>(i) / static_cast<float>(m_length)) * 0.45f;
		const ImVec4 body = {
			accents.ok.x * t + 0.12f * (1.f - t),
			accents.ok.y * t + 0.45f * (1.f - t),
			accents.ok.z * t + 0.22f * (1.f - t),
			1.f
		};
		const ImVec2 cMin = { gridMin.x + m_body[i].x * cell + 1.5f, gridMin.y + m_body[i].y * cell + 1.5f };
		const ImVec2 cMax = { cMin.x + cell - 3.f, cMin.y + cell - 3.f };
		dl->AddRectFilled(cMin, cMax, ImGui::GetColorU32(body), i == 0 ? 4.f : 3.f);
	}

	ImGui::SetCursorScreenPos(gridMin);
	ImGui::InvisibleButton("##snake_board", { gridW, gridH });
	const bool boardHovered = ImGui::IsItemHovered();
	const bool boardActive = ImGui::IsItemActive();

	if (boardHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_swipeActive = true;
		m_swipeOrigin = ImGui::GetIO().MousePos;
	}
	if (m_swipeActive && (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !boardActive))
	{
		const ImVec2 pos = ImGui::GetIO().MousePos;
		const float dx = pos.x - m_swipeOrigin.x;
		const float dy = pos.y - m_swipeOrigin.y;
		const float adx = dx < 0.f ? -dx : dx;
		const float ady = dy < 0.f ? -dy : dy;
		if (adx >= kSwipeMinPx || ady >= kSwipeMinPx)
		{
			if (adx > ady)
				SetDir(dx > 0.f ? Dir::Right : Dir::Left);
			else
				SetDir(dy > 0.f ? Dir::Down : Dir::Up);
		}
		m_swipeActive = false;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_W))
		SetDir(Dir::Up);
	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_D))
		SetDir(Dir::Right);
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_S))
		SetDir(Dir::Down);
	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_A))
		SetDir(Dir::Left);

	if (!m_alive && ImGui::IsKeyPressed(ImGuiKey_R))
		Reset();
	if (ImGui::IsKeyPressed(ImGuiKey_Space))
		m_paused = !m_paused;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		return true;

	return false;
}
