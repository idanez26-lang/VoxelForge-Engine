#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartTool.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{
// Complete value snapshot of one document cell. Empty cells always use palette
// zero. The reader itself belongs to the request lifetime; SmartToolPlan stores
// only the returned values and therefore never retains a document callback.
struct SmartToolVoxelState final
{
    bool Exists = false;
    std::uint8_t PaletteIndex = 0U;

    [[nodiscard]] bool operator==(const SmartToolVoxelState&) const noexcept =
        default;
};

using SmartToolVoxelReader = std::function<SmartToolVoxelState(
    Asset::Voxel::VoxelPosition)>;

// The planner only needs value data and a versioned cell reader.
// SourceIdentity and SourceRevision are opaque cache/integrity inputs; neither
// gives this domain layer ownership of a VoxelDocument.
struct SmartToolRequest final
{
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartAction Action = SmartAction::Add;
    SmartBrushRequest BrushRequest{};
    SmartToolVoxelReader ReadVoxel;
    // Internal preparation for Replace. The action remains unavailable in the
    // UI until SMART-03, but its Before/After contract can already be verified.
    std::optional<std::uint8_t> ReplacePaletteIndex;
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
    std::optional<std::uint8_t> ReplacePaletteIndex;
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
            ReplacePaletteIndex == other.ReplacePaletteIndex &&
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
    SmartBrushState state = request.BrushRequest.State;
    state.Shape = ResolveSmartBrushShape(request.Geometry, state.Shape);
    switch (request.Action)
    {
    case SmartAction::Erase: state.Mode = SmartBrushMode::Erase; break;
    case SmartAction::Paint: state.Mode = SmartBrushMode::Paint; break;
    case SmartAction::Replace: state.Mode = SmartBrushMode::Replace; break;
    case SmartAction::Add:
    default: state.Mode = SmartBrushMode::Add; break;
    }
    return {request.Geometry, request.Action, request.BrushRequest.Dimensions,
        state, request.BrushRequest.Placement,
        request.ReplacePaletteIndex,
        request.SourceIdentity, request.SourceRevision, request.SourceGeneration,
        request.SourceSubModelIndex, request.ActiveProfileUuid};
}
} // namespace VoxelForge::Editor
