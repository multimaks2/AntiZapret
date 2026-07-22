# 🚀 AntiZapret

<p align="center">
  <img width="1023" height="760" alt="1" src="https://github.com/user-attachments/assets/8d54b9b4-74ce-4f04-be34-ff4662cec7c7" />

</p>

<p align="center">
  <strong>Один клиент</strong> для обхода блокировок, Telegram-прокси, VPN и маршрутизации.<br>
  Меньше отдельных утилит — больше спокойствия 😌
</p>

<p align="center">
  <a href="https://github.com/multimaks2/AntiZapret/releases/download/v1.3.5i/AntiZapret-Installer.exe"><img src="https://img.shields.io/badge/download-Installer%20v1.3.5i-2ea44f?style=for-the-badge" alt="Download Installer"></a>
  <a href="https://github.com/multimaks2/AntiZapret/releases/tag/v1.3.5i"><img src="https://img.shields.io/badge/release-v1.3.5i-24292f?style=for-the-badge" alt="Release"></a>
  <img src="https://img.shields.io/badge/platform-Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Windows">
</p>

<details>
<summary>📸 Ещё скриншоты</summary>

<br>

| Антизапрет | TG WS Proxy |
|:---:|:---:|
| <img width="1023" height="760" alt="Антизапрет" src="https://github.com/user-attachments/assets/77117a35-a1c2-48a3-b361-705f79adbdc8" /> | <img width="1023" height="760" alt="TG WS Proxy" src="https://github.com/user-attachments/assets/6b278256-1786-4775-a1fb-1ccf29175d49" /> |

| VPN | Маршрутизация |
|:---:|:---:|
| <img width="1023" height="760" alt="VPN" src="https://github.com/user-attachments/assets/171a98bb-e275-4bc6-9c90-72385239f247" /> | <img width="1023" height="760" alt="Маршрутизация" src="https://github.com/user-attachments/assets/9dfc1649-0bb7-49ee-bf54-d42792d9af86" /> |

| Консоль | Настройки |
|:---:|:---:|
| <img width="1023" height="760" alt="Консоль" src="https://github.com/user-attachments/assets/b8fe9aae-301f-4cb3-89cf-0eee9b2ea584" /> | <img width="1023" height="760" alt="Настройки" src="https://github.com/user-attachments/assets/5eb49af6-fc89-4a15-a070-953e10632ace" /> |

| О приложении |
|:---:|
| <img width="1023" height="950" alt="О приложении" src="https://github.com/user-attachments/assets/31d36ba8-847c-4d56-b8a0-05e64627bb42" /> |

</details>

---

## ✨ Что внутри

<table>
<tr>
<td width="50%" valign="top">

### <img src="screen/icons/shield-halved.svg" height="18" alt=""> Антизапрет (zapret)
Обход блокировок
<img src="screen/icons/discord.svg" height="14" alt="Discord">
<img src="screen/icons/youtube.svg" height="14" alt="YouTube">
и связанных сервисов через стратегии и WinDivert 🛡️

</td>
<td width="50%" valign="top">

### <img src="screen/icons/telegram.svg" height="18" alt=""> TG WS Proxy
MTProto-прокси для ускорения и стабильности
<img src="screen/icons/telegram.svg" height="14" alt="Telegram">
Telegram ✈️

</td>
</tr>
<tr>
<td width="50%" valign="top">

### <img src="screen/icons/globe.svg" height="18" alt=""> VPN (mihomo)
Подключение к своим серверам (vmess / trojan и др.) с удобным списком узлов 🌍

</td>
<td width="50%" valign="top">

### <img src="screen/icons/route.svg" height="18" alt=""> Маршрутизация
Куда идёт трафик: обход / VPN / напрямую.
У популярных сервисов —
<img src="screen/icons/discord.svg" height="14" alt="">
<img src="screen/icons/youtube.svg" height="14" alt="">
<img src="screen/icons/spotify.svg" height="14" alt="">
<img src="screen/icons/steam.svg" height="14" alt="">
и другие бренды 🗺️

</td>
</tr>
<tr>
<td width="50%" valign="top">

### <img src="screen/icons/house.svg" height="18" alt=""> Главная
Статус сервисов, быстрый старт/стоп, график сети и переключатель **Мбит/с ↔ МБ/с** 📊

</td>
<td width="50%" valign="top">

### <img src="screen/icons/terminal.svg" height="18" alt=""> Консоль
&nbsp;&nbsp;<img src="screen/icons/gear.svg" height="18" alt=""> **Настройки**
Логи, автозапуск, темы оформления (включая Matrix 🟩) ⚙️

</td>
</tr>
</table>

Всё собрано **для удобства**: один пакет, один UI, меньше ручной возни 🎯

---

## 📥 Скачать

Последний релиз: **[v1.3.5i](https://github.com/multimaks2/AntiZapret/releases/tag/v1.3.5i)** 🎉

**Рекомендуется:** скачать и запустить установщик  
➡️ **[AntiZapret-Installer.exe](https://github.com/multimaks2/AntiZapret/releases/download/v1.3.5i/AntiZapret-Installer.exe)**  
(нужны права администратора)

Альтернатива без установщика: архив  
[`AntiZapret-1.3.5i-win32.zip`](https://github.com/multimaks2/AntiZapret/releases/download/v1.3.5i/AntiZapret-1.3.5i-win32.zip) → распаковать → запустить `AntiZapret.exe` **от имени администратора** 🔑

---

## 🛠️ Сборка из исходников

**Нужно:**
- 🪟 Windows
- 🧰 Visual Studio 2026 (toolset `v145`) + C++ desktop workload
- 🐍 Python 3 (для генерации стратегий, при необходимости)

```bat
create-app.bat
```

Откройте сгенерированное решение Visual Studio и соберите `Release|Win32`.  
Готовые файлы появятся в `bin/x32`: `AntiZapret.exe`, `AntiZapret-Installer.exe`, `AntiZapret-Updater.exe` ✅

---

## 📁 Структура репозитория

| Путь | Что там |
|:---|:---|
| 📂 `source/AntiZapret/` | исходный код основного приложения |
| 📂 `source/AntiZapret-Installer/` | мастер установки |
| 📂 `source/AntiZapret-Updater/` | обновление из GitHub Releases |
| 📂 `vendor/` | сторонние зависимости |
| 📂 `utils/` | Premake и скрипты сборки |
| 📂 `screen/` | скриншоты и иконки для README |
| 📄 `premake5.lua` | описание проектов Premake |

---

## 🙏 Благодарности

AntiZapret стоит на плечах открытых проектов. Огромное спасибо авторам и сообществу ❤️

### 🛡️ Обход блокировок и прокси
- [Flowseal / zapret-discord-youtube](https://github.com/Flowseal/zapret-discord-youtube) — стратегии, списки и runtime для Discord/YouTube
- [bol-van / zapret-win-bundle](https://github.com/bol-van/zapret-win-bundle) — `winws` и WinDivert в Windows-сборке
- [WinDivert](https://reqrypt.org/windivert.html) — перехват сетевого трафика
- [Flowseal / tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) — MTProto-прокси для Telegram

### 🌐 VPN и маршрутизация
- [MetaCubeX / mihomo](https://github.com/MetaCubeX/mihomo) — VPN-ядро (Clash Meta)
- [Wintun](https://www.wintun.net/) — TUN-адаптер для Windows
- [2dust / v2rayN](https://github.com/2dust/v2rayN) — идеи и подход к VPN-интерфейсу
- [runetfreedom / russia-v2ray-rules-dat](https://github.com/runetfreedom/russia-v2ray-rules-dat) — rule-set для маршрутизации
- [runetfreedom / russia-v2ray-custom-routing-list](https://github.com/runetfreedom/russia-v2ray-custom-routing-list) — пресеты маршрутизации
- [MetaCubeX / meta-rules-dat](https://github.com/MetaCubeX/meta-rules-dat) — GeoIP / geosite данные

### 🎨 Интерфейс и инструменты
- [ocornut / imgui](https://github.com/ocornut/imgui) — Dear ImGui
- [Font Awesome](https://fontawesome.com/) — иконки интерфейса и сервисов  
  🔎 каталог брендов: [fontawesome.com/search?f=brands](https://fontawesome.com/search?f=brands)
- [Lua](https://www.lua.org/) — встроенный скриптовый движок
- [Premake](https://premake.github.io/) — генерация проектов Visual Studio
- [flagcdn.com](https://flagcdn.com/) — иконки флагов стран
- Microsoft — DirectX 11 и Segoe MDL2 Assets

> AntiZapret **не аффилирован** с перечисленными проектами; права на код и торговые марки остаются у их авторов.  
> Сторонние компоненты сохраняют свои лицензии (см. `vendor/`).
