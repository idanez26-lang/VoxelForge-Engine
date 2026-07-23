#pragma once

#include "BrushEngine/SmartBrushEngine.h"

#include <cstddef>

namespace VoxelForge::Editor
{

// Shared, mode-neutral brush controls used by every Smart Brush tool.
void DrawSmartBrushOptions(SmartBrushState& state);

// Keeps preview metrics readable in narrow tool columns.
void DrawSmartBrushStatistics(
    const char* primaryLabel,
    std::size_t primaryValue,
    const char* secondaryLabel,
    std::size_t secondaryValue,
    std::size_t total,
    std::size_t clipped);

// SmartBrushEngine currently estimates shared geometry in Add mode only.
[[nodiscard]] std::size_t EstimateSmartBrushGeometry(
    const SmartBrushState& state) noexcept;

} // namespace VoxelForge::Editor
