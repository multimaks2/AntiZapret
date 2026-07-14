#include "ui/ui_about_page.h"

#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "version.h"
#include "imgui.h"

#include <cstdio>

namespace
{
	struct CreditEntry
	{
		const char* name;
		const char* note;
		const char* url;
	};

	void DrawCredit(const CreditEntry& entry, const UiThemeColors& colors)
	{
		ImGui::TextLinkOpenURL(entry.name, entry.url);
		if (entry.note && entry.note[0])
		{
			ImGui::SameLine(0.f, 8.f);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted("—");
			ImGui::SameLine(0.f, 8.f);
			const float wrapWidth = ImGui::GetContentRegionAvail().x;
			if (wrapWidth > 0.f)
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
			ImGui::TextUnformatted(entry.note);
			if (wrapWidth > 0.f)
				ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		ImGui::Dummy({ 0.f, 2.f });
	}

	void DrawCreditsSection(
		const char* title,
		const CreditEntry* entries,
		int count,
		const UiThemeColors& colors)
	{
		UiCommon::SectionHeader(title, colors);
		ImGui::Dummy({ 0.f, 4.f });
		for (int i = 0; i < count; ++i)
			DrawCredit(entries[i], colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
	}
}

void UiAboutPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	(void)fonts;
	const UiThemeColors colors = theme.GetColors();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xE946,
		"О приложении",
		"Автор, версия и используемые проекты",
		colors);

	if (UiCommon::BeginCard("##about_author", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;

		char versionLine[64] = {};
		snprintf(versionLine, sizeof versionLine, "AntiZapret %s", ANTIZAPRET_VERSION);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(versionLine);
		ImGui::PopStyleColor();

		ImGui::Dummy({ 0.f, 6.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Автор:");
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextLinkOpenURL("LLIEPLLIEHb", "https://github.com/multimaks2");

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth);
		ImGui::TextUnformatted(
			"Приложение для обхода блокировок Discord, YouTube и Telegram, "
			"ускорения Telegram через MTProto-прокси и управления VPN.");
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##about_credits", width, colors))
	{
		const CreditEntry devLibraries[] = {
			{ "Dear ImGui", "Интерфейс (ocornut/imgui)", "https://github.com/ocornut/imgui" },
			{ "Lua", "Встроенный скриптовый движок", "https://www.lua.org/" },
			{ "Premake5", "Генерация проектов Visual Studio", "https://premake.github.io/" },
			{ "DirectX 11", "Рендеринг (Microsoft)", "https://learn.microsoft.com/windows/win32/direct3d11/direct3d-11-graphics" },
		};
		DrawCreditsSection("Библиотеки и инструменты", devLibraries, 4, colors);

		const CreditEntry bypass[] = {
			{ "Flowseal / zapret-discord-youtube", "Обход Discord/YouTube: стратегии, списки и runtime", "https://github.com/Flowseal/zapret-discord-youtube" },
			{ "bol-van / zapret-win-bundle", "winws.exe и WinDivert (поставляется в составе сборки выше)", "https://github.com/bol-van/zapret-win-bundle" },
			{ "WinDivert", "Перехват сетевого трафика", "https://reqrypt.org/windivert.html" },
			{ "Flowseal / tg-ws-proxy", "MTProto-прокси для Telegram", "https://github.com/Flowseal/tg-ws-proxy" },
		};
		DrawCreditsSection("Обход блокировок", bypass, 4, colors);

		const CreditEntry vpn[] = {
			{ "mihomo", "VPN-ядро (Clash Meta)", "https://github.com/MetaCubeX/mihomo" },
			{ "Wintun", "TUN-адаптер для Windows", "https://www.wintun.net/" },
			{ "v2rayN", "Идеи и подход к VPN-интерфейсу", "https://github.com/2dust/v2rayN" },
			{ "russia-v2ray-rules-dat", "Rule-set для маршрутизации RUv1", "https://github.com/runetfreedom/russia-v2ray-rules-dat" },
			{ "russia-v2ray-custom-routing-list", "Пресеты маршрутизации", "https://github.com/runetfreedom/russia-v2ray-custom-routing-list" },
			{ "meta-rules-dat", "База GeoIP (geoip.metadb)", "https://github.com/MetaCubeX/meta-rules-dat" },
		};
		DrawCreditsSection("VPN и маршрутизация", vpn, 6, colors);

		const CreditEntry data[] = {
			{ "flagcdn.com", "Иконки флагов стран в списке VPN", "https://flagcdn.com/" },
			{ "Segoe MDL2 Assets", "Иконки интерфейса (Microsoft)", "https://learn.microsoft.com/windows/apps/design/style/segoe-ui-symbol-font" },
		};
		DrawCreditsSection("Данные и ресурсы", data, 2, colors);

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(
			"Спасибо всем авторам перечисленных проектов. "
			"AntiZapret не аффилирован с ними; ссылки ведут на оригинальные репозитории.");
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}
	UiCommon::EndCard();

	ImGui::PopStyleVar();
}
