#include "VoxelPencilTool.h"

#include "Commands/Voxel/VoxelEditTransaction.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <utility>

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
    Asset::Voxel::VoxelPosition adjacent{};
    if (context.DirectTarget)
    {
        if (document.GetVoxelCount() != 0U)
        {
            return Refused(
                VoxelToolResultCode::Failed, *context.DirectTarget, revision,
                "A direct Pencil target is valid only for an empty document.");
        }
        adjacent = *context.DirectTarget;
    }
    else
    {
        if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
            return Refused(VoxelToolResultCode::NoHit, {}, revision);
        const Asset::Voxel::VoxelPosition expectedAdjacent =
            CalculateAdjacent(*context.Hit);
        adjacent = context.Hit->AdjacentPosition;
        if (adjacent != expectedAdjacent)
        {
            return Refused(
                VoxelToolResultCode::Failed, adjacent, revision,
                "Voxel hit adjacency does not match its detected face.");
        }
    }
    if (context.EditSession == nullptr ||
        (!context.DirectTarget &&
         context.Hit->SubModelIndex != context.SubModelIndex) ||
        document.GetModel(context.SubModelIndex) == nullptr)
    {
        return Refused(
            VoxelToolResultCode::InvalidModel, adjacent, revision);
    }
    if (context.PaletteIndex == 0U || context.PaletteIndex > 255U)
    {
        return Refused(
            VoxelToolResultCode::InvalidPaletteIndex, adjacent, revision);
    }

    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    const bool inside = dimensions && adjacent.X >= 0 && adjacent.Y >= 0 &&
        adjacent.Z >= 0 &&
        static_cast<std::uint32_t>(adjacent.X) < dimensions->X &&
        static_cast<std::uint32_t>(adjacent.Y) < dimensions->Y &&
        static_cast<std::uint32_t>(adjacent.Z) < dimensions->Z;
    if (!inside)
    {
        return Refused(
            VoxelToolResultCode::TargetOutOfBounds, adjacent, revision);
    }
    if (document.HasVoxel(adjacent, context.SubModelIndex))
    {
        return Refused(
            VoxelToolResultCode::TargetOccupied, adjacent, revision);
    }

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
    const auto x = static_cast<std::uint32_t>(adjacent.X);
    const auto y = static_cast<std::uint32_t>(adjacent.Y);
    const auto z = static_cast<std::uint32_t>(adjacent.Z);
    const Voxel::Voxel* compatibilityVoxel = grid->Get(x, y, z);
    if (compatibilityVoxel == nullptr || compatibilityVoxel->IsOccupied())
    {
        return Refused(
            VoxelToolResultCode::Failed, adjacent, revision,
            "VoxelDocument and the editable compatibility grid diverged.");
    }

    const Voxel::Voxel replacement{
        static_cast<std::uint8_t>(context.PaletteIndex),
        Voxel::Voxel::OccupiedFlag};
    CommandResult applied;
    if (context.History != nullptr)
    {
        const VoxelEditHistoryResult historyResult = context.History->Execute(
            *context.EditSession,
            VoxelEditOperation{
                "Add Voxel",
                {VoxelChange{
                    context.SubModelIndex,
                    adjacent,
                    false,
                    0U,
                    true,
                    static_cast<std::uint8_t>(context.PaletteIndex)}}});
        applied = historyResult
            ? CommandResult::Success()
            : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        applied = ApplyVoxelEdit(
            *context.EditSession,
            context.EditSession->VoxelModelGeneration(),
            x, y, z, *compatibilityVoxel, replacement,
            context.SubModelIndex);
    }
    if (!applied)
    {
        return Refused(
            VoxelToolResultCode::Failed, adjacent,
            document.GetRevision(), applied.Message);
    }

    const auto voxel = document.GetVoxel(adjacent, context.SubModelIndex);
    const Voxel::Voxel* synchronizedVoxel = grid->Get(x, y, z);
    const std::uint64_t revisionAfter = document.GetRevision();
    if (!voxel || voxel->PaletteIndex != context.PaletteIndex ||
        synchronizedVoxel == nullptr || !synchronizedVoxel->IsOccupied() ||
        synchronizedVoxel->ColorIndex != context.PaletteIndex ||
        revisionAfter != revision + 1U || !document.IsDirty())
    {
        return {
            VoxelToolResultCode::Failed,
            true,
            adjacent,
            revision,
            revisionAfter,
            "The applied Pencil edit failed its consistency check."};
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
    case VoxelToolResultCode::InvalidModel: return "Invalid model";
    case VoxelToolResultCode::InvalidPaletteIndex: return "Invalid palette";
    case VoxelToolResultCode::Blocked: return "Blocked";
    case VoxelToolResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
