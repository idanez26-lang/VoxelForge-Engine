#include "VoxelForge/Core/Application.h"

#include "VoxelForge/Core/Event/EventDispatcher.h"
#include "VoxelForge/Core/Event/WindowEvent.h"
#include "VoxelForge/Core/Layer/Layer.h"
#include "VoxelForge/Core/Layer/LayerStack.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Core/Version.h"
#include "VoxelForge/Core/Window/Window.h"

#include <SDL3/SDL_timer.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace VoxelForge::Core
{

Application::Application(ApplicationSpecification specification)
    : specification_(std::move(specification)),
      layerStack_(std::make_unique<LayerStack>()),
      window_(nullptr),
      initialized_(false),
      running_(false)
{
}

Application::~Application() = default;

bool Application::Initialize()
{
    if (initialized_)
    {
        Logger::Instance().Warning(
            "Application initialization was requested more than once.");
        return true;
    }

    Logger::Instance().Info("Initializing Core...");
    Logger::Instance().Info("Logger initialized.");
    Logger::Instance().Info("Version system initialized.");
    Logger::Instance().Info("FileSystem initialized.");
    Logger::Instance().Info("Layer system initialized.");

    WindowSpecification windowSpecification;
    windowSpecification.Title = specification_.Name;
    windowSpecification.Width = specification_.WindowWidth;
    windowSpecification.Height = specification_.WindowHeight;
    windowSpecification.Resizable = specification_.WindowResizable;
    windowSpecification.Maximized = specification_.WindowMaximized;

    window_ = Window::Create(windowSpecification);
    window_->SetEventCallback(
        [this](VoxelForge::Event& event)
        {
            OnEvent(event);
        });

    Logger::Instance().Info("Window system initialized.");
    Logger::Instance().Info("Application initialized.");

    initialized_ = true;
    running_ = true;
    return true;
}

int Application::Run()
{
    try
    {
        if (!Initialize())
        {
            Logger::Instance().Error("VoxelForge failed to initialize.");
            return 1;
        }

        Logger::Instance().Info("VoxelForge Engine Ready.");

        while (running_)
        {
            window_->PollEvents();

            if (!running_)
                break;

            UpdateLayers();
            RenderLayerInterfaces();
            SDL_Delay(1);
        }

        Shutdown();
        return 0;
    }
    catch (const std::exception& exception)
    {
        Logger::Instance().Error(
            std::string("Application failure: ") + exception.what());
        Shutdown();
        return 1;
    }
}

void Application::Close() noexcept
{
    running_ = false;
}

void Application::OnEvent(VoxelForge::Event& event)
{
    VoxelForge::EventDispatcher dispatcher(event);
    dispatcher.Dispatch<VoxelForge::WindowCloseEvent>(
        [this](VoxelForge::WindowCloseEvent& closeEvent)
        {
            return OnWindowClose(closeEvent);
        });

    if (event.Handled || !layerStack_)
        return;

    for (auto iterator = layerStack_->rbegin();
         iterator != layerStack_->rend();
         ++iterator)
    {
        (*iterator)->OnEvent(event);

        if (event.Handled)
            break;
    }
}

Layer& Application::PushLayer(std::unique_ptr<Layer> layer)
{
    return layerStack_->PushLayer(std::move(layer));
}

Layer& Application::PushOverlay(std::unique_ptr<Layer> overlay)
{
    return layerStack_->PushOverlay(std::move(overlay));
}

std::unique_ptr<Layer> Application::PopLayer(Layer& layer)
{
    return layerStack_->PopLayer(layer);
}

std::unique_ptr<Layer> Application::PopOverlay(Layer& overlay)
{
    return layerStack_->PopOverlay(overlay);
}

const ApplicationSpecification& Application::GetSpecification() const noexcept
{
    return specification_;
}

bool Application::IsRunning() const noexcept
{
    return running_;
}

LayerStack& Application::GetLayerStack() noexcept
{
    return *layerStack_;
}

const LayerStack& Application::GetLayerStack() const noexcept
{
    return *layerStack_;
}

Window& Application::GetWindow() noexcept
{
    return *window_;
}

const Window& Application::GetWindow() const noexcept
{
    return *window_;
}

void Application::UpdateLayers()
{
    for (const auto& layer : *layerStack_)
        layer->OnUpdate();
}

void Application::RenderLayerInterfaces()
{
    for (const auto& layer : *layerStack_)
        layer->OnImGuiRender();
}

bool Application::OnWindowClose(VoxelForge::WindowCloseEvent&)
{
    Close();
    return true;
}

void Application::Shutdown()
{
    if (!initialized_)
    {
        window_.reset();
        return;
    }

    Logger::Instance().Info("Shutting down VoxelForge Engine...");

    if (layerStack_)
        layerStack_->Clear();

    window_.reset();
    running_ = false;
    initialized_ = false;

    Logger::Instance().Info("Shutdown complete.");
}

} // namespace VoxelForge::Core
