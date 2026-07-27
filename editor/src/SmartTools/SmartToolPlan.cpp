#include "SmartTools/SmartToolPlan.h"

#include <utility>
#include <algorithm>

namespace VoxelForge::Editor
{
SmartToolPlan::SmartToolPlan(
    const SmartToolRequest& request, SmartBrushResult result,
    const std::uint64_t planId,
    std::vector<SmartToolPlanCell> cells,
    std::vector<SmartToolDiagnostic> diagnostics)
    : geometry_(request.Geometry), mode_(request.Mode), action_(request.Action),
      brushState_(request.BrushRequest.State),
      activeProfileUuid_(request.ActiveProfileUuid),
      placement_(request.BrushRequest.Placement),
      brushResult_(std::move(result)), cacheKey_(MakeSmartToolRequestKey(request)),
      planId_(planId), revision_(request.SourceRevision), cells_(std::move(cells)),
      previewAlpha_(request.PreviewAlpha), diagnostics_(std::move(diagnostics))
{
    statistics_.Total = cells_.size();
    affectedPositions_.reserve(cells_.size());
    for (SmartToolPlanCell& cell : cells_)
    {
        if (request.HasPaletteColors)
        {
            cell.BeforeColor = request.PaletteColors[cell.Before.PaletteIndex];
            cell.AfterColor = request.PaletteColors[cell.After.PaletteIndex];
        }
        else
        {
            // Deterministic fallback for domain-only callers that do not own a
            // document palette.  Production requests always carry a snapshot.
            const float before = static_cast<float>(cell.Before.PaletteIndex) / 255.0F;
            const float after = static_cast<float>(cell.After.PaletteIndex) / 255.0F;
            cell.BeforeColor = {before, before, before, cell.Before.Exists ? 1.0F : 0.0F};
            cell.AfterColor = {after, after, after, cell.After.Exists ? 1.0F : 0.0F};
        }
        if (cell.OutOfBounds()) ++statistics_.Clipped;
        else if (HasSmartToolPlanCellFlag(
                     cell.Flags, SmartToolPlanCellFlag::Invalid))
            ++statistics_.Invalid;
        else if (cell.HasChange()) ++statistics_.Changed;
        else ++statistics_.Unchanged;
        if (cell.HasChange()) affectedPositions_.push_back(cell.WorldPosition);
        if (cell.Overlap()) ++statistics_.Overlaps;

        previewDiagnostics_.HasOverlap |= cell.Overlap();
        previewDiagnostics_.HasOutOfBounds |= cell.OutOfBounds();
        previewDiagnostics_.HasInvalid |= HasSmartToolPlanCellFlag(
            cell.Flags, SmartToolPlanCellFlag::Invalid);
        previewDiagnostics_.HasNoChange |= HasSmartToolPlanCellFlag(
            cell.Flags, SmartToolPlanCellFlag::NoChange);
        // Bounds describe the exact ghost list, including clipped/invalid
        // cells that the preview deliberately displays as diagnostics.
        if (!bounds_.HasValue)
        {
            bounds_.HasValue = true;
            bounds_.Minimum = cell.WorldPosition;
            bounds_.Maximum = cell.WorldPosition;
        }
        else
        {
            bounds_.Minimum.X = std::min(bounds_.Minimum.X, cell.WorldPosition.X);
            bounds_.Minimum.Y = std::min(bounds_.Minimum.Y, cell.WorldPosition.Y);
            bounds_.Minimum.Z = std::min(bounds_.Minimum.Z, cell.WorldPosition.Z);
            bounds_.Maximum.X = std::max(bounds_.Maximum.X, cell.WorldPosition.X);
            bounds_.Maximum.Y = std::max(bounds_.Maximum.Y, cell.WorldPosition.Y);
            bounds_.Maximum.Z = std::max(bounds_.Maximum.Z, cell.WorldPosition.Z);
        }
    }
    if (bounds_.HasValue)
    {
        bounds_.Dimensions = {static_cast<std::uint32_t>(
            bounds_.Maximum.X - bounds_.Minimum.X + 1),
            static_cast<std::uint32_t>(bounds_.Maximum.Y - bounds_.Minimum.Y + 1),
            static_cast<std::uint32_t>(bounds_.Maximum.Z - bounds_.Minimum.Z + 1)};
    }
    previewDiagnostics_.CanCommit = statistics_.Changed != 0U &&
        !previewDiagnostics_.HasInvalid;
}

SmartGeometry SmartToolPlan::Geometry() const noexcept { return geometry_; }
std::optional<SmartToolMode> SmartToolPlan::Mode() const noexcept { return mode_; }
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
const std::vector<Asset::Voxel::VoxelPosition>& SmartToolPlan::AffectedPositions() const noexcept
{ return affectedPositions_; }
const SmartToolPlanStatistics& SmartToolPlan::Statistics() const noexcept
{ return statistics_; }
const SmartToolPlanBounds& SmartToolPlan::Bounds() const noexcept { return bounds_; }
const SmartToolPlanPreviewDiagnostics& SmartToolPlan::PreviewDiagnostics() const noexcept
{ return previewDiagnostics_; }
float SmartToolPlan::PreviewAlpha() const noexcept { return previewAlpha_; }
bool SmartToolPlan::HasChanges() const noexcept
{ return statistics_.Changed != 0U; }
const std::vector<SmartToolDiagnostic>& SmartToolPlan::Diagnostics() const noexcept
{ return diagnostics_; }
} // namespace VoxelForge::Editor
