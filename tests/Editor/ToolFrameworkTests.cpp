#include "Tools/ToolContext.h"
#include "Tools/ToolManager.h"
#include "Selection/SelectionHandlePolicy.h"
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
        ActiveVoxelTool::Selection,
        ActiveVoxelTool::Move};
    const auto order = ToolManager::PrimaryToolOrder();
    Require(std::equal(order.begin(), order.end(), std::begin(expected)),
        "The professional toolbar order is not data-driven or is incomplete.");
    Require(ToolManager::Descriptor(ActiveVoxelTool::Pencil).Name ==
            "Smart Tool" &&
        ToolManager::Descriptor(ActiveVoxelTool::Fill).Panel ==
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
    // VF-WRAP-V1 : Wrap est un outil de plein droit — etat, panneau,
    // commande centralisee et raccourci, dans la famille Transform.
    manager.SetActiveTool(ActiveVoxelTool::Wrap);
    Require(state.IsWrapActive() &&
        manager.ActiveDescriptor().Tool == ActiveVoxelTool::Wrap &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Wrap &&
        manager.ActiveDescriptor().Command == EditorInputCommand::ToolWrap &&
        manager.ActiveDescriptor().Name == "Wrap",
        "Scale to Wrap did not select the Wrap tool, panel and command.");
    Require(!EditorInputService{}.ShortcutLabel(
        EditorInputCommand::ToolWrap).empty(),
        "Wrap has no keyboard shortcut.");
    manager.SetActiveTool(ActiveVoxelTool::Selection);
    Require(state.IsSelectionActive() &&
        manager.ActiveDescriptor().Panel == ToolPanelKind::Selection,
        "Wrap to Select did not select the Selection panel.");
    Require(manager.ActiveDescriptor().Panel != ToolPanelKind::Scale &&
        manager.ActiveDescriptor().Panel != ToolPanelKind::Wrap &&
        !state.IsWrapActive(),
        "Select retained the Scale or Wrap panel.");

    constexpr std::array activeChecks{
        &VoxelToolState::IsPencilActive, &VoxelToolState::IsEraserActive,
        &VoxelToolState::IsFillActive, &VoxelToolState::IsBoxActive,
        &VoxelToolState::IsLineActive, &VoxelToolState::IsSphereActive,
        &VoxelToolState::IsSelectionActive, &VoxelToolState::IsMoveActive,
        &VoxelToolState::IsDuplicateActive, &VoxelToolState::IsRotateActive,
        &VoxelToolState::IsMirrorActive, &VoxelToolState::IsScaleActive,
        &VoxelToolState::IsWrapActive, &VoxelToolState::IsAlignActive};
    Require(std::count_if(activeChecks.begin(), activeChecks.end(),
                [&state](const auto check) { return (state.*check)(); }) == 1,
        "ToolManager left more than one active tool.");
    // Wrap est exactement un des outils catalogues, une seule fois.
    const auto tools = ToolManager::Tools();
    Require(std::count_if(tools.begin(), tools.end(),
                [](const ToolDescriptor& tool)
                {
                    return tool.Tool == ActiveVoxelTool::Wrap;
                }) == 1,
        "Wrap is not catalogued exactly once among the tools.");
}

// VF-WRAP-V1 (correction visuelle) : autorite pure des poignees de faces.
// Wrap MONTRE et SAISIT les six poignees (comme Selection) ; Move/Rotate/
// Scale/Mirror/Align/Duplicate les montrent en guides sans les saisir ; les
// outils de dessin ne les montrent pas.
void TestSelectionHandlePolicy()
{
    Require(ToolShowsSelectionHandles(ActiveVoxelTool::Wrap) &&
        ToolPicksSelectionHandles(ActiveVoxelTool::Wrap),
        "Wrap must show and pick the editable bounds face handles.");
    Require(ToolShowsSelectionHandles(ActiveVoxelTool::Selection) &&
        ToolPicksSelectionHandles(ActiveVoxelTool::Selection),
        "Selection must keep showing and picking its face handles.");
    for (const ActiveVoxelTool guideOnly : {ActiveVoxelTool::Move,
             ActiveVoxelTool::Rotate, ActiveVoxelTool::Scale,
             ActiveVoxelTool::Mirror, ActiveVoxelTool::Align,
             ActiveVoxelTool::Duplicate})
        Require(ToolShowsSelectionHandles(guideOnly) &&
            !ToolPicksSelectionHandles(guideOnly),
            "Transform tools show handles as guides but do not pick them.");
    for (const ActiveVoxelTool none : {ActiveVoxelTool::Pencil,
             ActiveVoxelTool::Eraser, ActiveVoxelTool::Fill,
             ActiveVoxelTool::Box, ActiveVoxelTool::Line,
             ActiveVoxelTool::Sphere, ActiveVoxelTool::None})
        Require(!ToolShowsSelectionHandles(none) &&
            !ToolPicksSelectionHandles(none),
            "Drawing tools must not show selection handles.");
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
        TestSelectionHandlePolicy();
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
