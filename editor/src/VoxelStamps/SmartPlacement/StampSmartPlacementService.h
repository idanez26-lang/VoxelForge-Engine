#pragma once

#include "VoxelStamps/Placement/StampPlacementPlan.h"
#include "VoxelStamps/VoxelStamp.h"

#include <cstdint>

namespace VoxelForge::Editor::Stamps
{

enum class StampSmartPlacementMode : std::uint8_t
{
    Off,
    Suggest,
    PreviewAssist
};

enum class StampSmartPlacementSuggestionStatus : std::uint8_t
{
    Suggested,
    NoContext,
    MissingStamp,
    Disabled,
    TemporarilyBypassed,
    InvalidContext
};

/// Discrete scene facts supplied by viewport picking. Surface facts take
/// precedence over the workplane when both are present.
struct StampSmartPlacementTargetContext final
{
    bool HasSurface = false;
    StampFixedPoint SurfacePoint{};
    StampNormal SurfaceNormal{};
    bool HasWorkplane = false;
    StampFixedPoint WorkplanePoint{};
    StampNormal WorkplaneNormal{};

    [[nodiscard]] bool operator==(
        const StampSmartPlacementTargetContext&) const noexcept = default;
};

/// Complete, UI-independent input to the deterministic V1 suggestion policy.
/// The user transform is copied, never mutated in place, and remains the
/// authoritative fallback.
struct StampSmartPlacementContext final
{
    const VoxelStamp* Stamp = nullptr;
    StampPlacementTransform UserTransform{};
    StampSmartPlacementTargetContext Target{};
    bool Enabled = true;
    bool TemporarilyBypassed = false;
    bool OrientationLockedByUser = false;
};

/// Advisory result. Applying it is a caller decision; this object carries no
/// collision or commit policy and cannot reject a placement.
struct StampSmartPlacementSuggestion final
{
    StampSmartPlacementSuggestionStatus Status =
        StampSmartPlacementSuggestionStatus::NoContext;
    StampPlacementTransform Transform{};
    StampPivot Pivot{};
    StampNormal AlignmentNormal{};
    bool OrientationSuggested = false;
    bool PivotSuggested = false;
    bool SurfaceAligned = false;
    bool UsedWorkplane = false;

    [[nodiscard]] bool Available() const noexcept
    {
        return Status == StampSmartPlacementSuggestionStatus::Suggested;
    }

    [[nodiscard]] bool operator==(
        const StampSmartPlacementSuggestion&) const noexcept = default;
};

/// Returns the same suggestion for identical normalized inputs. The service
/// only proposes a transform/pivot relationship; it never reads a document,
/// changes geometry, evaluates collisions or makes a commit decision.
[[nodiscard]] StampSmartPlacementSuggestion SuggestPlacement(
    const StampSmartPlacementContext& context) noexcept;

} // namespace VoxelForge::Editor::Stamps
