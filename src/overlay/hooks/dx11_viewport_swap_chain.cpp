#include "dx11_viewport_swap_chain.h"

#include <algorithm>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool is_known_swap_effect(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool is_flip_model(DXGI_SWAP_EFFECT effect) {
    return effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

[[nodiscard]] bool is_flip_model_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return true;
        default:
            return false;
    }
}

// Flip-model rejects sRGB back buffers; the game gets that color space from its RTV.
[[nodiscard]] DXGI_FORMAT flip_model_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:
            return is_flip_model_format(format) ? format : DXGI_FORMAT_UNKNOWN;
    }
}

} // namespace

std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor) {
    if (!is_known_swap_effect(gameDescriptor.SwapEffect) || gameDescriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN) {
        return std::nullopt;
    }

    Dx11ViewportSwapChainConfig config{};
    config.flipModel = is_flip_model(gameDescriptor.SwapEffect);

    DXGI_SWAP_CHAIN_DESC &viewport = config.descriptor;
    DXGI_SWAP_EFFECT swapEffect = gameDescriptor.SwapEffect;
    DXGI_FORMAT format = gameDescriptor.BufferDesc.Format;
    if (config.flipModel) {
        const DXGI_FORMAT flipFormat = flip_model_format(format);
        if (flipFormat != DXGI_FORMAT_UNKNOWN) {
            format = flipFormat;
        } else {
            // No flip-model equivalent: bitblt takes the format unchanged.
            config.flipModel = false;
            swapEffect = DXGI_SWAP_EFFECT_DISCARD;
        }
    }

    viewport.BufferDesc.Width = 0;
    viewport.BufferDesc.Height = 0;
    viewport.BufferDesc.RefreshRate = {0, 1};
    viewport.BufferDesc.Format = format;
    viewport.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    viewport.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    viewport.SampleDesc = {1, 0};
    viewport.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    viewport.BufferCount = config.flipModel ? (std::clamp)(gameDescriptor.BufferCount, 2u, 16u)
                                            : (std::clamp)(gameDescriptor.BufferCount, 1u, 16u);
    viewport.OutputWindow = nullptr;
    viewport.Windowed = TRUE;
    viewport.SwapEffect = swapEffect;
    viewport.Flags = 0;
    return config;
}

const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
            return "DISCARD";
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
            return "SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
            return "FLIP_SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return "FLIP_DISCARD";
        default:
            return "UNKNOWN";
    }
}

} // namespace ModRenderHook
