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

[[nodiscard]] StampNormal RotateNormal(
    const StampNormal normal,
    const std::uint8_t quarterTurns) noexcept
{
    switch (quarterTurns)
    {
    case 0U:
        return normal;
    case 1U:
        return {.X = normal.Z, .Y = normal.Y,
            .Z = static_cast<std::int8_t>(-normal.X)};
    case 2U:
        return {.X = static_cast<std::int8_t>(-normal.X), .Y = normal.Y,
            .Z = static_cast<std::int8_t>(-normal.Z)};
    case 3U:
        return {.X = static_cast<std::int8_t>(-normal.Z), .Y = normal.Y,
            .Z = normal.X};
    default:
        return {};
    }
}

[[nodiscard]] std::optional<std::uint8_t> FindQuarterTurns(
    const StampNormal localNormal,
    const StampNormal targetNormal) noexcept
{
    if (localNormal.Y != targetNormal.Y)
    {
        return std::nullopt;
    }
    for (std::uint8_t quarterTurns = 0U; quarterTurns < 4U; ++quarterTurns)
    {
        if (RotateNormal(localNormal, quarterTurns) == targetNormal)
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
                targetNormal);
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
