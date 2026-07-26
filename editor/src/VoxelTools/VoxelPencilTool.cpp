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
}

VoxelToolResult VoxelPencilTool::Apply(const VoxelPencilContext& context)
{
    if (context.Plan == nullptr)
        return Refused(VoxelToolResultCode::Failed, {}, 0U,
            "A Smart Tool plan is required before Pencil can commit.");
    if (context.Execution.Document == nullptr)
        return Refused(VoxelToolResultCode::NoDocument, {}, 0U);
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

    const bool erasing = context.Plan->Action() == SmartAction::Erase;
    if (!erasing && context.Plan->Action() != SmartAction::Add)
        return Refused(VoxelToolResultCode::Failed, {}, revision,
            "VoxelPencilTool only applies Add and Erase actions.");
    const Asset::Voxel::VoxelPosition target = context.Plan->Placement().Target;
    if (document.GetModel(context.Execution.SubModelIndex) == nullptr)
        return Refused(VoxelToolResultCode::InvalidModel, target, revision);
    if (!erasing && (context.Plan->BrushState().PaletteIndex == 0U ||
        context.Plan->BrushState().PaletteIndex > 255U))
        return Refused(VoxelToolResultCode::InvalidPaletteIndex, target, revision);

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
    // Geometry, occupancy classification, and palette selection come only
    // from the plan. The remaining reads are an integrity guard before one
    // atomic transaction, never a geometry recomputation.
    for (const SmartToolPlanCell& cell : context.Plan->Cells())
    {
        if (cell.Operation == SmartToolCellOperation::Ignore) continue;
        const Asset::Voxel::VoxelPosition position = cell.Position;
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z));
        if (compatibilityVoxel == nullptr)
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "VoxelDocument and the editable compatibility grid diverged.");
        const bool documentOccupied =
            document.HasVoxel(position, context.Execution.SubModelIndex);
        if (documentOccupied != compatibilityVoxel->IsOccupied())
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "VoxelDocument and the editable compatibility grid diverged.");
        if ((cell.Operation == SmartToolCellOperation::Add && documentOccupied) ||
            (cell.Operation == SmartToolCellOperation::Erase && !documentOccupied))
        {
            return Refused(VoxelToolResultCode::Failed, target, revision,
                "The Smart Tool plan no longer matches the document state.");
        }

        if (cell.Operation == SmartToolCellOperation::Erase)
        {
            const auto voxel = document.GetVoxel(
                position, context.Execution.SubModelIndex);
            if (!voxel)
                return Refused(VoxelToolResultCode::Failed, target, revision,
                    "The Smart Tool erase plan changed during validation.");
            changes.push_back({context.Execution.SubModelIndex, position, true,
                voxel->PaletteIndex, false, 0U});
        }
        else if (cell.Operation == SmartToolCellOperation::Add)
        {
            changes.push_back({context.Execution.SubModelIndex, position,
                false, 0U, true, static_cast<std::uint8_t>(cell.PaletteIndex)});
        }
    }
    if (changes.empty())
        return Refused(erasing ? VoxelToolResultCode::TargetEmpty :
            VoxelToolResultCode::TargetOccupied, target, revision);

    CommandResult applied;
    if (context.Execution.History != nullptr)
    {
        const VoxelEditHistoryResult historyResult = context.Execution.History->Execute(
            *context.Execution.EditSession,
            VoxelEditOperation{
                erasing ? (changes.size() == 1U ? "Remove Voxel" : "Remove Brush")
                        : (changes.size() == 1U ? "Add Voxel" : "Add Brush"),
                std::move(changes)});
        applied = historyResult ? CommandResult::Success()
                                : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        applied = ApplyVoxelChanges(*context.Execution.EditSession,
            context.Execution.EditSession->VoxelModelGeneration(), changes,
            VoxelChangeDirection::Forward);
    }
    if (!applied)
        return Refused(VoxelToolResultCode::Failed, target,
            document.GetRevision(), applied.Message);

    const std::uint64_t revisionAfter = document.GetRevision();
    bool synchronized = revisionAfter == revision + 1U && document.IsDirty();
    for (const SmartToolPlanCell& cell : context.Plan->Cells())
    {
        if (cell.Operation == SmartToolCellOperation::Ignore) continue;
        const auto voxel = document.GetVoxel(
            cell.Position, context.Execution.SubModelIndex);
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(cell.Position.X),
            static_cast<std::uint32_t>(cell.Position.Y),
            static_cast<std::uint32_t>(cell.Position.Z));
        synchronized &= erasing
            ? !voxel && compatibilityVoxel != nullptr &&
                !compatibilityVoxel->IsOccupied()
            : voxel && voxel->PaletteIndex == cell.PaletteIndex &&
                compatibilityVoxel != nullptr && compatibilityVoxel->IsOccupied() &&
                compatibilityVoxel->ColorIndex == cell.PaletteIndex;
    }
    if (!synchronized)
    {
        return {VoxelToolResultCode::Failed, true, target, revision, revisionAfter,
            erasing ? "The applied Smart Erase edit failed its consistency check."
                    : "The applied Pencil edit failed its consistency check."};
    }
    return {VoxelToolResultCode::Applied, true, target, revision, revisionAfter, {}};
}

const char* VoxelToolResultCodeName(const VoxelToolResultCode code) noexcept
{
    switch (code)
    {
    case VoxelToolResultCode::Applied: return "Applied";
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
