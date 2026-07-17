#include "VoxelFillService.h"

#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <array>
#include <exception>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
VoxelFillResult Refused(
    const VoxelFillResultCode code,
    const Asset::Voxel::VoxelPosition position,
    const std::uint64_t revision,
    std::string error = {})
{
    return {
        code, false, position, 0U, 0U, 0U,
        revision, revision, std::move(error)};
}

std::uint64_t PositionKey(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return static_cast<std::uint64_t>(position.X) +
        static_cast<std::uint64_t>(dimensions.X) *
        (static_cast<std::uint64_t>(position.Y) +
         static_cast<std::uint64_t>(dimensions.Y) *
         static_cast<std::uint64_t>(position.Z));
}

bool IsInside(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions.X &&
        static_cast<std::uint32_t>(position.Y) < dimensions.Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions.Z;
}
}

VoxelFillResult VoxelFillService::Apply(const VoxelFillContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelFillResultCode::NoDocument, {},
            0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelFillResultCode::Blocked, {},
            revision);
    if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
        return Refused(VoxelFillResultCode::NoHit, {},
            revision);

    const Asset::Voxel::VoxelPosition start{
        static_cast<std::int32_t>(context.Hit->Coordinates.X),
        static_cast<std::int32_t>(context.Hit->Coordinates.Y),
        static_cast<std::int32_t>(context.Hit->Coordinates.Z)};
    if (context.PaletteIndex == 0U || context.PaletteIndex > 255U)
        return Refused(VoxelFillResultCode::InvalidPaletteIndex, start,
            revision);
    if (context.EditSession == nullptr || context.History == nullptr ||
        context.Hit->SubModelIndex != context.SubModelIndex ||
        context.Hit->DocumentRevision != revision)
    {
        return Refused(VoxelFillResultCode::InvalidModel, start,
            revision,
            "The Fill target does not belong to the active document state.");
    }
    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    if (!dimensions || !IsInside(start, *dimensions))
        return Refused(VoxelFillResultCode::InvalidModel, start,
            revision);
    const auto startVoxel = document.GetVoxel(start, context.SubModelIndex);
    if (!startVoxel)
        return Refused(VoxelFillResultCode::TargetMissing, start,
            revision);

    const auto replacement = static_cast<std::uint8_t>(context.PaletteIndex);
    const std::uint8_t previous = startVoxel->PaletteIndex;
    if (previous == replacement)
        return Refused(VoxelFillResultCode::SameColor, start,
            revision);

    Voxel::VoxelModel* compatibilityModel =
        context.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* compatibilityGrid = compatibilityModel == nullptr
        ? nullptr : compatibilityModel->GetGrid(context.SubModelIndex);
    if (context.EditSession->ActiveVoxelDocument() != &document ||
        compatibilityGrid == nullptr)
    {
        return Refused(VoxelFillResultCode::InvalidModel, start,
            revision);
    }

    static constexpr std::array Neighbors{
        Asset::Voxel::VoxelPosition{1, 0, 0},
        Asset::Voxel::VoxelPosition{-1, 0, 0},
        Asset::Voxel::VoxelPosition{0, 1, 0},
        Asset::Voxel::VoxelPosition{0, -1, 0},
        Asset::Voxel::VoxelPosition{0, 0, 1},
        Asset::Voxel::VoxelPosition{0, 0, -1}};
    std::vector<Asset::Voxel::VoxelPosition> pending;
    std::unordered_set<std::uint64_t> visited;
    std::vector<VoxelChange> changes;
    try
    {
        pending.push_back(start);
        visited.insert(PositionKey(start, *dimensions));
        while (!pending.empty())
        {
            const Asset::Voxel::VoxelPosition position = pending.back();
            pending.pop_back();
            const auto voxel = document.GetVoxel(
                position, context.SubModelIndex);
            if (!voxel || voxel->PaletteIndex != previous) continue;
            changes.push_back({
                context.SubModelIndex, position,
                true, previous, true, replacement});

            for (const Asset::Voxel::VoxelPosition offset : Neighbors)
            {
                const Asset::Voxel::VoxelPosition neighbor{
                    position.X + offset.X,
                    position.Y + offset.Y,
                    position.Z + offset.Z};
                if (!IsInside(neighbor, *dimensions)) continue;
                const std::uint64_t key = PositionKey(neighbor, *dimensions);
                if (visited.insert(key).second) pending.push_back(neighbor);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return Refused(VoxelFillResultCode::Failed, start,
            revision,
            "Unable to allocate the connected Fill region.");
    }
    catch (const std::exception& exception)
    {
        return Refused(VoxelFillResultCode::Failed, start,
            revision, exception.what());
    }
    if (changes.empty())
        return Refused(VoxelFillResultCode::TargetMissing, start,
            revision);

    const std::size_t changedVoxelCount = changes.size();
    const VoxelEditHistoryResult applied = context.History->Execute(
        *context.EditSession,
        VoxelEditOperation{"Fill Voxels", std::move(changes)});
    if (!applied)
        return Refused(VoxelFillResultCode::Failed, start,
            document.GetRevision(), applied.Message);

    const std::uint64_t revisionAfter = document.GetRevision();
    const auto filledStart = document.GetVoxel(start, context.SubModelIndex);
    if (!filledStart || filledStart->PaletteIndex != replacement ||
        revisionAfter != revision + 1U || !document.IsDirty())
    {
        return {
            VoxelFillResultCode::Failed, true, start, previous, replacement,
            0U, revision, revisionAfter,
            "The applied Fill edit failed its consistency check."};
    }
    return {
        VoxelFillResultCode::Applied, true, start, previous, replacement,
        changedVoxelCount,
        revision, revisionAfter, {}};
}

const char* VoxelFillResultCodeName(
    const VoxelFillResultCode code) noexcept
{
    switch (code)
    {
    case VoxelFillResultCode::Applied: return "Applied";
    case VoxelFillResultCode::NoDocument: return "No document";
    case VoxelFillResultCode::NoHit: return "No hit";
    case VoxelFillResultCode::TargetMissing: return "Target missing";
    case VoxelFillResultCode::SameColor: return "Same color";
    case VoxelFillResultCode::InvalidModel: return "Invalid model";
    case VoxelFillResultCode::InvalidPaletteIndex: return "Invalid palette index";
    case VoxelFillResultCode::Blocked: return "Blocked";
    case VoxelFillResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
