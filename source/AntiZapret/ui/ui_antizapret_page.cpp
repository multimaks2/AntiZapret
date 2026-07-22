#include "ui/ui_antizapret_page.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "ui/ui_common.h"
#include "zapret/strategies.hpp"
#include "zapret/strategy_descriptions.h"
#include "zapret/zapret_diagnostics.h"
#include "zapret/zapret_manager.h"
#include "zapret/zapret_store.h"
#include "zapret/zapret_update_apply.h"
#include "zapret/zapret_update_check.h"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
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

	// Detail-only: same look as StyledCheckbox, but aligned to a button row.
	// Do not change global StyledCheckbox (Additional / TG WS Proxy).
	bool DetailBesideButtonCheckbox(const char* label, bool* value, const UiThemeColors& colors)
	{
		const float boxSize = 18.f;
		const float rowH = UiMetrics::kSmallBtnHeight;
		const float labelGap = 8.f;
		const float textW = ImGui::CalcTextSize(label).x;
		const float textH = ImGui::GetTextLineHeight();

		ImGui::SameLine(0.f, UiMetrics::kGridGap);
		const ImVec2 pos = ImGui::GetCursorScreenPos();

		ImGui::PushID(label);
		ImGui::InvisibleButton("##detail_cb", { boxSize + labelGap + textW, rowH });
		const bool hovered = ImGui::IsItemHovered();
		bool changed = false;
		if (ImGui::IsItemClicked())
		{
			*value = !*value;
			changed = true;
		}
		ImGui::PopID();

		const float boxY = pos.y + (rowH - boxSize) * 0.5f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec4 bg = hovered ? UiCommon::WithAlpha(colors.navHover, 0.95f) : colors.inputBg;
		drawList->AddRectFilled(
			{ pos.x, boxY },
			{ pos.x + boxSize, boxY + boxSize },
			ImGui::GetColorU32(bg),
			UiMetrics::kCardRadius);
		drawList->AddRect(
			{ pos.x, boxY },
			{ pos.x + boxSize, boxY + boxSize },
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.35f)),
			UiMetrics::kCardRadius);

		if (*value)
		{
			const float pad = 4.f;
			drawList->AddLine(
				{ pos.x + pad, boxY + boxSize * 0.55f },
				{ pos.x + boxSize * 0.42f, boxY + boxSize - pad },
				ImGui::GetColorU32(colors.textPrimary),
				2.f);
			drawList->AddLine(
				{ pos.x + boxSize * 0.42f, boxY + boxSize - pad },
				{ pos.x + boxSize - pad, boxY + pad + 1.f },
				ImGui::GetColorU32(colors.textPrimary),
				2.f);
		}

		// Center text to the box (half text body above geometric center → optical match).
		const float textY = boxY + (boxSize - textH) * 0.5f;
		drawList->AddText(
			ImVec2(pos.x + boxSize + labelGap, textY),
			ImGui::GetColorU32(colors.textPrimary),
			label);
		return changed;
	}

	ImVec4 ComponentVersionAccent(ComponentUpdateStatus status)
	{
		return UiCommon::FixedVersionStatusAccent(status);
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
		bool showExtraStrategies,
		float innerWidth,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		int activeStrategy,
		StrategyTestState strategyTestState,
		int testingStrategyIndex,
		ZapretManager* manager,
		bool tgProxyRunning,
		int* outOpenDetailIndex)
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
		int gridIndex = 0;

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
			const bool isActive = activeStrategy == i;
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
				selected || isTesting || highlightBest,
				{ colWidth, UiMetrics::kBtnHeight },
				colors,
				accent))
			{
				selectedStrategy = i;
				if (manager)
					manager->RememberSelectedStrategy(i);
			}

			UiCommon::SetItemTooltip("ПКМ — подробности");
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && outOpenDetailIndex)
				*outOpenDetailIndex = i;

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
			m_quickStrategyTest = m_appSettings->GetQuickStrategyTest();
		}
		if (m_manager)
			m_selectedStrategy = m_manager->GetPreferredStrategyIndex(m_autoSelect);
		ClampSelectedStrategy();
		m_preferencesLoaded = true;
	}

	ApplyPendingDiagnostics();

	const ZapretRunStatus runStatus = m_manager ? m_manager->GetCachedRunStatus() : ZapretRunStatus::Stopped;
	const bool running = runStatus == ZapretRunStatus::Running || runStatus == ZapretRunStatus::Starting;
	const int activeStrategy = m_manager ? m_manager->GetActiveStrategyIndex() : -1;
	const bool discordOnline = m_manager ? m_manager->IsDiscordOnline() : false;
	const bool youtubeOnline = m_manager ? m_manager->IsYouTubeOnline() : false;
	const bool tgProxyRunning = m_tgProxyManager && m_tgProxyManager->IsRunning();
	const bool telegramProbeOk = m_manager ? m_manager->IsTelegramOnline() : false;
	const bool checkingServices = m_manager && m_manager->IsCheckingConnectivity();
	const StrategyTestState strategyTestState = m_manager
		? m_manager->GetStrategyTestState()
		: StrategyTestState::Idle;
	const bool strategyTestActive = strategyTestState != StrategyTestState::Idle;
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

	if (m_view == View::Detail)
	{
		if (!m_manager || m_detailStrategyIndex < 0
			|| !m_manager->IsStrategyVisible(m_detailStrategyIndex, true))
		{
			m_view = View::List;
			m_detailStrategyIndex = -1;
		}
		else
		{
			DrawDetailView(theme, fonts, width);
			return;
		}
	}

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
			ComponentVersionAccent(updateCheck.GetZapretStatus()),
			updateBtnLabel,
			showUpdateBtn && !updateApplying))
	{
		m_diagnosticsStatus.clear();
		ZapretUpdateApply::Instance().RequestApply(m_manager, m_tgProxyManager);
	}

	// Update can also start without this button; don't leave a stale diagnostics summary
	// that looks like it belongs to the update log.
	if (updateApplying && !m_diagnosticsStatus.empty()
		&& m_diagnosticsStatus.rfind("Диагностика:", 0) == 0)
	{
		m_diagnosticsStatus.clear();
	}

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Обход блокировок Discord, YouTube");
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
		const bool selectionInProgress = strategyTestState == StrategyTestState::Running;
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
			if (UiCommon::AccentButton("Остановить", { mainBtnW, btnH }, UiCommon::FixedStopAccent(), colors) && m_manager)
				m_manager->RequestStop();
		}
		else if (UiCommon::AccentButton(
			"Запустить",
			{ mainBtnW, btnH },
			UiCommon::FixedStartAccent(),
			colors,
			m_manager && !m_manager->IsOperationInFlight()) && m_manager)
		{
			m_manager->RequestStart(m_selectedStrategy, GameFilterModeFromIndex(m_gameFilterMode));
		}

		ImGui::SameLine(0.f, gap);
		if (UiCommon::SecondaryButton(
			"Перезапуск",
			{ restartBtnW, btnH },
			colors,
			m_manager != nullptr && !m_manager->IsOperationInFlight()) && m_manager)
		{
			m_manager->RequestStart(m_selectedStrategy, GameFilterModeFromIndex(m_gameFilterMode));
		}
		ImGui::SameLine(0.f, gap);
		const char* strategyTestLabel = m_manager ? m_manager->GetStrategyTestButtonLabel() : "Подбор стратегий";
		if (UiCommon::SecondaryButton(strategyTestLabel, { actionBtnW, btnH }, colors) && m_manager)
			m_manager->HandleStrategyTestButton(GameFilterModeFromIndex(m_gameFilterMode));
		if (strategyTestState == StrategyTestState::Running)
		{
			UiCommon::SetItemTooltip(
				"Можно полностью остановить, нажав правой кнопкой мыши.");
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && m_manager)
				m_manager->RequestStopStrategyTest();
		}
		ImGui::SameLine(0.f, gap);
		if (UiCommon::SecondaryButton(
			m_diagnosticsRunning.load() ? "Диагностика..." : "Запустить Диагностику",
			{ actionBtnW, btnH },
			colors,
			!m_diagnosticsRunning.load()))
			StartDiagnostics();

		ImGui::Dummy({ 0.f, 4.f });
		if (!m_diagnosticsStatus.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted(m_diagnosticsStatus.c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 4.f });
		}
		if (UiCommon::SecondaryButton("Обновить проверку", { 150.f, UiMetrics::kSmallBtnHeight }, colors) && m_manager)
		{
			const int strategyIndex = (activeStrategy >= 0) ? activeStrategy : m_selectedStrategy;
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
		const bool showServiceCheckLine = !strategyTestRunning
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

		const auto drawDiagConfirmButtons = [&](bool& askFlag, auto onYes) {
			constexpr float kDialogW = 340.f;
			constexpr float kBtnW = 112.f;
			const float btnH = UiMetrics::kSmallBtnHeight;

			ImGui::Dummy({ kDialogW, 0.f });
			ImGui::Dummy({ 0.f, 14.f });

			const float rowX = ImGui::GetCursorPosX();
			if (UiCommon::SecondaryButton("Нет", { kBtnW, btnH }, colors))
			{
				askFlag = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine(0.f, 0.f);
			ImGui::SetCursorPosX(rowX + kDialogW - kBtnW);
			if (UiCommon::AccentButton("Да", { kBtnW, btnH }, accents.ok, colors))
			{
				onYes();
				askFlag = false;
				ImGui::CloseCurrentPopup();
			}
		};

		const auto pushDiagModalStyle = [&]() {
			const bool light = UiCommon::IsLightTheme(colors);
			const ImVec4 popupBg = light
				? ImVec4(0.90f, 0.90f, 0.92f, 0.98f)
				: UiCommon::WithAlpha(colors.tileBg, 0.98f);
			ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
			ImGui::PushStyleColor(ImGuiCol_Border, UiCommon::WithAlpha(colors.tileBorder, light ? 0.55f : 0.40f));
			ImGui::PushStyleColor(ImGuiCol_TitleBg, popupBg);
			ImGui::PushStyleColor(ImGuiCol_TitleBgActive, popupBg);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
			ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 18.f, 16.f });
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 10.f, 8.f });
		};
		const auto popDiagModalStyle = []() {
			ImGui::PopStyleVar(5);
			ImGui::PopStyleColor(4);
		};

		if (m_askRemoveConflicts)
			ImGui::OpenPopup("##diag_conflicts");
		pushDiagModalStyle();
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			// Always: AlwaysAutoResize has size 0 on first frame; Appearing would pin the top edge to center.
			ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
				"##diag_conflicts",
				nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
			ImGui::TextUnformatted("Найдены конфликтующие bypass-сервисы.");
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted("Удалить их?");
			ImGui::PopStyleColor();
			for (const auto& s : m_pendingConflictServices)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
				ImGui::BulletText("%s", s.c_str());
				ImGui::PopStyleColor();
			}
			drawDiagConfirmButtons(m_askRemoveConflicts, [&]() {
				ZapretDiagnostics::RemoveServices(m_pendingConflictServices);
				AppLog::Instance().Append(LogSource::Zapret, "[Диагностика] Конфликтующие сервисы удалены.");
			});
			ImGui::EndPopup();
		}
		popDiagModalStyle();

		if (m_askClearDiscordCache)
			ImGui::OpenPopup("##diag_discord_cache");
		pushDiagModalStyle();
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
				"##diag_discord_cache",
				nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
			ImGui::TextUnformatted("Очистить кэш Discord?");
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextWrapped("Discord будет закрыт, папки Cache / Code Cache / GPUCache будут удалены.");
			ImGui::PopStyleColor();
			drawDiagConfirmButtons(m_askClearDiscordCache, [&]() {
				ZapretDiagnostics::ClearDiscordCache();
				AppLog::Instance().Append(LogSource::Zapret, "[Диагностика] Кэш Discord очищен.");
			});
			ImGui::EndPopup();
		}
		popDiagModalStyle();

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
		ImGui::Text("(%d)", visibleCount);
		ImGui::PopStyleColor();

		if (strategyTestActive && m_manager)
		{
			ImGui::SameLine(0.f, 10.f);
			const float progress = m_manager->GetStrategyTestProgress();
			const float barY = headerRowY + (textLineH - barH) * 0.5f;
			ImGui::SetCursorPosY(barY);

			const float barWidth = ImGui::GetContentRegionAvail().x;
			if (barWidth > 24.f)
			{
				const bool completed = strategyTestState == StrategyTestState::Completed;
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

		int openDetailIndex = -1;
		DrawStrategyGrid(
			m_selectedStrategy,
			m_showExtraStrategies,
			innerWidth,
			colors,
			accents,
			activeStrategy,
			strategyTestState,
			testingStrategyIndex,
			m_manager,
			tgProxyRunning,
			&openDetailIndex);
		if (openDetailIndex >= 0)
		{
			m_detailStrategyIndex = openDetailIndex;
			m_selectedStrategy = openDetailIndex;
			m_detailQuickTest = false;
			if (m_manager)
				m_manager->RememberSelectedStrategy(openDetailIndex);
			m_view = View::Detail;
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		const float detailsBtnW = 88.f;
		const float fileRowH = UiMetrics::kSmallBtnHeight;
		const float fileRowY = ImGui::GetCursorPosY();

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::SetCursorPosY(fileRowY + (fileRowH - ImGui::GetTextLineHeight()) * 0.5f);
		if (m_manager)
			ImGui::Text("Файл: %s", m_manager->GetStrategyFileName(m_selectedStrategy).c_str());
		else
			ImGui::TextUnformatted("Файл: —");
		ImGui::PopStyleColor();

		ImGui::SetCursorPos({ innerWidth - detailsBtnW, fileRowY });
		const bool canShowDetails = m_manager && !m_manager->GetStrategyLabel(m_selectedStrategy).empty();
		if (UiCommon::SecondaryButton("Подробно", { detailsBtnW, fileRowH }, colors, canShowDetails) && canShowDetails)
			m_strategyDetailsOpen = !m_strategyDetailsOpen;
		ImGui::SetCursorPosY(fileRowY + fileRowH);

		if (m_strategyDetailsOpen && canShowDetails)
		{
			const StrategyDescription* description = nullptr;
			const char* title = "";
			if (m_manager)
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

			const StrategyTestEntry* runtimeStats = m_manager
				? m_manager->GetStrategyResult(m_selectedStrategy)
				: nullptr;
			DrawStrategyRuntimeStats(runtimeStats, innerWidth, colors);
		}

		if (running && activeStrategy >= 0 && activeStrategy != m_selectedStrategy)
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

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		if (UiCommon::StyledCheckbox("Отобразить дополнительные стратегии", &m_showExtraStrategies, colors))
		{
			if (m_appSettings)
				m_appSettings->SetShowExtraStrategies(m_showExtraStrategies);
			ClampSelectedStrategy();
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		if (UiCommon::StyledCheckbox("Упрощенное тестирование", &m_quickStrategyTest, colors))
		{
			if (m_appSettings)
				m_appSettings->SetQuickStrategyTest(m_quickStrategyTest);
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}

void UiAntiZapretPage::DrawDetailView(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();
	if (!m_manager || m_detailStrategyIndex < 0)
	{
		m_view = View::List;
		return;
	}

	const int strategyIndex = m_detailStrategyIndex;
	const std::string label = m_manager->GetStrategyLabel(strategyIndex);
	const std::string fileName = m_manager->GetStrategyFileName(strategyIndex);
	const StrategyDescription* description = StrategyDescriptions::GetById(label.c_str());
	if (!description)
		description = StrategyDescriptions::GetById(fileName.c_str());
	const StrategyTestEntry* result = m_manager->GetStrategyResult(strategyIndex);
	const std::vector<StrategyTargetResultView> targets = m_manager->GetStrategyTargetResults(strategyIndex);
	const int activeStrategy = m_manager->GetActiveStrategyIndex();
	const bool running = m_manager->GetCachedRunStatus() == ZapretRunStatus::Running
		|| m_manager->GetCachedRunStatus() == ZapretRunStatus::Starting;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, UiMetrics::kRowGap));

	if (UiCommon::SecondaryButton("<- Назад", ImVec2(100.f, UiMetrics::kSmallBtnHeight), colors))
	{
		m_view = View::List;
		ImGui::PopStyleVar();
		return;
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Антизапрет  >  Стратегия");
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0.f, 4.f));
	UiCommon::PageTitle(fonts, 0xE774, label.c_str(), fileName.c_str(), colors);

	const float actionBtnW = 120.f;
	const float testBtnW = 140.f;
	const bool testBusy = m_manager->GetStrategyTestState() == StrategyTestState::Running
		|| m_manager->GetSmartStrategyTuneState() == SmartStrategyTuneState::Running;
	if (UiCommon::AccentButton(
			"Запустить",
			ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight),
			UiCommon::FixedStartAccent(),
			colors,
			!m_manager->IsOperationInFlight() && !testBusy)
		&& m_manager)
	{
		m_selectedStrategy = strategyIndex;
		m_manager->RememberSelectedStrategy(strategyIndex);
		m_manager->RequestStart(strategyIndex, GameFilterModeFromIndex(m_gameFilterMode));
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton(
			"Запустить тест",
			ImVec2(testBtnW, UiMetrics::kSmallBtnHeight),
			colors,
			!m_manager->IsOperationInFlight() && !testBusy)
		&& m_manager)
	{
		m_selectedStrategy = strategyIndex;
		m_manager->RememberSelectedStrategy(strategyIndex);
		m_manager->RequestSingleStrategyTest(
			strategyIndex,
			GameFilterModeFromIndex(m_gameFilterMode),
			m_detailQuickTest);
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	DetailBesideButtonCheckbox("Упрощенное тестирование", &m_detailQuickTest, colors);

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));

	if (UiCommon::BeginCard("##strategy_detail_info", width, colors))
	{
		UiCommon::SectionHeader(label.c_str(), colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		UiCommon::InfoLine("Файл", fileName.c_str(), colors);
		char statusBuf[128] = {};
		if (activeStrategy == strategyIndex && running)
			snprintf(statusBuf, sizeof statusBuf, "Активна сейчас");
		else if (activeStrategy == strategyIndex)
			snprintf(statusBuf, sizeof statusBuf, "Выбрана");
		else
			snprintf(statusBuf, sizeof statusBuf, "Не активна");
		UiCommon::InfoLine("Статус", statusBuf, colors);

		if (description)
		{
			ImGui::Dummy(ImVec2(0.f, UiMetrics::kRowGap));
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - UiMetrics::kCardPad * 2.f);
			ImGui::TextUnformatted(description->summary);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##strategy_detail_runtime", width, colors))
	{
		UiCommon::SectionHeader("Статистика работы", colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		char runtimeBuffer[64] = {};
		FormatRuntimeDuration(result ? result->runtimeSec : 0, runtimeBuffer, sizeof runtimeBuffer);
		UiCommon::InfoLine("Накопленное время", runtimeBuffer, colors);
		char dualBuf[32] = {};
		snprintf(dualBuf, sizeof dualBuf, "%d", result ? result->providerDualOutageCount : 0);
		UiCommon::InfoLine("Сбои провайдера (D+Y)", dualBuf, colors);
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##strategy_detail_test", width, colors))
	{
		UiCommon::SectionHeader("Последний тест", colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		if (!result)
		{
			UiCommon::CaptionText("Стратегия ещё не тестировалась.", colors, width);
		}
		else
		{
			char modeBuf[48] = {};
			snprintf(modeBuf, sizeof modeBuf, "%s", result->fullTest ? "Полный (standard)" : "Упрощенный");
			UiCommon::InfoLine("Режим", modeBuf, colors);

			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted("Сервисы");
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, 12.f);

			auto drawSvc = [&](const char* name, bool ok, const ImVec4& brand)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, brand);
				ImGui::TextUnformatted(name);
				ImGui::PopStyleColor();
				ImGui::SameLine(0.f, 4.f);
				ImGui::PushStyleColor(ImGuiCol_Text, ok ? accents.ok : accents.fail);
				ImGui::TextUnformatted(ok ? "OK" : "FAIL");
				ImGui::PopStyleColor();
			};

			drawSvc("Discord", result->discordOk, accents.discord);
			ImGui::SameLine(0.f, 10.f);
			drawSvc("YouTube", result->youtubeOk, accents.youtube);
			ImGui::SameLine(0.f, 10.f);
			drawSvc("Telegram", result->telegramOk, accents.telegram);

			char pingBuf[48] = {};
			if (result->pingMs >= 0)
				snprintf(pingBuf, sizeof pingBuf, "%d ms", result->pingMs);
			else
				snprintf(pingBuf, sizeof pingBuf, "—");
			UiCommon::InfoLine("Пинг", pingBuf, colors);

			if (result->fullTest || result->httpOk > 0 || result->httpErr > 0)
			{
				char scoreBuf[96] = {};
				snprintf(
					scoreBuf,
					sizeof scoreBuf,
					"HTTP OK: %d  ERR: %d  |  Ping OK: %d  Fail: %d",
					result->httpOk,
					result->httpErr,
					result->pingOk,
					result->pingFail);
				UiCommon::CaptionText(scoreBuf, colors, width);
			}
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##strategy_detail_targets", width, colors))
	{
		UiCommon::SectionHeader("Цели теста", colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		if (targets.empty())
		{
			UiCommon::CaptionText(
				"Нет данных теста. Нажмите «Запустить тест» или выполните подбор стратегий.",
				colors,
				width);
		}
		else if (ImGui::BeginTable(
					 "##strategy_targets",
					 3,
					 ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Цель", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Результат", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 64.f);
			ImGui::TableHeadersRow();
			for (const StrategyTargetResultView& row : targets)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImVec4 nameColor = colors.textPrimary;
				if (row.name.find("Discord") != std::string::npos)
					nameColor = accents.discord;
				else if (row.name.find("YouTube") != std::string::npos)
					nameColor = accents.youtube;
				else if (row.name.find("Telegram") != std::string::npos)
					nameColor = accents.telegram;
				ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
				ImGui::TextUnformatted(row.name.c_str());
				ImGui::PopStyleColor();

				ImGui::TableSetColumnIndex(1);
				const bool detailIsStatus = (row.detail == "OK" || row.detail == "FAIL");
				if (detailIsStatus)
					ImGui::PushStyleColor(ImGuiCol_Text, row.ok ? accents.ok : accents.fail);
				else if (row.isPing && row.ok)
					ImGui::PushStyleColor(ImGuiCol_Text, accents.ok);
				else if (row.isPing && !row.ok)
					ImGui::PushStyleColor(ImGuiCol_Text, accents.fail);
				else
					ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
				ImGui::TextUnformatted(row.detail.c_str());
				ImGui::PopStyleColor();

				ImGui::TableSetColumnIndex(2);
				ImGui::PushStyleColor(ImGuiCol_Text, row.ok ? accents.ok : accents.fail);
				ImGui::TextUnformatted(row.ok ? "OK" : "FAIL");
				ImGui::PopStyleColor();
			}
			ImGui::EndTable();
		}
	}
	UiCommon::EndCard();

	if (description)
	{
		UiCommon::CardGap();
		if (UiCommon::BeginCard("##strategy_detail_desc", width, colors))
		{
			const float innerWidth = ImGui::GetContentRegionAvail().x;
			DrawStrategyDetailsPanel(description, "Описание", innerWidth, colors);
		}
		UiCommon::EndCard();
	}
	else
	{
		UiCommon::CardGap();
		if (UiCommon::BeginCard("##strategy_detail_desc", width, colors))
		{
			UiCommon::SectionHeader("Описание", colors);
			ImGui::Dummy(ImVec2(0.f, 4.f));
			UiCommon::CaptionText(
				"Стратегия из файла (описание недоступно). Добавьте запись в strategy_descriptions.cpp.",
				colors,
				width);
		}
		UiCommon::EndCard();
	}

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kCardGap));
	ImGui::PopStyleVar();
}

void UiAntiZapretPage::StartDiagnostics()
{
	if (m_diagnosticsRunning.load())
		return;
	m_diagnosticsRunning.store(true);
	m_diagnosticsStatus = "Диагностика...";
	m_askClearDiscordCache = false;
	m_askRemoveConflicts = false;
	std::thread([this]() {
		auto report = ZapretDiagnostics::Run();
		{
			std::lock_guard<std::mutex> lock(m_diagnosticsMutex);
			m_diagnosticsReport = std::move(report);
			m_diagnosticsPending = true;
		}
		m_diagnosticsRunning.store(false);
	}).detach();
}

void UiAntiZapretPage::ApplyPendingDiagnostics()
{
	ZapretDiagnostics::Report report;
	{
		std::lock_guard<std::mutex> lock(m_diagnosticsMutex);
		if (!m_diagnosticsPending)
			return;
		report = std::move(m_diagnosticsReport);
		m_diagnosticsPending = false;
	}
	for (const auto& line : report.lines)
		AppLog::Instance().Append(LogSource::Zapret, std::string("[Диагностика] ") + line.text);
	char buf[128];
	snprintf(buf, sizeof buf, "Диагностика: ошибок %d, предупреждений %d", report.errorCount, report.warnCount);
	m_diagnosticsStatus = buf;
	if (!report.conflictingServices.empty())
	{
		m_pendingConflictServices = report.conflictingServices;
		m_askRemoveConflicts = true;
	}
	m_askClearDiscordCache = report.askClearDiscordCache;
}
