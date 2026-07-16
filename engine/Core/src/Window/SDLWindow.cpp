#include "Window/SDLWindow.h"

#include "VoxelForge/Core/Event/KeyboardEvent.h"
#include "VoxelForge/Core/Event/FileDropEvent.h"
#include "VoxelForge/Core/Event/MouseEvent.h"
#include "VoxelForge/Core/Event/WindowEvent.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Renderer/Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace VoxelForge::Core
{

SDLWindow::SDLWindow(const WindowSpecification& specification)
    : specification_(specification)
{
    try
    {
        Initialize();
    }
    catch (...)
    {
        Shutdown();
        throw;
    }
}

SDLWindow::~SDLWindow()
{
    Shutdown();
}

void SDLWindow::Initialize()
{
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
    {
        throw std::runtime_error(
            "SDL video subsystem must be initialized before SDLWindow.");
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (specification_.Resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    if (specification_.Maximized)
    {
        flags |= SDL_WINDOW_MAXIMIZED;
    }

    window_ = SDL_CreateWindow(
        specification_.Title.c_str(),
        static_cast<int>(specification_.Width),
        static_cast<int>(specification_.Height),
        flags);

    if (window_ == nullptr)
    {
        const std::string error = SDL_GetError();
        Shutdown();
        throw std::runtime_error(
            std::string("SDL window creation failed: ") + error);
    }

    gpuDevice_ = Renderer::Renderer::GetGPUDevice();

    if (gpuDevice_ == nullptr)
    {
        Shutdown();
        throw std::runtime_error(
            "SDL GPU device is unavailable during window initialization.");
    }

    if (!SDL_ClaimWindowForGPUDevice(gpuDevice_, window_))
    {
        const std::string error = SDL_GetError();
        Shutdown();
        throw std::runtime_error(
            std::string("SDL GPU failed to claim the window: ") + error);
    }

    windowClaimedByGPU_ = true;

    const SDL_GPUPresentMode presentMode =
        Renderer::Renderer::GetSpecification().EnableVSync
            ? SDL_GPU_PRESENTMODE_VSYNC
            : SDL_GPU_PRESENTMODE_IMMEDIATE;

    if (!SDL_SetGPUSwapchainParameters(
            gpuDevice_,
            window_,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            presentMode))
    {
        const std::string error = SDL_GetError();
        Shutdown();
        throw std::runtime_error(
            std::string("SDL GPU swapchain configuration failed: ") + error);
    }

    InitializeImGui();

    Logger::Instance().Info(
        "SDL3 GPU window created: " + specification_.Title + " (" +
        std::to_string(specification_.Width) + "x" +
        std::to_string(specification_.Height) + ").");

    Logger::Instance().Info(
        "Dear ImGui SDL GPU backend initialized.");
}

void SDLWindow::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(window_))
    {
        ImGui::DestroyContext();
        throw std::runtime_error(
            "Dear ImGui SDL3 platform backend initialization failed.");
    }

    ImGui_ImplSDLGPU3_InitInfo initInfo{};
    initInfo.Device = gpuDevice_;
    initInfo.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(gpuDevice_, window_);
    initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    initInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    initInfo.PresentMode =
        Renderer::Renderer::GetSpecification().EnableVSync
            ? SDL_GPU_PRESENTMODE_VSYNC
            : SDL_GPU_PRESENTMODE_IMMEDIATE;

    if (!ImGui_ImplSDLGPU3_Init(&initInfo))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error(
            "Dear ImGui SDL GPU renderer backend initialization failed.");
    }

    imguiInitialized_ = true;
}

void SDLWindow::Shutdown() noexcept
{
    if (gpuDevice_ != nullptr)
    {
        SDL_WaitForGPUIdle(gpuDevice_);
    }

    if (imguiInitialized_)
    {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }

    if (windowClaimedByGPU_ && gpuDevice_ != nullptr && window_ != nullptr)
    {
        SDL_ReleaseWindowFromGPUDevice(gpuDevice_, window_);
        windowClaimedByGPU_ = false;
    }

    gpuDevice_ = nullptr;

    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void SDLWindow::PollEvents()
{
    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        if (imguiInitialized_)
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (!eventCallback_)
        {
            continue;
        }

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                VoxelForge::WindowCloseEvent closeEvent;
                eventCallback_(closeEvent);
                break;
            }

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window_, &width, &height);

                specification_.Width =
                    static_cast<std::uint32_t>(width > 0 ? width : 0);
                specification_.Height =
                    static_cast<std::uint32_t>(height > 0 ? height : 0);

                VoxelForge::WindowResizeEvent resizeEvent(
                    specification_.Width,
                    specification_.Height);
                eventCallback_(resizeEvent);
                break;
            }

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            {
                VoxelForge::WindowFocusEvent focusEvent;
                eventCallback_(focusEvent);
                break;
            }

            case SDL_EVENT_WINDOW_FOCUS_LOST:
            {
                VoxelForge::WindowLostFocusEvent lostFocusEvent;
                eventCallback_(lostFocusEvent);
                break;
            }

            case SDL_EVENT_WINDOW_MOVED:
            {
                VoxelForge::WindowMovedEvent movedEvent(
                    event.window.data1,
                    event.window.data2);
                eventCallback_(movedEvent);
                break;
            }

            case SDL_EVENT_KEY_DOWN:
            {
                VoxelForge::KeyPressedEvent keyEvent(
                    static_cast<VoxelForge::KeyCode>(event.key.scancode),
                    event.key.repeat);
                eventCallback_(keyEvent);
                break;
            }

            case SDL_EVENT_KEY_UP:
            {
                VoxelForge::KeyReleasedEvent keyEvent(
                    static_cast<VoxelForge::KeyCode>(event.key.scancode));
                eventCallback_(keyEvent);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                VoxelForge::MouseMovedEvent mouseEvent(
                    event.motion.x,
                    event.motion.y);
                eventCallback_(mouseEvent);
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                VoxelForge::MouseScrolledEvent scrollEvent(
                    event.wheel.x,
                    event.wheel.y);
                eventCallback_(scrollEvent);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                VoxelForge::MouseButtonPressedEvent buttonEvent(
                    static_cast<VoxelForge::MouseCode>(event.button.button));
                eventCallback_(buttonEvent);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                VoxelForge::MouseButtonReleasedEvent buttonEvent(
                    static_cast<VoxelForge::MouseCode>(event.button.button));
                eventCallback_(buttonEvent);
                break;
            }

            case SDL_EVENT_DROP_BEGIN:
            {
                VoxelForge::FileDropBeginEvent dropEvent(
                    event.drop.x, event.drop.y);
                eventCallback_(dropEvent);
                break;
            }

            case SDL_EVENT_DROP_POSITION:
            {
                VoxelForge::FileDropPositionEvent dropEvent(
                    event.drop.x, event.drop.y);
                eventCallback_(dropEvent);
                break;
            }

            case SDL_EVENT_DROP_FILE:
            {
                if (event.drop.data != nullptr)
                {
                    const std::filesystem::path copiedPath(
                        std::u8string(reinterpret_cast<const char8_t*>(
                            event.drop.data)));
                    VoxelForge::FileDropFileEvent dropEvent(
                        copiedPath, event.drop.x, event.drop.y);
                    eventCallback_(dropEvent);
                }
                break;
            }

            case SDL_EVENT_DROP_COMPLETE:
            {
                VoxelForge::FileDropCompleteEvent dropEvent(
                    event.drop.x, event.drop.y);
                eventCallback_(dropEvent);
                break;
            }

            default:
                break;
        }
    }
}

void SDLWindow::BeginFrame()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void SDLWindow::EndFrame()
{
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    const bool minimized =
        drawData->DisplaySize.x <= 0.0F ||
        drawData->DisplaySize.y <= 0.0F;

    SDL_GPUCommandBuffer* commandBuffer =
        SDL_AcquireGPUCommandBuffer(gpuDevice_);

    if (commandBuffer == nullptr)
    {
        Logger::Instance().Error(
            std::string("SDL GPU command buffer acquisition failed: ") +
            SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchainTexture = nullptr;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commandBuffer,
            window_,
            &swapchainTexture,
            nullptr,
            nullptr))
    {
        Logger::Instance().Error(
            std::string("SDL GPU swapchain acquisition failed: ") +
            SDL_GetError());

        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }

    if (swapchainTexture != nullptr && !minimized)
    {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);

        SDL_GPUColorTargetInfo targetInfo{};
        targetInfo.texture = swapchainTexture;
        targetInfo.clear_color = SDL_FColor{0.07F, 0.07F, 0.09F, 1.0F};
        targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        targetInfo.store_op = SDL_GPU_STOREOP_STORE;
        targetInfo.mip_level = 0;
        targetInfo.layer_or_depth_plane = 0;
        targetInfo.cycle = false;

        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
            commandBuffer,
            &targetInfo,
            1,
            nullptr);

        if (renderPass == nullptr)
        {
            Logger::Instance().Error(
                std::string("SDL GPU render pass creation failed: ") +
                SDL_GetError());

            SDL_CancelGPUCommandBuffer(commandBuffer);
            return;
        }

        ImGui_ImplSDLGPU3_RenderDrawData(
            drawData,
            commandBuffer,
            renderPass);

        SDL_EndGPURenderPass(renderPass);
    }

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    {
        Logger::Instance().Error(
            std::string("SDL GPU command buffer submission failed: ") +
            SDL_GetError());
    }
}

void SDLWindow::SetEventCallback(EventCallback callback)
{
    eventCallback_ = std::move(callback);
}

bool SDLWindow::SetTitle(std::string title)
{
    if (window_ == nullptr)
    {
        Logger::Instance().Error(
            "Cannot change the title of an unavailable SDL window.");
        return false;
    }

    if (!SDL_SetWindowTitle(window_, title.c_str()))
    {
        Logger::Instance().Error(
            std::string("SDL window title update failed: ") +
            SDL_GetError());
        return false;
    }

    specification_.Title = std::move(title);
    return true;
}

const WindowSpecification& SDLWindow::GetSpecification() const noexcept
{
    return specification_;
}

std::uint32_t SDLWindow::GetWidth() const noexcept
{
    return specification_.Width;
}

std::uint32_t SDLWindow::GetHeight() const noexcept
{
    return specification_.Height;
}

void* SDLWindow::GetNativeHandle() const noexcept
{
    return window_;
}

void* SDLWindow::GetNativeGPUDeviceHandle() const noexcept
{
    return gpuDevice_;
}

} // namespace VoxelForge::Core
