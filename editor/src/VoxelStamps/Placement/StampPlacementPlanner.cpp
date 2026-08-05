#include "VoxelStamps/Placement/StampPlacementPlanner.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

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

    // STAMP-24 : un seul axe actif a la fois. Chaque quart de tour est une
    // permutation exacte des coordonnees, appliquee QuarterTurns fois autour
    // de RotationAxis (matrices de rotation directes standard) :
    //   X : (y, z) -> (-z,  y)
    //   Y : (x, z) -> ( z, -x)
    //   Z : (x, y) -> (-y,  x)
    if (transform.QuarterTurns > 3U) return false;
    std::int64_t rotatedX = mirroredX;
    std::int64_t rotatedY = relativeY;
    std::int64_t rotatedZ = mirroredZ;
    for (std::uint8_t turn = 0U; turn < transform.QuarterTurns; ++turn)
    {
        const std::int64_t previousX = rotatedX;
        const std::int64_t previousY = rotatedY;
        const std::int64_t previousZ = rotatedZ;
        switch (transform.RotationAxis)
        {
        case StampPlacementRotationAxis::LateralX:
            rotatedY = -previousZ;
            rotatedZ = previousY;
            break;
        case StampPlacementRotationAxis::VerticalY:
            rotatedX = previousZ;
            rotatedZ = -previousX;
            break;
        case StampPlacementRotationAxis::DepthZ:
            rotatedX = -previousY;
            rotatedY = previousX;
            break;
        default:
            return false;
        }
    }

    // Un nombre IMPAIR de quarts de tour echange les deux axes du plan de
    // rotation : un pivot demi-voxel change alors d'axe et la somme n'est
    // plus un multiple exact d'UnitsPerVoxel (cf. MakeRoundedGridCoordinate).
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
                   rotatedY,
               output.Y) &&
        toGrid(
               static_cast<std::int64_t>(transform.TargetPivot.Z) +
                   rotatedZ,
               output.Z);
}

// ---------------------------------------------------------------------------
// STAMP-25 : rotation a 45 degres.
//
// Un quart de tour est une permutation exacte de la grille. 45 degres ne l'est
// pas : aucun centre de voxel ne retombe sur une cellule. On reechantillonne
// donc, et on le fait dans le SENS INVERSE (destination -> source). Le sens
// direct (source -> destination) laisserait environ un tiers des cellules sans
// source, car une rotation de 45 degres etire les diagonales d'un facteur
// racine de 2 : le Stamp serait troue. En parcourant les cellules d'arrivee et
// en allant chercher leur source, la matiere reste pleine ; seuls les bords
// deviennent des escaliers.
// ---------------------------------------------------------------------------

// cos(45) = sin(45) en virgule fixe sur 16 bits, arrondi au plus proche.
[[nodiscard]] std::int64_t ScaleByCos45(const std::int64_t value) noexcept
{
    constexpr std::int64_t numerator = 46341; // round(2^16 * cos 45)
    constexpr std::int64_t denominator = 1LL << 16;
    const std::int64_t scaled = value * numerator;
    return scaled >= 0
        ? (scaled + denominator / 2) / denominator
        : -((-scaled + denominator / 2) / denominator);
}

struct RelativeFixedPoint final
{
    std::int64_t X = 0;
    std::int64_t Y = 0;
    std::int64_t Z = 0;
};

// Un quart de tour applique aux memes plans que MakeTransformedGridPosition.
[[nodiscard]] RelativeFixedPoint RotateQuarterTurns(
    RelativeFixedPoint value,
    const StampPlacementRotationAxis axis,
    const std::uint8_t quarterTurns) noexcept
{
    for (std::uint8_t turn = 0U; turn < quarterTurns % 4U; ++turn)
    {
        const RelativeFixedPoint previous = value;
        switch (axis)
        {
        case StampPlacementRotationAxis::LateralX:
            value.Y = -previous.Z;
            value.Z = previous.Y;
            break;
        case StampPlacementRotationAxis::VerticalY:
            value.X = previous.Z;
            value.Z = -previous.X;
            break;
        case StampPlacementRotationAxis::DepthZ:
            value.X = -previous.Y;
            value.Y = previous.X;
            break;
        default:
            break;
        }
    }
    return value;
}

// Demi-cran de +45 degres dans le plan de l'axe (matrices directes).
[[nodiscard]] RelativeFixedPoint RotateHalfQuarterStep(
    const RelativeFixedPoint value,
    const StampPlacementRotationAxis axis) noexcept
{
    RelativeFixedPoint rotated = value;
    switch (axis)
    {
    case StampPlacementRotationAxis::LateralX:
        rotated.Y = ScaleByCos45(value.Y - value.Z);
        rotated.Z = ScaleByCos45(value.Y + value.Z);
        break;
    case StampPlacementRotationAxis::VerticalY:
        rotated.X = ScaleByCos45(value.X + value.Z);
        rotated.Z = ScaleByCos45(value.Z - value.X);
        break;
    case StampPlacementRotationAxis::DepthZ:
        rotated.X = ScaleByCos45(value.X - value.Y);
        rotated.Y = ScaleByCos45(value.X + value.Y);
        break;
    default:
        break;
    }
    return rotated;
}

// Transposee de la precedente : -45 degres.
[[nodiscard]] RelativeFixedPoint UndoHalfQuarterStep(
    const RelativeFixedPoint value,
    const StampPlacementRotationAxis axis) noexcept
{
    RelativeFixedPoint restored = value;
    switch (axis)
    {
    case StampPlacementRotationAxis::LateralX:
        restored.Y = ScaleByCos45(value.Y + value.Z);
        restored.Z = ScaleByCos45(value.Z - value.Y);
        break;
    case StampPlacementRotationAxis::VerticalY:
        restored.X = ScaleByCos45(value.X - value.Z);
        restored.Z = ScaleByCos45(value.X + value.Z);
        break;
    case StampPlacementRotationAxis::DepthZ:
        restored.X = ScaleByCos45(value.X + value.Y);
        restored.Y = ScaleByCos45(value.Y - value.X);
        break;
    default:
        break;
    }
    return restored;
}

[[nodiscard]] RelativeFixedPoint ApplyMirror(
    const RelativeFixedPoint value,
    const StampPlacementMirrorMode mirror) noexcept
{
    RelativeFixedPoint mirrored = value;
    if (mirror == StampPlacementMirrorMode::X ||
        mirror == StampPlacementMirrorMode::XZ)
    {
        mirrored.X = -value.X;
    }
    if (mirror == StampPlacementMirrorMode::Z ||
        mirror == StampPlacementMirrorMode::XZ)
    {
        mirrored.Z = -value.Z;
    }
    return mirrored;
}

[[nodiscard]] std::int64_t PackLocalPosition(
    const std::int32_t x,
    const std::int32_t y,
    const std::int32_t z) noexcept
{
    constexpr std::int64_t bias = 1LL << 20;
    return ((static_cast<std::int64_t>(x) + bias) << 42) ^
        ((static_cast<std::int64_t>(y) + bias) << 21) ^
        (static_cast<std::int64_t>(z) + bias);
}

// Arrondi d'une coordonnee en virgule fixe vers la cellule la plus proche.
[[nodiscard]] std::int64_t RoundToCell(const std::int64_t fixed) noexcept
{
    constexpr std::int64_t units = StampFixedPoint::UnitsPerVoxel;
    return fixed >= 0
        ? (fixed + units / 2) / units
        : -((-fixed + units / 2) / units);
}

struct PlannedSample final
{
    std::size_t SourceOrdinal = 0U;
    StampLocalPosition LocalPosition{};
    Asset::Voxel::VoxelPosition World{};
    std::uint8_t LocalColorId = 0U;
};

// Garde-fou : au-dela, on refuse plutot que de balayer un volume absurde.
constexpr std::int64_t MaximumResampledCellCount = 8'000'000;

// Parcourt les cellules d'arrivee et va chercher, pour chacune, le voxel
// source dont elle provient (rotation inverse). Retourne false si le volume
// a balayer depasse le garde-fou.
[[nodiscard]] bool BuildResampledSamples(
    const VoxelStamp& stamp,
    const StampPlacementTransform& transform,
    std::vector<PlannedSample>& samples)
{
    constexpr std::int64_t units = StampFixedPoint::UnitsPerVoxel;
    const StampFixedPoint pivot = stamp.Pivot().LocalPosition;

    const auto toWorldFixed = [&](const RelativeFixedPoint relative)
    {
        RelativeFixedPoint value = ApplyMirror(relative, transform.Mirror);
        value = RotateQuarterTurns(
            value, transform.RotationAxis, transform.QuarterTurns);
        value = RotateHalfQuarterStep(value, transform.RotationAxis);
        return RelativeFixedPoint{
            value.X + transform.TargetPivot.X,
            value.Y + transform.TargetPivot.Y,
            value.Z + transform.TargetPivot.Z};
    };

    // Boite d'arrivee : les huit coins des bornes locales, elargis d'une
    // cellule pour absorber les arrondis.
    const StampBounds& bounds = stamp.Bounds();
    std::int64_t minimumX = 0;
    std::int64_t minimumY = 0;
    std::int64_t minimumZ = 0;
    std::int64_t maximumX = 0;
    std::int64_t maximumY = 0;
    std::int64_t maximumZ = 0;
    bool firstCorner = true;
    for (int corner = 0; corner < 8; ++corner)
    {
        const std::int32_t x = (corner & 1) ? bounds.Maximum.X : bounds.Minimum.X;
        const std::int32_t y = (corner & 2) ? bounds.Maximum.Y : bounds.Minimum.Y;
        const std::int32_t z = (corner & 4) ? bounds.Maximum.Z : bounds.Minimum.Z;
        const RelativeFixedPoint world = toWorldFixed({
            static_cast<std::int64_t>(x) * units - pivot.X,
            static_cast<std::int64_t>(y) * units - pivot.Y,
            static_cast<std::int64_t>(z) * units - pivot.Z});
        const std::int64_t cellX = RoundToCell(world.X);
        const std::int64_t cellY = RoundToCell(world.Y);
        const std::int64_t cellZ = RoundToCell(world.Z);
        if (firstCorner)
        {
            minimumX = maximumX = cellX;
            minimumY = maximumY = cellY;
            minimumZ = maximumZ = cellZ;
            firstCorner = false;
            continue;
        }
        minimumX = std::min(minimumX, cellX);
        minimumY = std::min(minimumY, cellY);
        minimumZ = std::min(minimumZ, cellZ);
        maximumX = std::max(maximumX, cellX);
        maximumY = std::max(maximumY, cellY);
        maximumZ = std::max(maximumZ, cellZ);
    }
    --minimumX; --minimumY; --minimumZ;
    ++maximumX; ++maximumY; ++maximumZ;

    const std::int64_t extentX = maximumX - minimumX + 1;
    const std::int64_t extentY = maximumY - minimumY + 1;
    const std::int64_t extentZ = maximumZ - minimumZ + 1;
    if (extentX <= 0 || extentY <= 0 || extentZ <= 0) return false;
    if (extentX > MaximumResampledCellCount / extentY ||
        extentX * extentY > MaximumResampledCellCount / extentZ)
    {
        return false;
    }

    // Table des voxels source, pour la recherche par position locale.
    std::unordered_map<std::int64_t, std::size_t> sourceByPosition;
    sourceByPosition.reserve(stamp.Voxels().size() * 2U);
    for (std::size_t ordinal = 0U; ordinal < stamp.Voxels().size(); ++ordinal)
    {
        const StampVoxel& voxel = stamp.Voxels()[ordinal];
        sourceByPosition.emplace(
            PackLocalPosition(
                voxel.Position.X, voxel.Position.Y, voxel.Position.Z),
            ordinal);
    }

    samples.reserve(stamp.Voxels().size());
    for (std::int64_t z = minimumZ; z <= maximumZ; ++z)
    {
        for (std::int64_t y = minimumY; y <= maximumY; ++y)
        {
            for (std::int64_t x = minimumX; x <= maximumX; ++x)
            {
                // Rotation inverse : cellule d'arrivee -> position locale.
                RelativeFixedPoint value{
                    x * units - transform.TargetPivot.X,
                    y * units - transform.TargetPivot.Y,
                    z * units - transform.TargetPivot.Z};
                value = UndoHalfQuarterStep(value, transform.RotationAxis);
                value = RotateQuarterTurns(
                    value, transform.RotationAxis,
                    static_cast<std::uint8_t>(
                        (4U - (transform.QuarterTurns % 4U)) % 4U));
                value = ApplyMirror(value, transform.Mirror);
                const std::int64_t localX = RoundToCell(value.X + pivot.X);
                const std::int64_t localY = RoundToCell(value.Y + pivot.Y);
                const std::int64_t localZ = RoundToCell(value.Z + pivot.Z);
                if (localX < bounds.Minimum.X || localX > bounds.Maximum.X ||
                    localY < bounds.Minimum.Y || localY > bounds.Maximum.Y ||
                    localZ < bounds.Minimum.Z || localZ > bounds.Maximum.Z)
                {
                    continue;
                }
                const auto found = sourceByPosition.find(PackLocalPosition(
                    static_cast<std::int32_t>(localX),
                    static_cast<std::int32_t>(localY),
                    static_cast<std::int32_t>(localZ)));
                if (found == sourceByPosition.end()) continue;
                const StampVoxel& source = stamp.Voxels()[found->second];
                samples.push_back({
                    found->second,
                    source.Position,
                    {static_cast<std::int32_t>(x),
                     static_cast<std::int32_t>(y),
                     static_cast<std::int32_t>(z)},
                    source.LocalColorId});
            }
        }
    }
    return true;
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
        if (request.Transform.QuarterTurns > 3U ||
            (request.Transform.RotationAxis !=
                 StampPlacementRotationAxis::VerticalY &&
             request.Transform.RotationAxis !=
                 StampPlacementRotationAxis::LateralX &&
             request.Transform.RotationAxis !=
                 StampPlacementRotationAxis::DepthZ))
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

        // STAMP-25 : les deux chemins produisent la meme liste d'echantillons
        // (source -> cellule d'arrivee) ; toute la suite est commune.
        std::vector<PlannedSample> samples;
        bool representable = true;
        if (!request.Transform.HalfQuarterStep)
        {
            samples.reserve(stamp.Voxels().size());
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
                samples.push_back({ordinal, source.Position, world,
                    source.LocalColorId});
            }
        }
        else if (!BuildResampledSamples(stamp, request.Transform, samples))
        {
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::UnsupportedRotation,
                StampPlacementDiagnosticSeverity::Error);
            representable = false;
        }
        else
        {
            plan.Statistics.ApproximateRotation = true;
            AddDiagnostic(
                plan, StampPlacementDiagnosticCode::ApproximateRotation,
                StampPlacementDiagnosticSeverity::Warning);
        }

        plan.Voxels.reserve(samples.size());
        bool collisionRejected = false;
        for (const PlannedSample& sample : samples)
        {
            const std::size_t ordinal = sample.SourceOrdinal;
            const Asset::Voxel::VoxelPosition world = sample.World;

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
                stamp.Palette()[sample.LocalColorId].Color;
            plan.Voxels.push_back({
                .SourceOrdinal = ordinal,
                .LocalPosition = sample.LocalPosition,
                .WorldPosition = world,
                .LocalPaletteIndex = sample.LocalColorId,
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
