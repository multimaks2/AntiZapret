#include "ui/ui_settings_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_store.h"
#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
		m_discordDownloadButtonMix = m_appSettings->GetDiscordDownloadButtonEnabled() ? 1.f : 0.f;
		m_discordImportAzMix = m_appSettings->GetDiscordImportAntiZapretEnabled() ? 1.f : 0.f;
		m_discordImportVpnMix = m_appSettings->GetDiscordImportVpnEnabled() ? 1.f : 0.f;
		m_discordImportTimedMix = m_appSettings->GetDiscordImportTimedEnabled() ? 1.f : 0.f;
		strncpy_s(
			m_discordDownloadUrl,
			sizeof m_discordDownloadUrl,
			m_appSettings->GetDiscordDownloadUrl().c_str(),
			_TRUNCATE);
		snprintf(
			m_discordImportMinutes,
			sizeof m_discordImportMinutes,
			"%d",
			m_appSettings->GetDiscordImportDurationMinutes());
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
	m_discordDownloadButtonMix = UiCommon::AnimateMix(
		m_discordDownloadButtonMix,
		m_appSettings && m_appSettings->GetDiscordDownloadButtonEnabled(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordImportAzMix = UiCommon::AnimateMix(
		m_discordImportAzMix,
		m_appSettings && m_appSettings->GetDiscordImportAntiZapretEnabled(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordImportVpnMix = UiCommon::AnimateMix(
		m_discordImportVpnMix,
		m_appSettings && m_appSettings->GetDiscordImportVpnEnabled(),
		deltaTime,
		kToggleAnimSpeed);
	m_discordImportTimedMix = UiCommon::AnimateMix(
		m_discordImportTimedMix,
		m_appSettings && m_appSettings->GetDiscordImportTimedEnabled(),
		deltaTime,
		kToggleAnimSpeed);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xf013,
		"Настройки",
		nullptr,
		colors,
		UiCommon::TitleIconFont::Solid);

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
			const UiThemeColors preview = ThemeManager::PaletteColors(id);
			const ImU32 cellFill = (hovered && !selected)
				? ImGui::GetColorU32(colors.navHover)
				: ImGui::GetColorU32(colors.tileBg);
			dl->AddRectFilled(cellMin, cellMax, cellFill, UiMetrics::kCardRadius);
			if (selected)
			{
				dl->AddRect(
					cellMin,
					cellMax,
					ImGui::GetColorU32(preview.navActive),
					UiMetrics::kCardRadius,
					0,
					2.f);
			}

			// Mini chrome: same bg / navActive the theme actually paints (no outline).
			const float swatch = 18.f;
			const ImVec2 swMin = { cellMin.x + 12.f, cellMin.y + (cellH - swatch) * 0.5f };
			const ImVec2 swMax = { swMin.x + swatch, swMin.y + swatch };
			dl->AddRectFilled(swMin, swMax, ImGui::GetColorU32(preview.bg), 4.f);
			dl->AddRectFilled(
				{ swMin.x, swMin.y + swatch * 0.55f },
				swMax,
				ImGui::GetColorU32(preview.navActive),
				4.f,
				ImDrawFlags_RoundCornersBottom);
			dl->AddRect(swMin, swMax, ImGui::GetColorU32(preview.tileBorder), 4.f, 0, 1.f);

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

	if (UiCommon::BeginCard("##settings_font_scale", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Масштаб шрифта", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Коэффициент увеличения текста интерфейса. По умолчанию 1.0.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (m_appSettings)
		{
			float scale = m_appSettings->GetFontScale();
			const float labelWidth = 148.f;
			const float sliderWidth = innerWidth - labelWidth - UiMetrics::kGridGap;

			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
			ImGui::TextUnformatted("Коэфф увеличения");
			ImGui::PopStyleColor();
			ImGui::SameLine(labelWidth);

			UiCommon::PushSliderStyle(colors);
			ImGui::SetNextItemWidth(sliderWidth);
			const bool changed = ImGui::SliderFloat(
				"##font_scale",
				&scale,
				AppSettings::kMinFontScale,
				AppSettings::kMaxFontScale,
				"%.2f x");
			UiCommon::PopSliderStyle();

			if (changed)
			{
				m_appSettings->SetFontScale(scale);
				ImGui::GetStyle().FontScaleMain = m_appSettings->GetFontScale();
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
				m_appSettings->SaveFontScale();
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

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	UiCommon::SectionHeader("Discord активность", colors);
	ImGui::Dummy({ 0.f, 4.f });
	UiCommon::CaptionText(
		"Видимость в Discord, кнопки скачивания и временный импорт стратегии/VPN для друзей.",
		colors,
		width);
	ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

	if (UiCommon::SettingRow("Показывать активность в Discord", width, colors, m_discordPresenceMix))
	{
		if (m_appSettings)
			m_appSettings->SetDiscordPresenceEnabled(!m_appSettings->GetDiscordPresenceEnabled());
	}

	if (m_appSettings && m_appSettings->GetDiscordPresenceEnabled())
	{
		if (UiCommon::SettingRow("Кнопка «Скачать»", width, colors, m_discordDownloadButtonMix))
			m_appSettings->SetDiscordDownloadButtonEnabled(!m_appSettings->GetDiscordDownloadButtonEnabled());

		if (m_appSettings->GetDiscordDownloadButtonEnabled())
		{
			ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
			UiCommon::CaptionText("Ссылка кнопки «Скачать AntiZapret»:", colors, width);
			UiCommon::PushInputStyle(colors);
			ImGui::SetNextItemWidth(width);
			if (ImGui::InputTextWithHint(
					"##discord_download_url",
					"https://github.com/.../latest/download/AntiZapret-Installer.exe",
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

		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap * 0.75f });
		UiCommon::CaptionText("Импорт", colors, width);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Кнопка в Discord появляется на вкладке Антизапрет или VPN, если включён соответствующий импорт. "
			"Друг получит вашу текущую стратегию или конфиг активного VPN-сервера (share-ссылку).",
			colors,
			width);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		char azLabel[96] = "Импорт Антизапрет";
		char vpnLabel[96] = "Импорт VPN";
		const int remainSec = m_appSettings->GetDiscordImportRemainingSeconds();
		if (m_appSettings->GetDiscordImportTimedEnabled() && remainSec >= 0
			&& (m_appSettings->GetDiscordImportAntiZapretEnabled() || m_appSettings->GetDiscordImportVpnEnabled()))
		{
			const int mm = remainSec / 60;
			const int ss = remainSec % 60;
			snprintf(azLabel, sizeof azLabel, "Импорт Антизапрет  ·  %d:%02d", mm, ss);
			snprintf(vpnLabel, sizeof vpnLabel, "Импорт VPN  ·  %d:%02d", mm, ss);
		}

		if (UiCommon::SettingRow(azLabel, width, colors, m_discordImportAzMix))
			m_appSettings->SetDiscordImportAntiZapretEnabled(!m_appSettings->GetDiscordImportAntiZapretEnabled());
		if (UiCommon::SettingRow(vpnLabel, width, colors, m_discordImportVpnMix))
			m_appSettings->SetDiscordImportVpnEnabled(!m_appSettings->GetDiscordImportVpnEnabled());

		if (UiCommon::SettingRow("Импорт на короткий промежуток времени", width, colors, m_discordImportTimedMix))
			m_appSettings->SetDiscordImportTimedEnabled(!m_appSettings->GetDiscordImportTimedEnabled());

		if (m_appSettings->GetDiscordImportTimedEnabled())
		{
			ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
			UiCommon::CaptionText("Длительность (минуты):", colors, width);
			UiCommon::PushInputStyle(colors);
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::InputText(
					"##discord_import_minutes",
					m_discordImportMinutes,
					sizeof m_discordImportMinutes,
					ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue))
			{
				const int minutes = std::atoi(m_discordImportMinutes);
				if (minutes > 0)
					m_appSettings->SetDiscordImportDurationMinutes(minutes);
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const int minutes = std::atoi(m_discordImportMinutes);
				if (minutes > 0)
					m_appSettings->SetDiscordImportDurationMinutes(minutes);
				snprintf(
					m_discordImportMinutes,
					sizeof m_discordImportMinutes,
					"%d",
					m_appSettings->GetDiscordImportDurationMinutes());
			}
			UiCommon::PopInputStyle();

			if (remainSec >= 0
				&& (m_appSettings->GetDiscordImportAntiZapretEnabled() || m_appSettings->GetDiscordImportVpnEnabled()))
			{
				ImGui::Dummy({ 0.f, 4.f });
				char countdown[96] = {};
				snprintf(
					countdown,
					sizeof countdown,
					"До отключения импорта: %d:%02d",
					remainSec / 60,
					remainSec % 60);
				UiCommon::CaptionText(countdown, colors, width);
			}
			else
			{
				ImGui::Dummy({ 0.f, 4.f });
				UiCommon::CaptionText(
					"Таймер запустится, когда включите импорт Антизапрет или VPN.",
					colors,
					width);
			}
		}
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
