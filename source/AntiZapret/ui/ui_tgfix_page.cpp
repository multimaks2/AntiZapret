#include "ui/ui_tgfix_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "ui/ui_common.h"
#include "zapret/zapret_update_apply.h"
#include "zapret/zapret_update_check.h"
#include "imgui.h"

#include <cstdio>
#include <string>

namespace
{
	const std::string kEmptyTelegramLink;

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
}

void UiTgFixPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();

	const bool running = m_manager && m_manager->IsRunning();
	const bool openTelegram = m_settings && m_settings->GetOpenTelegramOnProxyStart();
	const auto& updateCheck = ZapretUpdateCheck::Instance();
	std::string tgVersion = updateCheck.GetTgProxyLocalVersion();
	if (!IsDisplayVersion(tgVersion))
		tgVersion.clear();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });

	const bool updateAvailable = updateCheck.GetTgProxyStatus() == ComponentUpdateStatus::UpdateAvailable;
	const bool updateApplying = ZapretUpdateApply::Instance().IsApplyingTg();
	const std::string tgRemoteVersion = updateCheck.GetTgProxyRemoteVersion();
	const bool versionsDiffer = !tgVersion.empty()
		&& IsDisplayVersion(tgRemoteVersion)
		&& tgVersion != tgRemoteVersion;
	const bool showUpdateBtn = updateAvailable || updateApplying || versionsDiffer;
	const char* updateBtnLabel = showUpdateBtn
		? (updateApplying ? "Скачивание..." : "Скачать обновление")
		: nullptr;

	if (UiCommon::PageTitle(
			fonts,
			0xE8BD,
			"TG WS Proxy",
			nullptr,
			colors,
			tgVersion.empty() ? nullptr : tgVersion.c_str(),
			ComponentVersionAccent(updateCheck.GetTgProxyStatus()),
			updateBtnLabel,
			showUpdateBtn && !updateApplying))
	{
		ZapretUpdateApply::Instance().RequestApplyTg(m_manager);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Локальный MTProto-прокси для Telegram");
	ImGui::PopStyleColor();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (const std::string applyMsg = ZapretUpdateApply::Instance().GetTgStatusMessage();
		!applyMsg.empty() && showUpdateBtn)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextWrapped("%s", applyMsg.c_str());
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });
	}

	if (UiCommon::BeginCard("##tg_status", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Локальный прокси для Telegram", colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		const char* actionLabel = m_manager ? m_manager->GetPrimaryActionLabel() : "Запустить TG WS Proxy";
		const bool canAction = m_manager && m_manager->CanPrimaryAction();
		const ImVec4 actionColor = running ? UiCommon::FixedStopAccent() : UiCommon::FixedStartAccent();

		if (UiCommon::AccentButton(actionLabel, { innerWidth, 40.f }, actionColor, colors, canAction) && m_manager)
			m_manager->HandlePrimaryAction(openTelegram);

		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

		const char* processLabel = running
			? (m_manager->IsStartedByUs() ? "tg-ws-proxy" : "tg-ws-proxy (внешний)")
			: "—";
		UiCommon::InfoLine("Процесс: ", processLabel, colors);

		const char* envLabel = m_manager ? m_manager->GetEnvStatusLabel() : "—";
		UiCommon::InfoLine("Окружение: ", envLabel, colors);

		if (m_manager && !m_manager->GetStatusMessage().empty())
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextWrapped("%s", m_manager->GetStatusMessage().c_str());
			ImGui::PopStyleColor();
		}

		if (m_manager && !m_manager->GetErrorMessage().empty())
		{
			ImGui::Dummy({ 0.f, 4.f });
			ImGui::PushStyleColor(ImGuiCol_Text, accents.fail);
			ImGui::TextWrapped("%s", m_manager->GetErrorMessage().c_str());
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##tg_link", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Ссылка для Telegram", colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		const std::string& link = m_manager ? m_manager->GetTelegramLinkCached() : kEmptyTelegramLink;
		const char* linkText = link.empty() ? "tg://proxy?..." : link.c_str();

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, colors.classicControls ? 0.f : 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::BeginChild("##link_box", { innerWidth, 36.f }, ImGuiChildFlags_Borders);
		ImGui::SetCursorPos({ 10.f, 9.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(linkText);
		ImGui::PopStyleColor();
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);

		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

		const float btnW = (innerWidth - UiMetrics::kGridGap) * 0.5f;
		if (UiCommon::SecondaryButton("Копировать ссылку", { btnW, UiMetrics::kBtnHeight }, colors, m_manager != nullptr) && m_manager)
			m_manager->CopyTelegramLinkToClipboard();
		ImGui::SameLine(0.f, UiMetrics::kGridGap);
		if (UiCommon::SecondaryButton("Открыть в Telegram", { btnW, UiMetrics::kBtnHeight }, colors, m_manager != nullptr) && m_manager)
			m_manager->OpenTelegramLink();
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##tg_extra", width, colors))
	{
		UiCommon::SectionHeader("Дополнительно", colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (m_settings)
		{
			bool autoStart = m_settings->GetAutoStartTgProxyWithAntiZapret();
			if (UiCommon::StyledCheckbox("Запускать прокси вместе с Антизапретом", &autoStart, colors))
				m_settings->SetAutoStartTgProxyWithAntiZapret(autoStart);

			bool openOnStart = m_settings->GetOpenTelegramOnProxyStart();
			if (UiCommon::StyledCheckbox("Открывать Telegram при запуске прокси", &openOnStart, colors))
				m_settings->SetOpenTelegramOnProxyStart(openOnStart);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextWrapped(
			"Ссылка tg://proxy содержит secret=dd и 32-символьный ключ MTProto. "
			"Если Python или зависимости не установлены, кнопка запуска предложит установку.");
		ImGui::PopStyleColor();
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}
