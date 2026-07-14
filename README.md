# AntiZapret

Windows-приложение для обхода блокировок (zapret / VPN / Telegram proxy) с UI на Dear ImGui.

## Сборка

Требования:
- Windows
- Visual Studio 2026 (toolset `v145`) с C++ desktop workload
- Python 3 (для генерации стратегий, при необходимости)

```bat
create-app.bat
```

Откройте сгенерированное решение Visual Studio и соберите конфигурацию `Release|Win32`.

Готовый `AntiZapret.exe` появится в `bin/x32`.

## Структура

- `source/` — исходный код приложения
- `vendor/` — сторонние зависимости (imgui, lua, zapret runtime, tg-ws-proxy и др.)
- `utils/` — premake и скрипты сборки/генерации
- `premake5.lua` — описание проекта Premake

## Лицензии

Сторонние компоненты сохраняют свои лицензии (см. каталоги в `vendor/`).
