#pragma once

enum class UiTab
{
	Home,
	AntiZapret,
	TgWsProxy,
	Vpn,
	Routing,
	Console,
	Settings,
	About
};

namespace UiMetrics
{
	inline constexpr float kCardGap = 7.f;
	inline constexpr float kCardPad = 16.f;
	inline constexpr float kCardRadius = 8.f;
	inline constexpr float kBtnHeight = 34.f;
	inline constexpr float kSmallBtnHeight = 26.f;
	inline constexpr float kSectionGap = 8.f;
	inline constexpr float kRowGap = 6.f;
	inline constexpr float kGridGap = 8.f;
	inline constexpr int kStrategyCols = 2;
	inline constexpr int kStrategyColsMax = 4;
	inline constexpr float kStrategyMinColWidth = 172.f;
}
