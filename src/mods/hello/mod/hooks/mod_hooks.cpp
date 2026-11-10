#include "mod_hooks.h"

#include "config/mod_config.h"
#include "support/mod_log.h"

#include "overlay/overlay.h"
#include "sdk/hook_api.h"
#include "sdk/il2cpp/il2cpp_helpers.h"
#include "sdk/il2cpp/il2cpp_runtime.h"
#include "sdk/runtime_api.h"

#if defined(URK_WITH_IMGUI) && !URK_WITH_IMGUI
#undef URK_WITH_IMGUI
#define URK_WITH_IMGUI 0
#else
#define URK_WITH_IMGUI 1
#endif

#if URK_WITH_IMGUI
#include "ui/menu.h"
#endif

namespace {
URK::hooks::HookSet g_hooks;

#if URK_WITH_IMGUI
// The menu is this mod's own UI. The overlay only drives the frame, so a mod
// that wants hitboxes or extra windows registers its own frame callback too.
void DrawModMenu() {
    ModUI::render_menu();
}

UrkOverlayConfig OverlayConfig() {
    UrkOverlayConfig config{};
    config.display_name = ModConfig::display_name;
    config.mod_id = ModConfig::mod_id;
    config.default_language = ModConfig::default_language;
    config.menu_toggle_key = ModConfig::menu_toggle_key;
    config.localization = ModConfig::enable_localization;
    config.detached_viewports = ModConfig::enable_detached_viewports;
    config.menu_open = &ModConfig::show_menu;
    return config;
}
#endif
} // namespace

namespace ModHooks {
bool install(const URK_ModContext *ctx) {
    URK::set_context(ctx);
    URK::il2cpp::init(ctx);

    if (!URK::hooks::available()) {
        ModLog::warn("hook API is unavailable; no hooks were installed");
        return true;
    }

#if URK_WITH_IMGUI
    ModRenderHook::add_frame_callback(&DrawModMenu);
    if (!ModRenderHook::install(ctx, OverlayConfig()))
        ModLog::warn("ImGui render hook could not be installed; continuing without menu");
    ModLog::info("hook registry ready; ImGui render hook initialization requested");
#else
    ModLog::info("hook registry ready; this mod was built without the ImGui overlay");
#endif
    return true;
}

void uninstall() {
#if URK_WITH_IMGUI
    if (!ModRenderHook::uninstall())
        ModLog::error("ImGui render hook shutdown was incomplete; see hook diagnostics");
#endif
    g_hooks.detach_all();
}
} // namespace ModHooks

