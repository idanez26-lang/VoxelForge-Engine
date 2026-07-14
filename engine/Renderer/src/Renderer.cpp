#include "VoxelForge/Renderer/Renderer.h"

#include "VoxelForge/Core/Logger.h"
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

    if (description.empty())
    {
        description = "None";
    }

    return description;
}

} // namespace

bool Renderer::Initialize(const RendererSpecification& specification)
{
    if (initialized_)
    {
        Core::Logger::Instance().Warning(
            "Renderer initialization was requested more than once.");
        return true;
    }

    specification_ = specification;
    statistics_ = {};
    backendName_ = "None";
    shaderFormatsDescription_ = "None";

    Core::Logger::Instance().Info(
        "Initializing renderer. Requested API: " +
        std::string(ToString(specification_.API)) + ".");

    if (specification_.API != RendererAPI::SDLGPU)
    {
        Core::Logger::Instance().Error(
            "The requested renderer API is not implemented.");
        return false;
    }

    gpuDevice_ = SDL_CreateGPUDevice(
        GetRequestedShaderFormats(),
        specification_.EnableValidation,
        nullptr);

    if (gpuDevice_ == nullptr)
    {
        Core::Logger::Instance().Error(
            std::string("SDL GPU device creation failed: ") +
            SDL_GetError());
        return false;
    }

    const char* driverName = SDL_GetGPUDeviceDriver(gpuDevice_);
    backendName_ =
        driverName != nullptr ? driverName : "Unknown SDL GPU backend";

    shaderFormatsDescription_ = DescribeShaderFormats(
        SDL_GetGPUShaderFormats(gpuDevice_));

    initialized_ = true;

    Core::Logger::Instance().Info(
        "SDL GPU device created. Backend: " +
        backendName_ + ".");

    Core::Logger::Instance().Info(
        "SDL GPU shader formats: " +
        shaderFormatsDescription_ + ".");

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

    if (!initialized_)
    {
        backendName_ = "None";
        shaderFormatsDescription_ = "None";
        return;
    }

    initialized_ = false;
    statistics_ = {};
    backendName_ = "None";
    shaderFormatsDescription_ = "None";

    Core::Logger::Instance().Info(
        "SDL GPU device and renderer shut down.");
}

void Renderer::BeginFrame() noexcept
{
    if (!initialized_)
    {
        return;
    }

    statistics_.ResetPerFrame();
}

void Renderer::EndFrame() noexcept
{
    if (!initialized_)
    {
        return;
    }

    ++statistics_.FrameIndex;
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

const RendererSpecification& Renderer::GetSpecification() noexcept
{
    return specification_;
}

const RendererStatistics& Renderer::GetStatistics() noexcept
{
    return statistics_;
}

} // namespace VoxelForge::Renderer
