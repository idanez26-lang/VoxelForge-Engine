#include "ProjectSession/ProjectSessionMapping.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

EditorCameraState SampleCamera(
    const EditorCameraView view = EditorCameraView::Perspective)
{
    EditorCameraState camera;
    camera.Position = {7.25F, 4.5F, -9.0F};
    camera.RotationDegrees = {-20.0F, 125.0F, 0.0F};
    camera.Distance = 14.5F;
    camera.Target = {1.0F, 2.0F, 3.0F};
    camera.View = view;
    return camera;
}

void TestCameraRoundTripPreservesEveryField()
{
    constexpr EditorCameraView views[] = {
        EditorCameraView::Perspective, EditorCameraView::Front,
        EditorCameraView::Back, EditorCameraView::Left,
        EditorCameraView::Right, EditorCameraView::Top,
        EditorCameraView::Bottom};
    for (const EditorCameraView view : views)
    {
        const EditorCameraState original = SampleCamera(view);
        const EditorCameraState restored =
            FromSessionCamera(ToSessionCamera(original));
        Require(restored == original,
            "Camera state should survive a session round trip.");
    }
}

void TestPencilSmartActionsDriveThePersistedTool()
{
    VoxelToolState tools;
    tools.SetActiveTool(ActiveVoxelTool::Pencil);
    SmartTool smart;

    smart.SetAction(SmartAction::Add);
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .ActiveTool == ProjectSessionTool::Pencil,
        "Pencil with Add should persist as Pencil.");
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartAction == ProjectSessionSmartAction::Add,
        "Add smart action should persist as Add.");

    smart.SetAction(SmartAction::Erase);
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .ActiveTool == ProjectSessionTool::Eraser,
        "Pencil with Erase should persist as Eraser.");
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartAction == ProjectSessionSmartAction::Erase,
        "Erase smart action should persist as Erase.");

    smart.SetAction(SmartAction::Paint);
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .ActiveTool == ProjectSessionTool::Fill,
        "Pencil with Paint should persist as Fill.");
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartAction == ProjectSessionSmartAction::Paint,
        "Paint smart action should persist as Paint.");
}

void TestLegacyToolsPersistDirectly()
{
    const struct
    {
        ActiveVoxelTool Live;
        ProjectSessionTool Stored;
    } cases[] = {
        {ActiveVoxelTool::Eraser, ProjectSessionTool::Eraser},
        {ActiveVoxelTool::Fill, ProjectSessionTool::Fill},
        {ActiveVoxelTool::Box, ProjectSessionTool::Box},
        {ActiveVoxelTool::Line, ProjectSessionTool::Line},
        {ActiveVoxelTool::Sphere, ProjectSessionTool::Sphere},
        {ActiveVoxelTool::Selection, ProjectSessionTool::Pencil},
        {ActiveVoxelTool::None, ProjectSessionTool::Pencil},
    };
    SmartTool smart;
    for (const auto& testCase : cases)
    {
        VoxelToolState tools;
        tools.SetActiveTool(testCase.Live);
        Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
                .ActiveTool == testCase.Stored,
            std::string("Unexpected persisted tool for ") +
                ActiveVoxelToolName(testCase.Live) + ".");
    }
}

void TestSmartGeometryResolvesFromGeometryThenBrushShape()
{
    VoxelToolState tools;
    tools.SetActiveTool(ActiveVoxelTool::Pencil);
    SmartTool smart;

    smart.SetGeometry(SmartGeometry::Cube);
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartGeometry == ProjectSessionSmartGeometry::Cube,
        "Cube geometry should persist as Cube.");

    smart.SetGeometry(SmartGeometry::Sphere);
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartGeometry == ProjectSessionSmartGeometry::Sphere,
        "Sphere geometry should persist as Sphere.");

    smart.SetGeometry(SmartGeometry::Pencil);
    smart.Brush().Shape = SmartBrushShape::Cube;
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartGeometry == ProjectSessionSmartGeometry::Cube,
        "Pencil geometry should fall back to the Cube brush shape.");

    smart.Brush().Shape = SmartBrushShape::Sphere;
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartGeometry == ProjectSessionSmartGeometry::Sphere,
        "Pencil geometry should fall back to the Sphere brush shape.");

    smart.Brush().Shape = SmartBrushShape::Cylinder;
    Require(BuildSessionData(SampleCamera(), tools, smart, 1U, {})
            .SmartGeometry == ProjectSessionSmartGeometry::Pencil,
        "Non-persistable brush shapes should persist as Pencil.");
}

void TestPassThroughFields()
{
    VoxelToolState tools;
    tools.SetActiveTool(ActiveVoxelTool::Pencil);
    SmartTool smart;
    smart.Brush().Size = 7;

    const ProjectSessionData session = BuildSessionData(
        SampleCamera(), tools, smart, 42U, "Assets/Models/Maison.vox");
    Require(session.ActivePaletteIndex == 42U,
        "Palette index should pass through unchanged.");
    Require(session.SmartBrushSize == 7,
        "Brush size should pass through unchanged.");
    Require(session.LastModel == "Assets/Models/Maison.vox",
        "Last model path should pass through unchanged.");
    Require(session.Camera == ToSessionCamera(SampleCamera()),
        "Camera should map through ToSessionCamera.");
}

void TestApplyPreservesLegacyCubeSphereRule()
{
    ProjectSessionData session;
    session.SmartGeometry = ProjectSessionSmartGeometry::Cube;

    SmartTool smart;
    smart.SetGeometry(SmartGeometry::Fill);
    smart.Brush().Shape = SmartBrushShape::Cylinder;
    VoxelToolState tools;
    ApplySessionToTools(session, smart, tools);
    Require(smart.Geometry() == SmartGeometry::Pencil,
        "Live geometry should be normalized to Pencil.");
    Require(smart.Brush().Shape == SmartBrushShape::Cube,
        "Legacy Cube session should restore the Cube brush volume.");

    session.SmartGeometry = ProjectSessionSmartGeometry::Sphere;
    ApplySessionToTools(session, smart, tools);
    Require(smart.Brush().Shape == SmartBrushShape::Sphere,
        "Legacy Sphere session should restore the Sphere brush volume.");

    session.SmartGeometry = ProjectSessionSmartGeometry::Pencil;
    smart.Brush().Shape = SmartBrushShape::Cylinder;
    ApplySessionToTools(session, smart, tools);
    Require(smart.Brush().Shape == SmartBrushShape::Cylinder,
        "Pencil session should leave the brush shape untouched.");
}

void TestApplyRestoresActionsBrushSizeAndTools()
{
    ProjectSessionData session;
    session.SmartBrushSize = 9;

    const struct
    {
        ProjectSessionSmartAction Stored;
        SmartAction Live;
    } actions[] = {
        {ProjectSessionSmartAction::Add, SmartAction::Add},
        {ProjectSessionSmartAction::Erase, SmartAction::Erase},
        {ProjectSessionSmartAction::Paint, SmartAction::Paint},
    };
    for (const auto& testCase : actions)
    {
        session.SmartAction = testCase.Stored;
        SmartTool smart;
        VoxelToolState tools;
        ApplySessionToTools(session, smart, tools);
        Require(smart.Action() == testCase.Live,
            "Smart action was not restored as expected.");
        Require(smart.Brush().Size == 9,
            "Brush size was not restored.");
    }

    const struct
    {
        ProjectSessionTool Stored;
        ActiveVoxelTool Live;
    } toolCases[] = {
        {ProjectSessionTool::Pencil, ActiveVoxelTool::Pencil},
        {ProjectSessionTool::Eraser, ActiveVoxelTool::Pencil},
        {ProjectSessionTool::Fill, ActiveVoxelTool::Pencil},
        {ProjectSessionTool::Box, ActiveVoxelTool::Box},
        {ProjectSessionTool::Line, ActiveVoxelTool::Line},
        {ProjectSessionTool::Sphere, ActiveVoxelTool::Sphere},
    };
    for (const auto& testCase : toolCases)
    {
        session.ActiveTool = testCase.Stored;
        SmartTool smart;
        VoxelToolState tools;
        tools.SetActiveTool(ActiveVoxelTool::Move);
        ApplySessionToTools(session, smart, tools);
        Require(tools.ActiveTool() == testCase.Live,
            "Active tool was not restored as expected.");
    }
}

void TestBuildApplyBuildIsStable()
{
    // Persisted tool state should be a fixed point: applying a session and
    // rebuilding it must not drift (the Eraser/Fill states intentionally
    // collapse onto Pencil + smart action).
    ProjectSessionData session;
    session.ActiveTool = ProjectSessionTool::Eraser;
    session.SmartGeometry = ProjectSessionSmartGeometry::Cube;
    session.SmartAction = ProjectSessionSmartAction::Erase;
    session.SmartBrushSize = 4;
    session.ActivePaletteIndex = 42U;

    SmartTool smart;
    VoxelToolState tools;
    ApplySessionToTools(session, smart, tools);
    const ProjectSessionData rebuilt = BuildSessionData(
        SampleCamera(), tools, smart, session.ActivePaletteIndex, {});
    Require(rebuilt.ActiveTool == session.ActiveTool,
        "Rebuilt active tool drifted.");
    Require(rebuilt.SmartGeometry == session.SmartGeometry,
        "Rebuilt smart geometry drifted.");
    Require(rebuilt.SmartAction == session.SmartAction,
        "Rebuilt smart action drifted.");
    Require(rebuilt.SmartBrushSize == session.SmartBrushSize,
        "Rebuilt brush size drifted.");
    Require(rebuilt.ActivePaletteIndex == session.ActivePaletteIndex,
        "Rebuilt palette index drifted.");
}

} // namespace

int main()
{
    try
    {
        TestCameraRoundTripPreservesEveryField();
        TestPencilSmartActionsDriveThePersistedTool();
        TestLegacyToolsPersistDirectly();
        TestSmartGeometryResolvesFromGeometryThenBrushShape();
        TestPassThroughFields();
        TestApplyPreservesLegacyCubeSphereRule();
        TestApplyRestoresActionsBrushSizeAndTools();
        TestBuildApplyBuildIsStable();
        std::cout << "Project session mapping tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Project session mapping tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
