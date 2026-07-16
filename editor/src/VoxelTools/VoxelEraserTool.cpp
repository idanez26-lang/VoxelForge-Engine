#include "VoxelEraserTool.h"

#include "Commands/Voxel/VoxelEditTransaction.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{
VoxelEraserResult Refused(
    const VoxelEraserResultCode code,
    const Asset::Voxel::VoxelPosition position,
    const std::size_t modelIndex,
    const std::uint64_t revision,
    std::string error = {})
{
    return {
        code, false, position, 0U, modelIndex,
        revision, revision, std::move(error)};
}
}

VoxelEraserResult VoxelEraserTool::Apply(
    const VoxelEraserContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelEraserResultCode::NoDocument, {},
            context.SubModelIndex, 0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelEraserResultCode::Blocked, {},
            context.SubModelIndex, revision);
    if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
        return Refused(VoxelEraserResultCode::NoHit, {},
            context.SubModelIndex, revision);

    const Asset::Voxel::VoxelPosition position{
        static_cast<std::int32_t>(context.Hit->Coordinates.X),
        static_cast<std::int32_t>(context.Hit->Coordinates.Y),
        static_cast<std::int32_t>(context.Hit->Coordinates.Z)};
    if (context.EditSession == nullptr ||
        context.Hit->SubModelIndex != context.SubModelIndex ||
        document.GetModel(context.SubModelIndex) == nullptr)
    {
        return Refused(VoxelEraserResultCode::InvalidModel, position,
            context.SubModelIndex, revision);
    }
    if (context.Hit->DocumentRevision != revision)
    {
        return Refused(
            VoxelEraserResultCode::TargetMissing,
            position,
            context.SubModelIndex,
            revision,
            "The voxel hit belongs to an older document revision.");
    }
    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    const bool inside = dimensions && position.X >= 0 && position.Y >= 0 &&
        position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions->X &&
        static_cast<std::uint32_t>(position.Y) < dimensions->Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions->Z;
    const auto documentVoxel = inside
        ? document.GetVoxel(position, context.SubModelIndex)
        : std::nullopt;
    if (!documentVoxel)
    {
        return Refused(VoxelEraserResultCode::TargetMissing, position,
            context.SubModelIndex, revision);
    }

    Voxel::VoxelModel* model = context.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* grid = model == nullptr
        ? nullptr : model->GetGrid(context.SubModelIndex);
    if (context.EditSession->ActiveVoxelDocument() != &document ||
        grid == nullptr)
    {
        return Refused(VoxelEraserResultCode::InvalidModel, position,
            context.SubModelIndex, revision,
            "The editable document and compatibility grid are unavailable.");
    }
    const auto x = static_cast<std::uint32_t>(position.X);
    const auto y = static_cast<std::uint32_t>(position.Y);
    const auto z = static_cast<std::uint32_t>(position.Z);
    const Voxel::Voxel* compatibilityVoxel = grid->Get(x, y, z);
    if (compatibilityVoxel == nullptr || !compatibilityVoxel->IsOccupied() ||
        compatibilityVoxel->ColorIndex != documentVoxel->PaletteIndex)
    {
        return Refused(VoxelEraserResultCode::Failed, position,
            context.SubModelIndex, revision,
            "VoxelDocument and the editable compatibility grid diverged.");
    }

    const std::uint8_t removedPaletteIndex = documentVoxel->PaletteIndex;
    CommandResult applied;
    if (context.History != nullptr)
    {
        const VoxelEditHistoryResult historyResult = context.History->Execute(
            *context.EditSession,
            VoxelEditOperation{
                "Remove Voxel",
                {VoxelChange{
                    context.SubModelIndex,
                    position,
                    true,
                    removedPaletteIndex,
                    false,
                    0U}}});
        applied = historyResult
            ? CommandResult::Success()
            : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        applied = ApplyVoxelEdit(
            *context.EditSession,
            context.EditSession->VoxelModelGeneration(),
            x, y, z, *compatibilityVoxel, Voxel::Voxel{},
            context.SubModelIndex);
    }
    if (!applied)
    {
        return Refused(VoxelEraserResultCode::Failed, position,
            context.SubModelIndex, document.GetRevision(), applied.Message);
    }

    const Voxel::Voxel* synchronizedVoxel = grid->Get(x, y, z);
    const std::uint64_t revisionAfter = document.GetRevision();
    if (document.HasVoxel(position, context.SubModelIndex) ||
        synchronizedVoxel == nullptr || synchronizedVoxel->IsOccupied() ||
        revisionAfter != revision + 1U || !document.IsDirty())
    {
        return {
            VoxelEraserResultCode::Failed,
            true,
            position,
            removedPaletteIndex,
            context.SubModelIndex,
            revision,
            revisionAfter,
            "The applied Eraser edit failed its consistency check."};
    }
    return {
        VoxelEraserResultCode::Applied,
        true,
        position,
        removedPaletteIndex,
        context.SubModelIndex,
        revision,
        revisionAfter,
        {}};
}

const char* VoxelEraserResultCodeName(
    const VoxelEraserResultCode code) noexcept
{
    switch (code)
    {
    case VoxelEraserResultCode::Applied: return "Applied";
    case VoxelEraserResultCode::NoDocument: return "No document";
    case VoxelEraserResultCode::NoHit: return "No hit";
    case VoxelEraserResultCode::TargetMissing: return "Target missing";
    case VoxelEraserResultCode::InvalidModel: return "Invalid model";
    case VoxelEraserResultCode::Blocked: return "Blocked";
    case VoxelEraserResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
