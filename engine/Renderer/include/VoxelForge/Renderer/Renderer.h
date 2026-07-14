#pragma once

#include "VoxelForge/Renderer/RendererSpecification.h"
#include "VoxelForge/Renderer/RendererStatistics.h"

#include <string>

struct SDL_GPUDevice;

namespace VoxelForge::Renderer
{

class Renderer final
{
public:
    Renderer() = delete;

    static bool Initialize(
        const RendererSpecification& specification = {});

    static void Shutdown() noexcept;

    static void BeginFrame() noexcept;
    static void EndFrame() noexcept;

    [[nodiscard]] static bool IsInitialized() noexcept;
    [[nodiscard]] static bool HasGPUDevice() noexcept;

    [[nodiscard]] static SDL_GPUDevice* GetGPUDevice() noexcept;

    [[nodiscard]] static const std::string&
    GetBackendName() noexcept;

    [[nodiscard]] static const std::string&
    GetShaderFormatsDescription() noexcept;

    [[nodiscard]] static const RendererSpecification&
    GetSpecification() noexcept;

    [[nodiscard]] static const RendererStatistics&
    GetStatistics() noexcept;

private:
    static inline RendererSpecification specification_{};
    static inline RendererStatistics statistics_{};
    static inline SDL_GPUDevice* gpuDevice_ = nullptr;
    static inline std::string backendName_ = "None";
    static inline std::string shaderFormatsDescription_ = "None";
    static inline bool initialized_ = false;
};

} // namespace VoxelForge::Renderer
