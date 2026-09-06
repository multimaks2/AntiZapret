#include "ui/ui_dns_page.h"

#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <thread>

UiDnsPage::UiDnsPage() = default;

UiDnsPage::~UiDnsPage()
{
	m_probeCancel.store(true);
	while (m_probing.load() || m_applying.load())
		Sleep(10);
}

void UiDnsPage::EnsureInitialized()
{
	if (m_initialized)
		return;
	m_initialized = true;

	size_t count = 0;
	const DnsManager::PublicServer* servers = DnsManager::GetPublicServers(count);
	m_servers.clear();
	m_servers.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		ServerRow row;
		row.name = servers[i].name ? servers[i].name : "";
		row.ipv4 = servers[i].ipv4 ? servers[i].ipv4 : "";
		row.provider = servers[i].provider ? servers[i].provider : "";
		row.tags = servers[i].tags ? servers[i].tags : "";
		m_servers.push_back(std::move(row));
	}

	RefreshAdapters();
}

void UiDnsPage::RefreshAdapters()
{
	const std::string previousName =
		(m_adapterIndex >= 0 && m_adapterIndex < static_cast<int>(m_adapters.size()))
			? m_adapters[static_cast<size_t>(m_adapterIndex)].name
			: std::string();

	DnsManager::RefreshAdapters(m_adapters);

	m_adapterIndex = -1;
	if (!previousName.empty())
	{
		for (int i = 0; i < static_cast<int>(m_adapters.size()); ++i)
		{
			if (m_adapters[static_cast<size_t>(i)].name == previousName)
			{
				m_adapterIndex = i;
				break;
			}
		}
	}
	if (m_adapterIndex < 0)
		m_adapterIndex = DnsManager::FindPreferredAdapterIndex(m_adapters);
}

bool UiDnsPage::AdapterValid() const
{
	return m_adapterIndex >= 0 && m_adapterIndex < static_cast<int>(m_adapters.size());
}

const DnsManager::AdapterInfo* UiDnsPage::SelectedAdapter() const
{
	if (!AdapterValid())
		return nullptr;
	return &m_adapters[static_cast<size_t>(m_adapterIndex)];
}

void UiDnsPage::PumpProbeResults()
{
	const bool probingNow = m_probing.load();

	if (probingNow || m_wasProbing)
	{
		std::lock_guard<std::mutex> lock(m_probeMutex);
		const size_t n = (std::min)(m_probeLatencies.size(), m_servers.size());
		for (size_t i = 0; i < n; ++i)
		{
			const int latency = m_probeLatencies[i];
			if (latency == -2)
				continue;
			m_servers[i].latencyMs = latency;
			m_servers[i].probed = true;
			m_probeLatencies[i] = -2;
		}
	}

	if (m_wasProbing && !probingNow && !m_probeCancel.load())
		SortServersByLatency();

	m_wasProbing = probingNow;
}

void UiDnsPage::SortServersByLatency()
{
	std::string primaryIp;
	std::string alternateIp;
	if (m_primaryIndex >= 0 && m_primaryIndex < static_cast<int>(m_servers.size()))
		primaryIp = m_servers[static_cast<size_t>(m_primaryIndex)].ipv4;
	if (m_alternateIndex >= 0 && m_alternateIndex < static_cast<int>(m_servers.size()))
		alternateIp = m_servers[static_cast<size_t>(m_alternateIndex)].ipv4;

	std::stable_sort(m_servers.begin(), m_servers.end(), [](const ServerRow& a, const ServerRow& b) {
		const bool aOk = a.probed && a.latencyMs >= 0;
		const bool bOk = b.probed && b.latencyMs >= 0;
		if (aOk != bOk)
			return aOk && !bOk;
		if (aOk && bOk && a.latencyMs != b.latencyMs)
			return a.latencyMs < b.latencyMs;
		return false;
	});

	m_primaryIndex = -1;
	m_alternateIndex = -1;
	for (int i = 0; i < static_cast<int>(m_servers.size()); ++i)
	{
		const std::string& ip = m_servers[static_cast<size_t>(i)].ipv4;
		if (m_primaryIndex < 0 && !primaryIp.empty() && ip == primaryIp)
			m_primaryIndex = i;
		else if (m_alternateIndex < 0 && !alternateIp.empty() && ip == alternateIp)
			m_alternateIndex = i;
	}
}

void UiDnsPage::StartProbe()
{
	if (m_probing.load() || m_applying.load() || m_servers.empty())
		return;

	m_probeCancel.store(false);
	m_probing.store(true);
	m_probeDone.store(0);
	m_probeTotal.store(static_cast<int>(m_servers.size()));
	m_status = "Проверка DNS-серверов…";
	m_statusOk = true;

	{
		std::lock_guard<std::mutex> lock(m_probeMutex);
		m_probeLatencies.assign(m_servers.size(), -2);
	}

	for (ServerRow& row : m_servers)
	{
		row.probed = false;
		row.latencyMs = -1;
	}

	const size_t count = m_servers.size();
	std::vector<std::string> ips;
	ips.reserve(count);
	for (const ServerRow& row : m_servers)
		ips.push_back(row.ipv4);

	std::thread([this, ips]() {
		std::atomic<size_t> next { 0 };
		constexpr int kWorkers = 16;
		std::vector<std::thread> workers;
		workers.reserve(kWorkers);

		for (int w = 0; w < kWorkers; ++w)
		{
			workers.emplace_back([this, &ips, &next]() {
				for (;;)
				{
					if (m_probeCancel.load())
						return;
					const size_t index = next.fetch_add(1);
					if (index >= ips.size())
						return;

					const int ms = DnsManager::MeasureServerLatencyMs(ips[index].c_str(), 1500);
					{
						std::lock_guard<std::mutex> lock(m_probeMutex);
						if (index < m_probeLatencies.size())
							m_probeLatencies[index] = ms;
					}
					m_probeDone.fetch_add(1);
				}
			});
		}

		for (std::thread& t : workers)
		{
			if (t.joinable())
				t.join();
		}

		m_probing.store(false);
		if (!m_probeCancel.load())
		{
			m_status = "Проверка завершена";
			m_statusOk = true;
		}
	}).detach();
}

void UiDnsPage::ApplySelection()
{
	if (!AdapterValid() || m_applying.load() || m_probing.load())
		return;
	if (m_primaryIndex < 0 || m_primaryIndex >= static_cast<int>(m_servers.size()))
	{
		m_status = "Сначала выберите основной DNS (ПКМ по серверу)";
		m_statusOk = false;
		return;
	}

	const DnsManager::AdapterInfo adapter = m_adapters[static_cast<size_t>(m_adapterIndex)];
	const std::string primary = m_servers[static_cast<size_t>(m_primaryIndex)].ipv4;
	std::string alternate;
	if (m_alternateIndex >= 0 && m_alternateIndex < static_cast<int>(m_servers.size())
		&& m_alternateIndex != m_primaryIndex)
	{
		alternate = m_servers[static_cast<size_t>(m_alternateIndex)].ipv4;
	}

	m_applying.store(true);
	m_status = "Применение DNS…";
	m_statusOk = true;

	std::thread([this, adapter, primary, alternate]() {
		std::string error;
		const bool ok = DnsManager::ApplyStaticDns(
			adapter.nameWide,
			primary.c_str(),
			alternate.empty() ? nullptr : alternate.c_str(),
			error);

		if (ok)
		{
			char buf[192] = {};
			if (!alternate.empty())
			{
				snprintf(
					buf,
					sizeof buf,
					"Применено к «%s»: %s / %s",
					adapter.name.c_str(),
					primary.c_str(),
					alternate.c_str());
			}
			else
			{
				snprintf(
					buf,
					sizeof buf,
					"Применено к «%s»: %s",
					adapter.name.c_str(),
					primary.c_str());
			}
			m_status = buf;
			m_statusOk = true;
		}
		else
		{
			m_status = error.empty() ? "Не удалось применить DNS" : error;
			m_statusOk = false;
		}

		m_needRefreshAdapters.store(true);
		m_applying.store(false);
	}).detach();
}

void UiDnsPage::ApplyFastest()
{
	if (m_applying.load() || m_probing.load())
		return;

	std::vector<int> ranked;
	ranked.reserve(m_servers.size());
	for (int i = 0; i < static_cast<int>(m_servers.size()); ++i)
	{
		if (m_servers[static_cast<size_t>(i)].probed && m_servers[static_cast<size_t>(i)].latencyMs >= 0)
			ranked.push_back(i);
	}

	if (ranked.empty())
	{
		m_status = "Сначала нажмите «Проверить DNS сервера»";
		m_statusOk = false;
		return;
	}

	std::sort(ranked.begin(), ranked.end(), [this](int a, int b) {
		return m_servers[static_cast<size_t>(a)].latencyMs < m_servers[static_cast<size_t>(b)].latencyMs;
	});

	m_primaryIndex = ranked[0];
	m_alternateIndex = -1;
	const std::string& primaryIp = m_servers[static_cast<size_t>(m_primaryIndex)].ipv4;
	for (size_t i = 1; i < ranked.size(); ++i)
	{
		const int idx = ranked[i];
		if (m_servers[static_cast<size_t>(idx)].ipv4 != primaryIp)
		{
			m_alternateIndex = idx;
			break;
		}
	}
	ApplySelection();
}

void UiDnsPage::SetPrimary(int index, bool applyNow)
{
	if (index < 0 || index >= static_cast<int>(m_servers.size()))
		return;
	m_primaryIndex = index;
	if (m_alternateIndex == m_primaryIndex)
		m_alternateIndex = -1;
	else if (m_alternateIndex >= 0
		&& m_servers[static_cast<size_t>(m_alternateIndex)].ipv4
			== m_servers[static_cast<size_t>(m_primaryIndex)].ipv4)
	{
		m_alternateIndex = -1;
	}
	if (applyNow)
		ApplySelection();
}

void UiDnsPage::SetAlternate(int index, bool applyNow)
{
	if (index < 0 || index >= static_cast<int>(m_servers.size()))
		return;
	if (m_primaryIndex >= 0
		&& m_servers[static_cast<size_t>(index)].ipv4
			== m_servers[static_cast<size_t>(m_primaryIndex)].ipv4)
	{
		m_status = "Альтернативный DNS не может совпадать с основным (один IP)";
		m_statusOk = false;
		return;
	}
	if (index == m_primaryIndex)
	{
		m_status = "Альтернативный DNS не может совпадать с основным";
		m_statusOk = false;
		return;
	}
	m_alternateIndex = index;
	if (applyNow)
	{
		if (m_primaryIndex < 0)
		{
			m_status = "Сначала выберите основной DNS (слот 1)";
			m_statusOk = false;
			return;
		}
		ApplySelection();
	}
}

void UiDnsPage::ToggleSelect(int index)
{
	if (index < 0 || index >= static_cast<int>(m_servers.size()))
		return;

	// Снять слот 1 → 2 становится 1.
	if (index == m_primaryIndex)
	{
		m_primaryIndex = m_alternateIndex;
		m_alternateIndex = -1;
		return;
	}
	// Снять слот 2.
	if (index == m_alternateIndex)
	{
		m_alternateIndex = -1;
		return;
	}

	if (m_primaryIndex < 0)
	{
		m_primaryIndex = index;
		return;
	}

	const std::string& clickedIp = m_servers[static_cast<size_t>(index)].ipv4;
	const std::string& primaryIp = m_servers[static_cast<size_t>(m_primaryIndex)].ipv4;
	if (clickedIp == primaryIp)
	{
		m_status = "Этот IP уже выбран как основной (1)";
		m_statusOk = false;
		return;
	}

	if (m_alternateIndex < 0)
	{
		m_alternateIndex = index;
		return;
	}

	if (clickedIp == m_servers[static_cast<size_t>(m_alternateIndex)].ipv4)
	{
		m_status = "Этот IP уже выбран как альтернативный (2)";
		m_statusOk = false;
		return;
	}
	m_primaryIndex = index;
}

void UiDnsPage::RestoreDhcp()
{
	if (!AdapterValid() || m_applying.load() || m_probing.load())
		return;

	const DnsManager::AdapterInfo adapter = m_adapters[static_cast<size_t>(m_adapterIndex)];
	m_applying.store(true);
	m_status = "Возврат DNS к DHCP…";
	m_statusOk = true;

	std::thread([this, adapter]() {
		std::string error;
		const bool ok = DnsManager::ApplyDhcpDns(adapter.nameWide, error);
		if (ok)
		{
			char buf[160] = {};
			snprintf(buf, sizeof buf, "DNS адаптера «%s» возвращён к DHCP", adapter.name.c_str());
			m_status = buf;
			m_statusOk = true;
			m_primaryIndex = -1;
			m_alternateIndex = -1;
		}
		else
		{
			m_status = error.empty() ? "Не удалось вернуть DHCP" : error;
			m_statusOk = false;
		}
		m_needRefreshAdapters.store(true);
		m_applying.store(false);
	}).detach();
}

bool UiDnsPage::MatchesSearch(const ServerRow& row) const
{
	if (!m_search[0])
		return true;

	auto toLower = [](std::string s) {
		for (char& ch : s)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return s;
	};
	auto stripSpaces = [](std::string s) {
		s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) {
			return ch == ' ' || ch == '\t';
		}), s.end());
		return s;
	};
	auto stripDots = [](std::string s) {
		s.erase(std::remove(s.begin(), s.end(), '.'), s.end());
		return s;
	};

	const std::string needle = toLower(stripSpaces(m_search));
	if (needle.empty())
		return true;

	auto contains = [&](const std::string& hay) -> bool {
		if (hay.empty())
			return false;
		const std::string h = toLower(stripSpaces(hay));
		if (h.find(needle) != std::string::npos)
			return true;
		// IP: allow "1111" / "1.1.1" style search without exact dots.
		if (stripDots(h).find(stripDots(needle)) != std::string::npos)
			return true;
		return false;
	};

	return contains(row.name)
		|| contains(row.ipv4)
		|| contains(row.provider)
		|| contains(row.tags);
}

void UiDnsPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	EnsureInitialized();
	if (m_needRefreshAdapters.exchange(false))
		RefreshAdapters();
	PumpProbeResults();

	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();
	const bool busy = m_probing.load() || m_applying.load();

	auto iconUtf8 = []() -> std::string {
		wchar_t wide[] = { static_cast<wchar_t>(kDnsIcon), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof utf8), nullptr, nullptr);
		if (len <= 0)
			return {};
		return std::string(utf8, static_cast<size_t>(len));
	};
	const std::string dnsGlyph = iconUtf8();
	ImFont* solidFont = fonts.GetSolidFont();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		kDnsIcon,
		"DNS",
		"Проверка и смена DNS-серверов для сетевого адаптера",
		colors,
		UiCommon::TitleIconFont::Solid);

	// Узкий блок: текущий DNS в системе (выбранный / основной адаптер).
	{
		const DnsManager::AdapterInfo* current = SelectedAdapter();
		char dnsText[160] = {};
		char adapterText[128] = {};
		if (current)
		{
			snprintf(adapterText, sizeof adapterText, "%s", current->name.c_str());
			if (!current->primaryDns.empty() && !current->alternateDns.empty())
			{
				snprintf(
					dnsText,
					sizeof dnsText,
					"%s  /  %s",
					current->primaryDns.c_str(),
					current->alternateDns.c_str());
			}
			else if (!current->primaryDns.empty())
			{
				snprintf(dnsText, sizeof dnsText, "%s", current->primaryDns.c_str());
			}
			else
			{
				snprintf(dnsText, sizeof dnsText, "DHCP / не задан");
			}
		}
		else
		{
			snprintf(dnsText, sizeof dnsText, "—");
			snprintf(adapterText, sizeof adapterText, "адаптер не выбран");
		}

		const float barH = 36.f;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.f, 0.f });
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
		ImGui::PushStyleColor(ImGuiCol_Border, colors.tileBorder);
		ImGui::BeginChild("##dns_current_bar", { width, barH }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

		ImDrawList* barDl = ImGui::GetWindowDrawList();
		const ImVec2 barMin = ImGui::GetWindowPos();
		const float contentY = barMin.y + (barH - ImGui::GetFontSize()) * 0.5f;
		float x = barMin.x + 12.f;

		if (solidFont && !dnsGlyph.empty())
		{
			barDl->AddText(
				solidFont,
				14.f,
				{ x, contentY + 1.f },
				ImGui::GetColorU32(accents.ok),
				dnsGlyph.c_str());
			x += 22.f;
		}

		barDl->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize(),
			{ x, contentY },
			ImGui::GetColorU32(colors.textMuted),
			"Текущий DNS:");
		x += ImGui::CalcTextSize("Текущий DNS:").x + 8.f;

		barDl->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize(),
			{ x, contentY },
			ImGui::GetColorU32(colors.textPrimary),
			dnsText);

		const ImVec2 adapterSize = ImGui::CalcTextSize(adapterText);
		barDl->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize() * 0.92f,
			{ barMin.x + width - 12.f - adapterSize.x, contentY },
			ImGui::GetColorU32(colors.textMuted),
			adapterText);

		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}
	UiCommon::CardGap();

	// Компактный блок адаптера.
	{
		const float rowH = 34.f;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.f, 6.f });
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
		ImGui::PushStyleColor(ImGuiCol_Border, colors.tileBorder);
		ImGui::BeginChild("##dns_adapter", { width, rowH + 12.f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

		const float innerWidth = ImGui::GetContentRegionAvail().x;
		const float comboW = innerWidth - 108.f;
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(comboW > 100.f ? comboW : innerWidth * 0.65f);
		const char* preview = AdapterValid()
			? m_adapters[static_cast<size_t>(m_adapterIndex)].name.c_str()
			: "(нет адаптеров)";
		if (ImGui::BeginCombo("##dns_adapter_combo", preview))
		{
			for (int i = 0; i < static_cast<int>(m_adapters.size()); ++i)
			{
				const DnsManager::AdapterInfo& adapter = m_adapters[static_cast<size_t>(i)];
				char label[256] = {};
				if (adapter.isTunnel)
					snprintf(label, sizeof label, "%s (туннель)%s", adapter.name.c_str(), adapter.isUp ? "" : " — выкл");
				else
					snprintf(label, sizeof label, "%s%s", adapter.name.c_str(), adapter.isUp ? "" : " — выкл");

				const bool selected = i == m_adapterIndex;
				if (ImGui::Selectable(label, selected) && !busy)
					m_adapterIndex = i;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		UiCommon::PopInputStyle();

		ImGui::SameLine(0.f, 8.f);
		if (UiCommon::SecondaryButton("Обновить", { 100.f, rowH }, colors, !busy))
			RefreshAdapters();

		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}
	UiCommon::CardGap();

	if (UiCommon::BeginCard("##dns_actions", width, colors))
	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		const float gap = UiMetrics::kGridGap;
		const float btnW = (innerWidth - gap * 2.f) / 3.f;
		const float btnH = UiMetrics::kSmallBtnHeight + 4.f;

		const bool canProbe = !busy && !m_servers.empty();
		const bool canApplyFastest = !busy;
		const bool hasSelection = m_primaryIndex >= 0;
		const bool canApply = !busy && hasSelection;

		if (UiCommon::AccentButton(
				"Проверить DNS сервера",
				{ btnW, btnH },
				accents.ok,
				colors,
				canProbe))
		{
			StartProbe();
		}
		ImGui::SameLine(0.f, gap);
		if (UiCommon::AccentButton(
				"Применить самые быстрые",
				{ btnW, btnH },
				accents.download,
				colors,
				canApplyFastest))
		{
			ApplyFastest();
		}
		ImGui::SameLine(0.f, gap);
		if (hasSelection)
		{
			if (UiCommon::AccentButton(
					"Применить выбранные",
					{ btnW, btnH },
					accents.ok,
					colors,
					canApply))
			{
				ApplySelection();
			}
		}
		else if (UiCommon::SecondaryButton(
					 "Применить выбранные",
					 { btnW, btnH },
					 colors,
					 false))
		{
		}

		ImGui::Dummy({ 0.f, 4.f });
		if (UiCommon::SecondaryButton(
				"Вернуть DNS к DHCP",
				{ innerWidth, UiMetrics::kSmallBtnHeight },
				colors,
				!busy && AdapterValid()))
		{
			RestoreDhcp();
		}

		if (m_probing.load())
		{
			ImGui::Dummy({ 0.f, 4.f });
			const int done = m_probeDone.load();
			const int total = (std::max)(1, m_probeTotal.load());
			const float frac = static_cast<float>(done) / static_cast<float>(total);
			char progress[64] = {};
			snprintf(progress, sizeof progress, "Проверено %d / %d", done, total);
			ImGui::ProgressBar(frac, { innerWidth, 16.f }, progress);
		}

		if (!m_status.empty())
		{
			ImGui::Dummy({ 0.f, 2.f });
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				m_statusOk ? UiCommon::FixedStartAccent() : UiCommon::FixedStopAccent());
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth);
			ImGui::TextUnformatted(m_status.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	// Карточка списка заполняет оставшуюся высоту. NoScrollWithMouse — иначе крутится родитель.
	const ImVec2 pageWinPos = ImGui::GetWindowPos();
	const float pageBottom = pageWinPos.y + ImGui::GetWindowSize().y;
	const float listCardH = (std::max)(180.f, pageBottom - ImGui::GetCursorScreenPos().y - 2.f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { UiMetrics::kCardPad, 10.f });
	ImGui::BeginChild(
		"##dns_list",
		{ width, listCardH },
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	{
		const float innerWidth = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Публичные DNS-серверы", colors);
		ImGui::Dummy({ 0.f, 4.f });

		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(innerWidth);
		ImGui::InputTextWithHint(
			"##dns_search",
			"Поиск по имени, IP, провайдеру, тегу…",
			m_search,
			sizeof m_search);
		UiCommon::PopInputStyle();
		ImGui::Dummy({ 0.f, 8.f });

		int visibleCount = 0;
		for (const ServerRow& row : m_servers)
		{
			if (MatchesSearch(row))
				++visibleCount;
		}
		char countLabel[64] = {};
		snprintf(countLabel, sizeof countLabel, "Показано: %d / %d", visibleCount, static_cast<int>(m_servers.size()));
		UiCommon::CaptionText(countLabel, colors, innerWidth);
		ImGui::Dummy({ 0.f, 4.f });

		const float rowH = 40.f;
		const float rowGap = 6.f;
		const float listH = (std::max)(120.f, ImGui::GetContentRegionAvail().y);
		const float pingColW = 88.f;
		const float ipColW = 118.f;
		const float dt = ImGui::GetIO().DeltaTime;

		auto drawSlotBadge = [&](ImDrawList* dl, ImVec2 cellMin, const char* label, const ImVec4& accent) {
			ImFont* font = ImGui::GetFont();
			const float fontSize = ImGui::GetFontSize() * 0.78f;
			const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, label);
			const float padX = 4.f;
			const float padY = 1.f;
			const ImVec2 min = { cellMin.x + 4.f, cellMin.y + 3.f };
			const ImVec2 max = { min.x + textSize.x + padX * 2.f, min.y + textSize.y + padY * 2.f };
			dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(UiCommon::WithAlpha(accent, 0.14f)), 3.f);
			dl->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(accent), 3.f, 0, 1.25f);
			dl->AddText(font, fontSize, { min.x + padX, min.y + padY }, ImGui::ColorConvertFloat4ToU32(accent), label);
		};

		m_listScroll.Draw(
			"##dns_server_list_scroll",
			{ innerWidth, listH },
			dt,
			[&](float contentWidth) {
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				if (visibleCount == 0)
				{
					ImGui::Dummy({ 0.f, 12.f });
					UiCommon::CaptionText("Ничего не найдено", colors, contentWidth);
					return;
				}

				for (int i = 0; i < static_cast<int>(m_servers.size()); ++i)
				{
					const ServerRow& row = m_servers[static_cast<size_t>(i)];
					if (!MatchesSearch(row))
						continue;

					ImGui::PushID(i);
					const ImVec2 rowMin = ImGui::GetCursorScreenPos();
					ImGui::InvisibleButton("##dns_row", { contentWidth, rowH });
					const bool hovered = ImGui::IsItemHovered();
					const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
					const bool isPrimary = i == m_primaryIndex;
					const bool isAlternate = i == m_alternateIndex;
					const ImVec2 rowMax = ImGui::GetItemRectMax();

					if (clicked && !busy)
						ToggleSelect(i);

					ImVec4 fill = UiCommon::WithAlpha(colors.tileBg, 0.55f);
					if (isPrimary)
						fill = UiCommon::WithAlpha(accents.ok, 0.16f);
					else if (isAlternate)
						fill = UiCommon::WithAlpha(accents.download, 0.16f);
					else if (hovered)
						fill = colors.navHover;

					drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(fill), 6.f);
					ImVec4 border = colors.tileBorder;
					float borderThickness = 1.f;
					if (isPrimary)
					{
						border = accents.ok;
						borderThickness = 1.35f;
					}
					else if (isAlternate)
					{
						border = accents.download;
						borderThickness = 1.35f;
					}
					drawList->AddRect(rowMin, rowMax, ImGui::GetColorU32(border), 6.f, 0, borderThickness);

					if (isPrimary)
						drawSlotBadge(drawList, rowMin, "1", accents.ok);
					else if (isAlternate)
						drawSlotBadge(drawList, rowMin, "2", accents.download);

					ImVec4 nameColor = colors.textPrimary;
					if (isPrimary)
						nameColor = accents.ok;
					else if (isAlternate)
						nameColor = accents.download;

					const float iconSize = 14.f;
					const float iconX = rowMin.x + 22.f;
					const float iconY = rowMin.y + (rowH - iconSize) * 0.5f;
					if (solidFont && !dnsGlyph.empty())
					{
						drawList->AddText(
							solidFont,
							iconSize,
							{ iconX, iconY },
							ImGui::GetColorU32(nameColor),
							dnsGlyph.c_str());
					}

					char title[192] = {};
					snprintf(title, sizeof title, "%s", row.name.c_str());
					char role[48] = {};
					if (isPrimary)
						snprintf(role, sizeof role, "основной");
					else if (isAlternate)
						snprintf(role, sizeof role, "альтернативный");
					else if (!row.provider.empty())
						snprintf(role, sizeof role, "%s", row.provider.c_str());

					const float textX = rowMin.x + 44.f;
					const float textMaxX = rowMax.x - pingColW - ipColW - 8.f;
					drawList->AddText(
						ImGui::GetFont(),
						ImGui::GetFontSize(),
						{ textX, rowMin.y + (role[0] ? 5.f : (rowH - ImGui::GetFontSize()) * 0.5f) },
						ImGui::GetColorU32(nameColor),
						title,
						nullptr,
						textMaxX - textX);
					if (role[0])
					{
						drawList->AddText(
							ImGui::GetFont(),
							ImGui::GetFontSize() * 0.85f,
							{ textX, rowMin.y + 21.f },
							ImGui::GetColorU32(colors.textMuted),
							role,
							nullptr,
							textMaxX - textX);
					}

					const ImVec2 ipSize = ImGui::CalcTextSize(row.ipv4.c_str());
					const float ipX = rowMax.x - pingColW - 8.f - ipSize.x;
					drawList->AddText(
						ImGui::GetFont(),
						ImGui::GetFontSize(),
						{ ipX, rowMin.y + (rowH - ipSize.y) * 0.5f },
						ImGui::GetColorU32(colors.textPrimary),
						row.ipv4.c_str());

					char latency[32] = {};
					ImVec4 latencyColor = colors.textMuted;
					if (!row.probed)
					{
						snprintf(latency, sizeof latency, "—");
					}
					else if (row.latencyMs < 0)
					{
						snprintf(latency, sizeof latency, "тайм-аут");
						latencyColor = accents.fail;
					}
					else
					{
						snprintf(latency, sizeof latency, "%d мс", row.latencyMs);
						if (row.latencyMs <= 40)
							latencyColor = accents.ok;
						else if (row.latencyMs <= 100)
							latencyColor = accents.warn;
						else
							latencyColor = accents.fail;
					}

					const ImVec2 pingSize = ImGui::CalcTextSize(latency);
					drawList->AddText(
						ImGui::GetFont(),
						ImGui::GetFontSize(),
						{ rowMax.x - 12.f - pingSize.x, rowMin.y + (rowH - pingSize.y) * 0.5f },
						ImGui::GetColorU32(latencyColor),
						latency);

					if (ImGui::BeginPopupContextItem("##dns_row_menu"))
					{
						if (ImGui::MenuItem("Выбрать как 1 (основной)", nullptr, false, !busy))
							SetPrimary(i, false);
						if (ImGui::MenuItem("Выбрать как 2 (альтернативный)", nullptr, false, !busy))
							SetAlternate(i, false);
						ImGui::Separator();
						if (ImGui::MenuItem("Применить этот как основной сейчас", nullptr, false, !busy))
							SetPrimary(i, true);
						if (ImGui::MenuItem("Применить этот как альтернативный сейчас", nullptr, false, !busy))
							SetAlternate(i, true);
						ImGui::EndPopup();
					}

					ImGui::PopID();
					ImGui::Dummy({ 0.f, rowGap });
				}
			},
			2.f,
			nullptr,
			true,
			true);
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	ImGui::PopStyleVar();
}
