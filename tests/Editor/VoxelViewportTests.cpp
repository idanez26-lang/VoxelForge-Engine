#include "EditorCamera.h"
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
