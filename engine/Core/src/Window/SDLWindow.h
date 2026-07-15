#pragma once

#include "VoxelForge/Core/Window/Window.h"

struct SDL_GPUDevice;
struct SDL_Window;

namespace VoxelForge::Core
{

class SDLWindow final : public Window
{
public:
    explicit SDLWindow(const WindowSpecification& specification);
    ~SDLWindow() override;

    void PollEvents() override;
    void BeginFrame() override;
    void EndFrame() override;
    void SetEventCallback(EventCallback callback) override;
    [[nodiscard]] bool SetTitle(std::string title) override;

    [[nodiscard]] const WindowSpecification& GetSpecification() const noexcept override;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept override;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept override;
    [[nodiscard]] void* GetNativeHandle() const noexcept override;
    [[nodiscard]] void* GetNativeGPUDeviceHandle() const noexcept override;

private:
    void Initialize();
    void InitializeImGui();
    void Shutdown() noexcept;

    WindowSpecification specification_;
    EventCallback eventCallback_;
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* gpuDevice_ = nullptr;
    bool windowClaimedByGPU_ = false;
    bool imguiInitialized_ = false;
};

} // namespace VoxelForge::Core
