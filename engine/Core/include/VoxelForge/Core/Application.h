#pragma once

#include "VoxelForge/Core/ApplicationSpecification.h"

#include <memory>
#include <functional>

namespace VoxelForge
{
class Event;
class WindowCloseEvent;
}

namespace VoxelForge::Core
{

class Layer;
class LayerStack;
class Window;

class Application final
{
public:
    using WindowCloseRequestCallback = std::function<bool()>;
    explicit Application(ApplicationSpecification specification = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] int Run();
    void Close() noexcept;
    void OnEvent(VoxelForge::Event& event);
    void SetWindowCloseRequestCallback(WindowCloseRequestCallback callback);

    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> overlay);
    std::unique_ptr<Layer> PopLayer(Layer& layer);
    std::unique_ptr<Layer> PopOverlay(Layer& overlay);

    [[nodiscard]] const ApplicationSpecification& GetSpecification() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] LayerStack& GetLayerStack() noexcept;
    [[nodiscard]] const LayerStack& GetLayerStack() const noexcept;
    [[nodiscard]] Window& GetWindow() noexcept;
    [[nodiscard]] const Window& GetWindow() const noexcept;

private:
    bool Initialize();
    void UpdateLayers();
    void RenderLayerInterfaces();
    void Shutdown();
    bool OnWindowClose(VoxelForge::WindowCloseEvent& event);

    ApplicationSpecification specification_;
    std::unique_ptr<LayerStack> layerStack_;
    std::unique_ptr<Window> window_;
    WindowCloseRequestCallback windowCloseRequestCallback_;
    bool initialized_;
    bool running_;
};

} // namespace VoxelForge::Core
