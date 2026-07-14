#include "Window/SDLWindow.h"

#include "VoxelForge/Core/Event/KeyboardEvent.h"
#include "VoxelForge/Core/Event/MouseEvent.h"
#include "VoxelForge/Core/Event/WindowEvent.h"
#include "VoxelForge/Core/Logger.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace VoxelForge::Core
{

SDLWindow::SDLWindow(const WindowSpecification& specification)
    : specification_(specification)
{
    Initialize();
}

SDLWindow::~SDLWindow()
{
    Shutdown();
}

void SDLWindow::Initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        throw std::runtime_error(
            std::string("SDL video initialization failed: ") + SDL_GetError());
    }

    ownsSDL_ = true;

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

    renderer_ = SDL_CreateRenderer(window_, nullptr);

    if (renderer_ == nullptr)
    {
        const std::string error = SDL_GetError();
        Shutdown();
        throw std::runtime_error(
            std::string("SDL renderer creation failed: ") + error);
    }

    InitializeImGui();

    Logger::Instance().Info(
        "SDL3 window created: " + specification_.Title + " (" +
        std::to_string(specification_.Width) + "x" +
        std::to_string(specification_.Height) + ").");
    Logger::Instance().Info("Dear ImGui initialized.");
}

void SDLWindow::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_))
    {
        ImGui::DestroyContext();
        throw std::runtime_error(
            "Dear ImGui SDL3 backend initialization failed.");
    }

    if (!ImGui_ImplSDLRenderer3_Init(renderer_))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error(
            "Dear ImGui SDL renderer backend initialization failed.");
    }

    imguiInitialized_ = true;
}

void SDLWindow::Shutdown() noexcept
{
    if (imguiInitialized_)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }

    if (renderer_ != nullptr)
    {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (ownsSDL_)
    {
        SDL_Quit();
        ownsSDL_ = false;
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

            default:
                break;
        }
    }
}

void SDLWindow::BeginFrame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void SDLWindow::EndFrame()
{
    ImGui::Render();

    SDL_SetRenderDrawColor(renderer_, 18, 18, 22, 255);
    SDL_RenderClear(renderer_);

    ImGui_ImplSDLRenderer3_RenderDrawData(
        ImGui::GetDrawData(),
        renderer_);

    SDL_RenderPresent(renderer_);
}

void SDLWindow::SetEventCallback(EventCallback callback)
{
    eventCallback_ = std::move(callback);
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

void* SDLWindow::GetNativeRendererHandle() const noexcept
{
    return renderer_;
}

} // namespace VoxelForge::Core
