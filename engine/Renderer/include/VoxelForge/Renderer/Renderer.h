#pragma once
#include "VoxelForge/Renderer/RendererSpecification.h"
#include "VoxelForge/Renderer/RendererStatistics.h"
namespace VoxelForge::Renderer {
class Renderer final {
public:
 Renderer() = delete;
 static bool Initialize(const RendererSpecification& specification = {});
 static void Shutdown() noexcept;
 static void BeginFrame() noexcept;
 static void EndFrame() noexcept;
 [[nodiscard]] static bool IsInitialized() noexcept;
 [[nodiscard]] static const RendererSpecification& GetSpecification() noexcept;
 [[nodiscard]] static const RendererStatistics& GetStatistics() noexcept;
private:
 static inline RendererSpecification specification_{};
 static inline RendererStatistics statistics_{};
 static inline bool initialized_ = false;
};
}
