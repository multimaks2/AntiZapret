dofile("utils/buildactions/utils.lua")

local imgui = "vendor/imgui-1.92.6"
local lua = "vendor/lua/src"
local discordRpc = "vendor/discord-rpc"
local rapidjson = "vendor/rapidjson/include"

workspace "AntiZapret"
	configurations { "Debug", "Release" }
	platforms { "Win32" }
	location "."
	startproject "AntiZapret"
	language "C++"
	cppdialect "C++17"
	system "windows"

	filter "system:windows"
		toolset "v145"
		buildoptions { "/utf-8" }
		defines { "UNICODE", "_UNICODE" }
		multiprocessorcompile "On"

	filter "platforms:Win32"
		architecture "x86"

	filter "configurations:Debug"
		symbols "On"
		runtime "Debug"
		-- /ZI breaks /MP; /Zi keeps PDB and allows parallel ClCompile.
		editandcontinue "Off"

	filter "configurations:Release"
		symbols "Off"
		optimize "Size"
		buildoptions { "/Gy" }
		linkoptions { "/OPT:REF", "/OPT:ICF" }

	filter {}

project "AntiZapret"
	kind "WindowedApp"
	location "build"
	targetname "AntiZapret"
	objdir ("build/obj/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}")

	filter "configurations:Release"
		targetdir "bin/x32"
		debugdir "bin/x32"

	filter "configurations:Debug"
		targetdir "bin/x32/Debug"
		debugdir "bin/x32/Debug"

	filter {}

	includedirs {
		"source/AntiZapret",
		imgui,
		imgui .. "/backends",
		lua,
		discordRpc .. "/include",
		discordRpc .. "/src",
		rapidjson,
	}

	defines {
		"DISCORD_WINDOWS",
	}

	linkoptions { "/MANIFESTUAC:level='requireAdministrator'" }

	files {
		"source/AntiZapret/main.cpp",
		"source/AntiZapret/version.h",
		"source/AntiZapret/image/app.rc",
		"source/AntiZapret/image/resource.h",
		"source/AntiZapret/app/application.h",
		"source/AntiZapret/app/application.cpp",
		"source/AntiZapret/app/app_settings.h",
		"source/AntiZapret/app/app_settings.cpp",
		"source/AntiZapret/app/app_config.h",
		"source/AntiZapret/app/app_version.h",
		"source/AntiZapret/app/app_version.cpp",
		"source/AntiZapret/app/app_update_gate.h",
		"source/AntiZapret/app/app_update_gate.cpp",
		"source/AntiZapret/app/settings_document.h",
		"source/AntiZapret/app/settings_document.cpp",
		"source/AntiZapret/app/app_log.h",
		"source/AntiZapret/app/app_log.cpp",
		"source/AntiZapret/app/process_job.h",
		"source/AntiZapret/app/process_job.cpp",
		"source/AntiZapret/app/protocol_handler.h",
		"source/AntiZapret/app/protocol_handler.cpp",
		"source/AntiZapret/net/traffic_monitor.h",
		"source/AntiZapret/net/traffic_monitor.cpp",
		"source/AntiZapret/ui/ui_types.h",
		"source/AntiZapret/ui/ui_layout.h",
		"source/AntiZapret/zapret/zapret_types.h",
		"source/AntiZapret/gfx/theme_manager.h",
		"source/AntiZapret/gfx/theme_manager.cpp",
		"source/AntiZapret/gfx/font_manager.h",
		"source/AntiZapret/gfx/font_manager.cpp",
		"source/AntiZapret/gfx/d3d11_renderer.h",
		"source/AntiZapret/gfx/d3d11_renderer.cpp",
		"source/AntiZapret/window/window_manager.h",
		"source/AntiZapret/window/window_manager.cpp",
		"source/AntiZapret/ui/ui_shell.h",
		"source/AntiZapret/ui/ui_shell.cpp",
		"source/AntiZapret/ui/ui_common.h",
		"source/AntiZapret/ui/ui_common.cpp",
		"source/AntiZapret/ui/ui_sidebar.h",
		"source/AntiZapret/ui/ui_sidebar.cpp",
		"source/AntiZapret/ui/ui_home_page.h",
		"source/AntiZapret/ui/ui_home_page.cpp",
		"source/AntiZapret/ui/ui_antizapret_page.h",
		"source/AntiZapret/ui/ui_antizapret_page.cpp",
		"source/AntiZapret/ui/ui_tgfix_page.h",
		"source/AntiZapret/ui/ui_tgfix_page.cpp",
		"source/AntiZapret/ui/ui_vpn_page.h",
		"source/AntiZapret/ui/ui_vpn_page.cpp",
		"source/AntiZapret/ui/ui_routing_page.h",
		"source/AntiZapret/ui/ui_routing_page.cpp",
		"source/AntiZapret/ui/ui_settings_page.h",
		"source/AntiZapret/ui/ui_settings_page.cpp",
		"source/AntiZapret/ui/ui_about_page.h",
		"source/AntiZapret/ui/ui_about_page.cpp",
		"source/AntiZapret/ui/ui_console_page.h",
		"source/AntiZapret/ui/ui_console_page.cpp",
		"source/AntiZapret/ui/ui_smooth_scroll.h",
		"source/AntiZapret/ui/ui_smooth_scroll.cpp",
		"source/AntiZapret/ui/ui_page_host.h",
		"source/AntiZapret/ui/ui_page_host.cpp",
		"source/AntiZapret/lua/lua_runtime.h",
		"source/AntiZapret/lua/lua_runtime.cpp",
		"source/AntiZapret/lua/lua_api.h",
		"source/AntiZapret/lua/lua_api.cpp",
		"source/AntiZapret/zapret/strategies.hpp",
		"source/AntiZapret/zapret/zapret_paths.h",
		"source/AntiZapret/zapret/zapret_paths.cpp",
		"source/AntiZapret/zapret/strategy_argument_builder.h",
		"source/AntiZapret/zapret/strategy_argument_builder.cpp",
		"source/AntiZapret/zapret/zapret_manager.h",
		"source/AntiZapret/zapret/zapret_manager.cpp",
		"source/AntiZapret/zapret/strategy_descriptions.h",
		"source/AntiZapret/zapret/strategy_descriptions.cpp",
		"source/AntiZapret/zapret/zapret_connectivity.h",
		"source/AntiZapret/zapret/zapret_connectivity.cpp",
		"source/AntiZapret/zapret/zapret_diagnostics.h",
		"source/AntiZapret/zapret/zapret_diagnostics.cpp",
		"source/AntiZapret/zapret/zapret_strategy_probe.h",
		"source/AntiZapret/zapret/zapret_strategy_probe.cpp",
		"source/AntiZapret/zapret/strategy_bat_parser.h",
		"source/AntiZapret/zapret/strategy_bat_parser.cpp",
		"source/AntiZapret/zapret/zapret_update_check.h",
		"source/AntiZapret/zapret/zapret_update_check.cpp",
		"source/AntiZapret/zapret/zapret_update_apply.h",
		"source/AntiZapret/zapret/zapret_update_apply.cpp",
		"source/AntiZapret/zapret/zapret_store.h",
		"source/AntiZapret/zapret/zapret_store.cpp",
		"source/AntiZapret/zapret/smart_strategy_engine.h",
		"source/AntiZapret/zapret/smart_strategy_engine.cpp",
		"source/AntiZapret/tgproxy/tg_ws_proxy_manager.h",
		"source/AntiZapret/tgproxy/tg_ws_proxy_manager.cpp",
		"source/AntiZapret/vpn/vpn_node.h",
		"source/AntiZapret/vpn/vpn_import.h",
		"source/AntiZapret/vpn/vpn_import.cpp",
		"source/AntiZapret/vpn/vpn_geo.h",
		"source/AntiZapret/vpn/vpn_geo.cpp",
		"source/AntiZapret/vpn/vpn_flag_icons.h",
		"source/AntiZapret/vpn/vpn_flag_icons.cpp",
		"source/AntiZapret/vpn/vpn_manager.h",
		"source/AntiZapret/vpn/vpn_manager.cpp",
		"source/AntiZapret/vpn/vpn_mihomo_api.h",
		"source/AntiZapret/vpn/vpn_mihomo_api.cpp",
		"source/AntiZapret/vpn/vpn_node_probe.h",
		"source/AntiZapret/vpn/vpn_node_probe.cpp",
		"source/AntiZapret/vpn/vpn_config_builder.h",
		"source/AntiZapret/vpn/vpn_config_builder.cpp",
		"source/AntiZapret/vpn/vpn_routing.h",
		"source/AntiZapret/vpn/vpn_routing.cpp",
		"source/AntiZapret/vpn/vpn_rules_updater.h",
		"source/AntiZapret/vpn/vpn_rules_updater.cpp",
		"source/AntiZapret/vpn/vpn_module_update_check.h",
		"source/AntiZapret/vpn/vpn_module_update_check.cpp",
		"source/AntiZapret/vpn/vpn_module_update_apply.h",
		"source/AntiZapret/vpn/vpn_module_update_apply.cpp",
		"source/AntiZapret/vpn/vpn_store.h",
		"source/AntiZapret/vpn/vpn_store.cpp",
		"source/AntiZapret/vpn/vpn_service_routes.h",
		"source/AntiZapret/vpn/vpn_service_routes.cpp",
		"source/AntiZapret/vpn/vpn_adult_sites.h",
		"source/AntiZapret/vpn/vpn_adult_sites.cpp",
		"source/AntiZapret/vpn/vpn_service_fallback_domains.h",
		"source/AntiZapret/vpn/vpn_service_fallback_domains.cpp",
		"source/AntiZapret/vpn/vpn_discord_voice_rules.h",
		"source/AntiZapret/vpn/vpn_discord_voice_rules.cpp",
		"source/AntiZapret/vpn/vpn_domain_routes.h",
		"source/AntiZapret/vpn/vpn_domain_routes.cpp",
		"source/AntiZapret/vpn/vpn_transport_settings.h",
		"source/AntiZapret/vpn/vpn_transport_settings.cpp",
		"source/AntiZapret/discord/discord_presence.h",
		"source/AntiZapret/discord/discord_presence.cpp",
		discordRpc .. "/src/discord_rpc.cpp",
		discordRpc .. "/src/rpc_connection.cpp",
		discordRpc .. "/src/serialization.cpp",
		discordRpc .. "/src/connection_win.cpp",
		discordRpc .. "/src/discord_register_win.cpp",
		imgui .. "/imgui.cpp",
		imgui .. "/imgui_draw.cpp",
		imgui .. "/imgui_tables.cpp",
		imgui .. "/imgui_widgets.cpp",
		imgui .. "/backends/imgui_impl_win32.cpp",
		imgui .. "/backends/imgui_impl_dx11.cpp",
		lua .. "/lapi.c",
		lua .. "/lauxlib.c",
		lua .. "/lbaselib.c",
		lua .. "/lcode.c",
		lua .. "/ldblib.c",
		lua .. "/ldebug.c",
		lua .. "/ldo.c",
		lua .. "/ldump.c",
		lua .. "/lfunc.c",
		lua .. "/lgc.c",
		lua .. "/linit.c",
		lua .. "/liolib.c",
		lua .. "/llex.c",
		lua .. "/lmathlib.c",
		lua .. "/lmem.c",
		lua .. "/loadlib.c",
		lua .. "/lobject.c",
		lua .. "/lopcodes.c",
		lua .. "/loslib.c",
		lua .. "/lparser.c",
		lua .. "/lstate.c",
		lua .. "/lstring.c",
		lua .. "/lstrlib.c",
		lua .. "/ltable.c",
		lua .. "/ltablib.c",
		lua .. "/ltm.c",
		lua .. "/lundump.c",
		lua .. "/lutf8lib.c",
		lua .. "/lvm.c",
		lua .. "/lzio.c",
	}

	vpaths {
		["source"] = {
			"source/AntiZapret/main.cpp",
			"source/AntiZapret/version.h",
		},
		["source/app"] = {
			"source/AntiZapret/app/**",
		},
		["source/net"] = {
			"source/AntiZapret/net/**",
		},
		["source/gfx"] = {
			"source/AntiZapret/gfx/**",
		},
		["source/window"] = {
			"source/AntiZapret/window/**",
		},
		["source/ui"] = {
			"source/AntiZapret/ui/**",
		},
		["source/image"] = {
			"source/AntiZapret/image/app.rc",
			"source/AntiZapret/image/resource.h",
		},
		["source/lua"] = {
			"source/AntiZapret/lua/**",
		},
		["source/zapret"] = {
			"source/AntiZapret/zapret/**",
		},
		["source/tgproxy"] = {
			"source/AntiZapret/tgproxy/**",
		},
		["source/vpn"] = {
			"source/AntiZapret/vpn/**",
		},
		["source/discord"] = {
			"source/AntiZapret/discord/**",
		},
		["vendor/discord-rpc"] = {
			discordRpc .. "/src/**",
			discordRpc .. "/include/**",
		},
		["vendor/imgui"] = {
			imgui .. "/imgui.cpp",
			imgui .. "/imgui_draw.cpp",
			imgui .. "/imgui_tables.cpp",
			imgui .. "/imgui_widgets.cpp",
			imgui .. "/backends/imgui_impl_win32.cpp",
			imgui .. "/backends/imgui_impl_dx11.cpp",
		},
		["vendor/lua"] = {
			lua .. "/**.c",
			lua .. "/**.h",
		},
	}

	filter { "files:" .. lua .. "/**.c" }
		language "C"
		defines { "_CRT_SECURE_NO_WARNINGS" }
	filter {}

	filter { "files:" .. discordRpc .. "/src/**.cpp" }
		warnings "Off"
		defines { "_CRT_SECURE_NO_WARNINGS" }
	filter {}

	links {
		"d3d11",
		"dxgi",
		"d3dcompiler",
		"dwmapi",
		"shell32",
		"advapi32",
		"bcrypt",
		"wininet",
		"windowscodecs",
		"iphlpapi",
		"ws2_32",
		"psapi",
	}

	local deployScript = path.getabsolute("utils/buildactions/deploy-zapret-runtime.ps1")

	filter "configurations:Release"
		postbuildcommands {
			'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. deployScript .. '" -TargetDir "' .. path.getabsolute("bin/x32") .. '"',
		}

	filter "configurations:Debug"
		postbuildcommands {
			'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. deployScript .. '" -TargetDir "' .. path.getabsolute("bin/x32/Debug") .. '"',
		}

	filter {}

project "AntiZapret-Updater"
	kind "WindowedApp"
	location "build"
	targetname "AntiZapret-Updater"
	objdir ("build/obj/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}")

	filter "configurations:Release"
		targetdir "bin/x32"
		debugdir "bin/x32"

	filter "configurations:Debug"
		targetdir "bin/x32/Debug"
		debugdir "bin/x32/Debug"

	filter {}

	includedirs {
		"source",
		"source/AntiZapret",
		imgui,
		imgui .. "/backends",
	}

	linkoptions { "/MANIFESTUAC:level='requireAdministrator'" }

	files {
		"source/AntiZapret-Updater/main.cpp",
		"source/AntiZapret-Updater/updater_ui.h",
		"source/AntiZapret-Updater/updater_ui.cpp",
		"source/AntiZapret/ui/ui_smooth_scroll.h",
		"source/AntiZapret/ui/ui_smooth_scroll.cpp",
		imgui .. "/imgui.cpp",
		imgui .. "/imgui_draw.cpp",
		imgui .. "/imgui_tables.cpp",
		imgui .. "/imgui_widgets.cpp",
		imgui .. "/backends/imgui_impl_win32.cpp",
		imgui .. "/backends/imgui_impl_dx11.cpp",
	}

	vpaths {
		["source"] = {
			"source/AntiZapret-Updater/main.cpp",
		},
		["source/ui"] = {
			"source/AntiZapret-Updater/updater_ui.h",
			"source/AntiZapret-Updater/updater_ui.cpp",
			"source/AntiZapret/ui/**",
		},
		["vendor/imgui"] = {
			imgui .. "/imgui.cpp",
			imgui .. "/imgui_draw.cpp",
			imgui .. "/imgui_tables.cpp",
			imgui .. "/imgui_widgets.cpp",
			imgui .. "/backends/imgui_impl_win32.cpp",
			imgui .. "/backends/imgui_impl_dx11.cpp",
		},
	}

	links {
		"wininet",
		"shell32",
		"d3d11",
		"dxgi",
		"dwmapi",
		"advapi32",
	}

project "AntiZapret-Installer"
	kind "WindowedApp"
	location "build"
	targetname "AntiZapret-Installer"
	objdir ("build/obj/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}")

	filter "configurations:Release"
		targetdir "bin/x32"
		debugdir "bin/x32"

	filter "configurations:Debug"
		targetdir "bin/x32/Debug"
		debugdir "bin/x32/Debug"

	filter {}

	includedirs {
		"source",
		imgui,
		imgui .. "/backends",
	}

	linkoptions { "/MANIFESTUAC:level='requireAdministrator'" }

	files {
		"source/AntiZapret-Installer/main.cpp",
		"source/AntiZapret-Installer/installer_ui.h",
		"source/AntiZapret-Installer/installer_ui.cpp",
		"source/AntiZapret-Installer/installer_install.cpp",
		"source/AntiZapret-Installer/installer.rc",
		"source/AntiZapret-Installer/resource.h",
		imgui .. "/imgui.cpp",
		imgui .. "/imgui_draw.cpp",
		imgui .. "/imgui_tables.cpp",
		imgui .. "/imgui_widgets.cpp",
		imgui .. "/backends/imgui_impl_win32.cpp",
		imgui .. "/backends/imgui_impl_dx11.cpp",
	}

	vpaths {
		["source"] = {
			"source/AntiZapret-Installer/main.cpp",
			"source/AntiZapret-Installer/installer.rc",
			"source/AntiZapret-Installer/resource.h",
		},
		["source/AntiZapret/ui"] = {
			"source/AntiZapret-Installer/installer_ui.h",
			"source/AntiZapret-Installer/installer_ui.cpp",
		},
		["vendor/imgui"] = {
			imgui .. "/imgui.cpp",
			imgui .. "/imgui_draw.cpp",
			imgui .. "/imgui_tables.cpp",
			imgui .. "/imgui_widgets.cpp",
			imgui .. "/backends/imgui_impl_win32.cpp",
			imgui .. "/backends/imgui_impl_dx11.cpp",
		},
	}

	links {
		"shell32",
		"ole32",
		"d3d11",
		"dxgi",
		"dwmapi",
		"wininet",
		"advapi32",
	}
