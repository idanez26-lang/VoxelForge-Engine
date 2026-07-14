#pragma once
#include <cstdint>
namespace VoxelForge::Renderer {
struct RendererStatistics {
 std::uint64_t FrameIndex = 0;
 std::uint32_t DrawCalls = 0;
 std::uint32_t TriangleCount = 0;
 std::uint32_t VisibleObjects = 0;
 void ResetPerFrame() noexcept { DrawCalls=0; TriangleCount=0; VisibleObjects=0; }
};
}
