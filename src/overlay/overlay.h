#pragma once

#include "sdk/mod_sdk.h"

// Shared overlay sources, compiled into every mod that enables the overlay.
// Nothing here is a separate module: the state below lives in the mod's own DLL.

struct UrkOverlayConfig {
    const char *display_name = "";
    // Stable id used for deployed resources: locales/<resource_id>.
    const char *mod_id = "";
    const char *default_language = "en";
    // Win32 virtual-key code that toggles the menu; 0 disables the toggle.
    int menu_toggle_key = 0x09;
    bool localization = false;
    bool detached_viewports = false;
    // Mod-owned menu state. The overlay toggles it and never keeps a copy.
    bool *menu_open = nullptr;
};

enum class UrkOverlayApi : int {
    Unknown = 0,
    D3D11 = 1,
    D3D12 = 2,
    OpenGL = 3,
};

struct UrkOverlayDevice {
    UrkOverlayApi api = UrkOverlayApi::Unknown;
    // ID3D11Device* / ID3D12Device*. Null for OpenGL.
    void *device = nullptr;
    // ID3D12CommandQueue* on D3D12; null elsewhere.
    void *command_queue = nullptr;
};

namespace ModRenderHook {

// Copied at install(); the strings must outlive the overlay, which they do when
// they come from the mod's own config constants.
extern UrkOverlayConfig g_options;

bool install(const URK_ModContext *context, const UrkOverlayConfig &options);
bool uninstall();

// Runs once per frame inside the ImGui frame, after the built-in highlight pass
// and before the draw data is submitted. Use it for custom overlays.
void add_frame_callback(void (*callback)());

// Runs once when the presentation device becomes known. Use it to create GPU
// resources that need the device.
void add_device_ready_callback(void (*callback)(const UrkOverlayDevice &device));

} // namespace ModRenderHook

