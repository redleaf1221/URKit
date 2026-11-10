#pragma once

#include <cstdint>
#include <span>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

struct WindowMessagePumpResult {
    std::uint32_t dispatched = 0;
    std::uint32_t foreignThreadWindows = 0;
    bool backlogRemaining = false;
};

[[nodiscard]] WindowMessagePumpResult pump_owned_window_messages(std::span<const HWND> windows,
                                                                 std::size_t messageBudget = 512);

[[nodiscard]] bool is_imgui_platform_window(HWND window);

} // namespace ModRenderHook
