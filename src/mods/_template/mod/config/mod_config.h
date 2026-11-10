#pragma once

namespace ModConfig {
inline constexpr auto project_name = "ExampleMod";
// Stable namespace for this mod's deployed resources. Do not change it after release.
inline constexpr auto mod_id = "ExampleMod_5d8628ce";
inline constexpr auto display_name = "ExampleMod";
inline constexpr auto author = "";
inline constexpr auto version = "0.1.0";
inline constexpr auto url = "";
inline constexpr auto social = "";
inline constexpr auto description = "URKit IL2CPP native mod profile";
inline constexpr bool is_il2cpp_backend = true;
// Closed at start, so the game keeps its input; a one-time hint names the key.
inline bool show_menu = false;
// English is used as the fixed language when localization support is not generated.
inline bool enable_localization = false;
inline constexpr auto default_language = "en";
// Lets ImGui windows move to another monitor.
inline bool enable_detached_viewports = false;
// Win32 virtual-key code used by the generated ImGui WndProc toggle.
// Default: VK_TAB (0x09). Change this value to customize the menu key.
inline int menu_toggle_key = 0x09;
} // namespace ModConfig
