#pragma once
#include <cstdint>
#include <string_view>
namespace VoxelForge::Renderer {
enum class RendererAPI : std::uint8_t { None = 0, SDLGPU };
[[nodiscard]] constexpr std::string_view ToString(RendererAPI api) noexcept {
 switch(api){ case RendererAPI::SDLGPU: return "SDL GPU"; case RendererAPI::None: default: return "None"; }
}
}
