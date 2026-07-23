#include "Tools/ToolContext.h"
#include "Tools/ToolManager.h"
#include "Tools/ToolPanel.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void TestOfficialToolbarOrder()
{
    constexpr ActiveVoxelTool expected[] = {
        ActiveVoxelTool::Pencil,
        ActiveVoxelTool::Box,
        ActiveVoxelTool::Line,
        ActiveVoxelTool::Selection,
        ActiveVoxelTool::Move,
        ActiveVoxelTool::Rotate};
    const auto order = ToolManager::PrimaryToolOrder();
    Require(std::equal(order.begin(), order.end(), std::begin(expected)),
        "The professional toolbar order is not data-driven or is incomplete.");
    Require(ToolManager::Descriptor(ActiveVoxelTool::Fill).Panel ==
            ToolPanelKind::Smart &&
        ToolManager::Descriptor(ActiveVoxelTool::Eraser).Panel ==
            ToolPanelKind::Smart,
        "Legacy Paint and Erase did not converge on the Smart panel.");
}

void TestSingleActiveToolTransitionsAndPanelSelection()
{
    VoxelToolState state;
    ToolManager manager(state);
    Require(manager.ActiveTool() == ActiveVoxelTool::Pencil &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Smart,
        "ToolManager did not initialize the Pencil tool and panel.");

    manager.SetActiveTool(ActiveVoxelTool::Move);
    Require(state.IsMoveActive() &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Move,
        "Pencil to Move did not select the Move panel.");
    manager.SetActiveTool(ActiveVoxelTool::Rotate);
    Require(state.IsRotateActive() &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Rotate,
        "Move to Rotate did not select the Rotate panel.");
    manager.SetActiveTool(ActiveVoxelTool::Scale);
    Require(state.IsScaleActive() &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Scale,
        "Rotate to Scale did not select the Scale panel.");
    manager.SetActiveTool(ActiveVoxelTool::Selection);
    Require(state.IsSelectionActive() &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Selection,
        "Scale to Select did not select the Selection panel.");
    Require(manager.ActiveDescriptor().Panel != ToolPanelKind::Scale,
        "Select retained the Scale panel.");

    constexpr std::array activeChecks{
        &VoxelToolState::IsPencilActive, &VoxelToolState::IsEraserActive,
        &VoxelToolState::IsFillActive, &VoxelToolState::IsBoxActive,
        &VoxelToolState::IsLineActive, &VoxelToolState::IsSphereActive,
        &VoxelToolState::IsSelectionActive, &VoxelToolState::IsMoveActive,
        &VoxelToolState::IsDuplicateActive, &VoxelToolState::IsRotateActive,
        &VoxelToolState::IsMirrorActive, &VoxelToolState::IsScaleActive,
        &VoxelToolState::IsAlignActive};
    Require(std::count_if(activeChecks.begin(), activeChecks.end(),
                [&state](const auto check) { return (state.*check)(); }) == 1,
        "ToolManager left more than one active tool.");
}

void TestExistingStateAndLegacyShortcutsArePreserved()
{
    VoxelToolState state;
    state.SetActiveTool(ActiveVoxelTool::Box);
    ToolManager manager(state);
    Require(manager.ActiveTool() == ActiveVoxelTool::Box &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Generic,
        "ToolManager did not preserve the existing VoxelToolState.");

    const EditorInputService input;
    constexpr std::array legacyCommands{
        EditorInputCommand::ToolBox, EditorInputCommand::ToolLine,
        EditorInputCommand::ToolSphere, EditorInputCommand::ToolDuplicate,
        EditorInputCommand::ToolMirror, EditorInputCommand::ToolAlign};
    Require(std::all_of(legacyCommands.begin(), legacyCommands.end(),
                [&input](const EditorInputCommand command)
                {
                    return !input.ShortcutLabel(command).empty();
                }),
        "A legacy tool shortcut is no longer exposed.");

    Require(ToolPanel::Host() == ToolPanelHost::IntegratedWorkspace,
        "ToolPanel is no longer an embedded workspace renderer.");
}

void TestMetadataAndFutureOrderingFoundation()
{
    const auto tools = ToolManager::Tools();
    for (const ToolDescriptor& tool : tools)
    {
        Require(!tool.Name.empty(), "A tool has no display metadata.");
        if (tool.Tool != ActiveVoxelTool::None)
            Require(tool.Command != EditorInputCommand::None,
                "An active tool has no centralized input command.");
    }
    Require(ToolManager::Descriptor(static_cast<ActiveVoxelTool>(255)).Tool ==
            ActiveVoxelTool::None,
        "Unknown tools do not fall back safely.");
    Require(std::none_of(tools.begin(), tools.end(),
                [](const ToolDescriptor& tool)
                {
                    return tool.Name == "Save" ||
                        tool.Command == EditorInputCommand::FileSave;
                }),
        "Save is incorrectly catalogued as an active tool.");
}

void TestContextDefaults()
{
    ToolContext context;
    Require(context.Smart.Geometry() == SmartGeometry::Pencil &&
        context.Smart.Action() == SmartAction::Add &&
        context.Smart.Brush().Shape == SmartBrushShape::Cube &&
        context.Smart.Brush().Dimension == SmartBrushDimension::Volume3D &&
        context.Smart.Brush().Orientation == SmartBrushOrientation::Auto &&
        context.Smart.Brush().Size == 1 && !context.Smart.Statistics().Available &&
        context.Scale.Uniform && !context.Scale.Snap,
        "Professional tool option defaults are invalid.");
}

void TestSmartBrushStatePersistsAcrossToolChanges()
{
    ToolContext context;
    VoxelToolState state;
    ToolManager manager(state);
    context.Smart.Brush().Shape = SmartBrushShape::Sphere;
    context.Smart.Brush().Dimension = SmartBrushDimension::Surface2D;
    context.Smart.Brush().Orientation = SmartBrushOrientation::Z;
    context.Smart.Brush().Size = 12;

    manager.SetActiveTool(ActiveVoxelTool::Fill);
    Require(manager.ActiveDescriptor().Panel == ToolPanelKind::Smart &&
        context.Smart.Brush().Shape == SmartBrushShape::Sphere &&
        context.Smart.Brush().Dimension == SmartBrushDimension::Surface2D &&
        context.Smart.Brush().Orientation == SmartBrushOrientation::Z &&
        context.Smart.Brush().Size == 12,
        "Paint did not use the shared Smart Brush options.");
    manager.SetActiveTool(ActiveVoxelTool::Pencil);
    Require(manager.ActiveDescriptor().Panel == ToolPanelKind::Smart &&
        context.Smart.Brush().Shape == SmartBrushShape::Sphere &&
        context.Smart.Brush().Dimension == SmartBrushDimension::Surface2D &&
        context.Smart.Brush().Orientation == SmartBrushOrientation::Z &&
        context.Smart.Brush().Size == 12 &&
        context.Smart.Action() == SmartAction::Add,
        "Shared Smart Brush options did not persist back to Pencil.");
}
}

int main()
{
    try
    {
        TestOfficialToolbarOrder();
        TestSingleActiveToolTransitionsAndPanelSelection();
        TestExistingStateAndLegacyShortcutsArePreserved();
        TestMetadataAndFutureOrderingFoundation();
        TestContextDefaults();
        TestSmartBrushStatePersistsAcrossToolChanges();
        std::cout << "Professional tool framework tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Professional tool framework tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
