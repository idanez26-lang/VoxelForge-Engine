#pragma once
#include <cstdint>
namespace VoxelForge::Renderer {
struct RenderTargetSpecification {
 std::uint32_t Width = 1280;
 std::uint32_t Height = 720;
 bool HasColorAttachment = true;
 bool HasDepthAttachment = true;
 bool IsSampled = true;
};
}
