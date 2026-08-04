#include "VoxelStamps/Placement/StampPlacementPlanner.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

void AddDiagnostic(
    StampPlacementPlan& plan,
    const StampPlacementDiagnosticCode code,
    const StampPlacementDiagnosticSeverity severity,
    const std::optional<std::size_t> sourceOrdinal = std::nullopt)
{
    plan.Diagnostics.push_back({code, severity, sourceOrdinal});
}

[[nodiscard]] bool MakeGridCoordinate(
    const std::int64_t fixed,
    std::int32_t& output) noexcept
{
    constexpr std::int64_t units = StampFixedPoint::UnitsPerVoxel;
    if (fixed % units != 0)
    {
        return false;
    }
    const std::int64_t gridCoordinate = fixed / units;
    if (gridCoordinate < std::numeric_limits<std::int32_t>::min() ||
        gridCoordinate > std::numeric_limits<std::int32_t>::max())
    {
        return false;
    }
    output = static_cast<std::int32_t>(gridCoordinate);
    return true;
}

// STAMP-23 : un Stamp de largeur ou de profondeur impaire a un pivot centre
// sur un DEMI voxel (ex. "mur" 18x24x49 -> pivot Z = 24,5). Une rotation de
// 90 ou 270 degres echange X et Z, donc ce demi-voxel change d'axe et la
// somme n'est plus un multiple exact d'UnitsPerVoxel : le plan entier etait
// refuse (PositionNotRepresentable) et l'apercu disparaissait.
// Le residu est IDENTIQUE pour tous les voxels du Stamp, donc arrondir au
// voxel le plus proche translate le Stamp d'un demi-voxel sans jamais le
// deformer. Reserve aux rotations qui echangent les axes ; les rotations 0
// et 180 gardent le refus strict (une cible sous-voxel reste une erreur).
[[nodiscard]] bool MakeRoundedGridCoordinate(
    const std::int64_t fixed,
    std::int32_t& output) noexcept
{
    constexpr std::int64_t units = StampFixedPoint::UnitsPerVoxel;
    const std::int64_t shifted = fixed + units / 2;
    std::int64_t gridCoordinate = shifted / units;
    if (shifted % units != 0 && shifted < 0)
    {
        --gridCoordinate; // division tronquee -> plancher pour les negatifs
    }
    if (gridCoordinate < std::numeric_limits<std::int32_t>::min() ||
        gridCoordinate > std::numeric_limits<std::int32_t>::max())
    {
        return false;
    }
    output = static_cast<std::int32_t>(gridCoordinate);
    return true;
}

[[nodiscard]] bool MakeTransformedGridPosition(
    const StampPlacementTransform& transform,
    const StampLocalPosition local,
    const StampFixedPoint pivot,
    Asset::Voxel::VoxelPosition& output) noexcept
{
    constexpr std::int64_t units = StampFixedPoint::UnitsPerVoxel;
    const std::int64_t relativeX =
        static_cast<std::int64_t>(local.X) * units - pivot.X;
    const std::int64_t relativeY =
        static_cast<std::int64_t>(local.Y) * units - pivot.Y;
    const std::int64_t relativeZ =
        static_cast<std::int64_t>(local.Z) * units - pivot.Z;

    std::int64_t mirroredX = relativeX;
    std::int64_t mirroredZ = relativeZ;
    switch (transform.Mirror)
    {
    case StampPlacementMirrorMode::None:
        break;
    case StampPlacementMirrorMode::X:
        mirroredX = -relativeX;
        break;
    case StampPlacementMirrorMode::Z:
        mirroredZ = -relativeZ;
        break;
    case StampPlacementMirrorMode::XZ:
        mirroredX = -relativeX;
        mirroredZ = -relativeZ;
        break;
    default:
        return false;
    }

    std::int64_t rotatedX = mirroredX;
    std::int64_t rotatedZ = mirroredZ;
    switch (transform.QuarterTurns)
    {
    case 0U:
        break;
    case 1U:
        rotatedX = mirroredZ;
        rotatedZ = -mirroredX;
        break;
    case 2U:
        rotatedX = -mirroredX;
        rotatedZ = -mirroredZ;
        break;
    case 3U:
        rotatedX = -mirroredZ;
        rotatedZ = mirroredX;
        break;
    default:
        return false;
    }

    // Seules les rotations qui echangent X et Z peuvent transferer un pivot
    // demi-voxel d'un axe a l'autre (cf. MakeRoundedGridCoordinate).
    const bool axesSwapped =
        transform.QuarterTurns == 1U || transform.QuarterTurns == 3U;
    const auto toGrid = [axesSwapped](
        const std::int64_t fixed, std::int32_t& coordinate) noexcept
    {
        return axesSwapped
            ? MakeRoundedGridCoordinate(fixed, coordinate)
            : MakeGridCoordinate(fixed, coordinate);
    };

    return toGrid(
               static_cast<std::int64_t>(transform.TargetPivot.X) +
                   rotatedX,
               output.X) &&
        toGrid(
               static_cast<std::int64_t>(transform.TargetPivot.Y) +
                   relativeY,
               output.Y) &&
        toGrid(
               static_cast<std::int64_t>(transform.TargetPivot.Z) +
                   rotatedZ,
               output.Z);
}

[[nodiscard]] bool IsWithinDimensions(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint64_t>(position.X) < dimensions.X &&
        static_cast<std::uint64_t>(position.Y) < dimensions.Y &&
        static_cast<std::uint64_t>(position.Z) < dimensions.Z;
}

void ExtendBounds(
    StampPlacementBounds& bounds,
    const Asset::Voxel::VoxelPosition position) noexcept
{
    if (!bounds.Valid)
    {
        bounds.Minimum = position;
        bounds.Maximum = position;
        bounds.Valid = true;
        return;
    }
    bounds.Minimum.X = std::min(bounds.Minimum.X, position.X);
    bounds.Minimum.Y = std::min(bounds.Minimum.Y, position.Y);
    bounds.Minimum.Z = std::min(bounds.Minimum.Z, position.Z);
    bounds.Maximum.X = std::max(bounds.Maximum.X, position.X);
    bounds.Maximum.Y = std::max(bounds.Maximum.Y, position.Y);
    bounds.Maximum.Z = std::max(bounds.Maximum.Z, position.Z);
}

} // namespace

StampPlacementPlan StampPlacementPlanner::Build(
    const StampPlacementPlannerRequest& request) noexcept
{
    StampPlacementPlan plan;
    try
    {
        if (request.Stamp == nullptr)
        {
            AddDiagnostic(plan, StampPlacementDiagnosticCode::MissingStamp,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }
        if (request.Document == nullptr)
        {
            plan.Stamp = request.Stamp->Identity();
            AddDiagnostic(plan, StampPlacementDiagnosticCode::MissingDocument,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }
        if (request.DocumentGeneration == 0U)
        {
            plan.Stamp = request.Stamp->Identity();
            AddDiagnostic(
                plan,
                StampPlacementDiagnosticCode::InvalidDocumentGeneration,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }
        const auto dimensions =
            request.Document->GetDimensions(request.TargetSubModel);
        if (!dimensions)
        {
            plan.Stamp = request.Stamp->Identity();
            AddDiagnostic(plan, StampPlacementDiagnosticCode::InvalidSubModel,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }

        const VoxelStamp& stamp = *request.Stamp;
        plan.Stamp = stamp.Identity();
        plan.Variant = request.Variant;
        plan.Document = MakeStampDocumentIdentity(*request.Document);
        plan.DocumentGeneration = request.DocumentGeneration;
        plan.DocumentRevision = request.Document->GetRevision();
        plan.TargetSubModel = request.TargetSubModel;
        plan.LocalBounds = stamp.Bounds();
        plan.Pivot = stamp.Pivot();
        plan.Transform = request.Transform;
        plan.CollisionPolicy = request.CollisionPolicy;
        plan.DocumentPaletteBefore = request.Document->GetPaletteSnapshot();
        plan.Statistics.TotalVoxelCount = stamp.Voxels().size();
        plan.ResourceLimitEvaluation = EvaluateStampLimits(
            stamp.ResourceUsage(), request.ResourceLimits);

        if (plan.Variant &&
            (plan.Variant->GroupId.Value() == 0U ||
             plan.Variant->VariantId.Value() == 0U ||
             plan.Variant->StampId != plan.Stamp.Id ||
             plan.Variant->ExpectedContentHash.empty() ||
             plan.Variant->ExpectedContentHash != plan.Stamp.ContentHash))
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::InvalidVariantIdentity,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }

        plan.CacheKey = {
            .Stamp = plan.Stamp,
            .Variant = plan.Variant,
            .Document = plan.Document,
            .DocumentGeneration = plan.DocumentGeneration,
            .DocumentRevision = plan.DocumentRevision,
            .TargetSubModel = plan.TargetSubModel,
            .Transform = plan.Transform,
            .CollisionPolicy = plan.CollisionPolicy,
            .PaletteCapacity = request.PaletteCapacity,
            .ReservedDocumentPaletteIndex =
                request.ReservedDocumentPaletteIndex,
            .ResourceLimits = request.ResourceLimits};

        const bool hasSoftResourceLimitWarning =
            plan.ResourceLimitEvaluation.Status ==
                StampLimitStatus::SoftLimitWarning;
        switch (plan.ResourceLimitEvaluation.Status)
        {
        case StampLimitStatus::Accepted:
        case StampLimitStatus::SoftLimitWarning:
            break;
        case StampLimitStatus::HardLimitExceeded:
        case StampLimitStatus::ArithmeticOverflow:
            AddDiagnostic(
                plan,
                StampPlacementDiagnosticCode::HardResourceLimitExceeded,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        case StampLimitStatus::InvalidConfiguration:
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::InvalidResourceLimits,
                StampPlacementDiagnosticSeverity::Error);
            return plan;
        }

        bool transformSupported = true;
        if (request.Transform.QuarterTurns > 3U)
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::UnsupportedRotation,
                StampPlacementDiagnosticSeverity::Error);
            transformSupported = false;
        }
        if (request.Transform.Mirror != StampPlacementMirrorMode::None &&
            request.Transform.Mirror != StampPlacementMirrorMode::X &&
            request.Transform.Mirror != StampPlacementMirrorMode::Z &&
            request.Transform.Mirror != StampPlacementMirrorMode::XZ)
        {
            AddDiagnostic(plan, StampPlacementDiagnosticCode::UnsupportedMirror,
                StampPlacementDiagnosticSeverity::Error);
            transformSupported = false;
        }
        bool collisionPolicySupported = true;
        switch (request.CollisionPolicy)
        {
        case StampCollisionPolicy::Overwrite:
        case StampCollisionPolicy::Reject:
        case StampCollisionPolicy::SkipOccupied:
            break;
        default:
            collisionPolicySupported = false;
            AddDiagnostic(
                plan,
                StampPlacementDiagnosticCode::UnsupportedCollisionPolicy,
                StampPlacementDiagnosticSeverity::Error);
            break;
        }

        plan.Voxels.reserve(stamp.Voxels().size());
        bool representable = true;
        bool collisionRejected = false;
        for (std::size_t ordinal = 0U; ordinal < stamp.Voxels().size();
             ++ordinal)
        {
            const StampVoxel& source = stamp.Voxels()[ordinal];
            Asset::Voxel::VoxelPosition world{};
            if (!MakeTransformedGridPosition(
                    request.Transform, source.Position,
                    stamp.Pivot().LocalPosition, world))
            {
                AddDiagnostic(
                    plan,
                    StampPlacementDiagnosticCode::PositionNotRepresentable,
                    StampPlacementDiagnosticSeverity::Error, ordinal);
                representable = false;
                break;
            }

            const bool outOfBounds = !IsWithinDimensions(world, *dimensions);
            std::optional<Asset::Voxel::Voxel> existing;
            if (!outOfBounds)
            {
                existing = request.Document->GetVoxel(
                    world, request.TargetSubModel);
            }
            const bool overlap = existing.has_value();
            const bool skipped = overlap &&
                request.CollisionPolicy ==
                    StampCollisionPolicy::SkipOccupied;
            const StampColor color =
                stamp.Palette()[source.LocalColorId].Color;
            plan.Voxels.push_back({
                .SourceOrdinal = ordinal,
                .LocalPosition = source.Position,
                .WorldPosition = world,
                .LocalPaletteIndex = source.LocalColorId,
                .DocumentPaletteIndex = 0U,
                .Color = color,
                .ExistingVoxel = existing,
                .FinalVoxel = {},
                .Overlap = overlap,
                .Skipped = skipped,
                .OutOfBounds = outOfBounds});
            ++plan.Statistics.PlannedVoxelCount;
            if (overlap)
            {
                ++plan.Statistics.OverlapCount;
            }
            if (skipped)
            {
                ++plan.Statistics.SkippedVoxelCount;
            }
            if (outOfBounds)
            {
                ++plan.Statistics.OutOfBoundsCount;
            }
            ExtendBounds(plan.WorldBounds, world);

            if (overlap &&
                request.CollisionPolicy == StampCollisionPolicy::Reject)
            {
                collisionRejected = true;
            }
        }

        std::array<bool, 256U> requiredLocalColorFlags{};
        for (const StampPlannedVoxel& voxel : plan.Voxels)
        {
            if (!voxel.Skipped)
            {
                requiredLocalColorFlags[voxel.LocalPaletteIndex] = true;
            }
        }
        std::array<std::uint8_t, 256U> requiredLocalColorIds{};
        std::size_t requiredLocalColorCount = 0U;
        for (std::size_t localColorId = 0U;
             localColorId < stamp.Palette().size(); ++localColorId)
        {
            if (requiredLocalColorFlags[localColorId])
            {
                requiredLocalColorIds[requiredLocalColorCount++] =
                    static_cast<std::uint8_t>(localColorId);
            }
        }

        std::optional<std::span<const std::uint8_t>> requiredSelection;
        if (request.CollisionPolicy == StampCollisionPolicy::SkipOccupied)
        {
            requiredSelection = std::span<const std::uint8_t>{
                requiredLocalColorIds.data(), requiredLocalColorCount};
        }
        PaletteMappingRequest paletteRequest{
            .StampPalette = stamp.Palette(),
            .StampVoxels = stamp.Voxels(),
            .DocumentPalette = plan.DocumentPaletteBefore,
            .PaletteCapacity = request.PaletteCapacity,
            .ReservedDocumentPaletteIndex =
                request.ReservedDocumentPaletteIndex,
            .RequiredLocalColorIds = requiredSelection};
        for (std::size_t modelIndex = 0U;
             modelIndex < request.Document->GetModelCount(); ++modelIndex)
        {
            const Asset::Voxel::VoxelSubModel* const model =
                request.Document->GetModel(modelIndex);
            if (model == nullptr)
            {
                continue;
            }
            model->ForEachVoxel(
                [&paletteRequest](
                    const Asset::Voxel::VoxelPosition,
                    const Asset::Voxel::Voxel voxel)
                {
                    paletteRequest
                        .OccupiedDocumentPaletteIndices[voxel.PaletteIndex] =
                        true;
                });
        }
        PaletteMappingResult paletteMapping =
            PaletteMappingEngine::Plan(paletteRequest);
        const bool paletteMappingSucceeded = paletteMapping.IsSuccess();
        plan.PaletteStatus = paletteMapping.Status;
        plan.PaletteMapping = std::move(paletteMapping.Plan);
        plan.Statistics.AddedPaletteColorCount =
            plan.PaletteMapping.AddedColorCount;
        plan.Statistics.ReusedPaletteColorCount =
            plan.PaletteMapping.ReusedColorCount;
        if (!paletteMappingSucceeded)
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::PaletteMappingFailed,
                StampPlacementDiagnosticSeverity::Error);
        }

        std::array<std::uint8_t, 256U> localToDocument{};
        if (paletteMappingSucceeded)
        {
            for (const PaletteMappingEntry& entry :
                 plan.PaletteMapping.LocalToDocument)
            {
                localToDocument[entry.LocalColorId] =
                    entry.DocumentPaletteIndex;
            }
        }
        for (StampPlannedVoxel& voxel : plan.Voxels)
        {
            if (voxel.Skipped && voxel.ExistingVoxel)
            {
                voxel.DocumentPaletteIndex =
                    voxel.ExistingVoxel->PaletteIndex;
                voxel.FinalVoxel = *voxel.ExistingVoxel;
            }
            else
            {
                voxel.DocumentPaletteIndex =
                    localToDocument[voxel.LocalPaletteIndex];
                voxel.FinalVoxel =
                    Asset::Voxel::Voxel{voxel.DocumentPaletteIndex};
            }

            if (!paletteMappingSucceeded || voxel.OutOfBounds)
            {
                continue;
            }
            if (voxel.Skipped ||
                (voxel.ExistingVoxel &&
                    voxel.ExistingVoxel->PaletteIndex ==
                        voxel.DocumentPaletteIndex))
            {
                ++plan.Statistics.UnchangedVoxelCount;
            }
            else
            {
                ++plan.Statistics.ChangedVoxelCount;
            }
        }

        if (plan.Statistics.OutOfBoundsCount != 0U)
        {
            AddDiagnostic(plan, StampPlacementDiagnosticCode::OutOfBounds,
                StampPlacementDiagnosticSeverity::Error);
        }
        if (collisionRejected)
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::CollisionRejected,
                StampPlacementDiagnosticSeverity::Error);
        }
        if (hasSoftResourceLimitWarning)
        {
            AddDiagnostic(
                plan,
                StampPlacementDiagnosticCode::SoftResourceLimitExceeded,
                StampPlacementDiagnosticSeverity::Warning);
        }

        const bool hasChanges =
            plan.Statistics.ChangedVoxelCount != 0U ||
            plan.PaletteMapping.HasPaletteChanges();
        const bool collisionAllowsCommit = collisionPolicySupported &&
            !collisionRejected;
        if (representable && paletteMappingSucceeded &&
            transformSupported && collisionAllowsCommit &&
            plan.Statistics.OutOfBoundsCount == 0U && !hasChanges)
        {
            AddDiagnostic(plan, StampPlacementDiagnosticCode::NoChanges,
                StampPlacementDiagnosticSeverity::Information);
        }
        plan.CanCommit = representable && paletteMappingSucceeded &&
            transformSupported && collisionAllowsCommit &&
            plan.Statistics.OutOfBoundsCount == 0U && hasChanges &&
            !plan.HasErrors();
    }
    catch (const std::bad_alloc&)
    {
        plan = {};
        try
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::AllocationFailure,
                StampPlacementDiagnosticSeverity::Error);
        }
        catch (...)
        {
        }
    }
    catch (...)
    {
        plan = {};
        try
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::AllocationFailure,
                StampPlacementDiagnosticSeverity::Error);
        }
        catch (...)
        {
        }
    }
    return plan;
}

} // namespace VoxelForge::Editor::Stamps
