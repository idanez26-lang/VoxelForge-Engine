#include "SmartTool.h"

#include <algorithm>

namespace VoxelForge::Editor
{
SmartGeometry SmartTool::Geometry() const noexcept { return geometry_; }
SmartToolMode SmartTool::Mode() const noexcept { return mode_; }
SmartAction SmartTool::Action() const noexcept { return action_; }

void SmartTool::SetGeometry(const SmartGeometry geometry) noexcept
{
    if (geometry_ == geometry) return;
    geometry_ = geometry;
    preview_ = {};
    statistics_ = {};
}

void SmartTool::SetMode(const SmartToolMode mode) noexcept
{
    if (mode_ == mode) return;
    mode_ = mode;
    preview_ = {};
    statistics_ = {};
}

void SmartTool::SetAction(const SmartAction action) noexcept
{
    if (action_ == action) return;
    action_ = action;
    preview_ = {};
    statistics_ = {};
}

SmartBrushState& SmartTool::Brush() noexcept { return brush_; }
const SmartBrushState& SmartTool::Brush() const noexcept { return brush_; }
const SmartToolPreview& SmartTool::Preview() const noexcept { return preview_; }
const SmartToolStatistics& SmartTool::Statistics() const noexcept
{
    return statistics_;
}

void SmartTool::SetPreview(
    const SmartToolPreviewState state,
    SmartBrushRenderPlan plan) noexcept
{
    preview_ = {state, plan};
}

void SmartTool::SetStatistics(
    const std::size_t total,
    const std::size_t changed,
    const std::size_t unchanged,
    const std::size_t clipped) noexcept
{
    statistics_ = {true, total, changed, unchanged, clipped};
}

void SmartTool::ClearStatistics() noexcept { statistics_ = {}; }
float SmartTool::PreviewAlpha() const noexcept { return previewAlpha_; }
void SmartTool::SetPreviewAlpha(const float alpha) noexcept
{
    previewAlpha_ = std::clamp(alpha, 0.0F, 1.0F);
    preview_ = {};
    statistics_ = {};
}

bool SmartTool::IsOperational() const noexcept
{
    return geometry_ == SmartGeometry::Pencil &&
        (action_ == SmartAction::Add || action_ == SmartAction::Erase ||
         action_ == SmartAction::Paint);
}
} // namespace VoxelForge::Editor
