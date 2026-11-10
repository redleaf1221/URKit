#pragma once
#include <array>
#include <d3d11.h>

namespace ModRenderHook {
class Dx11OutputMergerStateGuard final {
  public:
    explicit Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept;
    ~Dx11OutputMergerStateGuard();

    Dx11OutputMergerStateGuard(const Dx11OutputMergerStateGuard &) = delete;
    Dx11OutputMergerStateGuard &operator=(const Dx11OutputMergerStateGuard &) = delete;

  private:
    ID3D11DeviceContext *context_{};
    std::array<ID3D11RenderTargetView *, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets_{};
    ID3D11DepthStencilView *depth_stencil_{};
};
} // namespace ModRenderHook
