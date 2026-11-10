#pragma once

#include <optional>

#include <dxgi.h>

namespace ModRenderHook {

struct Dx11ViewportSwapChainConfig {
    DXGI_SWAP_CHAIN_DESC descriptor{};
    bool flipModel = false;
};

[[nodiscard]] std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor);

[[nodiscard]] const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect);

} // namespace ModRenderHook
