#include "VoxelSphereService.h"

#include "VoxelHistory/VoxelEditHistory.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

VoxelSphereResult Refused(
    const VoxelSphereResultCode code,
    const VoxelSpherePreview sphere,
    const std::uint64_t revision,
    std::string error = {})
{
    return {code, false, sphere, 0U, revision, revision, std::move(error)};
}

std::int32_t ClampCoordinate(
    const std::int64_t value,
    const std::int32_t minimum,
    const std::int32_t maximum) noexcept
{
    return static_cast<std::int32_t>(std::clamp(
        value, static_cast<std::int64_t>(minimum),
        static_cast<std::int64_t>(maximum)));
}
}

std::uint32_t VoxelSphereService::CalculateRadius(
    const Position center,
    const Position radiusPoint) noexcept
{
    const long double dx = static_cast<long double>(radiusPoint.X) - center.X;
    const long double dy = static_cast<long double>(radiusPoint.Y) - center.Y;
    const long double dz = static_cast<long double>(radiusPoint.Z) - center.Z;
    const long double radius = std::floor(std::sqrt(dx * dx + dy * dy + dz * dz));
    if (!std::isfinite(radius) || radius <= 0.0L) return 0U;
    return static_cast<std::uint32_t>(std::min(
        radius, static_cast<long double>(std::numeric_limits<std::uint32_t>::max())));
}

std::vector<Position> VoxelSphereService::CalculatePositions(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    const Position center,
    const Position radiusPoint)
{
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions || dimensions->X == 0U || dimensions->Y == 0U ||
        dimensions->Z == 0U)
        return {};

    const std::uint32_t radius = CalculateRadius(center, radiusPoint);
    const std::int64_t radius64 = radius;
    const std::int32_t maximumX = static_cast<std::int32_t>(dimensions->X - 1U);
    const std::int32_t maximumY = static_cast<std::int32_t>(dimensions->Y - 1U);
    const std::int32_t maximumZ = static_cast<std::int32_t>(dimensions->Z - 1U);
    const std::int32_t minimumX = ClampCoordinate(
        static_cast<std::int64_t>(center.X) - radius64, 0, maximumX);
    const std::int32_t minimumY = ClampCoordinate(
        static_cast<std::int64_t>(center.Y) - radius64, 0, maximumY);
    const std::int32_t minimumZ = ClampCoordinate(
        static_cast<std::int64_t>(center.Z) - radius64, 0, maximumZ);
    const std::int32_t clippedMaximumX = ClampCoordinate(
        static_cast<std::int64_t>(center.X) + radius64, 0, maximumX);
    const std::int32_t clippedMaximumY = ClampCoordinate(
        static_cast<std::int64_t>(center.Y) + radius64, 0, maximumY);
    const std::int32_t clippedMaximumZ = ClampCoordinate(
        static_cast<std::int64_t>(center.Z) + radius64, 0, maximumZ);
    if (minimumX > clippedMaximumX || minimumY > clippedMaximumY ||
        minimumZ > clippedMaximumZ)
        return {};

    const std::size_t capacity =
        static_cast<std::size_t>(clippedMaximumX - minimumX + 1) *
        static_cast<std::size_t>(clippedMaximumY - minimumY + 1) *
        static_cast<std::size_t>(clippedMaximumZ - minimumZ + 1);
    std::vector<Position> positions;
    positions.reserve(capacity);
    const long double radiusSquared =
        static_cast<long double>(radius) * radius;
    for (std::int32_t z = minimumZ; z <= clippedMaximumZ; ++z)
        for (std::int32_t y = minimumY; y <= clippedMaximumY; ++y)
            for (std::int32_t x = minimumX; x <= clippedMaximumX; ++x)
            {
                const long double dx = static_cast<long double>(x) - center.X;
                const long double dy = static_cast<long double>(y) - center.Y;
                const long double dz = static_cast<long double>(z) - center.Z;
                if (dx * dx + dy * dy + dz * dz <= radiusSquared)
                    positions.push_back({x, y, z});
            }
    return positions;
}

VoxelSphereResult VoxelSphereService::Apply(const VoxelSphereContext& context)
{
    const VoxelSpherePreview sphere{
        context.Center, CalculateRadius(context.Center, context.RadiusPoint)};
    if (context.Document == nullptr)
        return Refused(VoxelSphereResultCode::NoDocument, sphere, 0U);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    const std::uint64_t revision = document.GetRevision();
    if (context.Blocked)
        return Refused(VoxelSphereResultCode::Blocked, sphere, revision);
    if (context.PaletteIndex == 0U || context.PaletteIndex > 255U)
        return Refused(
            VoxelSphereResultCode::InvalidPaletteIndex, sphere, revision);
    if (context.EditSession == nullptr || context.History == nullptr ||
        context.EditSession->ActiveVoxelDocument() != &document)
        return Refused(VoxelSphereResultCode::InvalidModel, sphere, revision);

    std::vector<VoxelChange> changes;
    try
    {
        const auto positions = CalculatePositions(document, context.SubModelIndex,
            context.Center, context.RadiusPoint);
        if (positions.empty())
            return Refused(VoxelSphereResultCode::NoTarget, sphere, revision);
        changes.reserve(positions.size());
        const auto paletteIndex = static_cast<std::uint8_t>(context.PaletteIndex);
        for (const Position position : positions)
            if (!document.HasVoxel(position, context.SubModelIndex))
                changes.push_back({context.SubModelIndex, position,
                    false, 0U, true, paletteIndex});
    }
    catch (const std::bad_alloc&)
    {
        return Refused(VoxelSphereResultCode::Failed, sphere, revision,
            "Unable to allocate the Sphere operation.");
    }
    catch (const std::exception& exception)
    {
        return Refused(VoxelSphereResultCode::Failed, sphere, revision,
            exception.what());
    }
    if (changes.empty())
        return Refused(VoxelSphereResultCode::NoChanges, sphere, revision);

    const std::size_t changedVoxelCount = changes.size();
    const VoxelEditHistoryResult applied = context.History->Execute(
        *context.EditSession,
        VoxelEditOperation{"Create Voxel Sphere", std::move(changes)});
    if (!applied)
        return Refused(VoxelSphereResultCode::Failed, sphere,
            document.GetRevision(), applied.Message);
    const std::uint64_t revisionAfter = document.GetRevision();
    if (revisionAfter != revision + 1U || !document.IsDirty())
        return {VoxelSphereResultCode::Failed, true, sphere, changedVoxelCount,
            revision, revisionAfter,
            "The applied Sphere edit failed its consistency check."};
    return {VoxelSphereResultCode::Applied, true, sphere, changedVoxelCount,
        revision, revisionAfter, {}};
}

bool VoxelSphereInteraction::Begin(
    const Position center,
    const std::uint64_t documentGeneration) noexcept
{
    if (center_) return false;
    center_ = center;
    radiusPoint_ = center;
    documentGeneration_ = documentGeneration;
    return true;
}

bool VoxelSphereInteraction::Update(
    const std::optional<Position> radiusPoint) noexcept
{
    if (!center_ || radiusPoint_ == radiusPoint) return false;
    radiusPoint_ = radiusPoint;
    return true;
}

void VoxelSphereInteraction::Cancel() noexcept
{
    center_.reset();
    radiusPoint_.reset();
    documentGeneration_ = 0U;
}

bool VoxelSphereInteraction::IsActive() const noexcept { return center_.has_value(); }
std::optional<Position> VoxelSphereInteraction::Center() const noexcept { return center_; }
std::optional<Position> VoxelSphereInteraction::RadiusPoint() const noexcept { return radiusPoint_; }
std::uint64_t VoxelSphereInteraction::DocumentGeneration() const noexcept { return documentGeneration_; }

const char* VoxelSphereResultCodeName(const VoxelSphereResultCode code) noexcept
{
    switch (code)
    {
    case VoxelSphereResultCode::Applied: return "Applied";
    case VoxelSphereResultCode::NoDocument: return "No document";
    case VoxelSphereResultCode::NoTarget: return "No target";
    case VoxelSphereResultCode::InvalidModel: return "Invalid model";
    case VoxelSphereResultCode::InvalidPaletteIndex: return "Invalid palette index";
    case VoxelSphereResultCode::NoChanges: return "No changes";
    case VoxelSphereResultCode::Blocked: return "Blocked";
    case VoxelSphereResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
