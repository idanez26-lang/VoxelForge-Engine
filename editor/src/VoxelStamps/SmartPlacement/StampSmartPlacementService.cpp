#include "VoxelStamps/SmartPlacement/StampSmartPlacementService.h"

#include <optional>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] StampNormal PrincipalAxis(const StampNormal normal) noexcept
{
    const int nonZeroCount = (normal.X != 0 ? 1 : 0) +
        (normal.Y != 0 ? 1 : 0) + (normal.Z != 0 ? 1 : 0);
    if (nonZeroCount != 1)
    {
        return {};
    }
    if (normal.X != 0)
    {
        return {.X = static_cast<std::int8_t>(normal.X > 0 ? 1 : -1)};
    }
    if (normal.Y != 0)
    {
        return {.Y = static_cast<std::int8_t>(normal.Y > 0 ? 1 : -1)};
    }
    return {.Z = static_cast<std::int8_t>(normal.Z > 0 ? 1 : -1)};
}

[[nodiscard]] StampNormal MirrorNormal(
    StampNormal normal,
    const StampPlacementMirrorMode mirror) noexcept
{
    if (mirror == StampPlacementMirrorMode::X ||
        mirror == StampPlacementMirrorMode::XZ)
    {
        normal.X = static_cast<std::int8_t>(-normal.X);
    }
    if (mirror == StampPlacementMirrorMode::Z ||
        mirror == StampPlacementMirrorMode::XZ)
    {
        normal.Z = static_cast<std::int8_t>(-normal.Z);
    }
    return normal;
}

// STAMP-24 (24-3) : la normale tourne autour de l'axe ACTIF, avec les memes
// permutations que le planificateur (cf. MakeTransformedGridPosition) :
//   X : (y, z) -> (-z,  y)
//   Y : (x, z) -> ( z, -x)
//   Z : (x, y) -> (-y,  x)
[[nodiscard]] StampNormal RotateNormal(
    const StampNormal normal,
    const StampPlacementRotationAxis axis,
    const std::uint8_t quarterTurns) noexcept
{
    if (quarterTurns > 3U) return {};
    StampNormal rotated = normal;
    for (std::uint8_t turn = 0U; turn < quarterTurns; ++turn)
    {
        const StampNormal previous = rotated;
        switch (axis)
        {
        case StampPlacementRotationAxis::LateralX:
            rotated.Y = static_cast<std::int8_t>(-previous.Z);
            rotated.Z = previous.Y;
            break;
        case StampPlacementRotationAxis::VerticalY:
            rotated.X = previous.Z;
            rotated.Z = static_cast<std::int8_t>(-previous.X);
            break;
        case StampPlacementRotationAxis::DepthZ:
            rotated.X = static_cast<std::int8_t>(-previous.Y);
            rotated.Y = previous.X;
            break;
        default:
            return {};
        }
    }
    return rotated;
}

// La suggestion respecte l'axe choisi par l'utilisateur : si aucune des
// quatre orientations autour de CET axe n'aligne le Stamp sur la surface,
// on ne suggere rien plutot que de basculer sur un autre axe dans son dos.
[[nodiscard]] std::optional<std::uint8_t> FindQuarterTurns(
    const StampNormal localNormal,
    const StampNormal targetNormal,
    const StampPlacementRotationAxis axis) noexcept
{
    for (std::uint8_t quarterTurns = 0U; quarterTurns < 4U; ++quarterTurns)
    {
        if (RotateNormal(localNormal, axis, quarterTurns) == targetNormal)
        {
            return quarterTurns;
        }
    }
    return std::nullopt;
}

} // namespace

StampSmartPlacementSuggestion SuggestPlacement(
    const StampSmartPlacementContext& context) noexcept
{
    StampSmartPlacementSuggestion suggestion{};
    suggestion.Transform = context.UserTransform;

    if (context.Stamp == nullptr)
    {
        suggestion.Status =
            StampSmartPlacementSuggestionStatus::MissingStamp;
        return suggestion;
    }
    suggestion.Pivot = context.Stamp->Pivot();

    if (!context.Enabled)
    {
        suggestion.Status = StampSmartPlacementSuggestionStatus::Disabled;
        return suggestion;
    }
    if (context.TemporarilyBypassed)
    {
        suggestion.Status =
            StampSmartPlacementSuggestionStatus::TemporarilyBypassed;
        return suggestion;
    }
    if (!context.Target.HasSurface && !context.Target.HasWorkplane)
    {
        suggestion.Status = StampSmartPlacementSuggestionStatus::NoContext;
        return suggestion;
    }

    const bool useSurface = context.Target.HasSurface;
    const StampNormal targetNormal = PrincipalAxis(useSurface
        ? context.Target.SurfaceNormal
        : context.Target.WorkplaneNormal);
    if (targetNormal == StampNormal{})
    {
        suggestion.Status =
            StampSmartPlacementSuggestionStatus::InvalidContext;
        return suggestion;
    }

    suggestion.Status = StampSmartPlacementSuggestionStatus::Suggested;
    suggestion.AlignmentNormal = targetNormal;
    suggestion.Transform.TargetPivot = useSurface
        ? context.Target.SurfacePoint
        : context.Target.WorkplanePoint;
    suggestion.PivotSuggested = true;
    suggestion.SurfaceAligned = useSurface;
    suggestion.UsedWorkplane = !useSurface;

    if (!context.OrientationLockedByUser)
    {
        const StampNormal localNormal = PrincipalAxis(
            context.Stamp->Pivot().LocalNormal);
        if (localNormal != StampNormal{})
        {
            const auto quarterTurns = FindQuarterTurns(
                MirrorNormal(localNormal, context.UserTransform.Mirror),
                targetNormal, context.UserTransform.RotationAxis);
            if (quarterTurns)
            {
                suggestion.Transform.QuarterTurns = *quarterTurns;
                suggestion.OrientationSuggested = true;
            }
        }
    }

    return suggestion;
}

} // namespace VoxelForge::Editor::Stamps
