#pragma once

namespace ModRenderHook {

struct DxgiVTableTargets {
    void *present{};
    void *present1{};
    void *resizeBuffers{};
};

using DxgiDiscoveryDiagnosticSink = void (*)(const char *message);

[[nodiscard]] DxgiVTableTargets discover_dxgi_hook_targets(bool preferDx12,
                                                            DxgiDiscoveryDiagnosticSink diagnosticSink);
[[nodiscard]] void *discover_dx12_execute_command_lists_target(DxgiDiscoveryDiagnosticSink diagnosticSink);

} // namespace ModRenderHook
