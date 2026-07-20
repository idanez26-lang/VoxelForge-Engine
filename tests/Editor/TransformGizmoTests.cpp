#include "TransformGizmo/TransformGizmoModel.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelPosition;
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool Near(
    const float left, const float right, const float tolerance = 0.0001F)
{
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] TransformGizmoUpdateContext MakeContext(
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}),
    const ActiveVoxelTool tool = ActiveVoxelTool::Move)
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 7U;
    context.SelectionDocumentGeneration = 7U;
    context.Bounds = bounds;
    context.ActiveTool = tool;
    context.CameraPosition = {0.0F, 0.0F, -10.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.VerticalFieldOfViewDegrees = 45.0F;
    context.ViewportHeightPixels = 900.0F;
    return context;
}

void TestVisibilityModesAndStateMachine()
{
    TransformGizmoModel model;
    Require(!model.View().Visible &&
            model.View().State == TransformGizmoInteractionState::Hidden &&
            model.View().Mode == TransformGizmoMode::None &&
            model.View().ActiveAxis == TransformGizmoAxis::None,
        "The initial gizmo state must be fully hidden and neutral.");

    auto context = MakeContext();
    context.DocumentActive = false;
    Require(!model.Update(context) && !model.View().Visible,
        "No document must keep the initial gizmo hidden.");
    context = MakeContext();
    context.SelectionEmpty = true;
    Require(!model.Update(context) && !model.View().Visible,
        "An empty selection must keep the gizmo hidden.");
    context = MakeContext({}, ActiveVoxelTool::Pencil);
    Require(!model.Update(context) && !model.View().Visible,
        "An incompatible tool must keep the gizmo hidden.");
    context = MakeContext();
    context.SelectionDocumentGeneration = 6U;
    Require(!model.Update(context) && !model.View().Visible,
        "A stale selection generation must keep the gizmo hidden.");

    for (const auto [tool, mode] : {
             std::pair{ActiveVoxelTool::Move, TransformGizmoMode::Move},
             std::pair{ActiveVoxelTool::Rotate, TransformGizmoMode::Rotate},
             std::pair{ActiveVoxelTool::Scale, TransformGizmoMode::Scale}})
    {
        context = MakeContext(
            SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1}), tool);
        Require(model.Update(context) && model.View().Visible &&
                model.View().Mode == mode &&
                model.View().State == TransformGizmoInteractionState::Idle &&
                model.View().ActiveAxis == TransformGizmoAxis::None,
            "Move, Rotate and Scale must synchronize to an idle gizmo.");
    }
    context.Closing = true;
    Require(model.Update(context) && !model.View().Visible,
        "Closing must purge the gizmo.");
    model.Reset();
    Require(!model.View().Visible &&
            model.View().ActiveAxis == TransformGizmoAxis::None &&
            model.View().Mode == TransformGizmoMode::None,
        "Reset must purge mode, axis and visibility.");
}

void TestExactSpatialCentersAndInvalidation()
{
    TransformGizmoModel model;
    auto context = MakeContext(
        SelectionBounds::FromCorners({4, 2, 7}, {4, 2, 7}));
    Require(model.Update(context) &&
            model.View().Center == Vec3{4.5F, 2.5F, 7.5F},
        "A single voxel must be centered on the cell center.");

    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    Require(model.Update(context) && model.View().Center == Vec3{1, 1, 1},
        "Even selection dimensions must use spatial bounds.");
    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {2, 2, 2});
    Require(model.Update(context) &&
            model.View().Center == Vec3{1.5F, 1.5F, 1.5F},
        "Odd selection dimensions must use spatial bounds.");
    context.Bounds = SelectionBounds::FromCorners({2, 4, 6}, {7, 8, 10});
    context.ModelCenter = {3.0F, 2.0F, 1.0F};
    Require(model.Update(context) &&
            model.View().Center == Vec3{2.0F, 4.5F, 7.5F},
        "Asymmetric bounds must be translated by the viewport model center.");

    const Vec3 movedCenter = model.View().Center;
    context.Bounds = SelectionBounds::FromCorners({5, 4, 6}, {10, 8, 10});
    Require(model.Update(context) &&
            model.View().Center == movedCenter + Vec3{3.0F, 0.0F, 0.0F},
        "Move must update the center from the current bounds.");
    context.Bounds = SelectionBounds::FromCorners({2, 4, 6}, {7, 8, 10});
    Require(model.Update(context) && model.View().Center == movedCenter,
        "Undo must restore the center from restored bounds.");
    context.Bounds = SelectionBounds::FromCorners({5, 4, 6}, {10, 8, 10});
    Require(model.Update(context) &&
            model.View().Center == movedCenter + Vec3{3.0F, 0.0F, 0.0F},
        "Redo must restore the moved center.");

    context.SelectionEmpty = true;
    Require(model.Update(context) && !model.View().Visible,
        "Clearing selection must invalidate the previous center.");
    context = MakeContext();
    Require(model.Update(context) && model.View().Visible,
        "A valid replacement document selection must rebuild the gizmo.");
    context.ActiveDocumentGeneration = 8U;
    Require(model.Update(context) && !model.View().Visible,
        "Changing document generation must purge the old gizmo.");
}

void TestScreenStableScaleAndDegenerateInputs()
{
    auto context = MakeContext();
    const Vec3 center{0.5F, 0.5F, 0.5F};
    const float nearLength =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    context.CameraPosition.Z = -20.5F;
    const float farLength =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    Require(nearLength > 0.0F && farLength > nearLength &&
            Near(farLength / nearLength, 2.0F, 0.01F),
        "Perspective world length must grow linearly with camera depth.");
    const auto projectedPixels = [&context](
        const float worldLength, const float depth)
    {
        return worldLength * context.ViewportHeightPixels /
            (depth * 2.0F * std::tan(
                DegreesToRadians(context.VerticalFieldOfViewDegrees) * 0.5F));
    };
    Require(Near(projectedPixels(nearLength, 10.5F),
                 TransformGizmoModel::DesiredAxisLengthPixels, 0.01F) &&
            Near(projectedPixels(farLength, 21.0F),
                 TransformGizmoModel::DesiredAxisLengthPixels, 0.01F),
        "Perspective projection must keep the requested pixel length.");

    context.Projection = TransformGizmoProjection::Orthographic;
    context.OrthographicWorldHeight = 32.0F;
    const float orthographicNear =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    context.CameraPosition.Z = -500.0F;
    const float orthographicFar =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    Require(Near(orthographicNear, orthographicFar) &&
            Near(orthographicNear * context.ViewportHeightPixels /
                    context.OrthographicWorldHeight,
                TransformGizmoModel::DesiredAxisLengthPixels),
        "Orthographic sizing must be independent of camera distance.");

    TransformGizmoModel model;
    context = MakeContext();
    context.ViewportHeightPixels = 0.0F;
    Require(!model.Update(context) && !model.View().Visible,
        "A zero-height viewport must be rejected safely.");
    context = MakeContext();
    context.CameraForward = {};
    Require(!model.Update(context) && !model.View().Visible,
        "A degenerate camera direction must be rejected safely.");
}

void TestImmutableRenderViewAndConstantPrimitives()
{
    SelectionService selection;
    selection.SetDocumentGeneration(7U);
    const std::vector<VoxelPosition> voxels{{2, 3, 4}, {8, 6, 5}};
    Require(selection.ApplySortedVolume(voxels,
            SelectionBounds::FromCorners({2, 3, 4}, {8, 6, 5}),
            SelectionMode::Replace),
        "Selection fixture setup failed.");
    const auto selectionBefore = std::vector<VoxelPosition>(
        selection.Voxels().begin(), selection.Voxels().end());
    const SelectionBounds boundsBefore = selection.EditableBounds();
    TransformGizmoModel model;
    auto context = MakeContext(boundsBefore);
    Require(model.Update(context), "A valid render view was not produced.");
    const TransformGizmoView first = model.View();
    Require(first.Axes.size() == TransformGizmoModel::AxisPrimitiveCount &&
            TransformGizmoModel::TotalPrimitiveCount == 4U &&
            first.Axes[0].Axis == TransformGizmoAxis::X &&
            first.Axes[1].Axis == TransformGizmoAxis::Y &&
            first.Axes[2].Axis == TransformGizmoAxis::Z,
        "The renderer view must contain exactly three ordered axes.");
    for (const TransformGizmoAxisView& axis : first.Axes)
        Require(axis.Start == first.Center &&
                Near(Length(axis.End - axis.Start), first.AxisLength),
            "Axes must share one origin and one length.");
    const Vec3 xDelta = first.Axes[0].End - first.Axes[0].Start;
    const Vec3 yDelta = first.Axes[1].End - first.Axes[1].Start;
    const Vec3 zDelta = first.Axes[2].End - first.Axes[2].Start;
    Require(Near(xDelta.X, first.AxisLength) && Near(xDelta.Y, 0.0F) &&
            Near(xDelta.Z, 0.0F) && Near(yDelta.X, 0.0F) &&
            Near(yDelta.Y, first.AxisLength) && Near(yDelta.Z, 0.0F) &&
            Near(zDelta.X, 0.0F) && Near(zDelta.Y, 0.0F) &&
            Near(zDelta.Z, first.AxisLength) &&
            first.Axes[0].Color[0] > first.Axes[0].Color[1] &&
            first.Axes[1].Color[1] > first.Axes[1].Color[0] &&
            first.Axes[2].Color[2] > first.Axes[2].Color[0],
        "Axes must be orthogonal and retain X red, Y green and Z blue.");
    Require(!model.Update(context) && model.View() == first,
        "An unchanged frame must not rebuild the immutable view.");
    Require(selection.EditableBounds() == boundsBefore &&
            std::equal(selection.Voxels().begin(), selection.Voxels().end(),
                selectionBefore.begin()),
        "Gizmo evaluation must not mutate selection or its bounds.");
}

void TestNoDocumentOrHistoryMutation()
{
    using namespace VoxelForge;
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{8U, 8U, 8U}, {
        {2U, 3U, 4U, 7U},
        {5U, 6U, 1U, 11U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "transform-gizmo-memory.vox", "gizmo-asset");
    Require(loaded.Succeeded(),
        "Unable to build the non-mutation document fixture.");
    Asset::Voxel::VoxelDocument document = std::move(*loaded.Document);
    VoxelEditHistory history;

    const std::uint64_t revisionBefore = document.GetRevision();
    const bool dirtyBefore = document.IsDirty();
    const std::uint64_t voxelCountBefore = document.GetVoxelCount();
    const auto boundsBefore = document.GetBounds();
    const auto firstVoxelBefore = document.GetVoxel({2, 3, 4});
    const std::size_t undoCountBefore = history.UndoCount();
    const std::size_t redoCountBefore = history.RedoCount();
    const std::size_t historyMemoryBefore = history.EstimatedMemory();
    const bool savedStateBefore = history.IsAtSavedState();

    TransformGizmoModel model;
    const auto context = MakeContext(
        SelectionBounds::FromCorners({2, 3, 4}, {5, 6, 4}),
        ActiveVoxelTool::Scale);
    Require(model.Update(context) && model.View().Visible,
        "The non-mutation fixture did not produce a visible gizmo.");
    Require(document.GetRevision() == revisionBefore &&
            document.IsDirty() == dirtyBefore &&
            document.GetVoxelCount() == voxelCountBefore &&
            document.GetBounds() == boundsBefore &&
            document.GetVoxel({2, 3, 4}) == firstVoxelBefore,
        "Gizmo evaluation must not mutate document data, revision or dirty.");
    Require(history.UndoCount() == undoCountBefore &&
            history.RedoCount() == redoCountBefore &&
            history.EstimatedMemory() == historyMemoryBefore &&
            history.IsAtSavedState() == savedStateBefore &&
            !history.CanUndo() && !history.CanRedo(),
        "Gizmo evaluation must not create or modify edit history.");
}
}

int main()
{
    try
    {
        TestVisibilityModesAndStateMachine();
        TestExactSpatialCentersAndInvalidation();
        TestScreenStableScaleAndDegenerateInputs();
        TestImmutableRenderViewAndConstantPrimitives();
        TestNoDocumentOrHistoryMutation();
    }
    catch (const std::exception& error)
    {
        std::cerr << "TransformGizmoTests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "TransformGizmoTests passed\n";
    return EXIT_SUCCESS;
}
