#include "VoxelLineService.h"

#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

VoxelLineResult Refused(
    const VoxelLineResultCode code,
    const std::uint64_t revision,
    std::string error = {})
{
    return {code, false, 0U, revision, revision, std::move(error)};
}

bool IsInside(
    const Position position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions.X &&
        static_cast<std::uint32_t>(position.Y) < dimensions.Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions.Z;
}

bool PositionLess(const Position left, const Position right) noexcept
{
    return std::tie(left.X, left.Y, left.Z) <
        std::tie(right.X, right.Y, right.Z);
}
}

std::vector<Asset::Voxel::VoxelPosition>
VoxelLineService::CalculatePositions(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    Position pointA,
    Position pointB)
{
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions) return {};
    if (PositionLess(pointB, pointA)) std::swap(pointA, pointB);

    const std::int64_t dx = std::llabs(
        static_cast<std::int64_t>(pointB.X) - pointA.X);
    const std::int64_t dy = std::llabs(
        static_cast<std::int64_t>(pointB.Y) - pointA.Y);
    const std::int64_t dz = std::llabs(
        static_cast<std::int64_t>(pointB.Z) - pointA.Z);
    const std::int32_t stepX = pointB.X >= pointA.X ? 1 : -1;
    const std::int32_t stepY = pointB.Y >= pointA.Y ? 1 : -1;
    const std::int32_t stepZ = pointB.Z >= pointA.Z ? 1 : -1;
    const std::int64_t maximumSteps = std::max({dx, dy, dz}) + 1;
    if (maximumSteps > 1'000'000LL)
        throw std::length_error("Voxel line exceeds the safe calculation limit.");

    std::vector<Position> positions;
    positions.reserve(static_cast<std::size_t>(maximumSteps));
    const auto appendIfInside = [&](const Position position)
    {
        if (IsInside(position, *dimensions)) positions.push_back(position);
    };

    Position current = pointA;
    appendIfInside(current);
    if (dx >= dy && dx >= dz)
    {
        std::int64_t errorY = 2 * dy - dx;
        std::int64_t errorZ = 2 * dz - dx;
        while (current.X != pointB.X)
        {
            current.X += stepX;
            if (errorY >= 0) { current.Y += stepY; errorY -= 2 * dx; }
            if (errorZ >= 0) { current.Z += stepZ; errorZ -= 2 * dx; }
            errorY += 2 * dy;
            errorZ += 2 * dz;
            appendIfInside(current);
        }
    }
    else if (dy >= dx && dy >= dz)
    {
        std::int64_t errorX = 2 * dx - dy;
        std::int64_t errorZ = 2 * dz - dy;
        while (current.Y != pointB.Y)
        {
            current.Y += stepY;
            if (errorX >= 0) { current.X += stepX; errorX -= 2 * dy; }
            if (errorZ >= 0) { current.Z += stepZ; errorZ -= 2 * dy; }
            errorX += 2 * dx;
            errorZ += 2 * dz;
            appendIfInside(current);
        }
    }
    else
    {
        std::int64_t errorX = 2 * dx - dz;
        std::int64_t errorY = 2 * dy - dz;
        while (current.Z != pointB.Z)
        {
            current.Z += stepZ;
            if (errorX >= 0) { current.X += stepX; errorX -= 2 * dz; }
            if (errorY >= 0) { current.Y += stepY; errorY -= 2 * dz; }
            errorX += 2 * dx;
            errorY += 2 * dy;
            appendIfInside(current);
        }
    }
    return positions;
}

VoxelLineResult VoxelLineService::Apply(const VoxelLineContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelLineResultCode::NoDocument, 0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelLineResultCode::Blocked, revision);
    if (context.PaletteIndex == 0U || context.PaletteIndex > 255U)
        return Refused(VoxelLineResultCode::InvalidPaletteIndex, revision);
    if (context.EditSession == nullptr || context.History == nullptr ||
        context.EditSession->ActiveVoxelDocument() != &document)
    {
        return Refused(VoxelLineResultCode::InvalidModel, revision);
    }
    Voxel::VoxelModel* compatibilityModel =
        context.EditSession->ActiveVoxelModel();
    if (compatibilityModel == nullptr ||
        compatibilityModel->GetGrid(context.SubModelIndex) == nullptr)
    {
        return Refused(VoxelLineResultCode::InvalidModel, revision);
    }

    std::vector<VoxelChange> changes;
    try
    {
        const auto positions = CalculatePositions(
            document, context.SubModelIndex, context.PointA, context.PointB);
        if (positions.empty())
            return Refused(VoxelLineResultCode::NoTarget, revision);
        changes.reserve(positions.size());
        const auto paletteIndex = static_cast<std::uint8_t>(context.PaletteIndex);
        for (const Position position : positions)
            if (!document.HasVoxel(position, context.SubModelIndex))
                changes.push_back({context.SubModelIndex, position,
                    false, 0U, true, paletteIndex});
    }
    catch (const std::bad_alloc&)
    {
        return Refused(VoxelLineResultCode::Failed, revision,
            "Unable to allocate the Line operation.");
    }
    catch (const std::exception& exception)
    {
        return Refused(VoxelLineResultCode::Failed, revision, exception.what());
    }
    if (changes.empty())
        return Refused(VoxelLineResultCode::NoChanges, revision);

    const std::size_t changedVoxelCount = changes.size();
    const VoxelEditHistoryResult applied = context.History->Execute(
        *context.EditSession,
        VoxelEditOperation{"Create Voxel Line", std::move(changes)});
    if (!applied)
        return Refused(
            VoxelLineResultCode::Failed, document.GetRevision(), applied.Message);
    const std::uint64_t revisionAfter = document.GetRevision();
    if (revisionAfter != revision + 1U || !document.IsDirty())
        return {VoxelLineResultCode::Failed, true, changedVoxelCount,
            revision, revisionAfter,
            "The applied Line edit failed its consistency check."};
    return {VoxelLineResultCode::Applied, true, changedVoxelCount,
        revision, revisionAfter, {}};
}

bool VoxelLineInteraction::Begin(
    const Position point,
    const std::uint64_t documentGeneration) noexcept
{
    if (pointA_) return false;
    pointA_ = point;
    pointB_ = point;
    documentGeneration_ = documentGeneration;
    return true;
}

bool VoxelLineInteraction::Update(const std::optional<Position> point) noexcept
{
    if (!pointA_ || pointB_ == point) return false;
    pointB_ = point;
    return true;
}

void VoxelLineInteraction::Cancel() noexcept
{
    pointA_.reset();
    pointB_.reset();
    documentGeneration_ = 0U;
}

bool VoxelLineInteraction::IsActive() const noexcept { return pointA_.has_value(); }
std::optional<Position> VoxelLineInteraction::PointA() const noexcept { return pointA_; }
std::optional<Position> VoxelLineInteraction::PointB() const noexcept { return pointB_; }
std::uint64_t VoxelLineInteraction::DocumentGeneration() const noexcept { return documentGeneration_; }

const char* VoxelLineResultCodeName(const VoxelLineResultCode code) noexcept
{
    switch (code)
    {
    case VoxelLineResultCode::Applied: return "Applied";
    case VoxelLineResultCode::NoDocument: return "No document";
    case VoxelLineResultCode::NoTarget: return "No target";
    case VoxelLineResultCode::InvalidModel: return "Invalid model";
    case VoxelLineResultCode::InvalidPaletteIndex: return "Invalid palette index";
    case VoxelLineResultCode::NoChanges: return "No changes";
    case VoxelLineResultCode::Blocked: return "Blocked";
    case VoxelLineResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
