#include "Selection/SelectionService.h"
#include "Selection/SelectionInteraction.h"
#include "Selection/SelectionHighlightPolicy.h"
#include "Selection/SelectionVolumeCache.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelPosition;
using VoxelForge::Editor::SelectionMode;
using VoxelForge::Editor::SelectionService;
using VoxelForge::Editor::SelectionBounds;
using VoxelForge::Editor::SelectionInteraction;
using VoxelForge::Editor::SelectionInteractionMode;
using VoxelForge::Editor::SelectionPointerRelease;
using VoxelForge::Editor::SelectionFace;
using VoxelForge::Editor::SelectionHandle;
using VoxelForge::Editor::SelectionHighlightPolicy;
using VoxelForge::Editor::SelectionVolumeCache;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void TestEmptyAndSingleSelection()
{
    SelectionService selection;
    Require(selection.Empty() && selection.Count() == 0U,
        "A new selection must be empty.");
    Require(!selection.Bounds().Valid && !selection.Center(),
        "An empty selection must not expose bounds or a center.");
    Require(selection.Select({4, 2, 7}), "The first selection must change.");
    Require(selection.Count() == 1U && selection.Contains({4, 2, 7}),
        "Single selection lookup failed.");
    Require(selection.Bounds().Minimum == VoxelPosition{4, 2, 7} &&
            selection.Bounds().Maximum == VoxelPosition{4, 2, 7} &&
            selection.Bounds().Dimensions() ==
                VoxelForge::Asset::Voxel::VoxelDimensions{1, 1, 1},
        "Single selection bounds are invalid.");
    Require(selection.Center() ==
            VoxelForge::Editor::SelectionCenter{4.0F, 2.0F, 7.0F},
        "Single selection center is invalid.");
}

void TestModesAndDeterminism()
{
    SelectionService selection;
    const std::vector<VoxelPosition> initial{{3, 1, 2}, {0, 0, 0},
        {3, 1, 2}, {1, 5, -2}};
    Require(selection.Apply(initial, SelectionMode::Replace),
        "Replace should accept the positions.");
    Require(selection.Count() == 3U,
        "Replace must remove duplicate positions.");
    const auto voxels = selection.Voxels();
    Require(voxels[0] == VoxelPosition{0, 0, 0} &&
            voxels[1] == VoxelPosition{1, 5, -2} &&
            voxels[2] == VoxelPosition{3, 1, 2},
        "Selection iteration must be deterministic.");
    Require(selection.Select({7, 7, 7}, SelectionMode::Replace) &&
            selection.Count() == 1U && selection.Contains({7, 7, 7}),
        "Replace mode must discard the previous selection.");
    Require(selection.Apply(initial, SelectionMode::Replace),
        "Replace should restore the multi-selection.");
    Require(!selection.Select({3, 1, 2}, SelectionMode::Add),
        "Adding a duplicate must be a no-op.");
    Require(selection.Select({9, 9, 9}, SelectionMode::Add) &&
            selection.Count() == 4U,
        "Add mode failed.");
    Require(selection.Select({1, 5, -2}, SelectionMode::Subtract) &&
            !selection.Contains({1, 5, -2}),
        "Subtract mode failed.");
    const std::vector<VoxelPosition> intersection{{3, 1, 2}, {8, 8, 8}};
    Require(selection.Apply(intersection, SelectionMode::Intersect) &&
            selection.Count() == 1U && selection.Contains({3, 1, 2}),
        "Intersect mode failed.");
}

void TestBoundsCenterAndClear()
{
    SelectionService selection;
    const std::vector<VoxelPosition> positions{{-2, 4, 8}, {6, -4, 2}};
    static_cast<void>(selection.Apply(positions, SelectionMode::Replace));
    Require(selection.Bounds().Minimum == VoxelPosition{-2, -4, 2} &&
            selection.Bounds().Maximum == VoxelPosition{6, 4, 8} &&
            selection.Bounds().Dimensions() ==
                VoxelForge::Asset::Voxel::VoxelDimensions{9, 9, 7},
        "Multi-selection bounds are invalid.");
    Require(selection.Center() ==
            VoxelForge::Editor::SelectionCenter{2.0F, 0.0F, 5.0F},
        "Multi-selection center is invalid.");
    Require(selection.Clear() && selection.Empty(), "Clear failed.");
    Require(!selection.Clear(), "Clearing an empty selection must be a no-op.");
}

void TestDocumentLifetime()
{
    SelectionService selection;
    selection.SetDocumentGeneration(42U);
    static_cast<void>(selection.Select({1, 2, 3}));
    selection.SetDocumentGeneration(42U);
    Require(selection.Count() == 1U,
        "The same document generation must preserve selection.");
    selection.SetDocumentGeneration(43U);
    Require(selection.Empty() && selection.DocumentGeneration() == 43U,
        "Changing documents must clear selection.");
    static_cast<void>(selection.Select({1, 2, 3}));
    selection.ClearDocument();
    Require(selection.Empty() && selection.DocumentGeneration() == 0U,
        "Closing the document must clear all selection state.");
}

void TestSpatialBounds()
{
    const SelectionBounds invalid;
    Require(!invalid.Valid && invalid.Dimensions() ==
        VoxelForge::Asset::Voxel::VoxelDimensions{},
        "Invalid bounds must stay empty.");
    const SelectionBounds bounds = SelectionBounds::FromCorners(
        {5, 7, 9}, {-2, 3, 1});
    Require(bounds.Minimum == VoxelPosition{-2, 3, 1} &&
            bounds.Maximum == VoxelPosition{5, 7, 9},
        "Inverse corners must be normalized.");
    Require(bounds.Dimensions() ==
            VoxelForge::Asset::Voxel::VoxelDimensions{8, 5, 9},
        "Normalized dimensions are invalid.");
    Require(bounds.Contains({-2, 3, 1}) && bounds.Contains({5, 7, 9}) &&
            !bounds.Contains({6, 7, 9}),
        "Contains must include both boundaries only.");
    const SelectionBounds clipped = bounds.ClampedTo({4, 4, 4});
    Require(clipped.Minimum == VoxelPosition{0, 3, 1} &&
            clipped.Maximum == VoxelPosition{3, 3, 3},
        "Document clipping must keep bounds inside the voxel volume.");
}

void TestVolumeModesIgnoreEmptyCells()
{
    SelectionService selection;
    const std::vector<VoxelPosition> existing{
        {0, 0, 0}, {2, 0, 0}, {8, 8, 8}, {12, 12, 12}};
    const SelectionBounds first = SelectionBounds::FromCorners({0, 0, 0}, {3, 3, 3});
    Require(selection.SelectVolume(existing, first, SelectionMode::Replace) &&
            selection.Count() == 2U && !selection.Contains({1, 0, 0}),
        "Volume selection must select existing voxels only.");
    const SelectionBounds second = SelectionBounds::FromCorners({8, 8, 8}, {9, 9, 9});
    Require(selection.SelectVolume(existing, second, SelectionMode::Add) &&
            selection.Count() == 3U,
        "Volume Add failed.");
    Require(selection.SelectVolume(existing, first, SelectionMode::Subtract) &&
            selection.Count() == 1U && selection.Contains({8, 8, 8}),
        "Volume Subtract failed.");
    static_cast<void>(selection.SelectVolume(existing, first, SelectionMode::Add));
    Require(selection.SelectVolume(existing, first, SelectionMode::Intersect) &&
            selection.Count() == 2U,
        "Volume Intersect failed.");
    Require(selection.EditableBounds() == first,
        "The editable bounds must describe the last manipulated volume.");
}

void TestInteractionLifecycle()
{
    SelectionInteraction interaction;
    Require(!SelectionInteraction::ExceedsDragThreshold(2.0F, 2.0F) &&
            SelectionInteraction::ExceedsDragThreshold(4.0F, 0.0F),
        "The drag threshold must be deterministic.");
    Require(interaction.PointerDown(
            VoxelPosition{5, 5, 5}, 11U, SelectionMode::Add,
            100.0F, 100.0F),
        "PointerDown must begin creation from an idle state.");
    Require(interaction.Mode() == SelectionInteractionMode::Creating &&
            !interaction.IsDragRecognized(),
        "MouseDown must capture Creating before the pointer threshold.");
    Require(!interaction.PointerMove(
            102.0F, 102.0F, VoxelPosition{1, 2, 3}),
        "Movement below the threshold must remain a simple click.");
    Require(interaction.PointerMove(
            108.0F, 100.0F, VoxelPosition{1, 2, 3}),
        "MouseMove beyond the threshold must recognize a volume drag.");
    Require(interaction.CurrentBounds().Minimum == VoxelPosition{1, 2, 3} &&
            interaction.CurrentBounds().Maximum == VoxelPosition{5, 5, 5},
        "Creation preview must normalize its corners.");
    const SelectionPointerRelease created = interaction.PointerUp();
    Require(created.WasDrag && created.Bounds &&
            created.Bounds->Maximum == VoxelPosition{5, 5, 5} &&
            created.Operation == SelectionMode::Add &&
            interaction.Mode() == SelectionInteractionMode::Idle,
        "Creation commit failed.");

    Require(interaction.PointerDown(
            VoxelPosition{2, 2, 2}, 11U, SelectionMode::Replace,
            20.0F, 20.0F),
        "A second pointer sequence must begin.");
    static_cast<void>(interaction.PointerMove(
        21.0F, 21.0F, VoxelPosition{2, 2, 2}));
    const SelectionPointerRelease click = interaction.PointerUp();
    Require(!click.WasDrag && !click.Bounds && !interaction.IsActive(),
        "A sub-threshold pointer cycle must remain a simple click.");

    Require(interaction.PointerDown(
            VoxelPosition{1, 1, 1}, 13U, SelectionMode::Replace,
            0.0F, 0.0F),
        "A new volume drag must begin after validation.");
    Require(!interaction.ValidateDocumentGeneration(14U) && !interaction.IsActive(),
        "A document generation change must purge the interaction.");
}

void TestSelectionHandleGenerationProjectionAndPicking()
{
    const SelectionBounds bounds = SelectionBounds::FromCorners(
        {2, 4, 6}, {8, 10, 14});
    const auto handles = VoxelForge::Editor::GenerateSelectionHandles(bounds);
    Require(handles.size() == 6U &&
            handles[0].Face == SelectionFace::XMinimum &&
            handles[1].Face == SelectionFace::XMaximum &&
            handles[2].Face == SelectionFace::YMinimum &&
            handles[3].Face == SelectionFace::YMaximum &&
            handles[4].Face == SelectionFace::ZMinimum &&
            handles[5].Face == SelectionFace::ZMaximum,
        "Selection bounds must generate exactly the six face handles.");
    Require(handles[0].WorldPosition == VoxelForge::Editor::Vec3{2.0F, 7.5F, 10.5F} &&
            handles[1].WorldPosition == VoxelForge::Editor::Vec3{9.0F, 7.5F, 10.5F} &&
            handles[2].WorldPosition == VoxelForge::Editor::Vec3{5.5F, 4.0F, 10.5F} &&
            handles[3].WorldPosition == VoxelForge::Editor::Vec3{5.5F, 11.0F, 10.5F} &&
            handles[4].WorldPosition == VoxelForge::Editor::Vec3{5.5F, 7.5F, 6.0F} &&
            handles[5].WorldPosition == VoxelForge::Editor::Vec3{5.5F, 7.5F, 15.0F},
        "Each handle must be centered on exactly one bounds face.");

    VoxelForge::Editor::Matrix4 projection =
        VoxelForge::Editor::IdentityMatrix();
    projection[10] = 0.1F;
    projection[11] = 0.5F;
    const auto projected = VoxelForge::Editor::ProjectSelectionHandles(
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}), {},
        {0.0F, 0.0F, 100.0F, 100.0F}, projection);
    Require(std::all_of(projected.begin(), projected.end(),
            [](const SelectionHandle& handle) { return handle.Visible; }),
        "All six handles of a visible box must produce screen primitives.");
    const auto picked = VoxelForge::Editor::PickSelectionHandle(
        projected, projected[1].ScreenPosition, 8.0F);
    Require(picked && picked->Face == SelectionFace::XMaximum,
        "Screen-space picking must identify the hovered face handle.");
    Require(!VoxelForge::Editor::PickSelectionHandle(
            projected, {-100.0F, -100.0F}, 8.0F),
        "Picking outside every handle must return no face.");
}

void TestSelectionHandleSpatialBarycentres()
{
    const auto thin = VoxelForge::Editor::GenerateSelectionHandles(
        SelectionBounds::FromCorners({2, 4, 6}, {2, 4, 13}),
        {10.0F, 20.0F, 30.0F});
    const std::array<VoxelForge::Editor::Vec3, 6U> expectedThin{{
        {-8.0F, -15.5F, -20.0F},
        {-7.0F, -15.5F, -20.0F},
        {-7.5F, -16.0F, -20.0F},
        {-7.5F, -15.0F, -20.0F},
        {-7.5F, -15.5F, -24.0F},
        {-7.5F, -15.5F, -16.0F}}};
    for (std::size_t index = 0U; index < thin.size(); ++index)
        Require(thin[index].WorldPosition == expectedThin[index],
            "Long thin selection handles must use world-space face barycentres.");

    const auto wide = VoxelForge::Editor::GenerateSelectionHandles(
        SelectionBounds::FromCorners({1, 2, 3}, {5, 6, 5}));
    const std::array<VoxelForge::Editor::Vec3, 6U> expectedWide{{
        {1.0F, 4.5F, 4.5F},
        {6.0F, 4.5F, 4.5F},
        {3.5F, 2.0F, 4.5F},
        {3.5F, 7.0F, 4.5F},
        {3.5F, 4.5F, 3.0F},
        {3.5F, 4.5F, 6.0F}}};
    for (std::size_t index = 0U; index < wide.size(); ++index)
        Require(wide[index].WorldPosition == expectedWide[index],
            "Wide odd-sized selection handles must remain centered per face.");
}

void TestHandleBarycentresAfterSuccessiveAxisResizes()
{
    const auto dimensions =
        VoxelForge::Asset::Voxel::VoxelDimensions{12, 12, 12};
    SelectionBounds bounds = SelectionBounds::FromCorners(
        {1, 1, 1}, {2, 2, 2});
    bounds = VoxelForge::Editor::ResizeSelectionBounds(
        bounds, SelectionFace::XMaximum, 2, dimensions);
    bounds = VoxelForge::Editor::ResizeSelectionBounds(
        bounds, SelectionFace::YMinimum, -1, dimensions);
    bounds = VoxelForge::Editor::ResizeSelectionBounds(
        bounds, SelectionFace::ZMaximum, 3, dimensions);
    Require(bounds.Minimum == VoxelPosition{1, 0, 1} &&
            bounds.Maximum == VoxelPosition{4, 2, 5},
        "Successive X, Y and Z resizes must produce the expected current bounds.");

    const auto handles = VoxelForge::Editor::GenerateSelectionHandles(bounds);
    const std::array<VoxelForge::Editor::Vec3, 6U> expected{{
        {1.0F, 1.5F, 3.5F},
        {5.0F, 1.5F, 3.5F},
        {3.0F, 0.0F, 3.5F},
        {3.0F, 3.0F, 3.5F},
        {3.0F, 1.5F, 1.0F},
        {3.0F, 1.5F, 6.0F}}};
    for (std::size_t index = 0U; index < handles.size(); ++index)
        Require(handles[index].WorldPosition == expected[index],
            "Every frame must derive handle barycentres from current resized bounds.");

    VoxelForge::Editor::Matrix4 projection =
        VoxelForge::Editor::IdentityMatrix();
    projection[0] = 0.1F;
    projection[5] = 0.1F;
    projection[10] = 0.1F;
    projection[11] = 0.5F;
    const auto projected = VoxelForge::Editor::ProjectSelectionHandles(
        bounds, {}, {0.0F, 0.0F, 200.0F, 200.0F}, projection);
    for (std::size_t index = 0U; index < projected.size(); ++index)
        Require(projected[index].WorldPosition == handles[index].WorldPosition,
            "Rendering and picking must consume the same recalculated handle list.");
}

void TestFaceResizeRules()
{
    const SelectionBounds original = SelectionBounds::FromCorners(
        {2, 3, 4}, {7, 8, 9});
    const auto dimensions =
        VoxelForge::Asset::Voxel::VoxelDimensions{12, 12, 12};
    const auto xMaximum = VoxelForge::Editor::ResizeSelectionBounds(
        original, SelectionFace::XMaximum, 3, dimensions);
    Require(xMaximum.Minimum == original.Minimum &&
            xMaximum.Maximum == VoxelPosition{10, 8, 9},
        "X maximum resize must leave the opposite face fixed.");
    const auto xMinimum = VoxelForge::Editor::ResizeSelectionBounds(
        original, SelectionFace::XMinimum, -8, dimensions);
    Require(xMinimum.Minimum == VoxelPosition{0, 3, 4} &&
            xMinimum.Maximum == original.Maximum,
        "X minimum resize must clip to the document and keep X maximum fixed.");
    const auto yMinimum = VoxelForge::Editor::ResizeSelectionBounds(
        original, SelectionFace::YMinimum, 99, dimensions);
    const auto zMaximum = VoxelForge::Editor::ResizeSelectionBounds(
        original, SelectionFace::ZMaximum, -99, dimensions);
    Require(yMinimum.Minimum.Y == yMinimum.Maximum.Y &&
            zMaximum.Maximum.Z == zMaximum.Minimum.Z,
        "A face must stop at its opposite face with a one-voxel minimum.");
    Require(yMinimum.Minimum.X == original.Minimum.X &&
            yMinimum.Maximum.X == original.Maximum.X &&
            zMaximum.Minimum.Y == original.Minimum.Y &&
            zMaximum.Maximum.Y == original.Maximum.Y,
        "Resizing one axis must not alter either other axis.");
}

void TestResizePointerLifecycleAndCancel()
{
    SelectionInteraction interaction;
    const SelectionBounds original = SelectionBounds::FromCorners(
        {2, 2, 2}, {6, 6, 6});
    Require(interaction.BeginResizingFace(
            SelectionFace::XMaximum, original, 21U,
            100.0F, 80.0F, {10.0F, 0.0F}),
        "MouseDown on a handle must enter ResizingFace.");
    Require(interaction.Mode() == SelectionInteractionMode::ResizingFace &&
            interaction.ActiveFace() == SelectionFace::XMaximum &&
            interaction.IsDragRecognized(),
        "The active face resize state must be explicit and captured.");
    Require(interaction.PointerMove(
            130.0F, 80.0F, std::nullopt,
            VoxelForge::Asset::Voxel::VoxelDimensions{16, 16, 16}) &&
            interaction.CurrentBounds().Maximum.X == 9 &&
            interaction.CurrentBounds().Minimum == original.Minimum,
        "MouseMove must resize one face by snapped voxel increments.");
    const SelectionPointerRelease release = interaction.PointerUp();
    Require(release.WasDrag && release.Bounds &&
            release.Mode == SelectionInteractionMode::ResizingFace &&
            release.Face == SelectionFace::XMaximum &&
            release.Bounds->Maximum.X == 9 && !interaction.IsActive(),
        "MouseUp must validate resized bounds and return to Idle.");

    Require(interaction.BeginResizingFace(
            SelectionFace::YMinimum, original, 21U,
            50.0F, 50.0F, {0.0F, -10.0F}),
        "A second resize must begin from Idle.");
    static_cast<void>(interaction.PointerMove(
        50.0F, 30.0F, std::nullopt,
        VoxelForge::Asset::Voxel::VoxelDimensions{16, 16, 16}));
    const auto restored = interaction.Cancel();
    Require(restored && *restored == original && !interaction.IsActive(),
        "Esc cancellation must restore the exact original bounds.");

    Require(interaction.BeginResizingFace(
            SelectionFace::ZMaximum, original, 30U,
            10.0F, 10.0F, {5.0F, 0.0F}),
        "Generation cancellation setup failed.");
    Require(!interaction.ValidateDocumentGeneration(31U) &&
            !interaction.IsActive(),
        "Changing document generation must cancel an active resize.");
}

void TestHandlePickingToSelectionRecalculation()
{
    const SelectionBounds original = SelectionBounds::FromCorners(
        {0, 0, 0}, {1, 1, 1});
    VoxelForge::Editor::SelectionHandles handles =
        VoxelForge::Editor::GenerateSelectionHandles(original);
    handles[1].Visible = true;
    handles[1].ScreenPosition = {60.0F, 40.0F};
    handles[1].ScreenAxisPerVoxel = {12.0F, 0.0F};
    const auto hovered = VoxelForge::Editor::PickSelectionHandle(
        handles, {60.0F, 40.0F});
    Require(hovered && hovered->Face == SelectionFace::XMaximum,
        "The input cycle must begin from an actually picked handle.");

    SelectionInteraction interaction;
    Require(interaction.BeginResizingFace(
            hovered->Face, original, 44U,
            hovered->ScreenPosition.X, hovered->ScreenPosition.Y,
            hovered->ScreenAxisPerVoxel),
        "Picked handle MouseDown must capture the resize interaction.");
    Require(interaction.PointerMove(
            84.0F, 40.0F, std::nullopt,
            VoxelForge::Asset::Voxel::VoxelDimensions{8, 8, 8}),
        "Held MouseMove must update the picked face.");

    SelectionService selection;
    selection.SetDocumentGeneration(44U);
    const std::vector<VoxelPosition> existing{
        {0, 0, 0}, {1, 1, 1}, {2, 0, 0}, {3, 1, 1}, {4, 0, 0}};
    static_cast<void>(selection.SelectVolume(
        existing, interaction.CurrentBounds(), SelectionMode::Replace));
    Require(selection.EditableBounds() == interaction.CurrentBounds() &&
            selection.Count() == 4U && !selection.Contains({4, 0, 0}),
        "Live resize must recalculate existing contained voxels and ignore empties.");

    const SelectionPointerRelease release = interaction.PointerUp();
    Require(release.Bounds && *release.Bounds == selection.EditableBounds() &&
            interaction.Mode() == SelectionInteractionMode::Idle,
        "MouseUp must preserve the recalculated resized bounds.");
}

std::vector<VoxelPosition> DenseVolume(const std::int32_t extent)
{
    std::vector<VoxelPosition> voxels;
    voxels.reserve(static_cast<std::size_t>(extent) *
        static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent));
    for (std::int32_t z = 0; z < extent; ++z)
        for (std::int32_t y = 0; y < extent; ++y)
            for (std::int32_t x = 0; x < extent; ++x)
                voxels.push_back({x, y, z});
    return voxels;
}

void TestSelectionVolumeCacheScalesWithExistingVoxels()
{
    SelectionVolumeCache cache;
    std::vector<VoxelPosition> sparse;
    for (std::int32_t index = 0; index < 4096; ++index)
        sparse.push_back({index % 64, (index / 64) % 64, index / 4096});
    cache.UpdateSource(std::move(sparse), 9U, 17U);
    const SelectionBounds nearlyFull = SelectionBounds::FromCorners(
        {0, 0, 0}, {62, 63, 63});
    const auto first = cache.Evaluate(nearlyFull);
    Require(first.Recalculated && first.Voxels.size() == 4032U,
        "A large sparse bounds evaluation must remain exact.");
    Require(cache.Metrics().VisitedVoxelCount == 4096U &&
            cache.Metrics().BoundsEvaluationCount == 1U,
        "Volume evaluation must visit existing voxels, not empty AABB cells.");

    const std::size_t capacityGrowths =
        cache.Metrics().ResultCapacityGrowthCount;
    const auto unchanged = cache.Evaluate(nearlyFull);
    Require(!unchanged.Recalculated && unchanged.Voxels.size() == 4032U &&
            cache.Metrics().BoundsCacheHitCount == 1U &&
            cache.Metrics().VisitedVoxelCount == 4096U &&
            cache.Metrics().ResultCapacityGrowthCount == capacityGrowths,
        "Unchanged voxel bounds must reuse the result without a scan or growth.");

    const auto changed = cache.Evaluate(SelectionBounds::FromCorners(
        {0, 0, 0}, {61, 63, 63}));
    Require(changed.Recalculated && changed.Voxels.size() == 3968U &&
            cache.Metrics().BoundsEvaluationCount == 2U &&
            cache.Metrics().VisitedVoxelCount == 8192U &&
            cache.Metrics().ResultCapacityGrowthCount == capacityGrowths,
        "Changing one voxel coordinate must trigger exactly one reused scan.");
}

void TestSelectionVolumeCacheDenseAndRevisionInvalidation()
{
    SelectionVolumeCache cache;
    cache.UpdateSource(DenseVolume(64), 31U, 8U);
    const auto full = cache.Evaluate(SelectionBounds::FromCorners(
        {0, 0, 0}, {63, 63, 63}));
    Require(full.Recalculated && full.Voxels.size() == 64U * 64U * 64U,
        "A selection covering a dense 64-cubed document must remain exact.");
    const auto medium = cache.Evaluate(SelectionBounds::FromCorners(
        {16, 16, 16}, {47, 47, 47}));
    Require(medium.Recalculated && medium.Voxels.size() == 32U * 32U * 32U,
        "A medium dense selection must contain the exact voxel count.");
    Require(cache.SourceCurrent(31U, 8U) &&
            !cache.SourceCurrent(31U, 9U),
        "Document revision changes must invalidate the source identity.");
    cache.UpdateSource({{1, 2, 3}}, 31U, 9U);
    const auto refreshed = cache.Evaluate(SelectionBounds::FromCorners(
        {0, 0, 0}, {63, 63, 63}));
    Require(refreshed.Recalculated && refreshed.Voxels.size() == 1U &&
            cache.Metrics().SourceRefreshCount == 2U,
        "A revised document must replace stale cached voxel positions.");
}

void TestCachedVolumeApplicationAndHighlightPolicy()
{
    SelectionVolumeCache cache;
    cache.UpdateSource({{4, 0, 0}, {0, 0, 0}, {2, 0, 0}}, 1U, 1U);
    const SelectionBounds bounds = SelectionBounds::FromCorners(
        {0, 0, 0}, {3, 0, 0});
    const auto evaluated = cache.Evaluate(bounds);
    SelectionService selection;
    Require(selection.ApplySortedVolume(
            evaluated.Voxels, bounds, SelectionMode::Replace) &&
            selection.Count() == 2U && selection.Contains({0, 0, 0}) &&
            selection.Contains({2, 0, 0}),
        "Cached sorted voxels must apply without changing selection semantics.");
    Require(!selection.ApplySortedVolume(
            cache.Evaluate(bounds).Voxels, bounds, SelectionMode::Replace),
        "Reapplying unchanged cached bounds must be a no-op.");

    const auto small = SelectionHighlightPolicy::Build(256U, true);
    Require(small.DrawIndividualVoxels &&
            small.IndividualVoxelCount == 256U &&
            small.EstimatedVertexCount == 256U * 288U,
        "Small interactive selections must keep individual outlines.");
    const auto largeInteraction = SelectionHighlightPolicy::Build(257U, true);
    Require(!largeInteraction.DrawIndividualVoxels &&
            largeInteraction.IndividualVoxelCount == 0U &&
            largeInteraction.EstimatedVertexCount == 0U,
        "Large interactions must use the global box instead of huge geometry.");
    const auto persistentLimit = SelectionHighlightPolicy::Build(2048U, false);
    const auto largePersistent = SelectionHighlightPolicy::Build(2049U, false);
    Require(persistentLimit.DrawIndividualVoxels &&
            !largePersistent.DrawIndividualVoxels,
        "Persistent individual outlines must switch at the documented limit.");
}
}

int main()
{
    try
    {
        TestEmptyAndSingleSelection();
        TestModesAndDeterminism();
        TestBoundsCenterAndClear();
        TestDocumentLifetime();
        TestSpatialBounds();
        TestVolumeModesIgnoreEmptyCells();
        TestInteractionLifecycle();
        TestSelectionHandleGenerationProjectionAndPicking();
        TestSelectionHandleSpatialBarycentres();
        TestHandleBarycentresAfterSuccessiveAxisResizes();
        TestFaceResizeRules();
        TestResizePointerLifecycleAndCancel();
        TestHandlePickingToSelectionRecalculation();
        TestSelectionVolumeCacheScalesWithExistingVoxels();
        TestSelectionVolumeCacheDenseAndRevisionInvalidation();
        TestCachedVolumeApplicationAndHighlightPolicy();
        std::cout << "SelectionService tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SelectionService test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
