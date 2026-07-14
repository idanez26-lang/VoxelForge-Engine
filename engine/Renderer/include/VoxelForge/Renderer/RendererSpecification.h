#pragma once
#include "VoxelForge/Renderer/RendererAPI.h"
#include <cstdint>
#include <string>
namespace VoxelForge::Renderer {
struct RendererSpecification {
 RendererAPI API = RendererAPI::SDLGPU;
 std::string ApplicationName = "VoxelForge Studio";
 std::uint32_t FramesInFlight = 2;
 bool EnableValidation = true;
 bool EnableVSync = true;
};
}
