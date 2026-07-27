#pragma once

#include "SmartTools/SmartToolRequest.h"
#include "SmartTools/SmartToolResult.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace VoxelForge::Editor
{
class SmartToolPlanner;

enum class SmartToolCellOperation : std::uint8_t
{
    Add,
    Erase,
    Paint,
    Replace,
    Ignore
};
enum class SmartToolPlanPreviewState : std::uint8_t
{
    Added,
    Erased,
    Painted,
    Replaced,
    Ignored,
    Clipped,
    Invalid
};

enum class SmartToolPlanCellDiagnostic : std::uint8_t
{
    None,
    NoChange,
    Overlap,
    OutOfBounds,
    InvalidState
};

enum class SmartToolPlanCellFlag : std::uint32_t
{
    None = 0U,
    ExistingVoxel = 1U << 0U,
    FinalVoxel = 1U << 1U,
    Overlap = 1U << 2U,
    OutOfBounds = 1U << 3U,
    NoChange = 1U << 4U,
    Invalid = 1U << 5U
};

[[nodiscard]] constexpr SmartToolPlanCellFlag operator|(
    const SmartToolPlanCellFlag left,
    const SmartToolPlanCellFlag right) noexcept
{
    using Value = std::underlying_type_t<SmartToolPlanCellFlag>;
    return static_cast<SmartToolPlanCellFlag>(
        static_cast<Value>(left) | static_cast<Value>(right));
}

constexpr SmartToolPlanCellFlag& operator|=(
    SmartToolPlanCellFlag& left, const SmartToolPlanCellFlag right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool HasSmartToolPlanCellFlag(
    const SmartToolPlanCellFlag value,
    const SmartToolPlanCellFlag flag) noexcept
{
    using Value = std::underlying_type_t<SmartToolPlanCellFlag>;
    return (static_cast<Value>(value) & static_cast<Value>(flag)) != 0U;
}

struct SmartToolPlanCell final
{
    std::size_t SourceOrdinal = 0U;
    Asset::Voxel::VoxelPosition LocalPosition{};
    Asset::Voxel::VoxelPosition WorldPosition{};
    SmartToolVoxelState Before{};
    SmartToolVoxelState After{};
    SmartAction Action = SmartAction::Add;
    SmartToolCellOperation Operation = SmartToolCellOperation::Ignore;
    SmartToolPlanPreviewState PreviewState = SmartToolPlanPreviewState::Invalid;
    SmartToolPlanCellDiagnostic Diagnostic =
        SmartToolPlanCellDiagnostic::InvalidState;
    SmartToolPlanCellFlag Flags = SmartToolPlanCellFlag::Invalid;
    // Materialized by SmartToolPlan at the planning boundary.  They are kept
    // after the planner-owned fields to preserve its narrow aggregate contract.
    std::array<float, 4> BeforeColor{};
    std::array<float, 4> AfterColor{};

    [[nodiscard]] bool HasChange() const noexcept { return Before != After; }
    [[nodiscard]] bool ExistingVoxel() const noexcept
    {
        return HasSmartToolPlanCellFlag(
            Flags, SmartToolPlanCellFlag::ExistingVoxel);
    }
    [[nodiscard]] bool FinalVoxel() const noexcept
    {
        return HasSmartToolPlanCellFlag(
            Flags, SmartToolPlanCellFlag::FinalVoxel);
    }
    [[nodiscard]] bool Overlap() const noexcept
    {
        return HasSmartToolPlanCellFlag(Flags, SmartToolPlanCellFlag::Overlap);
    }
    [[nodiscard]] bool OutOfBounds() const noexcept
    {
        return HasSmartToolPlanCellFlag(
            Flags, SmartToolPlanCellFlag::OutOfBounds);
    }
};

// Precomputed plan metadata consumed verbatim by the preview.  Keeping it on
// the immutable plan prevents preview code from making business decisions.
struct SmartToolPlanBounds final
{
    bool HasValue = false;
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
    Asset::Voxel::VoxelDimensions Dimensions{};
};

struct SmartToolPlanPreviewDiagnostics final
{
    bool HasOverlap = false;
    bool HasOutOfBounds = false;
    bool HasInvalid = false;
    bool HasNoChange = false;
    bool CanCommit = false;
};

struct SmartToolPlanStatistics final
{
    std::size_t Total = 0U;
    std::size_t Changed = 0U;
    std::size_t Unchanged = 0U;
    std::size_t Clipped = 0U;
    std::size_t Overlaps = 0U;
    std::size_t Invalid = 0U;

    [[nodiscard]] bool IsConsistent() const noexcept
    {
        return Total == Changed + Unchanged + Clipped + Invalid;
    }
};

struct SmartToolDiagnostic final
{
    SmartToolStatus Status = SmartToolStatus::Error;
    std::string Message;
};

// A plan is built once by SmartToolPlanner and then treated as read-only by
// preview and execution. It deliberately contains no document, viewport, or
// renderer dependency.
class SmartToolPlan final
{
public:
    [[nodiscard]] SmartGeometry Geometry() const noexcept;
    [[nodiscard]] SmartAction Action() const noexcept;
    [[nodiscard]] const SmartBrushState& BrushState() const noexcept;
    [[nodiscard]] const std::string& ActiveProfileUuid() const noexcept;
    [[nodiscard]] const SmartBrushPlacement& Placement() const noexcept;
    [[nodiscard]] const SmartBrushResult& BrushResult() const noexcept;
    [[nodiscard]] const SmartToolRequestKey& CacheKey() const noexcept;
    [[nodiscard]] std::uint64_t PlanId() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] const std::vector<SmartToolPlanCell>& Cells() const noexcept;
    [[nodiscard]] const std::vector<Asset::Voxel::VoxelPosition>& AffectedPositions() const noexcept;
    [[nodiscard]] const SmartToolPlanStatistics& Statistics() const noexcept;
    [[nodiscard]] const SmartToolPlanBounds& Bounds() const noexcept;
    [[nodiscard]] const SmartToolPlanPreviewDiagnostics& PreviewDiagnostics() const noexcept;
    [[nodiscard]] float PreviewAlpha() const noexcept;
    [[nodiscard]] bool HasChanges() const noexcept;
    [[nodiscard]] const std::vector<SmartToolDiagnostic>& Diagnostics() const noexcept;

private:
    friend class SmartToolPlanner;

    SmartToolPlan(const SmartToolRequest& request, SmartBrushResult result,
        std::uint64_t planId,
        std::vector<SmartToolPlanCell> cells,
        std::vector<SmartToolDiagnostic> diagnostics);

    SmartGeometry geometry_ = SmartGeometry::Pencil;
    SmartAction action_ = SmartAction::Add;
    SmartBrushState brushState_{};
    std::string activeProfileUuid_;
    SmartBrushPlacement placement_{};
    SmartBrushResult brushResult_{};
    SmartToolRequestKey cacheKey_{};
    std::uint64_t planId_ = 0U;
    std::uint64_t revision_ = 0U;
    float previewAlpha_ = 0.5F;
    std::vector<SmartToolPlanCell> cells_;
    std::vector<Asset::Voxel::VoxelPosition> affectedPositions_;
    SmartToolPlanStatistics statistics_{};
    SmartToolPlanBounds bounds_{};
    SmartToolPlanPreviewDiagnostics previewDiagnostics_{};
    std::vector<SmartToolDiagnostic> diagnostics_;
};

using SmartToolPlanPtr = std::shared_ptr<const SmartToolPlan>;
} // namespace VoxelForge::Editor
