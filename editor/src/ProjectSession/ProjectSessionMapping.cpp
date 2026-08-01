#include "ProjectSession/ProjectSessionMapping.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{

ProjectSessionCameraView ToSessionView(const EditorCameraView view) noexcept
{
    switch (view)
    {
    case EditorCameraView::Front: return ProjectSessionCameraView::Front;
    case EditorCameraView::Back: return ProjectSessionCameraView::Back;
    case EditorCameraView::Left: return ProjectSessionCameraView::Left;
    case EditorCameraView::Right: return ProjectSessionCameraView::Right;
    case EditorCameraView::Top: return ProjectSessionCameraView::Top;
    case EditorCameraView::Bottom: return ProjectSessionCameraView::Bottom;
    case EditorCameraView::Perspective:
        return ProjectSessionCameraView::Perspective;
    }
    return ProjectSessionCameraView::Perspective;
}

EditorCameraView FromSessionView(const ProjectSessionCameraView view) noexcept
{
    switch (view)
    {
    case ProjectSessionCameraView::Front: return EditorCameraView::Front;
    case ProjectSessionCameraView::Back: return EditorCameraView::Back;
    case ProjectSessionCameraView::Left: return EditorCameraView::Left;
    case ProjectSessionCameraView::Right: return EditorCameraView::Right;
    case ProjectSessionCameraView::Top: return EditorCameraView::Top;
    case ProjectSessionCameraView::Bottom: return EditorCameraView::Bottom;
    case ProjectSessionCameraView::Perspective:
        return EditorCameraView::Perspective;
    }
    return EditorCameraView::Perspective;
}

ProjectSessionVector3 ToSessionVector(const Vec3& value) noexcept
{
    return {value.X, value.Y, value.Z};
}

Vec3 FromSessionVector(const ProjectSessionVector3& value) noexcept
{
    return {value.X, value.Y, value.Z};
}

} // namespace

ProjectSessionCamera ToSessionCamera(const EditorCameraState& camera) noexcept
{
    return {
        ToSessionVector(camera.Position),
        ToSessionVector(camera.RotationDegrees),
        camera.Distance,
        ToSessionVector(camera.Target),
        ToSessionView(camera.View)};
}

EditorCameraState FromSessionCamera(
    const ProjectSessionCamera& camera) noexcept
{
    return {
        FromSessionVector(camera.Position),
        FromSessionVector(camera.RotationDegrees),
        camera.Distance,
        FromSessionVector(camera.Target),
        FromSessionView(camera.View)};
}

ProjectSessionData BuildSessionData(
    const EditorCameraState& camera,
    const VoxelToolState& toolState,
    const SmartTool& smart,
    const std::size_t activePaletteIndex,
    std::filesystem::path lastModel)
{
    ProjectSessionData session;
    session.LastModel = std::move(lastModel);
    session.Camera = ToSessionCamera(camera);
    session.ActiveTool =
        (toolState.IsPencilActive() && smart.Action() == SmartAction::Erase)
        ? ProjectSessionTool::Eraser
        : (toolState.IsPencilActive() && smart.Action() == SmartAction::Paint)
        ? ProjectSessionTool::Fill
        : toolState.IsEraserActive()
        ? ProjectSessionTool::Eraser
        : toolState.IsFillActive()
        ? ProjectSessionTool::Fill
        : toolState.IsBoxActive()
        ? ProjectSessionTool::Box
        : toolState.IsLineActive()
        ? ProjectSessionTool::Line
        : toolState.IsSphereActive()
        ? ProjectSessionTool::Sphere
        : ProjectSessionTool::Pencil;
    session.ActivePaletteIndex = activePaletteIndex;
    const SmartBrushShape sessionShape = ResolveSmartBrushShape(
        smart.Geometry(), smart.Brush().Shape);
    session.SmartGeometry = sessionShape == SmartBrushShape::Cube
        ? ProjectSessionSmartGeometry::Cube
        : sessionShape == SmartBrushShape::Sphere
        ? ProjectSessionSmartGeometry::Sphere
        : ProjectSessionSmartGeometry::Pencil;
    session.SmartAction = smart.Action() == SmartAction::Erase
        ? ProjectSessionSmartAction::Erase
        : smart.Action() == SmartAction::Paint
        ? ProjectSessionSmartAction::Paint
        : ProjectSessionSmartAction::Add;
    session.SmartBrushSize = smart.Brush().Size;
    return session;
}

void ApplySessionToTools(
    const ProjectSessionData& session,
    SmartTool& smart,
    VoxelToolState& toolState) noexcept
{
    // Cube and Sphere were stored as SmartGeometry before Shape became the
    // sole active geometry selector. Preserve the legacy brush volume while
    // normalizing the live tool to Pencil for the current UI.
    smart.SetGeometry(SmartGeometry::Pencil);
    if (session.SmartGeometry == ProjectSessionSmartGeometry::Cube)
        smart.Brush().Shape = SmartBrushShape::Cube;
    else if (session.SmartGeometry == ProjectSessionSmartGeometry::Sphere)
        smart.Brush().Shape = SmartBrushShape::Sphere;
    smart.SetAction(
        session.SmartAction == ProjectSessionSmartAction::Erase
            ? SmartAction::Erase
            : session.SmartAction == ProjectSessionSmartAction::Paint
            ? SmartAction::Paint
            : SmartAction::Add);
    smart.Brush().Size = session.SmartBrushSize;
    toolState.SetActiveTool(
        session.ActiveTool == ProjectSessionTool::Eraser ||
        session.ActiveTool == ProjectSessionTool::Fill
            ? ActiveVoxelTool::Pencil
            : session.ActiveTool == ProjectSessionTool::Box
            ? ActiveVoxelTool::Box
            : session.ActiveTool == ProjectSessionTool::Line
            ? ActiveVoxelTool::Line
            : session.ActiveTool == ProjectSessionTool::Sphere
            ? ActiveVoxelTool::Sphere
            : ActiveVoxelTool::Pencil);
}

} // namespace VoxelForge::Editor
