#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace VoxelForge
{
class Event;
}

namespace VoxelForge::Core
{

struct WindowSpecification
{
    std::string Title = "VoxelForge Editor";
    std::uint32_t Width = 1280;
    std::uint32_t Height = 720;
    bool Resizable = true;
    bool Maximized = false;
};

class Window
{
public:
    using EventCallback = std::function<void(VoxelForge::Event&)>;

    virtual ~Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    virtual void PollEvents() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void SetEventCallback(EventCallback callback) = 0;

    [[nodiscard]] virtual const WindowSpecification& GetSpecification() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t GetWidth() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t GetHeight() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeRendererHandle() const noexcept = 0;

    [[nodiscard]] static std::unique_ptr<Window> Create(
        const WindowSpecification& specification);

protected:
    Window() = default;
};

} // namespace VoxelForge::Core
