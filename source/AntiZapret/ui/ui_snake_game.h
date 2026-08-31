#pragma once

#include "imgui.h"

struct UiThemeColors;
struct UiAccentColors;

// Mini-game for the minimize-button easter egg.
class UiSnakeGame
{
public:
	void Reset();
	void Update(float deltaTime);
	// Returns true when the player requested exit (close / Esc).
	bool Draw(
		const ImVec2& areaMin,
		const ImVec2& areaMax,
		const UiThemeColors& colors,
		const UiAccentColors& accents);

private:
	enum class Dir : int { Up = 0, Right, Down, Left };

	struct Cell
	{
		int x = 0;
		int y = 0;
	};

	static constexpr int kGridW = 24;
	static constexpr int kGridH = 16;
	static constexpr int kMaxLen = kGridW * kGridH;
	static constexpr float kStepSec = 0.11f;
	static constexpr float kSwipeMinPx = 28.f;

	void SpawnFood();
	void SetDir(Dir next);
	void Step();
	bool Occupied(int x, int y, int skipTail = 0) const;

	Cell m_body[kMaxLen] {};
	int m_length = 0;
	Cell m_food {};
	Dir m_dir = Dir::Right;
	Dir m_pendingDir = Dir::Right;
	float m_stepAge = 0.f;
	int m_score = 0;
	int m_best = 0;
	bool m_alive = true;
	bool m_paused = false;

	bool m_swipeActive = false;
	ImVec2 m_swipeOrigin {};
};
