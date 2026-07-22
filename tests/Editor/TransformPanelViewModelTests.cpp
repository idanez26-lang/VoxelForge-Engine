#include "TransformPanel/TransformPanelViewModel.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
namespace Asset = VoxelForge::Asset;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.001F;
}

TransformPanelSource Source(
    const Asset::Voxel::VoxelPosition minimum = {2, 3, 4},
    const Asset::Voxel::VoxelPosition maximum = {5, 6, 7})
{
    return {true,
        {TransformPivotMode::Center, {4.0F, 5.0F, 6.0F}},
        SelectionBounds::FromCorners(minimum, maximum), {}, {}};
}

void TestFrameworkToPanelSynchronization()
{
    TransformPanelViewModel model;
    TransformPanelSource source = Source();
    TransformPanelState state = model.Read(source);
    Require(state.Available && state.Position == Vec3{4.0F, 5.0F, 6.0F} &&
            state.RotationDegrees == Vec3{} &&
            state.Scale == Vec3{1.0F, 1.0F, 1.0F},
        "The panel did not expose the current framework state.");

    source.Pivot.WorldPosition = {9.0F, 8.0F, 7.0F};
    source.RotationPreview =
        TransformPanelRotation{VoxelRotationAxis::Z, -1};
    source.ScalePreviewDimensions = {8U, 4U, 2U};
    state = model.Read(source);
    Require(state.Position == Vec3{9.0F, 8.0F, 7.0F} &&
            state.RotationDegrees == Vec3{0.0F, 0.0F, -90.0F} &&
            Near(state.Scale.X, 2.0F) && Near(state.Scale.Y, 1.0F) &&
            Near(state.Scale.Z, 0.5F),
        "Gizmo previews were not reflected in the panel.");
}

void TestPanelRequests()
{
    TransformPanelViewModel model;
    const TransformPanelSource source = Source();
    const TransformPanelPositionEdit position =
        model.PreparePosition(source, {7.0F, 4.0F, 10.0F});
    Require(position.Ready() &&
            position.Delta == Asset::Voxel::VoxelPosition{3, -1, 4},
        "Panel Position did not produce the expected Move delta.");
    Require(model.PreparePosition(source, {4.5F, 5.0F, 6.0F}).Code ==
            TransformPanelEditCode::InvalidValue,
        "A non-grid Position was accepted.");

    const TransformPanelRotationEdit rotation =
        model.PrepareRotation(source, {0.0F, 90.0F, 0.0F});
    Require(rotation.Ready() && rotation.Axis == VoxelRotationAxis::Y &&
            rotation.QuarterTurns == 1,
        "Panel Rotation did not produce an exact quarter turn.");
    Require(model.PrepareRotation(source, {45.0F, 0.0F, 0.0F}).Code ==
            TransformPanelEditCode::UnsupportedRotation &&
            model.PrepareRotation(source, {90.0F, 90.0F, 0.0F}).Code ==
            TransformPanelEditCode::UnsupportedRotation,
        "Unsupported voxel rotations were accepted.");

    const TransformPanelScaleEdit scale =
        model.PrepareScale(source, {2.0F, 0.5F, 1.5F});
    Require(scale.Ready() &&
            scale.TargetDimensions ==
                Asset::Voxel::VoxelDimensions{8U, 2U, 6U},
        "Panel Scale did not produce the expected target dimensions.");
}

void TestSelectionPivotAndUndoRedoRefresh()
{
    TransformPanelViewModel model;
    TransformPanelSource source = Source({1, 1, 1}, {1, 1, 1});
    const TransformPanelState before = model.Read(source);

    // An applied transform, Undo, and Redo all update the shared selection and
    // pivot before the next read; the stateless panel follows each snapshot.
    source.Pivot.WorldPosition = {6.5F, 2.5F, 3.5F};
    source.SourceBounds = SelectionBounds::FromCorners({6, 2, 3}, {6, 2, 3});
    const TransformPanelState applied = model.Read(source);
    source = Source({1, 1, 1}, {1, 1, 1});
    const TransformPanelState undone = model.Read(source);
    source.Pivot.WorldPosition = {6.5F, 2.5F, 3.5F};
    source.SourceBounds = SelectionBounds::FromCorners({6, 2, 3}, {6, 2, 3});
    const TransformPanelState redone = model.Read(source);
    Require(before.Position != applied.Position &&
            undone.Position == before.Position &&
            redone.Position == applied.Position,
        "Panel state did not follow selection Apply/Undo/Redo snapshots.");

    source.Pivot.Mode = TransformPivotMode::Bottom;
    Require(model.Read(source).PivotMode == TransformPivotMode::Bottom &&
            std::string_view(TransformPanelViewModel::PivotModeName(
                TransformPivotMode::Top)) == "Top",
        "Shared pivot mode was not exposed by the panel.");
}

void TestUnavailableState()
{
    TransformPanelViewModel model;
    TransformPanelSource source;
    Require(!model.Read(source).Available &&
            model.PreparePosition(source, {}).Code ==
                TransformPanelEditCode::Unavailable &&
            model.PrepareRotation(source, {}).Code ==
                TransformPanelEditCode::Unavailable &&
            model.PrepareScale(source, {1.0F, 1.0F, 1.0F}).Code ==
                TransformPanelEditCode::Unavailable,
        "Unavailable panel state accepted an edit.");
}
}

int main()
{
    try
    {
        TestFrameworkToPanelSynchronization();
        TestPanelRequests();
        TestSelectionPivotAndUndoRedoRefresh();
        TestUnavailableState();
        std::cout << "Transform panel ViewModel tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Transform panel ViewModel tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
