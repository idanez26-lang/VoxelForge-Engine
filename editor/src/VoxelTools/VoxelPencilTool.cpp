#include "VoxelPencilTool.h"

#include "Commands/Voxel/VoxelEditTransaction.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
VoxelToolResult Refused(
    const VoxelToolResultCode code,
    const Asset::Voxel::VoxelPosition position,
    const std::uint64_t revision,
    std::string error = {})
{
    return {code, false, position, revision, revision, std::move(error)};
}

const char* OperationLabel(
    const SmartAction action, const std::size_t count) noexcept
{
    switch (action)
    {
    case SmartAction::Add:
        return count == 1U ? "Add Voxel" : "Add Brush";
    case SmartAction::Erase:
        return count == 1U ? "Remove Voxel" : "Remove Brush";
    case SmartAction::Paint:
        return "Paint Brush";
    case SmartAction::Replace:
        return count == 1U ? "Replace Voxel" : "Replace Brush";
    default: return "Smart Tool";
    }
}
}

VoxelToolResult VoxelPencilTool::Apply(const VoxelPencilContext& context)
{
    if (context.Execution.Document == nullptr)
        return Refused(VoxelToolResultCode::NoDocument, {}, 0U);
    if (context.Plan == nullptr)
        return Refused(VoxelToolResultCode::Failed, {}, 0U,
            "A Smart Tool plan is required before Pencil can commit.");
    if (context.Execution.EditSession == nullptr)
        return Refused(VoxelToolResultCode::InvalidModel, {},
            context.Execution.Document->GetRevision(),
            "The Smart Tool execution session is unavailable.");

    Asset::Voxel::VoxelDocument& document = *context.Execution.Document;
    const std::uint64_t revision = document.GetRevision();
    const SmartToolRequestKey& key = context.Plan->CacheKey();
    if (key.SourceIdentity != reinterpret_cast<std::uintptr_t>(&document) ||
        key.SourceRevision != revision ||
        key.SourceGeneration != context.Execution.SourceGeneration ||
        key.SourceSubModelIndex != context.Execution.SubModelIndex)
    {
        return Refused(VoxelToolResultCode::Failed, {}, revision,
            "The Smart Tool plan is stale or targets another document context.");
    }

    const SmartAction action = context.Plan->Action();
    if (action != SmartAction::Add && action != SmartAction::Erase &&
        action != SmartAction::Paint && action != SmartAction::Replace)
        return Refused(VoxelToolResultCode::Failed, {}, revision,
            "VoxelPencilTool cannot apply this Smart Tool action.");
    const Asset::Voxel::VoxelPosition target = context.Plan->Placement().Target;
    if (document.GetModel(context.Execution.SubModelIndex) == nullptr)
        return Refused(VoxelToolResultCode::InvalidModel, target, revision);

    Voxel::VoxelModel* model = context.Execution.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* grid = model == nullptr
        ? nullptr : model->GetGrid(context.Execution.SubModelIndex);
    if (context.Execution.EditSession->ActiveVoxelDocument() != &document ||
        grid == nullptr)
    {
        return Refused(VoxelToolResultCode::InvalidModel, target, revision,
            "The editable document and compatibility grid are unavailable.");
    }

    const SmartBrushResult& brush = context.Plan->BrushResult();
    if (brush.Code == SmartBrushResultCode::OutOfBounds)
        return Refused(VoxelToolResultCode::TargetOutOfBounds, target, revision);
    if (brush.Code != SmartBrushResultCode::Valid)
        return Refused(VoxelToolResultCode::Failed, target, revision, brush.Error);

    std::vector<VoxelChange> changes;
    changes.reserve(context.Plan->Cells().size());
    // Before/After, geometry, action and palettes come only from the plan.
    // Current document reads are an integrity guard before one atomic
    // transaction, never a business or geometry recomputation.
    for (const SmartToolPlanCell& cell : context.Plan->Cells())
    {
        if (!cell.HasChange()) continue;
        const Asset::Voxel::VoxelPosition position = cell.WorldPosition;
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z));
        if (compatibilityVoxel == nullptr)
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "VoxelDocument and the editable compatibility grid diverged.");
        const auto current = document.GetVoxel(
            position, context.Execution.SubModelIndex);
        const bool documentMatchesBefore =
            current.has_value() == cell.Before.Exists &&
            (!current || current->PaletteIndex == cell.Before.PaletteIndex);
        const bool compatibilityMatchesBefore =
            compatibilityVoxel->IsOccupied() == cell.Before.Exists &&
            (!cell.Before.Exists ||
                compatibilityVoxel->ColorIndex == cell.Before.PaletteIndex);
        if (!documentMatchesBefore || !compatibilityMatchesBefore)
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "The Smart Tool plan no longer matches the document state.");
        changes.push_back({context.Execution.SubModelIndex, position,
            cell.Before.Exists, cell.Before.PaletteIndex,
            cell.After.Exists, cell.After.PaletteIndex});
    }
    if (changes.empty())
        return Refused(action == SmartAction::Add
                ? VoxelToolResultCode::TargetOccupied
                : action == SmartAction::Erase
                ? VoxelToolResultCode::TargetEmpty
                : VoxelToolResultCode::NoChange,
            target, revision);

    return ApplyChanges(context.Execution, action, target, changes);
}

VoxelToolResult VoxelPencilTool::ApplyChanges(
    const SmartToolExecutionContext& execution, const SmartAction action,
    const Asset::Voxel::VoxelPosition target, const std::span<const VoxelChange> changes)
{
    if (execution.Document == nullptr)
        return Refused(VoxelToolResultCode::NoDocument, target, 0U);
    Asset::Voxel::VoxelDocument& document = *execution.Document;
    const std::uint64_t revision = document.GetRevision();
    if (execution.EditSession == nullptr)
        return Refused(VoxelToolResultCode::InvalidModel, target, revision,
            "The Smart Tool execution session is unavailable.");
    if (changes.empty())
        return Refused(VoxelToolResultCode::NoChange, target, revision);
    Voxel::VoxelModel* model = execution.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* grid = model == nullptr
        ? nullptr : model->GetGrid(execution.SubModelIndex);
    if (execution.EditSession->ActiveVoxelDocument() != &document || grid == nullptr)
        return Refused(VoxelToolResultCode::InvalidModel, target, revision,
            "The editable document and compatibility grid are unavailable.");

    for (const VoxelChange& change : changes)
    {
        if (change.SubModelIndex != execution.SubModelIndex)
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "A stroke change targets another voxel sub-model.");
        if (change.Position.X < 0 || change.Position.Y < 0 ||
            change.Position.Z < 0)
            return Refused(VoxelToolResultCode::TargetOutOfBounds, target,
                revision, "A stroke change has a negative voxel coordinate.");
        const auto current = document.GetVoxel(change.Position, change.SubModelIndex);
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(change.Position.X),
            static_cast<std::uint32_t>(change.Position.Y),
            static_cast<std::uint32_t>(change.Position.Z));
        const bool documentMatchesBefore = current.has_value() == change.ExistedBefore &&
            (!current || current->PaletteIndex == change.PaletteIndexBefore);
        const bool compatibilityMatchesBefore = compatibilityVoxel != nullptr &&
            compatibilityVoxel->IsOccupied() == change.ExistedBefore &&
            (!change.ExistedBefore ||
                compatibilityVoxel->ColorIndex == change.PaletteIndexBefore);
        if (!documentMatchesBefore || !compatibilityMatchesBefore)
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "The accumulated Smart Tool stroke no longer matches the document.");
    }

    std::vector<VoxelChange> ownedChanges{changes.begin(), changes.end()};
    CommandResult applied;
    if (execution.History != nullptr)
    {
        const VoxelEditHistoryResult historyResult = execution.History->Execute(
            *execution.EditSession,
            VoxelEditOperation{OperationLabel(action, ownedChanges.size()),
                std::move(ownedChanges)});
        applied = historyResult ? CommandResult::Success()
                                : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        applied = ApplyVoxelChanges(*execution.EditSession,
            execution.EditSession->VoxelModelGeneration(), ownedChanges,
            VoxelChangeDirection::Forward);
    }
    if (!applied)
        return Refused(VoxelToolResultCode::Failed, target,
            document.GetRevision(), applied.Message);

    const std::uint64_t revisionAfter = document.GetRevision();
    bool synchronized = revisionAfter == revision + 1U && document.IsDirty();
    for (const VoxelChange& change : changes)
    {
        const auto voxel = document.GetVoxel(change.Position, change.SubModelIndex);
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(change.Position.X),
            static_cast<std::uint32_t>(change.Position.Y),
            static_cast<std::uint32_t>(change.Position.Z));
        synchronized &= voxel.has_value() == change.ExistsAfter &&
            (!voxel || voxel->PaletteIndex == change.PaletteIndexAfter) &&
            compatibilityVoxel != nullptr &&
            compatibilityVoxel->IsOccupied() == change.ExistsAfter &&
            (!change.ExistsAfter ||
                compatibilityVoxel->ColorIndex == change.PaletteIndexAfter);
    }
    if (!synchronized)
    {
        return {VoxelToolResultCode::Failed, true, target, revision, revisionAfter,
            "The applied Smart Tool edit failed its consistency check."};
    }
    return {VoxelToolResultCode::Applied, true, target, revision, revisionAfter, {}};
}

const char* VoxelToolResultCodeName(const VoxelToolResultCode code) noexcept
{
    switch (code)
    {
    case VoxelToolResultCode::Applied: return "Applied";
    case VoxelToolResultCode::NoChange: return "No change";
    case VoxelToolResultCode::NoDocument: return "No document";
    case VoxelToolResultCode::NoHit: return "No hit";
    case VoxelToolResultCode::TargetOutOfBounds: return "Out of bounds";
    case VoxelToolResultCode::TargetOccupied: return "Occupied";
    case VoxelToolResultCode::TargetEmpty: return "Empty";
    case VoxelToolResultCode::InvalidModel: return "Invalid model";
    case VoxelToolResultCode::InvalidPaletteIndex: return "Invalid palette";
    case VoxelToolResultCode::Blocked: return "Blocked";
    case VoxelToolResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
