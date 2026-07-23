#pragma once

#include "BrushEngine/SmartBrushEngine.h"

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{
enum class SmartGeometry : std::uint8_t
{
    Pencil,
    Face,
    Box,
    Line,
    Cylinder,
    Diamond,
    Pattern,
    Scatter,
    Custom
};

enum class SmartAction : std::uint8_t
{
    Add,
    Erase,
    Paint,
    Replace,
    Fill,
    Smooth,
    Noise,
    Material,
    Random,
    AI
};

enum class SmartToolPreviewState : std::uint8_t
{
    Unavailable,
    Valid,
    NoChange,
    OutOfBounds
};

struct SmartToolPreview final
{
    SmartToolPreviewState State = SmartToolPreviewState::Unavailable;
    SmartBrushRenderPlan RenderPlan{};
};
struct SmartToolStatistics final
{
    bool Available = false;
    std::size_t Total = 0U;
    std::size_t Changed = 0U;
    std::size_t Unchanged = 0U;
    std::size_t Clipped = 0U;
};

// The single persistent Smart Tool state. Geometry and action decide what the
// user intends; SmartBrushEngine remains the exclusive geometry resolver.
class SmartTool final
{
public:
    [[nodiscard]] SmartGeometry Geometry() const noexcept;
    [[nodiscard]] SmartAction Action() const noexcept;
    void SetGeometry(SmartGeometry geometry) noexcept;
    void SetAction(SmartAction action) noexcept;
    [[nodiscard]] SmartBrushState& Brush() noexcept;
    [[nodiscard]] const SmartBrushState& Brush() const noexcept;
    [[nodiscard]] const SmartToolPreview& Preview() const noexcept;
    [[nodiscard]] const SmartToolStatistics& Statistics() const noexcept;
    void SetPreview(SmartToolPreviewState state,
        SmartBrushRenderPlan plan = {}) noexcept;
    void SetStatistics(std::size_t total, std::size_t changed,
        std::size_t unchanged, std::size_t clipped) noexcept;
    void ClearStatistics() noexcept;
    [[nodiscard]] bool IsOperational() const noexcept;

private:
    SmartGeometry geometry_ = SmartGeometry::Pencil;
    SmartAction action_ = SmartAction::Add;
    SmartBrushState brush_{};
    SmartToolPreview preview_{};
    SmartToolStatistics statistics_{};
};
} // namespace VoxelForge::Editor
