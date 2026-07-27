#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartTool.h"

#include <array>
#include <bit>
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
    // Optional only to preserve isolated legacy callers during migration.
    // The active Smart Tool always supplies an explicit SMART-04 mode.
    std::optional<SmartToolMode> Mode;
    SmartAction Action = SmartAction::Add;
    SmartBrushRequest BrushRequest{};
    SmartToolVoxelReader ReadVoxel;
    // A value snapshot captured at the planning boundary.  The plan copies the
    // exact Before/After colours it needs, so downstream preview code never
    // reads a palette or document.
    std::array<std::array<float, 4>, 256U> PaletteColors{};
    bool HasPaletteColors = false;
    // Presentation input is captured with the immutable plan.  It is never
    // read from UI state by the preview engine.
    float PreviewAlpha = 0.5F;
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
    std::optional<SmartToolMode> Mode;
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
    float PreviewAlpha = 0.5F;
    bool HasPaletteColors = false;
    std::uint64_t PaletteColorSignature = 0U;

    [[nodiscard]] bool operator==(const SmartToolRequestKey& other) const noexcept
    {
        return Geometry == other.Geometry && Mode == other.Mode && Action == other.Action &&
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
            ActiveProfileUuid == other.ActiveProfileUuid &&
            PreviewAlpha == other.PreviewAlpha &&
            HasPaletteColors == other.HasPaletteColors &&
            PaletteColorSignature == other.PaletteColorSignature;
    }
};

[[nodiscard]] inline SmartToolRequestKey MakeSmartToolRequestKey(
    const SmartToolRequest& request) noexcept
{
    constexpr std::uint64_t offsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t paletteSignature = offsetBasis;
    paletteSignature ^= request.HasPaletteColors ? 1ULL : 0ULL;
    paletteSignature *= prime;
    if (request.HasPaletteColors)
    {
        for (const std::array<float, 4>& color : request.PaletteColors)
            for (const float component : color)
            {
                paletteSignature ^= std::bit_cast<std::uint32_t>(component);
                paletteSignature *= prime;
            }
    }
    SmartGeometry geometry = request.Geometry;
    SmartBrushState state = request.BrushRequest.State;
    if (request.Mode)
    {
        geometry = SmartGeometry::Pencil;
        state.Shape = SmartBrushShape::Cube;
        state.Dimension = SmartBrushDimension::Volume3D;
        state.Orientation = SmartBrushOrientation::Auto;
        if (*request.Mode == SmartToolMode::SingleVoxel) state.Size = 1;
    }
    else state.Shape = ResolveSmartBrushShape(request.Geometry, state.Shape);
    switch (request.Action)
    {
    case SmartAction::Erase: state.Mode = SmartBrushMode::Erase; break;
    case SmartAction::Paint: state.Mode = SmartBrushMode::Paint; break;
    case SmartAction::Replace: state.Mode = SmartBrushMode::Replace; break;
    case SmartAction::Add:
    default: state.Mode = SmartBrushMode::Add; break;
    }
    return {geometry, request.Mode, request.Action, request.BrushRequest.Dimensions,
        state, request.BrushRequest.Placement,
        request.ReplacePaletteIndex,
        request.SourceIdentity, request.SourceRevision, request.SourceGeneration,
        request.SourceSubModelIndex, request.ActiveProfileUuid,
        request.PreviewAlpha, request.HasPaletteColors, paletteSignature};
}
} // namespace VoxelForge::Editor
