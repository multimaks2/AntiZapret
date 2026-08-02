#include "ui/ui_settings_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_store.h"
#include "imgui.h"

#include <algorithm>
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

	const char* MinutesWordRu(int minutes)
	{
		const int n = minutes < 0 ? -minutes : minutes;
		const int mod100 = n % 100;
		const int mod10 = n % 10;
		if (mod100 >= 11 && mod100 <= 14)
			return "минут";
		if (mod10 == 1)
			return "минуту";
		if (mod10 >= 2 && mod10 <= 4)
			return "минуты";
		return "минут";
	}

	bool DrawTimedImportRow(
		float width,
		const UiThemeColors& colors,
		float mix,
		char* minutesBuf,
		int minutesBufSize,
		AppSettings* appSettings)
	{
		const float rowH = 42.f;
		const float toggleW = 54.f;
		const float editW = 56.f;
		const float gap = 6.f;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImU32 lineColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.65f));
		drawList->AddLine({ pos.x, pos.y }, { pos.x + width, pos.y }, lineColor, 1.f);
		drawList->AddLine(
			{ pos.x, pos.y + rowH - 1.f },
			{ pos.x + width, pos.y + rowH - 1.f },
			lineColor,
			1.f);

		const float textY = pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
		float x = pos.x + 2.f;

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::SetCursorScreenPos({ x, textY });
		ImGui::TextUnformatted("Импорт на");
		ImGui::PopStyleColor();
		x += ImGui::CalcTextSize("Импорт на").x + gap;

		UiCommon::PushInputStyle(colors);
		const float inputH = ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos({ x, pos.y + (rowH - inputH) * 0.5f });
		ImGui::SetNextItemWidth(editW);
		const bool edited = ImGui::InputText(
			"##discord_import_minutes",
			minutesBuf,
			minutesBufSize,
			ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
		if (edited || ImGui::IsItemDeactivatedAfterEdit())
		{
			const int minutes = std::atoi(minutesBuf);
			if (appSettings && minutes > 0)
				appSettings->SetDiscordImportDurationMinutes(minutes);
			if (appSettings)
			{
				snprintf(
					minutesBuf,
					static_cast<size_t>(minutesBufSize),
					"%d",
					appSettings->GetDiscordImportDurationMinutes());
			}
		}
		UiCommon::PopInputStyle();
		x += editW + gap;

		const int minutesValue = appSettings ? appSettings->GetDiscordImportDurationMinutes() : std::atoi(minutesBuf);
		char suffix[96] = {};
		snprintf(suffix, sizeof suffix, "%s моих конфигураций", MinutesWordRu(minutesValue));

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::SetCursorScreenPos({ x, textY });
		ImGui::TextUnformatted(suffix);
		ImGui::PopStyleColor();

		ImGui::SetCursorScreenPos({ pos.x + width - toggleW, pos.y + (rowH - 22.f) * 0.5f });
		const bool toggled = UiCommon::ToggleSwitch("##discord_import_timed", mix, colors);

		ImGui::SetCursorScreenPos({ pos.x, pos.y + rowH });
		ImGui::Dummy({ width, 2.f });
		return toggled;
	}
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
		m_discordTotalUptimeMix = m_appSettings->GetDiscordShowTotalUptime() ? 1.f : 0.f;
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
	m_discordTotalUptimeMix = UiCommon::AnimateMix(
		m_discordTotalUptimeMix,
		m_appSettings && m_appSettings->GetDiscordShowTotalUptime(),
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

			UiCommon::PushSliderStyle(colors);
			ImGui::SetNextItemWidth(innerWidth);
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

	if (UiCommon::BeginCard("##settings_autostart", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Автозапуск", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Что поднимать автоматически при старте AntiZapret.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (UiCommon::SettingRow("Автозапуск приложения", innerWidth, colors, m_autostartAppMix, true))
		{
			if (m_appSettings)
				m_appSettings->SetAutostartApp(!m_appSettings->GetAutostartApp());
		}
		if (UiCommon::SettingRow("Автозапуск обхода", innerWidth, colors, m_autostartBypassMix, true))
		{
			if (m_appSettings)
				m_appSettings->SetAutostartBypass(!m_appSettings->GetAutostartBypass());
		}
		if (UiCommon::SettingRow("Автозапуск прокси Telegram", innerWidth, colors, m_autostartTelegramMix, true))
		{
			if (m_appSettings)
				m_appSettings->SetAutostartTelegram(!m_appSettings->GetAutostartTelegram());
		}
		if (UiCommon::SettingRow("Автозапуск VPN", innerWidth, colors, m_autostartVpnMix, true))
		{
			if (m_appSettings)
				m_appSettings->SetAutostartVpn(!m_appSettings->GetAutostartVpn());
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
		auto resetHwidToSystem = [&]() {
			strncpy_s(m_customHwid, sizeof m_customHwid, systemHwid.c_str(), _TRUNCATE);
			if (m_appSettings)
				m_appSettings->SetCustomHwid({});
		};

		const float resetBtnW = 108.f;
		const float fieldW = (std::max)(120.f, innerWidth - resetBtnW - UiMetrics::kGridGap);

		UiCommon::PushInputStyle(colors);
		const float rowH = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth(fieldW);
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
		const float fieldY = ImGui::GetItemRectMin().y;
		const float fieldH = ImGui::GetItemRectSize().y;
		UiCommon::PopInputStyle();

		ImGui::SameLine(0.f, UiMetrics::kGridGap);
		ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x, fieldY });
		if (UiCommon::SecondaryButton(
				"Сбросить",
				{ resetBtnW, fieldH > 0.f ? fieldH : rowH },
				colors))
		{
			resetHwidToSystem();
		}
	}
	UiCommon::EndCard();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::BeginCard("##settings_content", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Контент", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Доступ к каталогу 18+ в маршрутизации сервисов.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (UiCommon::SettingRow("Мне есть 18 лет", innerWidth, colors, m_confirmAdultMix, true))
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
	}
	UiCommon::EndCard();
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	if (UiCommon::BeginCard("##settings_discord", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Discord активность", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Видимость в Discord, кнопки скачивания и временный импорт стратегии/VPN для друзей.",
			colors,
			innerWidth);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		if (UiCommon::SettingRow(
				"Показывать активность в Discord",
				innerWidth,
				colors,
				m_discordPresenceMix,
				true))
		{
			if (m_appSettings)
				m_appSettings->SetDiscordPresenceEnabled(!m_appSettings->GetDiscordPresenceEnabled());
		}

		if (m_appSettings && m_appSettings->GetDiscordPresenceEnabled())
		{
			if (UiCommon::SettingRow(
					"Суммарное время работы приложения",
					innerWidth,
					colors,
					m_discordTotalUptimeMix,
					true,
					true,
					false))
			{
				m_appSettings->SetDiscordShowTotalUptime(!m_appSettings->GetDiscordShowTotalUptime());
			}
			UiCommon::CaptionText(
				m_appSettings->GetDiscordShowTotalUptime()
					? "В Discord показывается всё накопленное время работы AntiZapret."
					: "В Discord показывается время с момента текущего запуска.",
				colors,
				innerWidth);
			ImGui::Dummy({ 0.f, 6.f });
			UiCommon::SettingBlockFooterSep(innerWidth, colors);

			const bool downloadOn = m_appSettings->GetDiscordDownloadButtonEnabled();
			if (UiCommon::SettingRow(
					"Кнопка «Скачать»",
					innerWidth,
					colors,
					m_discordDownloadButtonMix,
					true,
					true,
					!downloadOn))
			{
				m_appSettings->SetDiscordDownloadButtonEnabled(!downloadOn);
			}

			if (m_appSettings->GetDiscordDownloadButtonEnabled())
			{
				UiCommon::CaptionText("Ссылка кнопки «Скачать AntiZapret»:", colors, innerWidth);
				ImGui::Dummy({ 0.f, 4.f });
				UiCommon::PushInputStyle(colors);
				ImGui::SetNextItemWidth(innerWidth);
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
				ImGui::Dummy({ 0.f, 6.f });
				UiCommon::SettingBlockFooterSep(innerWidth, colors);
			}

			ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
			UiCommon::SectionHeader("Импорт для друзей", colors);
			ImGui::Dummy({ 0.f, 4.f });
			UiCommon::CaptionText(
				"Как активировать: 1) включите «Импорт на N минут моих конфигураций»; "
				"2) отметьте Антизапрет и/или VPN; "
				"3) откройте соответствующую вкладку — только тогда стартует таймер "
				"для этой вкладки и в Discord появится кнопка импорта. "
				"Пока вы не зайдёте на вкладку, отсчёт не идёт.",
				colors,
				innerWidth);
			ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

			if (DrawTimedImportRow(
					innerWidth,
					colors,
					m_discordImportTimedMix,
					m_discordImportMinutes,
					sizeof m_discordImportMinutes,
					m_appSettings))
			{
				m_appSettings->SetDiscordImportTimedEnabled(
					!m_appSettings->GetDiscordImportTimedEnabled());
			}

			if (m_appSettings->GetDiscordImportTimedEnabled())
			{
				const int remainAz = m_appSettings->GetDiscordImportRemainingSecondsAz();
				const int remainVpn = m_appSettings->GetDiscordImportRemainingSecondsVpn();
				char azLabel[96] = "Импорт Антизапрет";
				char vpnLabel[96] = "Импорт VPN";
				if (remainAz >= 0)
				{
					snprintf(
						azLabel,
						sizeof azLabel,
						"Импорт Антизапрет  ·  %d:%02d",
						remainAz / 60,
						remainAz % 60);
				}
				if (remainVpn >= 0)
				{
					snprintf(
						vpnLabel,
						sizeof vpnLabel,
						"Импорт VPN  ·  %d:%02d",
						remainVpn / 60,
						remainVpn % 60);
				}

				if (UiCommon::SettingRow(azLabel, innerWidth, colors, m_discordImportAzMix, true))
				{
					m_appSettings->SetDiscordImportAntiZapretEnabled(
						!m_appSettings->GetDiscordImportAntiZapretEnabled());
				}
				if (UiCommon::SettingRow(vpnLabel, innerWidth, colors, m_discordImportVpnMix, true))
				{
					m_appSettings->SetDiscordImportVpnEnabled(
						!m_appSettings->GetDiscordImportVpnEnabled());
				}

				ImGui::Dummy({ 0.f, 4.f });
				if (remainAz < 0 && remainVpn < 0)
				{
					UiCommon::CaptionText(
						"Таймер ещё не запущен: откройте вкладку Антизапрет или VPN с включённым импортом.",
						colors,
						innerWidth);
				}
				else if (remainAz >= 0 || remainVpn >= 0)
				{
					char countdown[160] = {};
					if (remainAz >= 0 && remainVpn >= 0)
					{
						snprintf(
							countdown,
							sizeof countdown,
							"Активно: Антизапрет %d:%02d · VPN %d:%02d",
							remainAz / 60,
							remainAz % 60,
							remainVpn / 60,
							remainVpn % 60);
					}
					else if (remainAz >= 0)
					{
						snprintf(
							countdown,
							sizeof countdown,
							"Активен импорт Антизапрет: %d:%02d",
							remainAz / 60,
							remainAz % 60);
					}
					else
					{
						snprintf(
							countdown,
							sizeof countdown,
							"Активен импорт VPN: %d:%02d",
							remainVpn / 60,
							remainVpn % 60);
					}
					UiCommon::CaptionText(countdown, colors, innerWidth);
				}
			}
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
