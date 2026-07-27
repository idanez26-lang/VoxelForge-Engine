#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartToolLineConstraintResolver.h"

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{
// SMART-05 exposes one Smart Tool with a deliberately small set of brush modes.
// Future geometries remain separate work and are not implied by this enum.
enum class SmartToolMode : std::uint8_t
{
    SingleVoxel,
    CubeBrush,
    SphereBrush,
    CylinderBrush
};

inline constexpr int MaximumSmartToolBrushSize = MaximumSmartBrushRequestSize;

enum class SmartGeometry : std::uint8_t
{
    Pencil,
    // Cube and Sphere are local Smart Brush volumes. They intentionally do
    // not replace the legacy Box/Sphere construction tools below.
    Cube,
    Sphere,
    Face,
    Box,
    Line,
    Cylinder,
    Diamond,
    Pattern,
    Scatter,
    Custom
};

[[nodiscard]] constexpr SmartBrushShape ResolveSmartBrushShape(
    const SmartGeometry geometry,
    const SmartBrushShape pencilShape) noexcept
{
    return geometry == SmartGeometry::Cube ? SmartBrushShape::Cube
        : geometry == SmartGeometry::Sphere ? SmartBrushShape::Sphere
        : pencilShape;
}

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

// Pure transient presentation state. Time is supplied by the caller so this
// remains deterministic and independently testable.
class SmartBrushSizeFeedback final
{
public:
    static constexpr std::uint64_t DurationMilliseconds = 1250U;

    void Rearm(const int size, const SmartBrushShape shape,
        const SmartAction action, const std::uint64_t now) noexcept
    {
        size_ = size; shape_ = shape; action_ = action; expiresAt_ =
            now + DurationMilliseconds;
    }
    [[nodiscard]] bool IsVisible(const std::uint64_t now) const noexcept
    { return now < expiresAt_; }
    [[nodiscard]] int Size() const noexcept { return size_; }
    [[nodiscard]] SmartBrushShape Shape() const noexcept { return shape_; }
    [[nodiscard]] SmartAction Action() const noexcept { return action_; }

private:
    int size_ = 1;
    SmartBrushShape shape_ = SmartBrushShape::Cube;
    SmartAction action_ = SmartAction::Add;
    std::uint64_t expiresAt_ = 0U;
};

// The single persistent Smart Tool state. Geometry and action decide what the
// user intends; SmartBrushEngine remains the exclusive geometry resolver.
class SmartTool final
{
public:
    [[nodiscard]] SmartGeometry Geometry() const noexcept;
    [[nodiscard]] SmartToolMode Mode() const noexcept;
    [[nodiscard]] SmartAction Action() const noexcept;
    void SetGeometry(SmartGeometry geometry) noexcept;
    void SetMode(SmartToolMode mode) noexcept;
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
    [[nodiscard]] float PreviewAlpha() const noexcept;
    void SetPreviewAlpha(float alpha) noexcept;
    [[nodiscard]] std::optional<SmartToolLineAxis> LineConstraintAxis() const noexcept;
    void SetLineConstraintAxis(std::optional<SmartToolLineAxis> axis) noexcept;
    [[nodiscard]] bool IsOperational() const noexcept;

private:
    SmartGeometry geometry_ = SmartGeometry::Pencil;
    SmartToolMode mode_ = SmartToolMode::SingleVoxel;
    SmartAction action_ = SmartAction::Add;
    SmartBrushState brush_{};
    SmartToolPreview preview_{};
    SmartToolStatistics statistics_{};
    float previewAlpha_ = 0.5F;
    std::optional<SmartToolLineAxis> lineConstraintAxis_;
};
} // namespace VoxelForge::Editor
