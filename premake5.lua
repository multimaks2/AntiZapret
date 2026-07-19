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
		"source",
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
		"source/main.cpp",
		"source/version.h",
		"source/image/app.rc",
		"source/image/resource.h",
		"source/app/application.h",
		"source/app/application.cpp",
		"source/app/app_settings.h",
		"source/app/app_settings.cpp",
		"source/app/app_version.h",
		"source/app/app_version.cpp",
		"source/app/app_update_gate.h",
		"source/app/app_update_gate.cpp",
		"source/app/settings_document.h",
		"source/app/settings_document.cpp",
		"source/app/app_log.h",
		"source/app/app_log.cpp",
		"source/app/process_job.h",
		"source/app/process_job.cpp",
		"source/app/protocol_handler.h",
		"source/app/protocol_handler.cpp",
		"source/net/traffic_monitor.h",
		"source/net/traffic_monitor.cpp",
		"source/ui/ui_types.h",
		"source/ui/ui_layout.h",
		"source/zapret/zapret_types.h",
		"source/gfx/theme_manager.h",
		"source/gfx/theme_manager.cpp",
		"source/gfx/font_manager.h",
		"source/gfx/font_manager.cpp",
		"source/gfx/d3d11_renderer.h",
		"source/gfx/d3d11_renderer.cpp",
		"source/window/window_manager.h",
		"source/window/window_manager.cpp",
		"source/ui/ui_shell.h",
		"source/ui/ui_shell.cpp",
		"source/ui/ui_common.h",
		"source/ui/ui_common.cpp",
		"source/ui/ui_sidebar.h",
		"source/ui/ui_sidebar.cpp",
		"source/ui/ui_home_page.h",
		"source/ui/ui_home_page.cpp",
		"source/ui/ui_antizapret_page.h",
		"source/ui/ui_antizapret_page.cpp",
		"source/ui/ui_tgfix_page.h",
		"source/ui/ui_tgfix_page.cpp",
		"source/ui/ui_vpn_page.h",
		"source/ui/ui_vpn_page.cpp",
		"source/ui/ui_routing_page.h",
		"source/ui/ui_routing_page.cpp",
		"source/ui/ui_settings_page.h",
		"source/ui/ui_settings_page.cpp",
		"source/ui/ui_about_page.h",
		"source/ui/ui_about_page.cpp",
		"source/ui/ui_console_page.h",
		"source/ui/ui_console_page.cpp",
		"source/ui/ui_smooth_scroll.h",
		"source/ui/ui_smooth_scroll.cpp",
		"source/ui/ui_page_host.h",
		"source/ui/ui_page_host.cpp",
		"source/lua/lua_runtime.h",
		"source/lua/lua_runtime.cpp",
		"source/lua/lua_api.h",
		"source/lua/lua_api.cpp",
		"source/zapret/strategies.hpp",
		"source/zapret/zapret_paths.h",
		"source/zapret/zapret_paths.cpp",
		"source/zapret/strategy_argument_builder.h",
		"source/zapret/strategy_argument_builder.cpp",
		"source/zapret/zapret_manager.h",
		"source/zapret/zapret_manager.cpp",
		"source/zapret/strategy_descriptions.h",
		"source/zapret/strategy_descriptions.cpp",
		"source/zapret/zapret_connectivity.h",
		"source/zapret/zapret_connectivity.cpp",
		"source/zapret/zapret_store.h",
		"source/zapret/zapret_store.cpp",
		"source/zapret/smart_strategy_engine.h",
		"source/zapret/smart_strategy_engine.cpp",
		"source/tgproxy/tg_ws_proxy_manager.h",
		"source/tgproxy/tg_ws_proxy_manager.cpp",
		"source/vpn/vpn_node.h",
		"source/vpn/vpn_import.h",
		"source/vpn/vpn_import.cpp",
		"source/vpn/vpn_geo.h",
		"source/vpn/vpn_geo.cpp",
		"source/vpn/vpn_flag_icons.h",
		"source/vpn/vpn_flag_icons.cpp",
		"source/vpn/vpn_manager.h",
		"source/vpn/vpn_manager.cpp",
		"source/vpn/vpn_mihomo_api.h",
		"source/vpn/vpn_mihomo_api.cpp",
		"source/vpn/vpn_node_probe.h",
		"source/vpn/vpn_node_probe.cpp",
		"source/vpn/vpn_config_builder.h",
		"source/vpn/vpn_config_builder.cpp",
		"source/vpn/vpn_routing.h",
		"source/vpn/vpn_routing.cpp",
		"source/vpn/vpn_rules_updater.h",
		"source/vpn/vpn_rules_updater.cpp",
		"source/vpn/vpn_module_update_check.h",
		"source/vpn/vpn_module_update_check.cpp",
		"source/vpn/vpn_module_update_apply.h",
		"source/vpn/vpn_module_update_apply.cpp",
		"source/vpn/vpn_store.h",
		"source/vpn/vpn_store.cpp",
		"source/vpn/vpn_service_routes.h",
		"source/vpn/vpn_service_routes.cpp",
		"source/vpn/vpn_adult_sites.h",
		"source/vpn/vpn_adult_sites.cpp",
		"source/vpn/vpn_service_fallback_domains.h",
		"source/vpn/vpn_service_fallback_domains.cpp",
		"source/vpn/vpn_discord_voice_rules.h",
		"source/vpn/vpn_discord_voice_rules.cpp",
		"source/vpn/vpn_domain_routes.h",
		"source/vpn/vpn_domain_routes.cpp",
		"source/vpn/vpn_transport_settings.h",
		"source/vpn/vpn_transport_settings.cpp",
		"source/discord/discord_presence.h",
		"source/discord/discord_presence.cpp",
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
			"source/main.cpp",
		},
		["source/app"] = {
			"source/app/**",
		},
		["source/net"] = {
			"source/net/**",
		},
		["source/gfx"] = {
			"source/gfx/**",
		},
		["source/window"] = {
			"source/window/**",
		},
		["source/ui"] = {
			"source/ui/**",
		},
		["source/image"] = {
			"source/image/app.rc",
			"source/image/resource.h",
		},
		["source/lua"] = {
			"source/lua/**",
		},
		["source/zapret"] = {
			"source/zapret/**",
		},
		["source/discord"] = {
			"source/discord/**",
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

project "z-updater"
	kind "ConsoleApp"
	location "build"
	targetname "z-updater"
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
	}

	linkoptions { "/MANIFESTUAC:level='requireAdministrator'" }

	files {
		"source/z-updater/main.cpp",
	}

	links {
		"wininet",
		"shell32",
	}
