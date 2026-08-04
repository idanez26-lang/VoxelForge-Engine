#pragma once

#include "VoxelStamps/StampResourceLimits.h"
#include "VoxelStamps/StampTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampDomainError
{
    None,
    InvalidIdentity,
    InvalidBounds,
    EmptyPalette,
    InvalidPaletteEntry,
    EmptyVoxelSet,
    InvalidPaletteReference,
    DuplicateVoxelPosition,
    NonNormalizedCoordinates,
    InvalidPivot,
    UnsupportedScale,
    ResourceLimitExceeded,
    ArithmeticOverflow
};

struct StampValidationResult final
{
    StampDomainError Error = StampDomainError::None;
    std::string_view Message{"Stamp domain data is valid."};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Error == StampDomainError::None;
    }
};

[[nodiscard]] constexpr std::string_view StampDomainErrorMessage(
    const StampDomainError error) noexcept
{
    switch (error)
    {
    case StampDomainError::None:
        return "Stamp domain data is valid.";
    case StampDomainError::InvalidIdentity:
        return "Stamp identity must contain a non-zero UUID and a bounded content hash.";
    case StampDomainError::InvalidBounds:
        return "Stamp bounds must start at the local origin and match their dimensions.";
    case StampDomainError::EmptyPalette:
        return "Stamp palettes must contain at least one used color.";
    case StampDomainError::InvalidPaletteEntry:
        return "Stamp palette entries must use canonical contiguous local color IDs.";
    case StampDomainError::EmptyVoxelSet:
        return "Stamp voxel data must not be empty.";
    case StampDomainError::InvalidPaletteReference:
        return "Every stamp voxel must reference an existing used local palette color.";
    case StampDomainError::DuplicateVoxelPosition:
        return "Stamp voxel positions must be unique.";
    case StampDomainError::NonNormalizedCoordinates:
        return "Stamp voxel coordinates must be normalized to the complete local bounds.";
    case StampDomainError::InvalidPivot:
        return "Stamp pivot data is outside the local bounds or structurally invalid.";
    case StampDomainError::UnsupportedScale:
        return "Voxel Stamps V1 accepts only an exact 1:1 scale.";
    case StampDomainError::ResourceLimitExceeded:
        return "Stamp content exceeds a configured hard resource limit.";
    case StampDomainError::ArithmeticOverflow:
        return "Stamp size arithmetic overflowed before allocation.";
    }

    return "Unknown stamp domain error.";
}

[[nodiscard]] inline StampValidationResult ValidateStampTransform(
    const StampTransform& transform) noexcept
{
    if (!transform.IsUnitScale())
    {
        return {StampDomainError::UnsupportedScale,
                StampDomainErrorMessage(StampDomainError::UnsupportedScale)};
    }

    return {};
}

class VoxelStamp final
{
public:
    [[nodiscard]] static StampValidationResult Validate(
        const StampIdentity& identity,
        const StampBounds& bounds,
        const StampPivot& pivot,
        const StampTransform& transform,
        const std::span<const StampPaletteEntry> palette,
        const std::span<const StampVoxel> voxels,
        const StampResourceLimits& limits = DefaultStampResourceLimits())
    {
        if (identity.Id.Value() == 0U || identity.ContentHash.size() > 128U)
        {
            return MakeError(StampDomainError::InvalidIdentity);
        }

        if (!IsValidBounds(bounds))
        {
            return MakeError(StampDomainError::InvalidBounds);
        }

        if (palette.empty())
        {
            return MakeError(StampDomainError::EmptyPalette);
        }

        if (palette.size() > 256U)
        {
            return MakeError(StampDomainError::InvalidPaletteEntry);
        }

        for (std::size_t index = 0U; index < palette.size(); ++index)
        {
            if (palette[index].LocalColorId != index)
            {
                return MakeError(StampDomainError::InvalidPaletteEntry);
            }
        }

        if (voxels.empty())
        {
            return MakeError(StampDomainError::EmptyVoxelSet);
        }

        const StampResourceUsage usage = CalculateUsage(bounds, palette, voxels);
        const StampLimitEvaluation limitEvaluation = EvaluateStampLimits(usage, limits);
        if (limitEvaluation.Status == StampLimitStatus::ArithmeticOverflow)
        {
            return MakeError(StampDomainError::ArithmeticOverflow);
        }
        if (!limitEvaluation.IsAllowed())
        {
            return MakeError(StampDomainError::ResourceLimitExceeded);
        }

        if (!IsValidPivot(pivot, bounds))
        {
            return MakeError(StampDomainError::InvalidPivot);
        }

        const StampValidationResult transformValidation = ValidateStampTransform(transform);
        if (!transformValidation.IsValid())
        {
            return transformValidation;
        }

        std::array<bool, 256U> referencedPaletteIds{};
        const bool strictlyPositionSorted = std::adjacent_find(
            voxels.begin(), voxels.end(),
            [](const StampVoxel& left, const StampVoxel& right)
            {
                return !PositionLess(left.Position, right.Position);
            }) == voxels.end();
        std::unordered_set<StampLocalPosition, PositionHash> positions;
        if (!strictlyPositionSorted)
        {
            positions.reserve(voxels.size());
        }

        bool touchesMinimumX = false;
        bool touchesMinimumY = false;
        bool touchesMinimumZ = false;
        bool touchesMaximumX = false;
        bool touchesMaximumY = false;
        bool touchesMaximumZ = false;

        for (const StampVoxel& voxel : voxels)
        {
            if (voxel.LocalColorId >= palette.size())
            {
                return MakeError(StampDomainError::InvalidPaletteReference);
            }

            if (!Contains(bounds, voxel.Position))
            {
                return MakeError(StampDomainError::NonNormalizedCoordinates);
            }

            if (!strictlyPositionSorted &&
                !positions.insert(voxel.Position).second)
            {
                return MakeError(StampDomainError::DuplicateVoxelPosition);
            }

            referencedPaletteIds[voxel.LocalColorId] = true;
            touchesMinimumX = touchesMinimumX || voxel.Position.X == 0;
            touchesMinimumY = touchesMinimumY || voxel.Position.Y == 0;
            touchesMinimumZ = touchesMinimumZ || voxel.Position.Z == 0;
            touchesMaximumX = touchesMaximumX || voxel.Position.X == bounds.Maximum.X;
            touchesMaximumY = touchesMaximumY || voxel.Position.Y == bounds.Maximum.Y;
            touchesMaximumZ = touchesMaximumZ || voxel.Position.Z == bounds.Maximum.Z;
        }

        if (!touchesMinimumX || !touchesMinimumY || !touchesMinimumZ ||
            !touchesMaximumX || !touchesMaximumY || !touchesMaximumZ)
        {
            return MakeError(StampDomainError::NonNormalizedCoordinates);
        }

        for (const StampPaletteEntry& entry : palette)
        {
            if (!referencedPaletteIds[entry.LocalColorId])
            {
                return MakeError(StampDomainError::InvalidPaletteReference);
            }
        }

        return {};
    }

    [[nodiscard]] static std::optional<VoxelStamp> TryCreate(
        StampIdentity identity,
        StampBounds bounds,
        StampPivot pivot,
        StampTransform transform,
        std::vector<StampPaletteEntry> palette,
        std::vector<StampVoxel> voxels,
        const StampResourceLimits& limits = DefaultStampResourceLimits(),
        StampValidationResult* validation = nullptr)
    {
        const StampValidationResult result = Validate(
            identity, bounds, pivot, transform, palette, voxels, limits);
        if (validation != nullptr)
        {
            *validation = result;
        }
        if (!result.IsValid())
        {
            return std::nullopt;
        }

        return VoxelStamp{
            std::move(identity), std::move(bounds), std::move(pivot), std::move(transform),
            std::move(palette), std::move(voxels)};
    }

    [[nodiscard]] const StampIdentity& Identity() const noexcept { return identity_; }
    [[nodiscard]] const StampBounds& Bounds() const noexcept { return bounds_; }
    [[nodiscard]] const StampPivot& Pivot() const noexcept { return pivot_; }
    [[nodiscard]] const StampTransform& Transform() const noexcept { return transform_; }
    [[nodiscard]] std::span<const StampPaletteEntry> Palette() const noexcept
    {
        return palette_;
    }
    [[nodiscard]] std::span<const StampVoxel> Voxels() const noexcept
    {
        return voxels_;
    }
    [[nodiscard]] std::size_t RetainedBytes() const noexcept
    {
        return sizeof(VoxelStamp) + identity_.ContentHash.capacity() + 1U +
               palette_.capacity() * sizeof(StampPaletteEntry) +
               voxels_.capacity() * sizeof(StampVoxel);
    }
    [[nodiscard]] StampResourceUsage ResourceUsage() const noexcept
    {
        return CalculateUsage(bounds_, palette_, voxels_);
    }

    [[nodiscard]] bool operator==(const VoxelStamp& other) const noexcept = default;

private:
    struct PositionHash final
    {
        [[nodiscard]] std::size_t operator()(
            const StampLocalPosition& position) const noexcept
        {
            std::size_t value = std::hash<std::int32_t>{}(position.X);
            value ^= std::hash<std::int32_t>{}(position.Y) + 0x9e3779b9U +
                     (value << 6U) + (value >> 2U);
            value ^= std::hash<std::int32_t>{}(position.Z) + 0x9e3779b9U +
                     (value << 6U) + (value >> 2U);
            return value;
        }
    };

    [[nodiscard]] static bool PositionLess(
        const StampLocalPosition& left,
        const StampLocalPosition& right) noexcept
    {
        if (left.X != right.X) return left.X < right.X;
        if (left.Y != right.Y) return left.Y < right.Y;
        return left.Z < right.Z;
    }

    VoxelStamp(
        StampIdentity identity,
        StampBounds bounds,
        StampPivot pivot,
        StampTransform transform,
        std::vector<StampPaletteEntry> palette,
        std::vector<StampVoxel> voxels) noexcept
        : identity_(std::move(identity)),
          bounds_(std::move(bounds)),
          pivot_(std::move(pivot)),
          transform_(std::move(transform)),
          palette_(std::move(palette)),
          voxels_(std::move(voxels))
    {
    }

    [[nodiscard]] static StampValidationResult MakeError(
        const StampDomainError error) noexcept
    {
        return {error, StampDomainErrorMessage(error)};
    }

    [[nodiscard]] static bool IsValidBounds(const StampBounds& bounds) noexcept
    {
        if (bounds.Minimum != StampLocalPosition{} || bounds.Dimensions.X == 0U ||
            bounds.Dimensions.Y == 0U || bounds.Dimensions.Z == 0U ||
            bounds.Dimensions.X > static_cast<std::uint32_t>(
                                    std::numeric_limits<std::int32_t>::max()) ||
            bounds.Dimensions.Y > static_cast<std::uint32_t>(
                                    std::numeric_limits<std::int32_t>::max()) ||
            bounds.Dimensions.Z > static_cast<std::uint32_t>(
                                    std::numeric_limits<std::int32_t>::max()))
        {
            return false;
        }

        return bounds.Maximum == StampLocalPosition{
                                     .X = static_cast<std::int32_t>(bounds.Dimensions.X - 1U),
                                     .Y = static_cast<std::int32_t>(bounds.Dimensions.Y - 1U),
                                     .Z = static_cast<std::int32_t>(bounds.Dimensions.Z - 1U)};
    }

    [[nodiscard]] static bool Contains(
        const StampBounds& bounds,
        const StampLocalPosition& position) noexcept
    {
        return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
               position.X <= bounds.Maximum.X && position.Y <= bounds.Maximum.Y &&
               position.Z <= bounds.Maximum.Z;
    }

    [[nodiscard]] static bool IsValidPivot(
        const StampPivot& pivot,
        const StampBounds& bounds) noexcept
    {
        if (pivot.ResolvedMode == StampPivotMode::Auto ||
            (pivot.RequestedMode == StampPivotMode::Auto && pivot.AutoPolicyVersion == 0U) ||
            pivot.LocalNormal.X < -1 || pivot.LocalNormal.X > 1 ||
            pivot.LocalNormal.Y < -1 || pivot.LocalNormal.Y > 1 ||
            pivot.LocalNormal.Z < -1 || pivot.LocalNormal.Z > 1)
        {
            return false;
        }

        const int normalComponentCount = (pivot.LocalNormal.X != 0 ? 1 : 0) +
                                         (pivot.LocalNormal.Y != 0 ? 1 : 0) +
                                         (pivot.LocalNormal.Z != 0 ? 1 : 0);
        if (normalComponentCount > 1)
        {
            return false;
        }

        const std::int64_t maximumX =
            static_cast<std::int64_t>(bounds.Dimensions.X) * StampFixedPoint::UnitsPerVoxel;
        const std::int64_t maximumY =
            static_cast<std::int64_t>(bounds.Dimensions.Y) * StampFixedPoint::UnitsPerVoxel;
        const std::int64_t maximumZ =
            static_cast<std::int64_t>(bounds.Dimensions.Z) * StampFixedPoint::UnitsPerVoxel;
        return pivot.LocalPosition.X >= 0 && pivot.LocalPosition.Y >= 0 &&
               pivot.LocalPosition.Z >= 0 && pivot.LocalPosition.X <= maximumX &&
               pivot.LocalPosition.Y <= maximumY && pivot.LocalPosition.Z <= maximumZ;
    }

    [[nodiscard]] static StampResourceUsage CalculateUsage(
        const StampBounds& bounds,
        const std::span<const StampPaletteEntry> palette,
        const std::span<const StampVoxel> voxels) noexcept
    {
        const StampSizeEstimate estimate = EstimateDecodedStampBytes(
            0U, voxels.size(), sizeof(StampVoxel), palette.size(),
            sizeof(StampPaletteEntry));
        return {
            .VoxelCount = voxels.size(),
            .LargestAxisLength = std::max({bounds.Dimensions.X, bounds.Dimensions.Y,
                                           bounds.Dimensions.Z}),
            .DecodedBytes = estimate.DecodedBytes,
            .FileBytes = 0U,
            .ChunkCount = 0U,
            .ArithmeticOverflow = estimate.ArithmeticOverflow};
    }

    StampIdentity identity_;
    StampBounds bounds_;
    StampPivot pivot_;
    StampTransform transform_;
    std::vector<StampPaletteEntry> palette_;
    std::vector<StampVoxel> voxels_;
};

} // namespace VoxelForge::Editor::Stamps
