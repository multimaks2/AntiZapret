#include "ui/ui_settings_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
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
		m_loadedFromSettings = true;
	}

	const bool lightTheme = m_appSettings ? m_appSettings->GetLightTheme() : theme.IsLight();
	const float themeMix = lightTheme ? 1.f : 0.f;
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

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xE713,
		"Настройки",
		nullptr,
		colors);

	if (UiCommon::SettingRow("Темная/Светлая тема", width, colors, themeMix))
	{
		if (m_appSettings)
		{
			const bool nextLight = !m_appSettings->GetLightTheme();
			m_appSettings->SetLightTheme(nextLight);
			theme.SetLight(nextLight);
		}
		else
		{
			theme.SetLight(!theme.IsLight());
		}
	}

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

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::BeginCard("##settings_discord", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Discord", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Показывать активность AntiZapret в профиле Discord.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (UiCommon::SettingRow("Discord активность", innerWidth, colors, m_discordPresenceMix))
		{
			if (m_appSettings)
				m_appSettings->SetDiscordPresenceEnabled(!m_appSettings->GetDiscordPresenceEnabled());
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		if (UiCommon::SecondaryButton(
				"Поделиться методом",
				{ innerWidth, UiMetrics::kBtnHeight },
				colors))
		{
			// Placeholder: share method flow will be added later.
		}
	}
	UiCommon::EndCard();

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
