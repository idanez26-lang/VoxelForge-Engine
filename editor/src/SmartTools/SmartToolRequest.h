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

// A Face operation starts from the hit voxel, not from the resulting target.
// This keeps Add (which writes one cell along the normal) and Erase/Paint
// (which write the hit cell) on one explicit, immutable planning contract.
struct SmartToolFaceSeed final
{
    Asset::Voxel::VoxelPosition Position{};
    Asset::Voxel::VoxelPosition Normal{0, 1, 0};

    [[nodiscard]] bool operator==(const SmartToolFaceSeed&) const noexcept =
        default;
};

// A canonical, grid-aligned plane captured exactly once at Geometry
// MouseDown. Origin is the first point; U/V are the deterministic planar
// axes used by the planner to expand the selected shape.
struct SmartToolGeometryPlane final
{
    Asset::Voxel::VoxelPosition Origin{};
    Asset::Voxel::VoxelPosition Normal{0, 1, 0};
    Asset::Voxel::VoxelPosition UAxis{1, 0, 0};
    Asset::Voxel::VoxelPosition VAxis{0, 0, 1};
    // Geometric face coordinate, distinct from Origin's discrete sampling
    // coordinate (for example a +X face at voxel X is X + 1).
    float SurfaceCoordinate = 0.0F;

    [[nodiscard]] bool operator==(const SmartToolGeometryPlane&) const noexcept =
        default;
};

// The planner only needs value data and a versioned cell reader.
// SourceIdentity and SourceRevision are opaque cache/integrity inputs; neither
// gives this domain layer ownership of a VoxelDocument.
struct SmartToolRequest final
{
    SmartGeometry Geometry = SmartGeometry::Pencil;
    // Optional only to preserve isolated legacy callers during migration.
    // The active Smart Tool always supplies an explicit SMART-05 mode.
    std::optional<SmartToolMode> Mode;
    SmartAction Action = SmartAction::Add;
    SmartBrushRequest BrushRequest{};
    SmartToolVoxelReader ReadVoxel;
    // Face visibility is resolved against the source document captured at the
    // beginning of a stroke. The ordinary reader may include stroke-local
    // virtual edits so it remains the authoritative Before/After reader.
    SmartToolVoxelReader ReadFaceSupportVoxel;
    // Surface Add accepts only cells connected to the locked source surface
    // or to an earlier Add from this same stroke. This callback exposes that
    // transient extension layer without allowing unrelated document voxels to
    // bridge an island into the locked component.
    SmartToolVoxelReader ReadSurfaceExtensionVoxel;
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
    // Required only for SmartGeometry::Face. Workplanes intentionally cannot
    // seed a face because a face is defined by an existing exposed voxel.
    std::optional<SmartToolFaceSeed> FaceSeed;
    // Face Add depth is measured in exact voxel layers from the locked
    // exposed support surface. Face Paint/Erase always use one layer.
    int FaceDepth = 1;
    // Required only for SmartGeometry::Line. Point A is captured on MouseDown;
    // Placement.Target remains the live endpoint B.
    std::optional<Asset::Voxel::VoxelPosition> LineStart;
    // Required only for SmartGeometry::Geometry. Placement.Target is the
    // current, already projected point B. Cylinder height is signed along the
    // locked plane normal and always has a non-zero magnitude.
    std::optional<SmartToolGeometryPlane> GeometryPlane;
    std::optional<float> GeometrySurfaceCoordinate;
    int GeometryHeight = 1;
    std::uintptr_t SourceIdentity = 0U;
    std::uint64_t SourceRevision = 0U;
    // A transient overlay revision. It is zero for ordinary preview/commit
    // requests and advances only while a continuous stroke provides a virtual
    // document reader, preventing session cache reuse across stroke samples.
    std::uint64_t VirtualRevision = 0U;
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
    std::optional<SmartToolFaceSeed> FaceSeed;
    int FaceDepth = 1;
    std::optional<Asset::Voxel::VoxelPosition> LineStart;
    std::optional<SmartToolGeometryPlane> GeometryPlane;
    int GeometryHeight = 1;
    std::uintptr_t SourceIdentity = 0U;
    std::uint64_t SourceRevision = 0U;
    std::uint64_t VirtualRevision = 0U;
    std::uint64_t SourceGeneration = 0U;
    std::size_t SourceSubModelIndex = 0U;
    std::string ActiveProfileUuid;
    float PreviewAlpha = 0.5F;
    bool HasPaletteColors = false;
    std::uint64_t PaletteColorSignature = 0U;

    [[nodiscard]] bool operator==(const SmartToolRequestKey& other) const noexcept
    {
        return Geometry == other.Geometry &&
            Mode == other.Mode && Action == other.Action &&
            Dimensions.X == other.Dimensions.X &&
            Dimensions.Y == other.Dimensions.Y &&
            Dimensions.Z == other.Dimensions.Z && State == other.State &&
            Placement.Target == other.Placement.Target &&
            Placement.Normal == other.Placement.Normal &&
            ReplacePaletteIndex == other.ReplacePaletteIndex &&
            FaceSeed == other.FaceSeed &&
            FaceDepth == other.FaceDepth &&
            LineStart == other.LineStart &&
            GeometryPlane == other.GeometryPlane &&
            GeometryHeight == other.GeometryHeight &&
            SourceIdentity == other.SourceIdentity &&
            SourceRevision == other.SourceRevision &&
            VirtualRevision == other.VirtualRevision &&
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
        // Pencil owns its selected dimension.  Its V1 Surface2D mode is
        // always face/workplane-projected, however, so Auto is the resolved
        // orientation even when a legacy profile still contains a fixed axis.
        // This keeps preview, cached plan, and commit on the same slice.
        if (request.Geometry == SmartGeometry::Pencil)
        {
            state.Orientation = SmartBrushOrientation::Auto;
        }
        else
        {
            state.Dimension = request.Geometry == SmartGeometry::Surface
                ? SmartBrushDimension::Surface2D
                : SmartBrushDimension::Volume3D;
            state.Orientation = SmartBrushOrientation::Auto;
        }
        switch (*request.Mode)
        {
        case SmartToolMode::SingleVoxel:
            state.Shape = SmartBrushShape::Cube;
            state.Size = 1;
            break;
        case SmartToolMode::CubeBrush:
            state.Shape = SmartBrushShape::Cube;
            break;
        case SmartToolMode::SphereBrush:
            state.Shape = SmartBrushShape::Sphere;
            break;
        case SmartToolMode::CylinderBrush:
            state.Shape = SmartBrushShape::Cylinder;
            break;
        }
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
    return {geometry, request.Mode, request.Action,
        request.BrushRequest.Dimensions,
        state, request.BrushRequest.Placement,
        request.ReplacePaletteIndex,
        request.FaceSeed,
        request.FaceDepth,
        request.LineStart,
        request.GeometryPlane,
        request.GeometryHeight,
        request.SourceIdentity, request.SourceRevision, request.VirtualRevision,
        request.SourceGeneration,
        request.SourceSubModelIndex, request.ActiveProfileUuid,
        request.PreviewAlpha, request.HasPaletteColors, paletteSignature};
}
} // namespace VoxelForge::Editor
