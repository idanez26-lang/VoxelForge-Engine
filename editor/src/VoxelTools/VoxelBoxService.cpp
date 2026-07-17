#include "VoxelBoxService.h"

#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <exception>
#include <new>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
VoxelBoxResult Refused(
    const VoxelBoxResultCode code,
    const VoxelBoxBounds bounds,
    const std::uint64_t revision,
    std::string error = {})
{
    return {code, false, bounds, 0U, revision, revision, std::move(error)};
}

std::int32_t ClipEndpoint(
    const std::int32_t anchor,
    const std::int32_t endpoint) noexcept
{
    const std::int32_t maximumOffset =
        VoxelBoxService::MaximumExtentPerAxis - 1;
    return endpoint >= anchor
        ? std::min(endpoint, anchor + maximumOffset)
        : std::max(endpoint, anchor - maximumOffset);
}
}

std::optional<VoxelBoxBounds> VoxelBoxService::CalculateBounds(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    Asset::Voxel::VoxelPosition cornerA,
    Asset::Voxel::VoxelPosition cornerB) noexcept
{
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions || dimensions->X == 0U || dimensions->Y == 0U ||
        dimensions->Z == 0U)
    {
        return std::nullopt;
    }
    const auto clampCoordinate = [](const std::int32_t coordinate,
        const std::uint32_t extent) noexcept
    {
        return std::clamp(
            coordinate, 0, static_cast<std::int32_t>(extent - 1U));
    };
    cornerA = {
        clampCoordinate(cornerA.X, dimensions->X),
        clampCoordinate(cornerA.Y, dimensions->Y),
        clampCoordinate(cornerA.Z, dimensions->Z)};
    cornerB = {
        clampCoordinate(ClipEndpoint(cornerA.X, cornerB.X), dimensions->X),
        clampCoordinate(ClipEndpoint(cornerA.Y, cornerB.Y), dimensions->Y),
        clampCoordinate(ClipEndpoint(cornerA.Z, cornerB.Z), dimensions->Z)};
    return VoxelBoxBounds{
        {std::min(cornerA.X, cornerB.X),
         std::min(cornerA.Y, cornerB.Y),
         std::min(cornerA.Z, cornerB.Z)},
        {std::max(cornerA.X, cornerB.X),
         std::max(cornerA.Y, cornerB.Y),
         std::max(cornerA.Z, cornerB.Z)}};
}

VoxelBoxResult VoxelBoxService::Apply(const VoxelBoxContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelBoxResultCode::NoDocument, {}, 0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelBoxResultCode::Blocked, {}, revision);
    if (context.PaletteIndex == 0U || context.PaletteIndex > 255U)
        return Refused(VoxelBoxResultCode::InvalidPaletteIndex, {}, revision);
    if (context.EditSession == nullptr || context.History == nullptr ||
        context.EditSession->ActiveVoxelDocument() != &document)
    {
        return Refused(VoxelBoxResultCode::InvalidModel, {}, revision);
    }
    Voxel::VoxelModel* compatibilityModel =
        context.EditSession->ActiveVoxelModel();
    if (compatibilityModel == nullptr ||
        compatibilityModel->GetGrid(context.SubModelIndex) == nullptr)
    {
        return Refused(VoxelBoxResultCode::InvalidModel, {}, revision);
    }
    const auto bounds = CalculateBounds(
        document, context.SubModelIndex, context.CornerA, context.CornerB);
    if (!bounds)
        return Refused(VoxelBoxResultCode::NoTarget, {}, revision);

    std::vector<VoxelChange> changes;
    try
    {
        const std::size_t width = static_cast<std::size_t>(
            bounds->Maximum.X - bounds->Minimum.X + 1);
        const std::size_t height = static_cast<std::size_t>(
            bounds->Maximum.Y - bounds->Minimum.Y + 1);
        const std::size_t depth = static_cast<std::size_t>(
            bounds->Maximum.Z - bounds->Minimum.Z + 1);
        changes.reserve(width * height * depth);
        const auto paletteIndex = static_cast<std::uint8_t>(context.PaletteIndex);
        for (std::int32_t z = bounds->Minimum.Z; z <= bounds->Maximum.Z; ++z)
            for (std::int32_t y = bounds->Minimum.Y; y <= bounds->Maximum.Y; ++y)
                for (std::int32_t x = bounds->Minimum.X; x <= bounds->Maximum.X; ++x)
                {
                    const Asset::Voxel::VoxelPosition position{x, y, z};
                    if (!document.HasVoxel(position, context.SubModelIndex))
                        changes.push_back({
                            context.SubModelIndex, position,
                            false, 0U, true, paletteIndex});
                }
    }
    catch (const std::bad_alloc&)
    {
        return Refused(VoxelBoxResultCode::Failed, *bounds, revision,
            "Unable to allocate the Box operation.");
    }
    catch (const std::exception& exception)
    {
        return Refused(
            VoxelBoxResultCode::Failed, *bounds, revision, exception.what());
    }
    if (changes.empty())
        return Refused(VoxelBoxResultCode::NoChanges, *bounds, revision);

    const std::size_t changedVoxelCount = changes.size();
    const VoxelEditHistoryResult applied = context.History->Execute(
        *context.EditSession,
        VoxelEditOperation{"Create Voxel Box", std::move(changes)});
    if (!applied)
        return Refused(VoxelBoxResultCode::Failed, *bounds,
            document.GetRevision(), applied.Message);
    const std::uint64_t revisionAfter = document.GetRevision();
    if (revisionAfter != revision + 1U || !document.IsDirty())
    {
        return {VoxelBoxResultCode::Failed, true, *bounds, changedVoxelCount,
            revision, revisionAfter,
            "The applied Box edit failed its consistency check."};
    }
    return {VoxelBoxResultCode::Applied, true, *bounds, changedVoxelCount,
        revision, revisionAfter, {}};
}

bool VoxelBoxInteraction::Begin(
    const Asset::Voxel::VoxelPosition corner,
    const std::uint64_t documentGeneration) noexcept
{
    if (cornerA_) return false;
    cornerA_ = corner;
    cornerB_ = corner;
    documentGeneration_ = documentGeneration;
    return true;
}

bool VoxelBoxInteraction::Update(
    const std::optional<Asset::Voxel::VoxelPosition> corner) noexcept
{
    if (!cornerA_ || cornerB_ == corner) return false;
    cornerB_ = corner;
    return true;
}

void VoxelBoxInteraction::Cancel() noexcept
{
    cornerA_.reset();
    cornerB_.reset();
    documentGeneration_ = 0U;
}

bool VoxelBoxInteraction::IsActive() const noexcept { return cornerA_.has_value(); }
std::optional<Asset::Voxel::VoxelPosition> VoxelBoxInteraction::CornerA() const noexcept { return cornerA_; }
std::optional<Asset::Voxel::VoxelPosition> VoxelBoxInteraction::CornerB() const noexcept { return cornerB_; }
std::uint64_t VoxelBoxInteraction::DocumentGeneration() const noexcept { return documentGeneration_; }

const char* VoxelBoxResultCodeName(const VoxelBoxResultCode code) noexcept
{
    switch (code)
    {
    case VoxelBoxResultCode::Applied: return "Applied";
    case VoxelBoxResultCode::NoDocument: return "No document";
    case VoxelBoxResultCode::NoTarget: return "No target";
    case VoxelBoxResultCode::InvalidModel: return "Invalid model";
    case VoxelBoxResultCode::InvalidPaletteIndex: return "Invalid palette index";
    case VoxelBoxResultCode::NoChanges: return "No changes";
    case VoxelBoxResultCode::Blocked: return "Blocked";
    case VoxelBoxResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
