#include "Selection/SelectionService.h"
#include "Selection/SelectionInteraction.h"

#include <algorithm>
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
        std::cout << "SelectionService tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SelectionService test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
