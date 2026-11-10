#include "win32_input_coordinates.h"

namespace ModRenderHook {

bool client_mouse_to_desktop(HWND window, POINT *position) noexcept {
    return window && position && ClientToScreen(window, position) != FALSE;
}

bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept {
    if (!position)
        return false;
    if (multiViewport)
        return true;
    return window && ScreenToClient(window, position) != FALSE;
}

} // namespace ModRenderHook
