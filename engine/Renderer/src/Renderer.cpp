#include "VoxelForge/Renderer/Renderer.h"

#include "VoxelForge/Renderer/RendererAPI.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <string>

namespace VoxelForge::Renderer
{

namespace
{

[[nodiscard]] SDL_GPUShaderFormat GetRequestedShaderFormats() noexcept
{
    return
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_METALLIB;
}

[[nodiscard]] std::string DescribeShaderFormats(
    const SDL_GPUShaderFormat formats)
{
    std::string description;

    const auto appendFormat =
        [&description](const char* name)
        {
            if (!description.empty())
            {
                description += ", ";
            }

            description += name;
        };

    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
    {
        appendFormat("SPIR-V");
    }

    if ((formats & SDL_GPU_SHADERFORMAT_DXBC) != 0)
    {
        appendFormat("DXBC");
    }

    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
    {
        appendFormat("DXIL");
    }

    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
    {
        appendFormat("MSL");
    }

    if ((formats & SDL_GPU_SHADERFORMAT_METALLIB) != 0)
    {
        appendFormat("METALLIB");
    }

    return description.empty() ? "None" : description;
}

} // namespace

bool Renderer::Initialize(const RendererSpecification& specification)
{
    if (initialized_)
    {
        return true;
    }

    specification_ = specification;
    statistics_ = {};
    backendName_ = "None";
    shaderFormatsDescription_ = "None";
    lastError_.clear();

    if (specification_.API != RendererAPI::SDLGPU)
    {
        lastError_ = "The requested renderer API is not implemented.";
        return false;
    }

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            lastError_ =
                std::string("SDL video initialization failed: ") +
                SDL_GetError();
            return false;
        }

        ownsSDLVideo_ = true;
    }

    gpuDevice_ = SDL_CreateGPUDevice(
        GetRequestedShaderFormats(),
        specification_.EnableValidation,
        nullptr);

    if (gpuDevice_ == nullptr)
    {
        lastError_ =
            std::string("SDL GPU device creation failed: ") +
            SDL_GetError();

        if (ownsSDLVideo_)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            ownsSDLVideo_ = false;
        }

        return false;
    }

    const char* driverName = SDL_GetGPUDeviceDriver(gpuDevice_);
    backendName_ =
        driverName != nullptr ? driverName : "Unknown SDL GPU backend";

    shaderFormatsDescription_ = DescribeShaderFormats(
        SDL_GetGPUShaderFormats(gpuDevice_));

    initialized_ = true;
    return true;
}

void Renderer::Shutdown() noexcept
{
    if (gpuDevice_ != nullptr)
    {
        SDL_WaitForGPUIdle(gpuDevice_);
        SDL_DestroyGPUDevice(gpuDevice_);
        gpuDevice_ = nullptr;
    }

    initialized_ = false;
    statistics_ = {};
    backendName_ = "None";
    shaderFormatsDescription_ = "None";
    lastError_.clear();

    if (ownsSDLVideo_)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        ownsSDLVideo_ = false;
    }
}

void Renderer::BeginFrame() noexcept
{
    if (initialized_)
    {
        statistics_.ResetPerFrame();
    }
}

void Renderer::EndFrame() noexcept
{
    if (initialized_)
    {
        ++statistics_.FrameIndex;
    }
}

bool Renderer::IsInitialized() noexcept
{
    return initialized_;
}

bool Renderer::HasGPUDevice() noexcept
{
    return gpuDevice_ != nullptr;
}

SDL_GPUDevice* Renderer::GetGPUDevice() noexcept
{
    return gpuDevice_;
}

const std::string& Renderer::GetBackendName() noexcept
{
    return backendName_;
}

const std::string& Renderer::GetShaderFormatsDescription() noexcept
{
    return shaderFormatsDescription_;
}

const std::string& Renderer::GetLastError() noexcept
{
    return lastError_;
}

const RendererSpecification& Renderer::GetSpecification() noexcept
{
    return specification_;
}

const RendererStatistics& Renderer::GetStatistics() noexcept
{
    return statistics_;
}

} // namespace VoxelForge::Renderer
