#include "ui/ui_settings_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_store.h"
#include "imgui.h"

namespace
{
	struct ScrollPageSetting
	{
		const char* label;
		int pageIndex;
	};

	const ScrollPageSetting kScrollPages[] = {
		{ "Главная", 0 },
		{ "Антизапрет", 1 },
		{ "TG WS Proxy", 2 },
		{ "VPN", 3 },
		{ "Маршрутизация", 4 },
		{ "Консоль", 5 },
		{ "Настройки", 6 },
		{ "О приложении", 7 },
	};
}

void UiSettingsPage::SetAppSettings(AppSettings* settings)
{
	m_appSettings = settings;
	m_loadedFromSettings = false;
}

void UiSettingsPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const float deltaTime = ImGui::GetIO().DeltaTime;
	constexpr float kToggleAnimSpeed = 10.f;

	if (m_appSettings && !m_loadedFromSettings)
	{
		m_autostartAppMix = m_appSettings->GetAutostartApp() ? 1.f : 0.f;
		m_autostartBypassMix = m_appSettings->GetAutostartBypass() ? 1.f : 0.f;
		m_autostartTelegramMix = m_appSettings->GetAutostartTelegram() ? 1.f : 0.f;
		m_autostartVpnMix = m_appSettings->GetAutostartVpn() ? 1.f : 0.f;
		m_confirmAdultMix = m_appSettings->GetConfirmAdult() ? 1.f : 0.f;
		m_discordPresenceMix = m_appSettings->GetDiscordPresenceEnabled() ? 1.f : 0.f;
		m_discordShareButtonMix = m_appSettings->GetDiscordShareButtonEnabled() ? 1.f : 0.f;
		m_discordDownloadButtonMix = m_appSettings->GetDiscordDownloadButtonEnabled() ? 1.f : 0.f;
		strncpy_s(
			m_discordDownloadUrl,
			sizeof m_discordDownloadUrl,
			m_appSettings->GetDiscordDownloadUrl().c_str(),
			_TRUNCATE);
		const std::string storedHwid = m_appSettings->GetCustomHwid();
		const std::string systemHwid = VpnImport::GetSystemHwid();
		strncpy_s(
			m_customHwid,
			sizeof m_customHwid,
			(storedHwid.empty() ? systemHwid : storedHwid).c_str(),
			_TRUNCATE);
		m_loadedFromSettings = true;
	}

	const UiThemeId activeTheme = m_appSettings ? m_appSettings->GetThemeId() : theme.GetTheme();
	m_autostartAppMix = UiCommon::AnimateMix(
		m_autostartAppMix,
		m_appSettings && m_appSettings->GetAutostartApp(),
		deltaTime,
		kToggleAnimSpeed);
	m_autostartBypassMix = UiCommon::AnimateMix(
		m_autostartBypassMix,
		m_appSettings && m_appSettings->GetAutostartBypass(),
		deltaTime,
		kToggleAnimSpeed);
	m_autostartTelegramMix = UiCommon::AnimateMix(
		m_autostartTelegramMix,
		m_appSettings && m_appSettings->GetAutostartTelegram(),
		deltaTime,
		kToggleAnimSpeed);
	m_autostartVpnMix = UiCommon::AnimateMix(
		m_autostartVpnMix,
		m_appSettings && m_appSettings->GetAutostartVpn(),
		deltaTime,
		kToggleAnimSpeed);
	m_confirmAdultMix = UiCommon::AnimateMix(
		m_confirmAdultMix,
		m_appSettings && m_appSettings->GetConfirmAdult(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordPresenceMix = UiCommon::AnimateMix(
		m_discordPresenceMix,
		m_appSettings && m_appSettings->GetDiscordPresenceEnabled(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordShareButtonMix = UiCommon::AnimateMix(
		m_discordShareButtonMix,
		m_appSettings && m_appSettings->GetDiscordShareButtonEnabled(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordDownloadButtonMix = UiCommon::AnimateMix(
		m_discordDownloadButtonMix,
		m_appSettings && m_appSettings->GetDiscordDownloadButtonEnabled(),
		deltaTime,
		kToggleAnimSpeed);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xE713,
		"Настройки",
		nullptr,
		colors);

	if (UiCommon::BeginCard("##settings_theme", width, colors))
	{
		UiCommon::SectionHeader("Тема оформления", colors);
		UiCommon::CaptionText("Выберите палитру интерфейса", colors, ImGui::GetContentRegionAvail().x);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		const float avail = ImGui::GetContentRegionAvail().x;
		const float gap = UiMetrics::kGridGap;
		const int columns = avail > 520.f ? 4 : (avail > 360.f ? 3 : 2);
		const float cellW = (avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
		const float cellH = 44.f;

		for (int i = 0; i < ThemeManager::ThemeCount(); ++i)
		{
			const UiThemeId id = static_cast<UiThemeId>(i);
			const UiThemeInfo& info = ThemeManager::Info(id);
			const bool selected = activeTheme == id;

			if (i % columns != 0)
				ImGui::SameLine(0.f, gap);

			ImGui::PushID(i);
			const ImVec2 cellMin = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##theme", { cellW, cellH }))
			{
				if (m_appSettings)
					m_appSettings->SetThemeId(id);
				theme.SetTheme(id);
			}
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 cellMax = { cellMin.x + cellW, cellMin.y + cellH };
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(cellMin, cellMax, ImGui::GetColorU32(colors.tileBg), UiMetrics::kCardRadius);
			dl->AddRect(
				cellMin,
				cellMax,
				ImGui::GetColorU32(selected ? info.swatch : (hovered ? colors.navHover : colors.tileBorder)),
				UiMetrics::kCardRadius,
				0,
				selected ? 2.f : 1.f);

			const float swatch = 18.f;
			const ImVec2 swMin = { cellMin.x + 12.f, cellMin.y + (cellH - swatch) * 0.5f };
			const ImVec2 swMax = { swMin.x + swatch, swMin.y + swatch };
			dl->AddRectFilled(swMin, swMax, ImGui::GetColorU32(info.swatch), 4.f);
			dl->AddRect(swMin, swMax, ImGui::GetColorU32(colors.tileBorder), 4.f, 0, 1.f);

			const ImVec2 textSize = ImGui::CalcTextSize(info.name);
			dl->AddText(
				{ swMax.x + 10.f, cellMin.y + (cellH - textSize.y) * 0.5f },
				ImGui::GetColorU32(colors.textPrimary),
				info.name);
			ImGui::PopID();
		}
	}
	UiCommon::EndCard();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::BeginCard("##settings_hwid", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("HWID для VPN-подписок", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Заголовок x-hwid при импорте подписки. По умолчанию в поле уже стоит системный HWID — "
			"можно заменить на свой.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		const std::string systemHwid = VpnImport::GetSystemHwid();
		auto saveHwidFromEdit = [&]() {
			if (!m_appSettings)
				return;
			// Same as system (or empty) -> keep auto mode, don't store a duplicate override.
			if (m_customHwid[0] == '\0' || systemHwid == m_customHwid)
				m_appSettings->SetCustomHwid({});
			else
				m_appSettings->SetCustomHwid(m_customHwid);
		};

		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(innerWidth);
		if (ImGui::InputText(
				"##custom_hwid",
				m_customHwid,
				sizeof m_customHwid,
				ImGuiInputTextFlags_EnterReturnsTrue))
		{
			saveHwidFromEdit();
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			saveHwidFromEdit();
		UiCommon::PopInputStyle();
	}
	UiCommon::EndCard();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::SettingRow("Автозапуск приложения", width, colors, m_autostartAppMix))
	{
		if (m_appSettings)
			m_appSettings->SetAutostartApp(!m_appSettings->GetAutostartApp());
	}

	if (UiCommon::SettingRow("Автозапуск обхода", width, colors, m_autostartBypassMix))
	{
		if (m_appSettings)
			m_appSettings->SetAutostartBypass(!m_appSettings->GetAutostartBypass());
	}

	if (UiCommon::SettingRow("Автозапуск прокси Telegram", width, colors, m_autostartTelegramMix))
	{
		if (m_appSettings)
			m_appSettings->SetAutostartTelegram(!m_appSettings->GetAutostartTelegram());
	}

	if (UiCommon::SettingRow("Автозапуск VPN", width, colors, m_autostartVpnMix))
	{
		if (m_appSettings)
			m_appSettings->SetAutostartVpn(!m_appSettings->GetAutostartVpn());
	}

	if (UiCommon::SettingRow("Мне есть 18 лет", width, colors, m_confirmAdultMix))
	{
		if (m_appSettings)
		{
			m_appSettings->SetConfirmAdult(!m_appSettings->GetConfirmAdult());
			if (m_vpnManager)
			{
				VpnStore store;
				VpnStoreSettings settings;
				store.LoadSettings(settings);
				++settings.routingRevision;
				store.SaveSettings(settings);
				m_vpnManager->RequestReloadFromStore();
			}
		}
	}

	if (UiCommon::SettingRow("Discord активность", width, colors, m_discordPresenceMix))
	{
		if (m_appSettings)
			m_appSettings->SetDiscordPresenceEnabled(!m_appSettings->GetDiscordPresenceEnabled());
	}

	if (UiCommon::SettingRow("Кнопка «Импорт» в Discord", width, colors, m_discordShareButtonMix))
	{
		if (m_appSettings)
			m_appSettings->SetDiscordShareButtonEnabled(!m_appSettings->GetDiscordShareButtonEnabled());
	}

	if (UiCommon::SettingRow("Кнопка «Скачать» в Discord", width, colors, m_discordDownloadButtonMix))
	{
		if (m_appSettings)
			m_appSettings->SetDiscordDownloadButtonEnabled(!m_appSettings->GetDiscordDownloadButtonEnabled());
	}

	if (m_appSettings && m_appSettings->GetDiscordDownloadButtonEnabled())
	{
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		UiCommon::CaptionText("Ссылка кнопки «Скачать AntiZapret»:", colors, width);
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(width);
		if (ImGui::InputTextWithHint(
				"##discord_download_url",
				"https://github.com/.../releases/latest",
				m_discordDownloadUrl,
				sizeof m_discordDownloadUrl,
				ImGuiInputTextFlags_EnterReturnsTrue))
		{
			m_appSettings->SetDiscordDownloadUrl(m_discordDownloadUrl);
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			m_appSettings->SetDiscordDownloadUrl(m_discordDownloadUrl);
		UiCommon::PopInputStyle();
	}

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::BeginCard("##settings_scroll", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Скорость прокрутки", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Множитель колёсика мыши для каждой страницы. По умолчанию x2.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (m_appSettings)
		{
			const float labelWidth = 132.f;
			const float sliderWidth = innerWidth - labelWidth - UiMetrics::kGridGap;

			for (const ScrollPageSetting& page : kScrollPages)
			{
				float multiplier = m_appSettings->GetPageScrollMultiplier(page.pageIndex);
				ImGui::PushID(page.pageIndex);
				ImGui::AlignTextToFramePadding();
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
				ImGui::TextUnformatted(page.label);
				ImGui::PopStyleColor();
				ImGui::SameLine(labelWidth);

				UiCommon::PushSliderStyle(colors);
				ImGui::SetNextItemWidth(sliderWidth);
				const bool changed = ImGui::SliderFloat(
					"##scroll_multiplier",
					&multiplier,
					AppSettings::kMinScrollMultiplier,
					AppSettings::kMaxScrollMultiplier,
					"%.1f x");
				UiCommon::PopSliderStyle();

				if (changed)
					m_appSettings->SetPageScrollMultiplier(page.pageIndex, multiplier);
				if (ImGui::IsItemDeactivatedAfterEdit())
					m_appSettings->SavePageScrollMultipliers();

				ImGui::PopID();
				ImGui::Dummy({ 0.f, 4.f });
			}
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}
