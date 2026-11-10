#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

[[nodiscard]] bool client_mouse_to_desktop(HWND window, POINT *position) noexcept;
[[nodiscard]] bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept;

} // namespace ModRenderHook
