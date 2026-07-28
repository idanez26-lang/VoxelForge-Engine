#pragma once

#include <cstdint>

namespace VoxelForge::Editor
{
enum class ViewportDepthFormat : std::uint8_t
{
    Unavailable,
    D32Float,
    D24Unorm,
    D16Unorm
};

struct ViewportDepthFormatSupport final
{
    bool D32Float = false;
    bool D24Unorm = false;
    bool D16Unorm = false;
};

[[nodiscard]] constexpr ViewportDepthFormat SelectViewportDepthFormat(
    const ViewportDepthFormatSupport support) noexcept
{
    if (support.D32Float) return ViewportDepthFormat::D32Float;
    if (support.D24Unorm) return ViewportDepthFormat::D24Unorm;
    if (support.D16Unorm) return ViewportDepthFormat::D16Unorm;
    return ViewportDepthFormat::Unavailable;
}
} // namespace VoxelForge::Editor
