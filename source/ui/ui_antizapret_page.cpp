#include "ui/ui_antizapret_page.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "ui/ui_common.h"
#include "zapret/smart_strategy_engine.h"
#include "zapret/strategies.hpp"
#include "zapret/strategy_descriptions.h"
#include "zapret/zapret_manager.h"
#include "zapret/zapret_store.h"
#include "zapret/zapret_update_apply.h"
#include "zapret/zapret_update_check.h"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	constexpr const char* kSmartStrategyLabel = SmartStrategyEngine::kLabel;

	struct GameFilterItem
	{
		ZapretStrategies::GameFilterMode mode;
		const char* label;
	};

	const GameFilterItem kGameFilterItems[] = {
		{ ZapretStrategies::GameFilterMode::Disabled, "OFF" },
		{ ZapretStrategies::GameFilterMode::Tcp, "TCP" },
		{ ZapretStrategies::GameFilterMode::Udp, "UDP" },
		{ ZapretStrategies::GameFilterMode::All, "ALL" },
	};

	ImVec4 GameFilterAccent(const GameFilterItem& item, const UiAccentColors& accents)
	{
		switch (item.mode)
		{
		case ZapretStrategies::GameFilterMode::Disabled:
			return accents.fail;
		case ZapretStrategies::GameFilterMode::Tcp:
			return UiCommon::WithAlpha(accents.upload, 0.82f);
		case ZapretStrategies::GameFilterMode::Udp:
			return { 0.47f, 0.58f, 0.72f, 1.f };
		case ZapretStrategies::GameFilterMode::All:
			return UiCommon::WithAlpha(accents.ok, 0.82f);
		}
		return accents.ok;
	}

	ZapretStrategies::GameFilterMode GameFilterModeFromIndex(int index)
	{
		if (index < 0 || index >= 4)
			return ZapretStrategies::GameFilterMode::Disabled;
		return kGameFilterItems[index].mode;
	}

	ImVec4 ComponentVersionAccent(ComponentUpdateStatus status, const UiAccentColors& accents)
	{
		switch (status)
		{
		case ComponentUpdateStatus::UpToDate:
			return accents.ok;
		case ComponentUpdateStatus::Checking:
		case ComponentUpdateStatus::UpdateAvailable:
			return accents.warn;
		case ComponentUpdateStatus::Unknown:
		case ComponentUpdateStatus::Error:
		default:
			return accents.fail;
		}
	}

	bool IsDisplayVersion(const std::string& raw)
	{
		if (raw.empty() || raw == "—" || raw == "Unknown" || raw == "Установлен")
			return false;
		for (unsigned char ch : raw)
		{
			if (ch >= '0' && ch <= '9')
				return true;
		}
		return false;
	}

	ImVec2 StrategyGridButtonSize(int gridIndex, int cols, float colWidth, float gap, int& slotsUsed)
	{
		if (gridIndex % cols != 0)
		{
			slotsUsed = 1;
			return { colWidth, UiMetrics::kBtnHeight };
		}

		slotsUsed = cols;
		return { colWidth * cols + gap * (cols - 1), UiMetrics::kBtnHeight };
	}

	int StrategyGridColumns(float gridWidth, bool showExtraStrategies)
	{
		if (!showExtraStrategies)
			return UiMetrics::kStrategyCols;

		const float gap = UiMetrics::kGridGap;
		int cols = static_cast<int>((gridWidth + gap) / (UiMetrics::kStrategyMinColWidth + gap));
		if (cols < UiMetrics::kStrategyCols)
			cols = UiMetrics::kStrategyCols;
		if (cols > UiMetrics::kStrategyColsMax)
			cols = UiMetrics::kStrategyColsMax;
		return cols;
	}

	void DrawBulletList(const char* const* lines, const UiThemeColors& colors)
	{
		if (!lines)
			return;

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		for (int i = 0; lines[i] != nullptr; ++i)
			ImGui::BulletText("%s", lines[i]);
		ImGui::PopStyleColor();
	}

	void DrawStrategyDetailsPanel(
		const StrategyDescription* description,
		const char* title,
		float innerWidth,
		const UiThemeColors& colors)
	{
		if (!description)
			return;

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, UiCommon::WithAlpha(colors.sidebarBg, 0.55f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { UiMetrics::kCardPad, UiMetrics::kCardPad });
		ImGui::BeginChild("##strategy_details", { innerWidth, 0.f }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(title);
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth - UiMetrics::kCardPad * 2.f);
		ImGui::TextUnformatted(description->summary);
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Ключевые приёмы");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 2.f });
		DrawBulletList(description->keyPoints, colors);

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Как обходится ТСПУ");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth - UiMetrics::kCardPad * 2.f);
		ImGui::TextUnformatted(description->tspuDetail);
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

	void FormatRuntimeDuration(int runtimeSec, char* buffer, size_t bufferSize)
	{
		if (runtimeSec <= 0)
		{
			snprintf(buffer, bufferSize, "—");
			return;
		}

		const int hours = runtimeSec / 3600;
		const int minutes = (runtimeSec % 3600) / 60;
		if (hours > 0)
			snprintf(buffer, bufferSize, "%d ч %d мин", hours, minutes);
		else if (minutes > 0)
			snprintf(buffer, bufferSize, "%d мин", minutes);
		else
			snprintf(buffer, bufferSize, "%d сек", runtimeSec);
	}

	void DrawStrategyRuntimeStats(
		const StrategyTestEntry* result,
		float innerWidth,
		const UiThemeColors& colors)
	{
		if (!result || (result->runtimeSec <= 0 && result->providerDualOutageCount <= 0))
			return;

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, UiCommon::WithAlpha(colors.sidebarBg, 0.35f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { UiMetrics::kCardPad, UiMetrics::kCardPad });
		ImGui::BeginChild("##strategy_runtime_stats", { innerWidth, 0.f }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Статистика работы");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });

		char runtimeBuffer[64] = {};
		FormatRuntimeDuration(result->runtimeSec, runtimeBuffer, sizeof runtimeBuffer);

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::Text("Накопленное время: %s", runtimeBuffer);
		ImGui::Text(
			"Сбоев провайдера (Discord и YouTube одновременно недоступны): %d",
			result->providerDualOutageCount);
		ImGui::PopStyleColor();

		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

	void DrawStrategyGrid(
		int& selectedStrategy,
		int& selectedSmartIndex,
		bool hasSmartStrategy,
		bool showExtraStrategies,
		float innerWidth,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		int activeStrategy,
		StrategyTestState strategyTestState,
		int testingStrategyIndex,
		ZapretManager* manager,
		bool tgProxyRunning)
	{
		const float gap = UiMetrics::kGridGap;
		const bool testCompleted = strategyTestState == StrategyTestState::Completed;
		const bool testRunning = strategyTestState == StrategyTestState::Running;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, UiCommon::WithAlpha(colors.tileBg, 0.55f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { gap, gap });
		ImGui::BeginChild("##strategy_grid", { innerWidth, 0.f }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

		const float gridWidth = ImGui::GetContentRegionAvail().x;
		const int cols = StrategyGridColumns(gridWidth, showExtraStrategies);
		const float colWidth = (gridWidth - gap * (cols - 1)) / cols;
		const int count = manager
			? manager->GetVisibleStrategyCount(showExtraStrategies)
			: ZapretStrategies::CountVisibleStrategies(showExtraStrategies);
		const int bestStrategyIndex = manager ? manager->GetCachedBestStrategyIndex() : -1;
		const ImVec4 bestAccent = { 0.83f, 0.69f, 0.22f, 1.f };
		const ImVec4 smartAccent = UiCommon::WithAlpha(accents.upload, 0.88f);
		int gridIndex = 0;

		if (hasSmartStrategy)
		{
			int slotsUsed = 1;
			const ImVec2 buttonSize = StrategyGridButtonSize(gridIndex, cols, colWidth, gap, slotsUsed);
			const bool smartSelected = selectedSmartIndex == 0;
			const bool isActiveSmart = manager && manager->IsActiveSmartStrategy();
			const SmartStrategyStatus smartStatus = manager
				? manager->GetSmartStrategyStatus(tgProxyRunning)
				: SmartStrategyStatus {};

			const StrategyTestEntry* result = manager
				? manager->GetStore().GetResult(kSmartStrategyLabel)
				: nullptr;

			char labelBuffer[160] = {};
			if (!smartStatus.summary.empty())
			{
				if (result && result->pingMs >= 0)
				{
					snprintf(
						labelBuffer,
						sizeof labelBuffer,
						"%s · %s · %d ms",
						kSmartStrategyLabel,
						smartStatus.summary.c_str(),
						result->pingMs);
				}
				else
				{
					snprintf(
						labelBuffer,
						sizeof labelBuffer,
						"%s · %s",
						kSmartStrategyLabel,
						smartStatus.summary.c_str());
				}
			}
			else if (result && result->pingMs >= 0)
			{
				snprintf(labelBuffer, sizeof labelBuffer, "%s · %d ms", kSmartStrategyLabel, result->pingMs);
			}
			else
			{
				snprintf(labelBuffer, sizeof labelBuffer, "%s", kSmartStrategyLabel);
			}

			ImVec4 accent = smartAccent;
			if (isActiveSmart)
				accent = accents.ok;

			if (UiCommon::StrategyButton(
				-1,
				labelBuffer,
				smartSelected || isActiveSmart,
				buttonSize,
				colors,
				accent))
			{
				selectedSmartIndex = 0;
				if (manager)
					manager->RememberSmartStrategySelected();
			}

			const ImVec2 buttonMin = ImGui::GetItemRectMin();
			UiCommon::StrategyIndicatorState indicatorState;
			if (result)
			{
				indicatorState.hasResult = true;
				indicatorState.discordOk = result->discordOk;
				indicatorState.youtubeOk = result->youtubeOk;
				indicatorState.telegramOk = result->telegramOk;
			}
			if (tgProxyRunning)
			{
				indicatorState.hasResult = true;
				indicatorState.telegramOk = true;
			}
			UiCommon::DrawStrategyIndicators(buttonMin, indicatorState, colors);
			gridIndex += slotsUsed;
		}

		for (int pass = 0; pass < count; ++pass)
		{
			const int i = manager
				? manager->GetVisibleStrategyAt(pass, showExtraStrategies)
				: ZapretStrategies::GetVisibleStrategyAt(pass, showExtraStrategies);
			if (i < 0)
				break;

			if (gridIndex % cols != 0)
				ImGui::SameLine(0.f, gap);

			const bool selected = selectedStrategy == i;
			const bool isActive = activeStrategy == i
				&& !(manager && manager->IsActiveSmartStrategy());
			const bool isTesting = testRunning && testingStrategyIndex == i;
			const std::string strategyLabel = manager
				? manager->GetStrategyLabel(i)
				: std::string(ZapretStrategies::GetStrategyLabel(i));
			const char* strategyId = strategyLabel.c_str();
			const bool isBest = bestStrategyIndex >= 0 && bestStrategyIndex == i;
			const bool highlightBest = testCompleted && isBest;

			ImVec4 accent = accents.download;
			if (isTesting)
				accent = accents.warn;
			else if (highlightBest)
				accent = bestAccent;
			else if (isActive)
				accent = accents.ok;

			char labelBuffer[128] = {};
			if (manager)
			{
				if (const StrategyTestEntry* result = manager->GetStrategyResult(i))
				{
					if (result->pingMs >= 0)
						snprintf(labelBuffer, sizeof labelBuffer, "%s · %d ms", strategyId, result->pingMs);
					else
						snprintf(labelBuffer, sizeof labelBuffer, "%s", strategyId);
				}
				else
				{
					snprintf(labelBuffer, sizeof labelBuffer, "%s", strategyId);
				}
			}
			else
			{
				snprintf(labelBuffer, sizeof labelBuffer, "%s", strategyId);
			}

			if (UiCommon::StrategyButton(
				i,
				labelBuffer,
				(selected && selectedSmartIndex < 0) || isTesting || highlightBest,
				{ colWidth, UiMetrics::kBtnHeight },
				colors,
				accent))
			{
				selectedStrategy = i;
				selectedSmartIndex = -1;
				if (manager)
					manager->RememberSelectedStrategy(i);
			}

			const ImVec2 buttonMin = ImGui::GetItemRectMin();
			const ImVec2 buttonMax = ImGui::GetItemRectMax();
			if (isTesting)
			{
				const float pulse = 0.45f + 0.35f * std::sinf(static_cast<float>(ImGui::GetTime()) * 5.f);
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRect(
					buttonMin,
					buttonMax,
					ImGui::ColorConvertFloat4ToU32(UiCommon::WithAlpha(accents.warn, pulse)),
					UiMetrics::kCardRadius,
					0,
					2.5f);
			}
			else if (highlightBest)
			{
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRect(
					buttonMin,
					buttonMax,
					ImGui::ColorConvertFloat4ToU32(UiCommon::WithAlpha(bestAccent, 0.9f)),
					UiMetrics::kCardRadius,
					0,
					2.f);
			}

			UiCommon::StrategyIndicatorState indicatorState;
			if (manager)
			{
				if (const StrategyTestEntry* result = manager->GetStrategyResult(i))
				{
					indicatorState.hasResult = true;
					indicatorState.discordOk = result->discordOk;
					indicatorState.youtubeOk = result->youtubeOk;
					indicatorState.telegramOk = result->telegramOk;
				}
				indicatorState.isBest = isBest;
			}
			if (tgProxyRunning)
			{
				indicatorState.hasResult = true;
				indicatorState.telegramOk = true;
			}

			UiCommon::DrawStrategyIndicators(buttonMin, indicatorState, colors);
			++gridIndex;
		}

		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

	const char* RunStatusLabel(ZapretRunStatus status)
	{
		switch (status)
		{
		case ZapretRunStatus::Running: return "Работает";
		case ZapretRunStatus::Starting: return "Запускается";
		case ZapretRunStatus::Stopped: return "Не работает";
		}
		return "Не работает";
	}

	ImVec4 RunStatusColor(ZapretRunStatus status, const UiAccentColors& accents)
	{
		switch (status)
		{
		case ZapretRunStatus::Running: return accents.ok;
		case ZapretRunStatus::Starting: return accents.upload;
		case ZapretRunStatus::Stopped: return accents.fail;
		}
		return accents.fail;
	}
}

void UiAntiZapretPage::TryAutoStartTgProxy()
{
	if (!m_appSettings || !m_tgProxyManager)
		return;
	if (!m_appSettings->GetAutoStartTgProxyWithAntiZapret())
		return;
	if (m_appSettings->IsTgAutoStartSuppressed())
		return;
	if (m_tgProxyManager->IsRunning())
		return;
	if (m_manager && m_manager->IsStrategySelectionInProgress())
		return;

	if (m_startupAutostartBypass)
	{
		AppLog::Instance().Append(LogSource::Telegram, "Автозапуск: TG WS Proxy вместе с AntiZapret");
		m_startupAutostartBypass = false;
	}
	m_tgProxyManager->RequestStart(m_appSettings->GetOpenTelegramOnProxyStart());
}

void UiAntiZapretPage::ClampSelectedStrategy()
{
	if (!m_manager)
		return;
	if (m_manager->IsStrategyVisible(m_selectedStrategy, m_showExtraStrategies))
		return;

	const int fallback = m_manager->GetVisibleStrategyAt(0, m_showExtraStrategies);
	m_selectedStrategy = fallback >= 0 ? fallback : 0;
}

void UiAntiZapretPage::ApplyAutoSelectStrategyChange(bool running, int activeStrategy)
{
	if (!m_manager)
		return;

	if (running && activeStrategy >= 0)
	{
		m_selectedStrategy = activeStrategy;
		return;
	}

	m_selectedStrategy = m_manager->GetPreferredStrategyIndex(m_autoSelect);
}

void UiAntiZapretPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();

	if (!m_preferencesLoaded)
	{
		if (m_appSettings)
		{
			m_autoSelect = m_appSettings->GetAutoSelectBestStrategy();
			m_showExtraStrategies = m_appSettings->GetShowExtraStrategies();
		}
		if (m_manager)
		{
			m_hasSmartStrategy = m_manager->IsSmartStrategyEnabled();
			const std::string& last = m_manager->GetStore().GetLastStrategy();
			if (m_hasSmartStrategy && last == SmartStrategyEngine::kLabel)
			{
				m_selectedSmartIndex = 0;
				m_selectedStrategy = m_manager->GetPreferredStrategyIndex(false);
			}
			else
			{
				m_selectedSmartIndex = -1;
				m_selectedStrategy = m_manager->GetPreferredStrategyIndex(m_autoSelect);
			}
		}
		ClampSelectedStrategy();
		m_preferencesLoaded = true;
	}

	const ZapretRunStatus runStatus = m_manager ? m_manager->GetCachedRunStatus() : ZapretRunStatus::Stopped;
	const bool running = runStatus == ZapretRunStatus::Running || runStatus == ZapretRunStatus::Starting;
	const int activeStrategy = m_manager ? m_manager->GetActiveStrategyIndex() : -1;
	const bool discordOnline = m_manager ? m_manager->IsDiscordOnline() : false;
	const bool youtubeOnline = m_manager ? m_manager->IsYouTubeOnline() : false;
	const bool tgProxyRunning = m_tgProxyManager && m_tgProxyManager->IsRunning();
	const bool telegramProbeOk = m_manager ? m_manager->IsTelegramOnline() : false;
	const bool smartSelected = m_hasSmartStrategy && m_selectedSmartIndex == 0;
	const SmartStrategyStatus smartStatus = m_manager
		? m_manager->GetSmartStrategyStatus(tgProxyRunning)
		: SmartStrategyStatus {};
	const bool checkingServices = m_manager && m_manager->IsCheckingConnectivity();
	const StrategyTestState strategyTestState = m_manager
		? m_manager->GetStrategyTestState()
		: StrategyTestState::Idle;
	const SmartStrategyTuneState smartTuneState = m_manager
		? m_manager->GetSmartStrategyTuneState()
		: SmartStrategyTuneState::Idle;
	const bool strategyTestActive = strategyTestState != StrategyTestState::Idle;
	const bool smartTuneActive = smartTuneState != SmartStrategyTuneState::Idle;
	const int testingStrategyIndex = m_manager ? m_manager->GetStrategyTestActiveIndex() : -1;

	const bool blockTgAutoStart = m_manager && m_manager->IsStrategySelectionInProgress();

	if (!blockTgAutoStart
		&& m_prevStrategyTestState != StrategyTestState::Completed
		&& strategyTestState == StrategyTestState::Completed
		&& running)
	{
		TryAutoStartTgProxy();
	}
	if (!blockTgAutoStart && m_prevRunStatus != ZapretRunStatus::Running && runStatus == ZapretRunStatus::Running)
		TryAutoStartTgProxy();
	m_prevStrategyTestState = strategyTestState;
	m_prevRunStatus = runStatus;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });

	const auto& updateCheck = ZapretUpdateCheck::Instance();
	std::string azLocalVersion = updateCheck.GetZapretLocalVersion();
	if (azLocalVersion == "Unknown" || azLocalVersion == "—" || azLocalVersion == "Установлен")
		azLocalVersion.clear();
	else
	{
		bool hasDigit = false;
		for (unsigned char ch : azLocalVersion)
		{
			if (ch >= '0' && ch <= '9')
			{
				hasDigit = true;
				break;
			}
		}
		if (!hasDigit)
			azLocalVersion.clear();
	}

	const bool updateAvailable = updateCheck.GetZapretStatus() == ComponentUpdateStatus::UpdateAvailable;
	const bool updateApplying = ZapretUpdateApply::Instance().IsApplying();
	const std::string azRemoteVersion = updateCheck.GetZapretRemoteVersion();
	const bool versionsDiffer = !azLocalVersion.empty()
		&& IsDisplayVersion(azRemoteVersion)
		&& azLocalVersion != azRemoteVersion;
	const bool showUpdateBtn = updateAvailable || updateApplying || versionsDiffer;
	const char* updateBtnLabel = showUpdateBtn
		? (updateApplying ? "Скачивание..." : "Скачать обновление")
		: nullptr;

	if (UiCommon::PageTitle(
			fonts,
			0xE774,
			"Антизапрет",
			nullptr,
			colors,
			azLocalVersion.empty() ? nullptr : azLocalVersion.c_str(),
			ComponentVersionAccent(updateCheck.GetZapretStatus(), accents),
			updateBtnLabel,
			showUpdateBtn && !updateApplying))
	{
		ZapretUpdateApply::Instance().RequestApply(m_manager, m_tgProxyManager);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Обход блокировок Discord, YouTube и Telegram");
	ImGui::PopStyleColor();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (const std::string applyMsg = ZapretUpdateApply::Instance().GetStatusMessage();
		!applyMsg.empty() && showUpdateBtn)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextWrapped("%s", applyMsg.c_str());
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });
	}

	if (UiCommon::BeginCard("##status_card", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		const bool selectionInProgress = strategyTestState == StrategyTestState::Running
			|| smartTuneState == SmartStrategyTuneState::Running;
		const char* statusLabel = selectionInProgress ? "Тех. работы" : RunStatusLabel(runStatus);
		const ImVec4 statusColor = selectionInProgress
			? ImVec4(0.92f, 0.62f, 0.18f, 1.f)
			: RunStatusColor(runStatus, accents);
		const ImVec2 badgePad = { 10.f, 4.f };
		const ImVec2 badgeTextSize = ImGui::CalcTextSize(statusLabel);
		const float badgeH = badgeTextSize.y + badgePad.y * 2.f;
		const float badgeW = badgeTextSize.x + badgePad.x * 2.f;
		const char* statusTitle = "Статус:";
		const float statusTitleW = ImGui::CalcTextSize(statusTitle).x;
		const float statusGroupWidth = statusTitleW + 8.f + badgeW;
		const float statusRowY = ImGui::GetCursorPosY();
		const float textOffsetY = (badgeH - ImGui::GetTextLineHeight()) * 0.5f;

		const std::string runningStrategyLabel = [&]() -> std::string {
			if (!running || !m_manager)
				return {};
			if (m_manager->IsActiveSmartStrategy())
				return kSmartStrategyLabel;
			if (activeStrategy >= 0)
				return m_manager->GetStrategyLabel(activeStrategy);
			return {};
		}();

		std::string processValue = running ? "winws.exe" : "—";
		if (!runningStrategyLabel.empty())
			processValue += " (" + runningStrategyLabel + ")";

		ImGui::SetCursorPosY(statusRowY + textOffsetY);
		UiCommon::InfoLine("Процесс: ", processValue.c_str(), colors);
		ImGui::SameLine(innerWidth - statusGroupWidth);
		ImGui::SetCursorPosY(statusRowY + textOffsetY);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(statusTitle);
		ImGui::PopStyleColor();
		ImGui::SameLine(0.f, 8.f);
		ImGui::SetCursorPosY(statusRowY);
		UiCommon::StatusBadge(statusLabel, statusColor, colors);
		ImGui::SetCursorPosY(statusRowY + badgeH + 4.f);

		if (m_manager && !m_manager->GetErrorMessage().empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, accents.fail);
			ImGui::TextWrapped("%s", m_manager->GetErrorMessage().c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 4.f });
		}

		const float btnH = UiMetrics::kBtnHeight;
		const float gap = UiMetrics::kGridGap;
		const float mainBtnW = 132.f;
		const float restartBtnW = mainBtnW * 0.5f * 1.3f;
		const float actionBtnW = (innerWidth - mainBtnW - restartBtnW - gap * 3.f) * 0.5f;

		if (running)
		{
			if (UiCommon::AccentButton("Остановить", { mainBtnW, btnH }, accents.fail, colors) && m_manager)
				m_manager->RequestStop();
		}
		else if (UiCommon::AccentButton(
			"Запустить",
			{ mainBtnW, btnH },
			accents.download,
			colors,
			m_manager && !m_manager->IsOperationInFlight()) && m_manager)
		{
			if (smartSelected)
				m_manager->RequestStartSmartStrategy(GameFilterModeFromIndex(m_gameFilterMode));
			else
				m_manager->RequestStart(m_selectedStrategy, GameFilterModeFromIndex(m_gameFilterMode));
		}

		ImGui::SameLine(0.f, gap);
		if (UiCommon::SecondaryButton(
			"Перезапуск",
			{ restartBtnW, btnH },
			colors,
			m_manager != nullptr && !m_manager->IsOperationInFlight()) && m_manager)
		{
			if (smartSelected)
				m_manager->RequestStartSmartStrategy(GameFilterModeFromIndex(m_gameFilterMode));
			else
				m_manager->RequestStart(m_selectedStrategy, GameFilterModeFromIndex(m_gameFilterMode));
		}
		ImGui::SameLine(0.f, gap);
		const char* strategyTestLabel = m_manager ? m_manager->GetStrategyTestButtonLabel() : "Подбор стратегий";
		if (UiCommon::SecondaryButton(strategyTestLabel, { actionBtnW, btnH }, colors) && m_manager)
			m_manager->HandleStrategyTestButton(GameFilterModeFromIndex(m_gameFilterMode));
		if (strategyTestState == StrategyTestState::Running)
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip(
					"Можно полностью остановить, нажав правой кнопкой мыши.");
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && m_manager)
				m_manager->RequestStopStrategyTest();
		}
		ImGui::SameLine(0.f, gap);
		if (m_hasSmartStrategy)
		{
			const char* smartTuneLabel = m_manager
				? m_manager->GetSmartStrategyTuneButtonLabel()
				: "Умная стратегия";
			if (UiCommon::SecondaryButton(smartTuneLabel, { actionBtnW, btnH }, colors) && m_manager)
				m_manager->HandleSmartStrategyTuneButton(GameFilterModeFromIndex(m_gameFilterMode));
		}
		else if (UiCommon::SecondaryButton("Умная стратегия", { actionBtnW, btnH }, colors))
			AddSmartStrategy();

		ImGui::Dummy({ 0.f, 4.f });
		if (UiCommon::SecondaryButton("Обновить проверку", { 150.f, UiMetrics::kSmallBtnHeight }, colors) && m_manager)
		{
			const int strategyIndex = smartSelected
				? -1
				: ((activeStrategy >= 0) ? activeStrategy : m_selectedStrategy);
			m_manager->RequestConnectivityCheck(strategyIndex);
		}

		const float serviceGap = 18.f;
		ImGui::SameLine(0.f, serviceGap);
		UiCommon::DrawServiceInline("Discord", discordOnline, accents.discord, colors, accents);
		ImGui::SameLine(0.f, serviceGap);
		UiCommon::DrawServiceInline("YouTube", youtubeOnline, accents.youtube, colors, accents);
		ImGui::SameLine(0.f, serviceGap);
		if (tgProxyRunning)
		{
			UiCommon::DrawServiceInline(
				"Telegram",
				true,
				accents.telegram,
				colors,
				accents,
				"MTPROTO(ok)");
		}
		else
		{
			UiCommon::DrawServiceInline(
				"Telegram",
				telegramProbeOk,
				accents.telegram,
				colors,
				accents);
		}

		const bool strategyTestRunning = strategyTestState == StrategyTestState::Running;
		const bool smartTuneRunning = smartTuneState == SmartStrategyTuneState::Running;
		const bool showServiceCheckLine = !strategyTestRunning
			&& !smartTuneRunning
			&& (checkingServices || running);

		if (strategyTestRunning && m_manager)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, UiCommon::WithAlpha(accents.warn, 0.95f));
			const int total = m_manager->GetStrategyTestTotal();
			const int displayIndex = testingStrategyIndex >= 0 ? testingStrategyIndex + 1 : m_manager->GetStrategyTestCurrent();
			const std::string testingLabel = testingStrategyIndex >= 0
				? m_manager->GetStrategyLabel(testingStrategyIndex)
				: std::string();
			if (!testingLabel.empty())
			{
				ImGui::Text(
					"Тестирование: %s (%d/%d)",
					testingLabel.c_str(),
					displayIndex,
					total);
			}
			else
			{
				ImGui::Text("Тестирование стратегий... (%d/%d)", displayIndex, total);
			}
			ImGui::PopStyleColor();
		}
		else if (showServiceCheckLine)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			if (checkingServices)
				ImGui::TextUnformatted("Проверка доступности сервисов... (выполняется)");
			else
			{
				const int countdownSec = m_manager
					? m_manager->GetConnectivityCountdownSecondsCeil()
					: static_cast<int>(ZapretManager::kConnectivityIntervalSec);
				ImGui::Text("Проверка доступности сервисов... (%dс)", countdownSec);
			}
			ImGui::PopStyleColor();
		}
		else if (strategyTestState == StrategyTestState::Completed)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.69f, 0.22f, 1.f));
			ImGui::TextUnformatted("Тестирование завершено");
			ImGui::PopStyleColor();
		}
		else if (smartTuneState == SmartStrategyTuneState::Running && m_manager)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, UiCommon::WithAlpha(accents.warn, 0.95f));
			ImGui::Text(
				"Умная стратегия... (%d/%d)",
				m_manager->GetSmartStrategyTuneCurrent(),
				m_manager->GetSmartStrategyTuneTotal());
			ImGui::PopStyleColor();
		}
		else if (smartTuneState == SmartStrategyTuneState::Completed)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.69f, 0.22f, 1.f));
			ImGui::TextUnformatted("Умная стратегия настроена");
			ImGui::PopStyleColor();
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Игровой фильтр");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });

		ImGui::PushID("gamefilter");
		const int selectedIndex = m_gameFilterMode;
		for (int i = 0; i < 4; ++i)
		{
			const GameFilterItem& item = kGameFilterItems[i];
			if (UiCommon::StrategyButton(
				i,
				item.label,
				selectedIndex == i,
				{ 68.f, UiMetrics::kSmallBtnHeight },
				colors,
				GameFilterAccent(item, accents)))
			{
				m_gameFilterMode = i;
			}
			if (i < 3)
				ImGui::SameLine(0.f, UiMetrics::kGridGap);
		}
		ImGui::PopID();

		if (running)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextWrapped("После смены GameFilter нужен перезапуск стратегии.");
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##strategy_card", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		const float headerRowY = ImGui::GetCursorPosY();
		const float textLineH = ImGui::GetTextLineHeight();
		const float barH = 4.f;

		UiCommon::SectionHeader("Стратегии", colors);
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		const int visibleCount = m_manager
			? m_manager->GetVisibleStrategyCount(m_showExtraStrategies)
			: ZapretStrategies::CountVisibleStrategies(m_showExtraStrategies);
		ImGui::Text("(%d)", visibleCount + (m_hasSmartStrategy ? 1 : 0));
		ImGui::PopStyleColor();

		if ((strategyTestActive || smartTuneActive) && m_manager)
		{
			ImGui::SameLine(0.f, 10.f);
			const float progress = smartTuneActive
				? m_manager->GetSmartStrategyTuneProgress()
				: m_manager->GetStrategyTestProgress();
			const float barY = headerRowY + (textLineH - barH) * 0.5f;
			ImGui::SetCursorPosY(barY);

			const float barWidth = ImGui::GetContentRegionAvail().x;
			if (barWidth > 24.f)
			{
				const bool completed = strategyTestState == StrategyTestState::Completed
					|| smartTuneState == SmartStrategyTuneState::Completed;
				const ImVec4 barColor = completed
					? ImVec4(0.83f, 0.69f, 0.22f, 0.85f)
					: ImVec4(0.22f, 0.78f, 0.42f, 0.65f);

				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.f);
				ImGui::ProgressBar(progress, { barWidth, barH }, "");
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
			}
		}

		ImGui::SetCursorPosY(headerRowY + textLineH + UiMetrics::kRowGap);

		DrawStrategyGrid(
			m_selectedStrategy,
			m_selectedSmartIndex,
			m_hasSmartStrategy,
			m_showExtraStrategies,
			innerWidth,
			colors,
			accents,
			activeStrategy,
			strategyTestState,
			testingStrategyIndex,
			m_manager,
			tgProxyRunning);

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		const float detailsBtnW = 88.f;
		const float fileRowH = UiMetrics::kSmallBtnHeight;
		const float fileRowY = ImGui::GetCursorPosY();

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::SetCursorPosY(fileRowY + (fileRowH - ImGui::GetTextLineHeight()) * 0.5f);
		if (smartSelected)
		{
			if (!smartStatus.summary.empty())
				ImGui::Text("Файл: custom (%s)", smartStatus.summary.c_str());
			else
				ImGui::TextUnformatted("Файл: custom (general)");
		}
		else if (m_manager)
			ImGui::Text("Файл: %s", m_manager->GetStrategyFileName(m_selectedStrategy).c_str());
		else
			ImGui::TextUnformatted("Файл: —");
		ImGui::PopStyleColor();

		ImGui::SetCursorPos({ innerWidth - detailsBtnW, fileRowY });
		const bool canShowDetails = smartSelected
			|| (m_manager && !m_manager->GetStrategyLabel(m_selectedStrategy).empty());
		if (UiCommon::SecondaryButton("Подробно", { detailsBtnW, fileRowH }, colors, canShowDetails) && canShowDetails)
			m_strategyDetailsOpen = !m_strategyDetailsOpen;
		ImGui::SetCursorPosY(fileRowY + fileRowH);

		if (m_strategyDetailsOpen && canShowDetails)
		{
			const StrategyDescription* description = nullptr;
			const char* title = kSmartStrategyLabel;
			if (smartSelected)
			{
				description = StrategyDescriptions::GetSmartStrategy();
			}
			else if (m_manager)
			{
				title = m_manager->GetStrategyLabel(m_selectedStrategy).c_str();
				description = StrategyDescriptions::GetById(title);
				if (!description)
					description = StrategyDescriptions::GetById(
						m_manager->GetStrategyFileName(m_selectedStrategy).c_str());
			}

			if (description)
				DrawStrategyDetailsPanel(description, title, innerWidth, colors);
			else
			{
				ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
				ImGui::TextWrapped("Стратегия из файла (описание недоступно).");
				ImGui::PopStyleColor();
			}

			const StrategyTestEntry* runtimeStats = smartSelected
				? (m_manager ? m_manager->GetStore().GetResult(kSmartStrategyLabel) : nullptr)
				: (m_manager ? m_manager->GetStrategyResult(m_selectedStrategy) : nullptr);
			DrawStrategyRuntimeStats(runtimeStats, innerWidth, colors);

			if (smartSelected && !smartStatus.explanation.empty())
			{
				ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth - UiMetrics::kCardPad * 2.f);
				ImGui::TextUnformatted(smartStatus.explanation.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			}
		}

		if (running && m_manager && m_manager->IsActiveSmartStrategy() && !smartSelected)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			if (!smartStatus.summary.empty())
			{
				ImGui::TextWrapped(
					"Сейчас работает Умная стратегия (%s). Выберите карточку или нажмите «Перезапуск».",
					smartStatus.summary.c_str());
			}
			else
			{
				ImGui::TextUnformatted(
					"Сейчас работает Умная стратегия. Выберите карточку или нажмите «Перезапуск».");
			}
			ImGui::PopStyleColor();
		}
		else if (running && activeStrategy >= 0 && !smartSelected && activeStrategy != m_selectedStrategy)
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextWrapped("Выбрана другая стратегия. Нажмите «Перезапуск», чтобы применить.");
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##auto_card", width, colors))
	{
		UiCommon::SectionHeader("Дополнительно", colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		if (UiCommon::StyledCheckbox("Автовыбор лучшей стратегии", &m_autoSelect, colors))
		{
			if (m_appSettings)
				m_appSettings->SetAutoSelectBestStrategy(m_autoSelect);
			ApplyAutoSelectStrategyChange(running, activeStrategy);
		}

		if (m_autoSelect)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - UiMetrics::kCardPad * 2.f);
			ImGui::TextUnformatted(
				"При одновременной недоступности Discord и YouTube (~20 сек) переключится на стратегию "
				"с большим временем работы и меньшим числом сбоев; иначе — следующая по списку. "
				"Пока обход работает, стратегия не меняется.");
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		if (UiCommon::StyledCheckbox("Отобразить дополнительные стратегии", &m_showExtraStrategies, colors))
		{
			if (m_appSettings)
				m_appSettings->SetShowExtraStrategies(m_showExtraStrategies);
			ClampSelectedStrategy();
		}

		if (m_manager && !m_manager->GetStore().GetLastStrategy().empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::Text("Последняя стратегия: %s", m_manager->GetStore().GetLastStrategy().c_str());
			ImGui::Text("Файл: settings.ini [zapret_results]");
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}

void UiAntiZapretPage::AddSmartStrategy()
{
	if (m_hasSmartStrategy)
		return;

	m_hasSmartStrategy = true;
	if (m_manager)
		m_manager->SetSmartStrategyEnabled(true);
}
