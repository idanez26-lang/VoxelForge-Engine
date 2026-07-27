#pragma once

#include "SmartTools/SmartToolPlan.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{
// Renderer-neutral final-state presentation data.  It is composed by applying
// an already resolved immutable plan to a private document copy, then using the
// same VoxelMeshBuilder path as a committed document.  No planner work happens
// here: positions, colours and Before/After values come exclusively from Plan.
struct SmartToolExactPreviewMesh final
{
    bool Active = false;
    Mesh::MeshData Mesh;
    Voxel::VoxelPalette Palette;
    std::string Error;

    [[nodiscard]] bool Succeeded() const noexcept { return Error.empty(); }
    [[nodiscard]] bool Empty() const noexcept { return Mesh.Empty(); }
};

class SmartToolExactPreviewComposer final
{
public:
    [[nodiscard]] static SmartToolExactPreviewMesh Compose(
        const Asset::Voxel::VoxelDocument& document, const SmartToolPlan& plan);
};

// One-entry cache keyed by immutable plan identity plus the document identity
// and revision from which its Before state was observed.  It deliberately does
// not inspect the planner or renderer.
class SmartToolExactPreviewCache final
{
public:
    [[nodiscard]] const SmartToolExactPreviewMesh& Resolve(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentIdentity,
        SmartToolPlanPtr plan);
    void Clear() noexcept;
    [[nodiscard]] std::size_t BuildCount() const noexcept;

private:
    const Asset::Voxel::VoxelDocument* document_ = nullptr;
    std::uint64_t documentIdentity_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    SmartToolPlanPtr plan_;
    SmartToolExactPreviewMesh mesh_{};
    std::size_t buildCount_ = 0U;
};
} // namespace VoxelForge::Editor
