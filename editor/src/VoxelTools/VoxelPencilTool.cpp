#include "VoxelPencilTool.h"

#include "Commands/Voxel/VoxelEditTransaction.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

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

Asset::Voxel::VoxelPosition CalculateAdjacent(
    const VoxelRaycastHit& hit) noexcept
{
    const Asset::Voxel::VoxelPosition normal =
        VoxelHitFaceIntegerNormal(hit.Face);
    return {
        static_cast<std::int32_t>(hit.Coordinates.X) + normal.X,
        static_cast<std::int32_t>(hit.Coordinates.Y) + normal.Y,
        static_cast<std::int32_t>(hit.Coordinates.Z) + normal.Z};
}
}

VoxelToolResult VoxelPencilTool::Apply(const VoxelPencilContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelToolResultCode::NoDocument, {}, 0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelToolResultCode::Blocked, {}, revision);
    const bool erasing = context.State.Mode == SmartBrushMode::Erase;
    if (!erasing && context.State.Mode != SmartBrushMode::Add)
        return Refused(VoxelToolResultCode::Failed, {}, revision,
            "VoxelPencilTool only applies Add and Erase actions.");
    Asset::Voxel::VoxelPosition adjacent{};
    Asset::Voxel::VoxelPosition placementNormal{0, 1, 0};
    if (context.WorkplaneTarget)
    {
        if (erasing)
            return Refused(VoxelToolResultCode::NoHit, {}, revision);
        adjacent = *context.WorkplaneTarget;
    }
    else
    {
        if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
            return Refused(VoxelToolResultCode::NoHit, {}, revision);
        if (erasing)
        {
            if (context.Hit->DocumentRevision != revision)
                return Refused(VoxelToolResultCode::NoHit, {}, revision,
                    "The Smart Erase target belongs to an older document revision.");
            adjacent = {
                static_cast<std::int32_t>(context.Hit->Coordinates.X),
                static_cast<std::int32_t>(context.Hit->Coordinates.Y),
                static_cast<std::int32_t>(context.Hit->Coordinates.Z)};
            placementNormal = VoxelHitFaceIntegerNormal(context.Hit->Face);
        }
        else
        {
            const Asset::Voxel::VoxelPosition expectedAdjacent =
                CalculateAdjacent(*context.Hit);
            adjacent = context.Hit->AdjacentPosition;
            if (adjacent != expectedAdjacent)
            {
                return Refused(
                    VoxelToolResultCode::Failed, adjacent, revision,
                    "Voxel hit adjacency does not match its detected face.");
            }
            placementNormal = VoxelHitFaceIntegerNormal(context.Hit->Face);
        }
    }
    if (context.EditSession == nullptr ||
        (!context.WorkplaneTarget &&
         context.Hit->SubModelIndex != context.SubModelIndex) ||
        document.GetModel(context.SubModelIndex) == nullptr)
    {
        return Refused(
            VoxelToolResultCode::InvalidModel, adjacent, revision);
    }
    if (!erasing && (context.State.PaletteIndex == 0U ||
        context.State.PaletteIndex > 255U))
        return Refused(VoxelToolResultCode::InvalidPaletteIndex, adjacent, revision);
    Voxel::VoxelModel* model = context.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* grid = model == nullptr
        ? nullptr : model->GetGrid(context.SubModelIndex);
    if (context.EditSession->ActiveVoxelDocument() != &document ||
        grid == nullptr)
    {
        return Refused(
            VoxelToolResultCode::InvalidModel, adjacent, revision,
            "The editable document and compatibility grid are unavailable.");
    }
    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    if (!dimensions)
    {
        return Refused(
            VoxelToolResultCode::Failed, adjacent, revision,
            "The editable document dimensions are unavailable.");
    }
    const SmartBrushResult brush = SmartBrushEngine::Resolve({
        *dimensions,
        context.State,
        {adjacent, placementNormal},
        [&document, subModelIndex = context.SubModelIndex](
            const Asset::Voxel::VoxelPosition position)
        {
            return document.HasVoxel(position, subModelIndex);
        }});
    if (brush.Code == SmartBrushResultCode::OutOfBounds)
        return Refused(VoxelToolResultCode::TargetOutOfBounds, adjacent, revision);
    if (brush.Code == SmartBrushResultCode::Unsupported)
        return Refused(VoxelToolResultCode::Failed, adjacent, revision, brush.Error);
    if (brush.Code != SmartBrushResultCode::Valid)
        return Refused(VoxelToolResultCode::Failed, adjacent, revision, brush.Error);

    std::vector<VoxelChange> changes;
    changes.reserve(erasing ? brush.ExistingPositions.size() :
        brush.AddablePositions.size());
    // The engine has already validated the complete plan. Verify that the
    // compatibility grid agrees before creating one atomic transaction.
    for (const Asset::Voxel::VoxelPosition position : brush.Positions)
    {
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z));
        if (compatibilityVoxel == nullptr)
        {
            return Refused(
                VoxelToolResultCode::Failed, adjacent, revision,
                "VoxelDocument and the editable compatibility grid diverged.");
        }
        const bool documentOccupied =
            document.HasVoxel(position, context.SubModelIndex);
        if (documentOccupied)
        {
            if (!compatibilityVoxel->IsOccupied())
            {
                return Refused(
                    VoxelToolResultCode::Failed, adjacent, revision,
                    "VoxelDocument and the editable compatibility grid diverged.");
            }
            continue;
        }
        if (compatibilityVoxel->IsOccupied())
        {
            return Refused(
                VoxelToolResultCode::Failed, adjacent, revision,
                "VoxelDocument and the editable compatibility grid diverged.");
        }
    }
    const std::vector<Asset::Voxel::VoxelPosition>& changedPositions = erasing
        ? brush.ExistingPositions : brush.AddablePositions;
    for (const Asset::Voxel::VoxelPosition position : changedPositions)
    {
        if (erasing)
        {
            const auto voxel = document.GetVoxel(position, context.SubModelIndex);
            if (!voxel)
                return Refused(VoxelToolResultCode::Failed, adjacent, revision,
                    "The Smart Brush erase plan changed during validation.");
            changes.push_back({context.SubModelIndex, position, true,
                voxel->PaletteIndex, false, 0U});
        }
        else
        {
            changes.push_back({context.SubModelIndex, position, false, 0U, true,
                static_cast<std::uint8_t>(context.State.PaletteIndex)});
        }
    }
    if (changes.empty())
        return Refused(erasing ? VoxelToolResultCode::TargetEmpty :
            VoxelToolResultCode::TargetOccupied, adjacent, revision);

    CommandResult applied;
    if (context.History != nullptr)
    {
        const VoxelEditHistoryResult historyResult = context.History->Execute(
            *context.EditSession,
            VoxelEditOperation{
                erasing ? (changes.size() == 1U ? "Remove Voxel" :
                    "Remove Brush") : (changes.size() == 1U ? "Add Voxel" :
                    "Add Brush"),
                std::move(changes)});
        applied = historyResult
            ? CommandResult::Success()
            : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        applied = ApplyVoxelChanges(
            *context.EditSession,
            context.EditSession->VoxelModelGeneration(),
            changes,
            VoxelChangeDirection::Forward);
    }
    if (!applied)
    {
        return Refused(
            VoxelToolResultCode::Failed, adjacent,
            document.GetRevision(), applied.Message);
    }

    const std::uint64_t revisionAfter = document.GetRevision();
    bool synchronized = revisionAfter == revision + 1U && document.IsDirty();
    for (const Asset::Voxel::VoxelPosition position : changedPositions)
    {
        const auto voxel = document.GetVoxel(position, context.SubModelIndex);
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z));
        synchronized &= erasing
            ? !voxel && compatibilityVoxel != nullptr &&
                !compatibilityVoxel->IsOccupied()
            : voxel && voxel->PaletteIndex == context.State.PaletteIndex &&
                compatibilityVoxel != nullptr && compatibilityVoxel->IsOccupied() &&
                compatibilityVoxel->ColorIndex == context.State.PaletteIndex;
    }
    if (!synchronized)
    {
        return {
            VoxelToolResultCode::Failed,
            true,
            adjacent,
            revision,
            revisionAfter,
            erasing ? "The applied Smart Erase edit failed its consistency check."
                    : "The applied Pencil edit failed its consistency check."};
    }
    return {
        VoxelToolResultCode::Applied,
        true,
        adjacent,
        revision,
        revisionAfter,
        {}};
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
