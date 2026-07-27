#include "SmartTools/SmartToolPlan.h"

#include <utility>

namespace VoxelForge::Editor
{
SmartToolPlan::SmartToolPlan(
    const SmartToolRequest& request, SmartBrushResult result,
    const std::uint64_t planId,
    std::vector<SmartToolPlanCell> cells,
    std::vector<SmartToolDiagnostic> diagnostics)
    : geometry_(request.Geometry), action_(request.Action),
      brushState_(request.BrushRequest.State),
      activeProfileUuid_(request.ActiveProfileUuid),
      placement_(request.BrushRequest.Placement),
      brushResult_(std::move(result)), cacheKey_(MakeSmartToolRequestKey(request)),
      planId_(planId), revision_(request.SourceRevision), cells_(std::move(cells)),
      diagnostics_(std::move(diagnostics))
{
    statistics_.Total = cells_.size();
    for (const SmartToolPlanCell& cell : cells_)
    {
        if (cell.OutOfBounds()) ++statistics_.Clipped;
        else if (HasSmartToolPlanCellFlag(
                     cell.Flags, SmartToolPlanCellFlag::Invalid))
            ++statistics_.Invalid;
        else if (cell.HasChange()) ++statistics_.Changed;
        else ++statistics_.Unchanged;
        if (cell.Overlap()) ++statistics_.Overlaps;
    }
}

SmartGeometry SmartToolPlan::Geometry() const noexcept { return geometry_; }
SmartAction SmartToolPlan::Action() const noexcept { return action_; }
const SmartBrushState& SmartToolPlan::BrushState() const noexcept { return brushState_; }
const std::string& SmartToolPlan::ActiveProfileUuid() const noexcept
{ return activeProfileUuid_; }
const SmartBrushPlacement& SmartToolPlan::Placement() const noexcept { return placement_; }
const SmartBrushResult& SmartToolPlan::BrushResult() const noexcept { return brushResult_; }
const SmartToolRequestKey& SmartToolPlan::CacheKey() const noexcept { return cacheKey_; }
std::uint64_t SmartToolPlan::PlanId() const noexcept { return planId_; }
std::uint64_t SmartToolPlan::Revision() const noexcept { return revision_; }
const std::vector<SmartToolPlanCell>& SmartToolPlan::Cells() const noexcept
{ return cells_; }
const SmartToolPlanStatistics& SmartToolPlan::Statistics() const noexcept
{ return statistics_; }
bool SmartToolPlan::HasChanges() const noexcept
{ return statistics_.Changed != 0U; }
const std::vector<SmartToolDiagnostic>& SmartToolPlan::Diagnostics() const noexcept
{ return diagnostics_; }
} // namespace VoxelForge::Editor
