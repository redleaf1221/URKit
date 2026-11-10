#pragma once

#include <array>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace ModRenderHook {

enum class Dx12BeginFrameStatus {
    ready,
    gpu_busy,
    unavailable,
    reset_failed,
};

struct Dx12FrameSubmission {
    ID3D12GraphicsCommandList *commandList{};
    ID3D12Resource *backBuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
    UINT frameIndex{};
};

class Dx12OverlayResources final {
  public:
    using DiagnosticSink = void (*)(const char *message);

    Dx12OverlayResources() = default;
    ~Dx12OverlayResources();

    Dx12OverlayResources(const Dx12OverlayResources &) = delete;
    Dx12OverlayResources &operator=(const Dx12OverlayResources &) = delete;

    void set_diagnostic_sink(DiagnosticSink sink) noexcept;
    [[nodiscard]] bool capture_command_queue(ID3D12CommandQueue *queue) noexcept;
    [[nodiscard]] bool has_command_queue() const noexcept;
    [[nodiscard]] bool has_device_objects() const noexcept;
    [[nodiscard]] bool create(IDXGISwapChain *swapChain) noexcept;
    [[nodiscard]] bool wait_for_idle() const noexcept;
    void release_device_objects() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] Dx12BeginFrameStatus begin_frame(Dx12FrameSubmission *submission) noexcept;
    [[nodiscard]] bool submit_frame(const Dx12FrameSubmission &submission) const noexcept;
    [[nodiscard]] bool complete_frame(const Dx12FrameSubmission &submission) noexcept;

    [[nodiscard]] ID3D12Device *device() const noexcept;
    [[nodiscard]] ID3D12CommandQueue *command_queue() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap *srv_heap() const noexcept;
    [[nodiscard]] DXGI_FORMAT format() const noexcept;
    [[nodiscard]] int frame_count() const noexcept;

    static void allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle);
    static void free_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

  private:
    struct FrameContext {
        ID3D12CommandAllocator *allocator{};
        ID3D12Resource *backBuffer{};
        UINT64 fenceValue{};
    };

    static constexpr UINT kSrvDescriptorCapacity = 256;
    static constexpr UINT kFallbackSrvDescriptor = 0;
    static constexpr DWORD kDrainTimeoutMilliseconds = 2000;
    static Dx12OverlayResources *descriptorOwner_;

    void descriptor_handles(UINT index, D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                            D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) const noexcept;
    void report(const char *message) const noexcept;

    DiagnosticSink diagnosticSink_{};
    ID3D12Device *device_{};
    ID3D12CommandQueue *commandQueue_{};
    IDXGISwapChain3 *swapChain_{};
    ID3D12DescriptorHeap *rtvHeap_{};
    ID3D12DescriptorHeap *srvHeap_{};
    ID3D12GraphicsCommandList *commandList_{};
    ID3D12Fence *fence_{};
    HANDLE fenceEvent_{};
    UINT rtvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    UINT64 nextFenceValue_{1};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    bool synchronizationLost_{};
    std::array<bool, kSrvDescriptorCapacity> srvDescriptors_{};
    std::vector<FrameContext> frames_;
};

} // namespace ModRenderHook
