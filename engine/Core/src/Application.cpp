#include "VoxelForge/Core/Application.h"

#include "VoxelForge/Core/Event/EventDispatcher.h"
#include "VoxelForge/Core/Event/WindowEvent.h"
#include "VoxelForge/Core/Layer/Layer.h"
#include "VoxelForge/Core/Layer/LayerStack.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Core/Version.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace VoxelForge::Core
{

Application::Application(ApplicationSpecification specification)
    : specification_(std::move(specification)),
      layerStack_(std::make_unique<LayerStack>()),
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

    const std::string version = Version::Current().ToString();

    std::cout << "========================================\n";
    std::cout << " " << specification_.Name << "\n";
    std::cout << " v" << version << "\n";
    std::cout << "========================================\n\n";

    Logger::Instance().Info("Initializing Core...");
    Logger::Instance().Info("Logger initialized.");
    Logger::Instance().Info("Version system initialized.");
    Logger::Instance().Info("FileSystem initialized.");
    Logger::Instance().Info("Layer system initialized.");
    Logger::Instance().Info("Application initialized.");

    initialized_ = true;
    running_ = true;
    return true;
}

int Application::Run()
{
    if (!Initialize())
    {
        Logger::Instance().Error("VoxelForge failed to initialize.");
        return 1;
    }

    Logger::Instance().Info("VoxelForge Engine Ready.");

    // La boucle de mise à jour des layers sera ajoutée avec le système Window.
    if (specification_.PauseOnExit && running_)
    {
        std::cout << "\nPress Enter to close VoxelForge...\n";
        std::cin.get();
    }

    Close();
    Shutdown();
    return 0;
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

bool Application::OnWindowClose(VoxelForge::WindowCloseEvent&)
{
    Close();
    return true;
}

void Application::Shutdown()
{
    if (!initialized_)
        return;

    Logger::Instance().Info("Shutting down VoxelForge Engine...");

    if (layerStack_)
        layerStack_->Clear();

    running_ = false;
    initialized_ = false;
    Logger::Instance().Info("Shutdown complete.");
}

} // namespace VoxelForge::Core
