#include "EditorCamera.h"
#include "VoxelModelTransform.h"
#include "VoxelViewportState.h"

#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <cmath>
#include <array>
#include <iostream>
#include <string>

namespace
{
bool Check(const bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool Near(const float left, const float right, const float epsilon = 0.001F)
{
    return std::abs(left - right) <= epsilon;
}

bool SamePoint(
    const VoxelForge::Editor::Vec3 left,
    const VoxelForge::Editor::Vec3 right)
{
    return Near(left.X, right.X) && Near(left.Y, right.Y) && Near(left.Z, right.Z);
}

bool IsFinite(const std::array<float, 16>& matrix)
{
    for (const float value : matrix)
    {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

float ProjectDepth(
    const std::array<float, 16>& matrix,
    const VoxelForge::Editor::Vec3 point)
{
    const float clipZ = matrix[8] * point.X + matrix[9] * point.Y +
        matrix[10] * point.Z + matrix[11];
    const float clipW = matrix[12] * point.X + matrix[13] * point.Y +
        matrix[14] * point.Z + matrix[15];
    return clipZ / clipW;
}
}

int main()
{
    using namespace VoxelForge;
    bool passed = true;

    Editor::VoxelViewportState state;
    passed &= Check(!state.HasModel(), "Viewport must start empty.");
    passed &= Check(state.IsGridVisible() && state.AreAxesVisible() &&
        state.Background() == Editor::ViewportBackground::Dark,
        "Viewport guide defaults are incorrect.");
    state.SetGridVisible(false);
    state.SetAxesVisible(false);
    state.SetBackground(Editor::ViewportBackground::Light);
    passed &= Check(!state.IsGridVisible() && !state.AreAxesVisible() &&
        state.BackgroundColor()[0] > 0.6F,
        "Viewport session settings did not change.");
    state.SetGridVisible(true);
    state.SetAxesVisible(true);
    state.SetBackground(Editor::ViewportBackground::Neutral);

    Voxel::VoxelModel model;
    model.SetName("first");
    Voxel::VoxelGrid grid;
    passed &= Check(grid.Resize(2U, 3U, 4U), "Grid resize failed.");
    passed &= Check(grid.Set(0U, 0U, 0U, {1U, Voxel::Voxel::OccupiedFlag}),
        "Voxel insertion failed.");
    model.AddGrid(std::move(grid));
    const Mesh::MeshBuildResult mesh =
        Mesh::VoxelMeshBuilder::Build(*model.GetGrid(0U));
    passed &= Check(mesh.Succeeded && mesh.Mesh.has_value(), "Mesh build failed.");
    passed &= Check(SamePoint(
        Editor::CalculateVoxelMeshCenter(*mesh.Mesh), {0.5F, 0.5F, 0.5F}),
        "The occupied mesh center is incorrect for an asymmetric grid.");
    const Editor::Vec3 asymmetricCenter =
        Editor::CalculateVoxelMeshCenter(*mesh.Mesh);
    passed &= Check(SamePoint(
        Editor::VoxelGridToViewport({0.0F, 0.0F, 0.0F}, asymmetricCenter),
        {-0.5F, -0.5F, -0.5F}),
        "Grid-to-viewport recentering is incorrect.");
    const Editor::VoxelRay recenteredRay = Editor::ViewportToVoxelGrid(
        {{-2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}, asymmetricCenter);
    passed &= Check(SamePoint(recenteredRay.Origin, {-1.5F, 0.5F, 0.5F}) &&
        SamePoint(recenteredRay.Direction, {1.0F, 0.0F, 0.0F}),
        "Viewport-to-grid ray recentering is incorrect.");
    passed &= Check(
        state.Replace("first.vox", model, *mesh.Mesh), "State replacement failed.");
    const Editor::VoxelViewportStatistics first = state.Statistics();
    passed &= Check(first.Width == 2U && first.Height == 3U && first.Depth == 4U,
        "Viewport dimensions are incorrect.");
    passed &= Check(first.OccupiedVoxelCount == 1U && first.TriangleCount == 12U,
        "Viewport statistics are incorrect.");

    Voxel::VoxelModel replacement = model;
    passed &= Check(state.Replace("second.vox", replacement, *mesh.Mesh),
        "Second state replacement failed.");
    passed &= Check(state.Name() == "second.vox", "Replacement name is stale.");

    Voxel::VoxelModel multipleGrids = model;
    Voxel::VoxelGrid ignoredGrid;
    passed &= Check(ignoredGrid.Resize(9U, 8U, 7U),
        "Second grid resize failed.");
    multipleGrids.AddGrid(std::move(ignoredGrid));
    passed &= Check(state.Replace("multi.vox", multipleGrids, *mesh.Mesh),
        "Multi-grid state replacement failed.");
    passed &= Check(state.Statistics().Width == 2U &&
        state.Statistics().Height == 3U && state.Statistics().Depth == 4U,
        "Viewport v1 must report only the first grid.");

    const std::string validName = state.Name();
    Voxel::VoxelModel noGridModel;
    Mesh::MeshData emptyMesh;
    passed &= Check(!state.Replace("invalid.vox", noGridModel, emptyMesh),
        "A model without a grid must be rejected.");
    passed &= Check(state.Name() == validName && state.HasModel(),
        "A failed replacement must preserve the current state.");

    Voxel::VoxelModel emptyModel;
    Voxel::VoxelGrid emptyGrid;
    passed &= Check(emptyGrid.Resize(2U, 2U, 2U),
        "Empty editable grid setup failed.");
    emptyModel.AddGrid(std::move(emptyGrid));
    const Mesh::MeshBuildResult emptyBuilt =
        Mesh::VoxelMeshBuilder::Build(*emptyModel.GetGrid(0U));
    Editor::VoxelViewportState emptyState;
    passed &= Check(emptyBuilt.Succeeded && emptyBuilt.Mesh &&
        emptyBuilt.Mesh->Empty() &&
        emptyState.Replace("empty.vox", emptyModel, *emptyBuilt.Mesh) &&
        emptyState.HasModel() &&
        emptyState.Statistics().OccupiedVoxelCount == 0U &&
        emptyState.Statistics().VertexCount == 0U &&
        emptyState.Statistics().TriangleCount == 0U,
        "An editable model with an empty mesh must remain valid.");

    const auto color = Editor::ToViewportColor({255U, 128U, 0U, 64U});
    passed &= Check(std::abs(color[0] - 1.0F) < 0.0001F &&
        color[1] > 0.50F && color[1] < 0.51F && color[2] == 0.0F,
        "Palette conversion is incorrect.");

    Editor::EditorCamera camera;
    camera.Frame(2.0F, 3.0F, 4.0F);
    passed &= Check(camera.GetTarget().X == 0.0F &&
        camera.GetTarget().Y == 0.0F && camera.GetTarget().Z == 0.0F,
        "Camera framing target is incorrect.");
    passed &= Check(camera.GetDistance() > 2.0F,
        "Camera framing distance is too small.");
    camera.SetAspectRatio(16.0F / 9.0F);
    const auto matrix = camera.GetViewProjection();
    passed &= Check(IsFinite(matrix),
        "Projection contains non-finite values.");
    const Editor::VoxelRay centerRay = camera.CreateViewportRay(0.0F, 0.0F);
    const Editor::VoxelRay upperRightRay =
        camera.CreateViewportRay(1.0F, 1.0F);
    const Editor::VoxelRay lowerLeftRay =
        camera.CreateViewportRay(-1.0F, -1.0F);
    passed &= Check(SamePoint(centerRay.Direction, camera.GetForward()) &&
        Editor::Dot(upperRightRay.Direction, camera.GetRight()) > 0.0F &&
        Editor::Dot(upperRightRay.Direction, camera.GetUp()) > 0.0F &&
        Editor::Dot(lowerLeftRay.Direction, camera.GetRight()) < 0.0F &&
        Editor::Dot(lowerLeftRay.Direction, camera.GetUp()) < 0.0F,
        "Viewport coordinates do not produce camera-space rays correctly.");
    camera.SetAspectRatio(4.0F);
    const float wideHorizontal = Editor::Dot(
        camera.CreateViewportRay(1.0F, 0.0F).Direction, camera.GetRight());
    camera.SetAspectRatio(0.25F);
    const float verticalHorizontal = Editor::Dot(
        camera.CreateViewportRay(1.0F, 0.0F).Direction, camera.GetRight());
    passed &= Check(wideHorizontal > verticalHorizontal,
        "Wide and vertical viewport ray aspects are inconsistent.");
    for (const Editor::EditorCameraView view : {
             Editor::EditorCameraView::Front,
             Editor::EditorCameraView::Top,
             Editor::EditorCameraView::Perspective})
    {
        camera.SetView(view);
        passed &= Check(SamePoint(
            camera.CreateViewportRay(0.0F, 0.0F).Direction,
            camera.GetForward()),
            "A named camera view does not center its ray on forward.");
    }
    camera.SetAspectRatio(16.0F / 9.0F);
    const Editor::Vec3 target = camera.GetTarget();
    const float clipX = matrix[0] * target.X + matrix[1] * target.Y +
        matrix[2] * target.Z + matrix[3];
    const float clipY = matrix[4] * target.X + matrix[5] * target.Y +
        matrix[6] * target.Z + matrix[7];
    const float clipW = matrix[12] * target.X + matrix[13] * target.Y +
        matrix[14] * target.Z + matrix[15];
    passed &= Check(std::abs(clipX / clipW) < 0.0001F &&
        std::abs(clipY / clipW) < 0.0001F,
        "Camera target does not project to the viewport center.");

    camera.Frame(0.0F, 0.0F, 0.0F);
    camera.SetAspectRatio(0.0F);
    passed &= Check(camera.GetDistance() >= 1.0F &&
        IsFinite(camera.GetViewProjection()),
        "Empty framing or zero aspect ratio is unstable.");

    camera.Frame(100.0F, 1.0F, 0.0F);
    camera.SetAspectRatio(100.0F);
    passed &= Check(camera.GetDistance() > 100.0F &&
        IsFinite(camera.GetViewProjection()),
        "Flat model framing is unstable.");

    const float distanceBeforePan = camera.GetDistance();
    const Editor::Vec3 targetBeforePan = camera.GetTarget();
    camera.Pan(24.0F, -12.0F, 720.0F);
    const Editor::Vec3 pannedTarget = camera.GetTarget();
    passed &= Check(!SamePoint(targetBeforePan, pannedTarget) &&
        Near(distanceBeforePan, camera.GetDistance()),
        "Camera pan must move only the target.");

    camera.SetView(Editor::EditorCameraView::Front);
    passed &= Check(camera.GetForward().Z < -0.99F &&
        SamePoint(camera.GetTarget(), pannedTarget),
        "Front view is incorrect or changed the target.");
    camera.SetView(Editor::EditorCameraView::Back);
    passed &= Check(camera.GetForward().Z > 0.99F,
        "Back view is incorrect.");
    camera.SetView(Editor::EditorCameraView::Left);
    passed &= Check(camera.GetForward().X > 0.99F,
        "Left view is incorrect.");
    camera.SetView(Editor::EditorCameraView::Right);
    passed &= Check(camera.GetForward().X < -0.99F,
        "Right view is incorrect.");
    camera.SetView(Editor::EditorCameraView::Top);
    passed &= Check(camera.GetForward().Y < -0.99F,
        "Top view is incorrect.");
    camera.SetView(Editor::EditorCameraView::Bottom);
    passed &= Check(camera.GetForward().Y > 0.99F,
        "Bottom view is incorrect.");
    camera.SetView(Editor::EditorCameraView::Perspective);
    passed &= Check(camera.GetView() == Editor::EditorCameraView::Perspective &&
        SamePoint(camera.GetTarget(), pannedTarget),
        "Perspective view is incorrect or changed the target.");

    camera.Frame(10000.0F, 1.0F, 1.0F);
    camera.SetAspectRatio(0.01F);
    const auto longModelMatrix = camera.GetViewProjection();
    passed &= Check(camera.GetDistance() > 10000.0F &&
        IsFinite(longModelMatrix) &&
        ProjectDepth(longModelMatrix, camera.GetTarget()) > 0.0F &&
        ProjectDepth(longModelMatrix, camera.GetTarget()) < 1.0F,
        "Long model framing or extreme aspect ratio is unstable.");

    state.Clear();
    passed &= Check(!state.HasModel() && state.Name().empty(),
        "Viewport clear failed.");
    passed &= Check(state.IsGridVisible() && state.AreAxesVisible() &&
        state.Background() == Editor::ViewportBackground::Neutral,
        "Session settings must survive model and project clearing.");

    camera.Reset();
    passed &= Check(SamePoint(camera.GetTarget(), {}) &&
        Near(camera.GetDistance(), 12.0F) &&
        camera.GetView() == Editor::EditorCameraView::Perspective,
        "Camera reset is incorrect.");
    return passed ? 0 : 1;
}
