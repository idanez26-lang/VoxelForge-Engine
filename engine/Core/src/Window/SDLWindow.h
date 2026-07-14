#pragma once

#include "VoxelForge/Core/Window/Window.h"

struct SDL_Renderer;
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

    [[nodiscard]] const WindowSpecification& GetSpecification() const noexcept override;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept override;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept override;
    [[nodiscard]] void* GetNativeHandle() const noexcept override;
    [[nodiscard]] void* GetNativeRendererHandle() const noexcept override;

private:
    void Initialize();
    void InitializeImGui();
    void Shutdown() noexcept;

    WindowSpecification specification_;
    EventCallback eventCallback_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool ownsSDL_ = false;
    bool imguiInitialized_ = false;
};

} // namespace VoxelForge::Core
