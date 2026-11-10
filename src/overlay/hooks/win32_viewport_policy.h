#pragma once
#include <span>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {
struct Win32ViewportPolicyResult {
    HWND window{};
    const char *operation{};
    DWORD error{ERROR_SUCCESS};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ERROR_SUCCESS;
    }
};

struct Win32ViewportTopology {
    HWND owner{};
    DWORD windowThread{};
    LONG_PTR extendedStyle{};
};

class Win32ViewportPolicy final {
  public:
    [[nodiscard]] Win32ViewportPolicyResult apply(std::span<const HWND> windows, HWND gameWindow);
    void reset() noexcept;

  private:
    struct ConfiguredWindow {
        HWND window{};
        HWND owner{};
    };

    [[nodiscard]] bool is_configured(HWND window, HWND owner) const noexcept;
    void retain_live_windows(std::span<const HWND> windows);

    std::vector<ConfiguredWindow> configuredWindows_;
};

[[nodiscard]] Win32ViewportTopology query_viewport_topology(HWND window) noexcept;
} // namespace ModRenderHook
