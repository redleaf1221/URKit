#include "dx11_state_guard.h"

namespace ModRenderHook {
Dx11OutputMergerStateGuard::Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept : context_(context) {
    if (context_)
        context_->OMGetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(),
                                     &depth_stencil_);
}

Dx11OutputMergerStateGuard::~Dx11OutputMergerStateGuard() {
    if (!context_)
        return;

    context_->OMSetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(), depth_stencil_);
    for (ID3D11RenderTargetView *render_target : render_targets_)
        if (render_target)
            render_target->Release();
    if (depth_stencil_)
        depth_stencil_->Release();
}
} // namespace ModRenderHook
