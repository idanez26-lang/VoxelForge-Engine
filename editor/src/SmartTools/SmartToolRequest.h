#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartTool.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{
// The planner only needs value data and an occupancy query. SourceIdentity and
// SourceRevision are opaque cache inputs; neither binds the session to a
// VoxelDocument nor gives this domain layer ownership of one.
struct SmartToolRequest final
{
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartAction Action = SmartAction::Add;
    SmartBrushRequest BrushRequest{};
    std::uintptr_t SourceIdentity = 0U;
    std::uint64_t SourceRevision = 0U;
    std::uint64_t SourceGeneration = 0U;
    std::size_t SourceSubModelIndex = 0U;
    // Value snapshot only. It identifies the profile that supplied the brush
    // values without giving the plan a mutable dependency on that profile.
    std::string ActiveProfileUuid;
    // A real construction-plane interaction, distinct from ordinary hit
    // placement. It is session state, not part of geometry cache identity.
    std::optional<SmartBrushPlacement> Workplane;
};

struct SmartToolRequestKey final
{
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartAction Action = SmartAction::Add;
    Asset::Voxel::VoxelDimensions Dimensions{};
    SmartBrushState State{};
    SmartBrushPlacement Placement{};
    std::uintptr_t SourceIdentity = 0U;
    std::uint64_t SourceRevision = 0U;
    std::uint64_t SourceGeneration = 0U;
    std::size_t SourceSubModelIndex = 0U;
    std::string ActiveProfileUuid;

    [[nodiscard]] bool operator==(const SmartToolRequestKey& other) const noexcept
    {
        return Geometry == other.Geometry && Action == other.Action &&
            Dimensions.X == other.Dimensions.X &&
            Dimensions.Y == other.Dimensions.Y &&
            Dimensions.Z == other.Dimensions.Z && State == other.State &&
            Placement.Target == other.Placement.Target &&
            Placement.Normal == other.Placement.Normal &&
            SourceIdentity == other.SourceIdentity &&
            SourceRevision == other.SourceRevision &&
            SourceGeneration == other.SourceGeneration &&
            SourceSubModelIndex == other.SourceSubModelIndex &&
            ActiveProfileUuid == other.ActiveProfileUuid;
    }
};

[[nodiscard]] inline SmartToolRequestKey MakeSmartToolRequestKey(
    const SmartToolRequest& request) noexcept
{
    return {request.Geometry, request.Action, request.BrushRequest.Dimensions,
        request.BrushRequest.State, request.BrushRequest.Placement,
        request.SourceIdentity, request.SourceRevision, request.SourceGeneration,
        request.SourceSubModelIndex, request.ActiveProfileUuid};
}
} // namespace VoxelForge::Editor
