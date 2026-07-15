#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelSelection/VoxelSelectionState.h"

#include "VoxelForge/Voxel/VoxelGrid.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace
{
bool Check(const bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

VoxelForge::Voxel::VoxelGrid MakeGrid()
{
    VoxelForge::Voxel::VoxelGrid grid;
    static_cast<void>(grid.Resize(4U, 4U, 4U));
    static_cast<void>(grid.Set(1U, 1U, 1U,
        {0U, VoxelForge::Voxel::Voxel::OccupiedFlag}));
    static_cast<void>(grid.Set(2U, 2U, 2U, {9U, 0x80U}));
    return grid;
}

bool HitAt(
    const VoxelForge::Voxel::VoxelGrid& grid,
    const VoxelForge::Editor::VoxelRay ray,
    const VoxelForge::Editor::VoxelCoordinates expected,
    const VoxelForge::Editor::VoxelHitFace face)
{
    const auto hit = VoxelForge::Editor::RaycastVoxelGrid(grid, ray);
    return Check(hit && hit->Coordinates == expected && hit->Face == face,
        "Raycast returned an incorrect voxel or face.");
}
}

int main()
{
    using namespace VoxelForge;
    bool passed = true;
    Voxel::VoxelGrid grid = MakeGrid();
    const std::vector<Voxel::Voxel> originalData = grid.Data();

    passed &= HitAt(grid, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::NegativeX);
    passed &= HitAt(grid, {{6.0F, 1.5F, 1.5F}, {-1.0F, 0.0F, 0.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::PositiveX);
    passed &= HitAt(grid, {{1.5F, -2.0F, 1.5F}, {0.0F, 1.0F, 0.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::NegativeY);
    passed &= HitAt(grid, {{1.5F, 6.0F, 1.5F}, {0.0F, -1.0F, 0.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::PositiveY);
    passed &= HitAt(grid, {{1.5F, 1.5F, -2.0F}, {0.0F, 0.0F, 1.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::NegativeZ);
    passed &= HitAt(grid, {{1.5F, 1.5F, 6.0F}, {0.0F, 0.0F, -1.0F}},
        {1U, 1U, 1U}, Editor::VoxelHitFace::PositiveZ);

    const auto inside = Editor::RaycastVoxelGrid(
        grid, {{1.25F, 1.25F, 1.25F}, {1.0F, 0.0F, 0.0F}});
    passed &= Check(inside && inside->Distance == 0.0F &&
        inside->Face == Editor::VoxelHitFace::None &&
        inside->ColorIndex == 0U,
        "A ray inside an occupied color-zero voxel is incorrect.");
    const auto insideGrid = Editor::RaycastVoxelGrid(
        grid, {{0.25F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
    passed &= Check(insideGrid && insideGrid->Coordinates ==
        Editor::VoxelCoordinates{1U, 1U, 1U} &&
        insideGrid->Face == Editor::VoxelHitFace::NegativeX,
        "A ray beginning in an empty grid cell is incorrect.");
    passed &= Check(!Editor::RaycastVoxelGrid(
        grid, {{-1.0F, 3.5F, 3.5F}, {1.0F, 0.0F, 0.0F}}),
        "An empty ray must miss.");
    passed &= Check(!Editor::RaycastVoxelGrid(
        grid, {{-1.0F, 2.5F, 2.5F}, {1.0F, 0.0F, 0.0F}}),
        "Reserved voxel flags must not count as occupied.");
    passed &= Check(!Editor::RaycastVoxelGrid(
        grid, {{-1.0F, 1.5F, 1.5F}, {0.0F, 0.0F, 0.0F}}),
        "A zero direction must miss.");
    const float infinity = std::numeric_limits<float>::infinity();
    passed &= Check(!Editor::RaycastVoxelGrid(
        grid, {{infinity, 1.5F, 1.5F}, {-1.0F, 0.0F, 0.0F}}) &&
        !Editor::RaycastVoxelGrid(
            grid, {{-1.0F, 1.5F, 1.5F}, {infinity, 0.0F, 0.0F}}),
        "Non-finite rays must be rejected.");
    passed &= Check(!Editor::RaycastVoxelGrid(
        grid, {{-1.0F, 8.0F, 1.0F}, {0.0F, -1.0F, 0.0F}}),
        "A parallel ray outside the grid must miss.");

    Voxel::VoxelGrid boundary;
    passed &= Check(boundary.Resize(2U, 1U, 1U),
        "Boundary grid allocation failed.");
    passed &= Check(boundary.Set(0U, 0U, 0U,
        {4U, Voxel::Voxel::OccupiedFlag}), "Boundary voxel setup failed.");
    passed &= HitAt(boundary,
        {{2.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}},
        {0U, 0U, 0U}, Editor::VoxelHitFace::PositiveX);
    passed &= HitAt(boundary,
        {{0.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}},
        {0U, 0U, 0U}, Editor::VoxelHitFace::NegativeX);
    passed &= Check(!Editor::RaycastVoxelGrid(boundary,
        {{2.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}}),
        "A ray leaving an exact maximum boundary must miss.");
    passed &= Check(!Editor::RaycastVoxelGrid(boundary,
        {{0.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}}),
        "A ray leaving an exact minimum boundary must miss.");
    passed &= HitAt(boundary,
        {{1.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}},
        {0U, 0U, 0U}, Editor::VoxelHitFace::PositiveX);
    passed &= Check(boundary.Set(1U, 0U, 0U,
        {5U, Voxel::Voxel::OccupiedFlag}),
        "Internal boundary voxel setup failed.");
    const auto parallelBoundary = Editor::RaycastVoxelGrid(boundary,
        {{1.0F, 0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}});
    passed &= Check(parallelBoundary &&
        parallelBoundary->Coordinates == Editor::VoxelCoordinates{1U, 0U, 0U} &&
        parallelBoundary->Face == Editor::VoxelHitFace::None,
        "A stationary boundary axis must not become the hit face.");

    Voxel::VoxelGrid aligned;
    passed &= Check(aligned.Resize(5U, 1U, 1U) &&
        aligned.Set(1U, 0U, 0U, {2U, Voxel::Voxel::OccupiedFlag}) &&
        aligned.Set(3U, 0U, 0U, {3U, Voxel::Voxel::OccupiedFlag}),
        "Aligned voxel setup failed.");
    const Editor::VoxelRay alignedRay{{-1.0F, 0.5F, 0.5F},
        {1.0F, 0.0F, 0.0F}};
    const auto firstAlignedHit = Editor::RaycastVoxelGrid(aligned, alignedRay);
    const auto repeatedAlignedHit = Editor::RaycastVoxelGrid(aligned, alignedRay);
    passed &= Check(firstAlignedHit && repeatedAlignedHit &&
        firstAlignedHit == repeatedAlignedHit &&
        firstAlignedHit->Coordinates == Editor::VoxelCoordinates{1U, 0U, 0U},
        "The closest aligned voxel or deterministic result is incorrect.");

    Voxel::VoxelGrid empty;
    passed &= Check(!Editor::RaycastVoxelGrid(
        empty, {{}, {1.0F, 0.0F, 0.0F}}),
        "A zero-sized grid must miss.");
    Voxel::VoxelGrid flat;
    passed &= Check(flat.Resize(3U, 1U, 3U) &&
        flat.Set(2U, 0U, 2U, {7U, Voxel::Voxel::OccupiedFlag}),
        "Flat grid setup failed.");
    passed &= HitAt(flat, {{2.5F, 4.0F, 2.5F}, {0.0F, -1.0F, 0.0F}},
        {2U, 0U, 2U}, Editor::VoxelHitFace::PositiveY);

    Editor::VoxelSelectionState state;
    passed &= Check(!state.Hovered() && !state.Selected(),
        "Selection state must start empty.");
    const auto hit = Editor::RaycastVoxelGrid(
        grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
    passed &= Check(state.SetHovered(hit) && state.SelectHovered() &&
        state.Selected() == hit, "Hover-to-selection transition failed.");
    passed &= Check(!state.SetHovered(hit) && !state.SelectHovered(),
        "Stable selection must not report a GPU-relevant change.");
    passed &= Check(state.SetHovered(std::nullopt) && state.SelectHovered() &&
        !state.Selected(), "An empty click must clear selection.");
    passed &= Check(state.SetHovered(hit) && state.SelectHovered() &&
        state.ClearSelection() && !state.Selected(),
        "Escape-style selection clearing failed.");
    const auto replacementHit = Editor::RaycastVoxelGrid(
        aligned, {{6.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}});
    passed &= Check(state.SelectHovered() &&
        state.SetHovered(replacementHit) && state.SelectHovered() &&
        state.Selected() && state.Selected()->Face ==
            Editor::VoxelHitFace::PositiveX &&
        state.Selected()->ColorIndex == 3U,
        "Selection replacement did not preserve face and color metadata.");
    passed &= Check(state.Clear() && !state.Hovered() && !state.Selected(),
        "Full selection state clearing failed.");
    passed &= Check(grid.Data() == originalData,
        "Raycasting or selection modified the source grid.");

    return passed ? 0 : 1;
}
