#pragma once

#include "SmartTools/SmartToolRequest.h"
#include "SmartTools/SmartToolResult.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{
class SmartToolPlanner;

enum class SmartToolCellOperation : std::uint8_t { Add, Erase, Ignore };
enum class SmartToolPlanPreviewState : std::uint8_t
{
    Added,
    Erased,
    Ignored,
    Clipped,
    Invalid
};

struct SmartToolPlanCell final
{
    Asset::Voxel::VoxelPosition Position{};
    SmartToolCellOperation Operation = SmartToolCellOperation::Ignore;
    std::size_t PaletteIndex = 0U;
    SmartToolPlanPreviewState PreviewState = SmartToolPlanPreviewState::Invalid;
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
    [[nodiscard]] const std::vector<SmartToolDiagnostic>& Diagnostics() const noexcept;

private:
    friend class SmartToolPlanner;

    SmartToolPlan(const SmartToolRequest& request, SmartBrushResult result,
        std::uint64_t planId, std::uint64_t revision,
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
    std::vector<SmartToolPlanCell> cells_;
    std::vector<SmartToolDiagnostic> diagnostics_;
};

using SmartToolPlanPtr = std::shared_ptr<const SmartToolPlan>;
} // namespace VoxelForge::Editor
