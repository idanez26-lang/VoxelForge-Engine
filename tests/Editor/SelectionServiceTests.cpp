#include "Selection/SelectionService.h"

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
}

int main()
{
    try
    {
        TestEmptyAndSingleSelection();
        TestModesAndDeterminism();
        TestBoundsCenterAndClear();
        TestDocumentLifetime();
        std::cout << "SelectionService tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SelectionService test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
