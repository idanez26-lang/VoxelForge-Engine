#include "Toolbar/EditorToolbarModel.h"
#include "SmartTools/SmartTool.h"

#include <algorithm>
#include <cmath>
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

void TestOrderGroupsAndTooltips()
{
    const EditorInputService inputService;
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    constexpr EditorToolbarAction expected[] = {
        EditorToolbarAction::Pencil,
        EditorToolbarAction::Selection,
        EditorToolbarAction::Transform};
    Require(buttons.size() == std::size(expected),
        "Toolbar does not expose exactly Smart Tool, Selection, and Transform.");
    for (std::size_t index = 0U; index < buttons.size(); ++index)
    {
        Require(buttons[index].Action == expected[index],
            "Toolbar action order is incorrect.");
        Require(!buttons[index].Name.empty() &&
            !buttons[index].Description.empty(),
            "A Toolbar action has no usable tooltip text.");
    }
    Require(buttons[0].Name == "Smart Tool" &&
        buttons[0].Group == EditorToolbarGroup::Sculpt &&
        buttons[1].Group == EditorToolbarGroup::Selection &&
        buttons[2].Group == EditorToolbarGroup::Transform,
        "Toolbar visual groups are incorrect.");
    Require(inputService.ShortcutLabel(buttons[0].Command) == "P" &&
        inputService.ShortcutLabel(buttons[1].Command) == "V" &&
        inputService.ShortcutLabel(buttons[2].Command) == "M",
        "Toolbar does not use the centralized shortcut bindings.");
}

void TestAvailabilityAndSingleActiveTool()
{
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    const EditorToolbarState noDocument{};
    Require(std::none_of(buttons.begin(), buttons.end(),
        [&noDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsEnabled(button, noDocument) ||
                EditorToolbarModel::IsActive(button, noDocument);
        }), "Toolbar is actionable without a voxel document.");

    EditorToolbarState cleanDocument{true, false, ActiveVoxelTool::Pencil};
    Require(EditorToolbarModel::IsEnabled(buttons[0], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[1], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[2], cleanDocument),
        "Smart toolbar availability is incorrect.");
    const auto activeCount = std::count_if(buttons.begin(), buttons.end(),
        [&cleanDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsActive(button, cleanDocument);
        });
    Require(activeCount == 1,
        "Toolbar does not expose exactly one active tool.");

    SmartTool smartTool;
    const auto requireSmartToolOnly = [&buttons, &smartTool](
        const SmartToolMode mode,
        const SmartAction action,
        const std::string_view description)
    {
        smartTool.SetGeometry(SmartGeometry::Pencil);
        smartTool.SetMode(mode);
        smartTool.SetAction(action);
        const EditorToolbarState smartState{
            true, false, ActiveVoxelTool::Pencil};
        const auto active = std::count_if(buttons.begin(), buttons.end(),
            [&smartState](const EditorToolbarButton& button)
            {
                return EditorToolbarModel::IsActive(button, smartState);
            });
        Require(smartTool.IsOperational() &&
            EditorToolbarModel::IsActive(buttons[0], smartState) &&
            !EditorToolbarModel::IsActive(buttons[1], smartState) &&
            !EditorToolbarModel::IsActive(buttons[2], smartState) && active == 1,
            description);
    };
    constexpr SmartToolMode smartModes[] = {
        SmartToolMode::SingleVoxel, SmartToolMode::CubeBrush,
        SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush};
    constexpr SmartAction smartActions[] = {
        SmartAction::Add, SmartAction::Erase, SmartAction::Paint};
    for (const SmartToolMode mode : smartModes)
    {
        for (const SmartAction action : smartActions)
        {
            requireSmartToolOnly(mode, action,
                "Smart Tool is not exclusively active for a supported configuration.");
        }
    }
    for (const SmartGeometry unsupported :
        {SmartGeometry::Cube, SmartGeometry::Sphere})
    {
        smartTool.SetGeometry(unsupported);
        Require(!smartTool.IsOperational(),
            "A removed SmartGeometry remains operational in the SMART-05 contract.");
    }

    const EditorToolbarState selectionState{
        true, false, ActiveVoxelTool::Selection};
    Require(!EditorToolbarModel::IsActive(buttons[0], selectionState) &&
        EditorToolbarModel::IsActive(buttons[1], selectionState) &&
        !EditorToolbarModel::IsActive(buttons[2], selectionState),
        "Selection is not the only active toolbar button for Selection.");

    cleanDocument.CanMoveSelection = true;
    Require(EditorToolbarModel::IsEnabled(buttons[2], cleanDocument),
        "Transform is disabled with a valid selection.");

    constexpr ActiveVoxelTool transformTools[] = {
        ActiveVoxelTool::Move, ActiveVoxelTool::Duplicate,
        ActiveVoxelTool::Rotate, ActiveVoxelTool::Mirror,
        ActiveVoxelTool::Scale, ActiveVoxelTool::Align};
    for (const ActiveVoxelTool tool : transformTools)
    {
        cleanDocument.ActiveTool = tool;
        Require(EditorToolbarModel::IsActive(buttons[2], cleanDocument) &&
            !EditorToolbarModel::IsActive(buttons[0], cleanDocument) &&
            !EditorToolbarModel::IsActive(buttons[1], cleanDocument),
            "Transform is not the only active button for a transform-family tool.");
    }
}

void TestHiddenToolbarActionsAndLegacyShortcuts()
{
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    constexpr EditorToolbarAction hiddenActions[] = {
        EditorToolbarAction::Eraser, EditorToolbarAction::Paint,
        EditorToolbarAction::Face, EditorToolbarAction::Box,
        EditorToolbarAction::Line, EditorToolbarAction::Sphere};
    for (const EditorToolbarAction action : hiddenActions)
    {
        Require(std::none_of(buttons.begin(), buttons.end(),
            [action](const EditorToolbarButton& button)
            {
                return button.Action == action;
            }), "A hidden legacy action remains in PrimaryButtons.");
    }

    const EditorInputService inputService;
    const EditorCommandAvailability available{true};
    const auto resolve = [&inputService, &available](
        const EditorInputKey key, const bool shift = false)
    {
        EditorInputFrame frame;
        frame.SetPressed(key);
        frame.Shift = shift;
        return inputService.Resolve(frame, available);
    };
    Require(resolve(EditorInputKey::P) == EditorInputCommand::ToolPencil &&
        resolve(EditorInputKey::E) == EditorInputCommand::ToolEraser &&
        resolve(EditorInputKey::F, true) == EditorInputCommand::ToolFill,
        "Legacy Pencil, Eraser, or Paint shortcuts no longer resolve.");
}

void TestVoxelToolStatePipeline()
{
    VoxelToolState state;
    for (const EditorToolbarButton& button : EditorToolbarModel::Buttons())
    {
        if (button.Tool == ActiveVoxelTool::None) continue;
        state.SetActiveTool(button.Tool);
        Require(state.ActiveTool() == button.Tool &&
            (button.Tool == ActiveVoxelTool::Selection ||
             button.Tool == ActiveVoxelTool::Move
             || button.Tool == ActiveVoxelTool::Duplicate
             || button.Tool == ActiveVoxelTool::Rotate
             || button.Tool == ActiveVoxelTool::Mirror
             || button.Tool == ActiveVoxelTool::Scale
             || button.Tool == ActiveVoxelTool::Align
                ? !state.IsEditingToolActive()
                : state.IsEditingToolActive()),
            "Toolbar tool selection bypasses or conflicts with VoxelToolState.");
    }
    state.Reset();
    Require(state.ActiveTool() == ActiveVoxelTool::Pencil,
        "Closing or replacing a document leaves an incoherent tool state.");
}

void TestResponsiveLayout()
{
    const EditorToolbarLayout wide =
        EditorToolbarModel::CalculateLayout(900.0F, 16.0F);
    const EditorToolbarLayout medium =
        EditorToolbarModel::CalculateLayout(360.0F, 16.0F);
    const EditorToolbarLayout narrow =
        EditorToolbarModel::CalculateLayout(100.0F, 16.0F);
    Require(!wide.WrapGroups && wide.ButtonSize >= 32.0F,
        "Wide Toolbar wraps or shrinks unnecessarily.");
    Require(medium.ButtonSize > 0.0F && medium.ItemSpacing > 0.0F,
        "Medium Toolbar layout is invalid.");
    Require(narrow.WrapGroups && narrow.ButtonSize >= 24.0F &&
        std::isfinite(narrow.ButtonSize) &&
        std::isfinite(narrow.ItemSpacing) &&
        std::isfinite(narrow.GroupSpacing),
        "Narrow Toolbar layout is clipped, negative, or non-finite.");
}
}

int main()
{
    try
    {
        TestOrderGroupsAndTooltips();
        TestAvailabilityAndSingleActiveTool();
        TestHiddenToolbarActionsAndLegacyShortcuts();
        TestVoxelToolStatePipeline();
        TestResponsiveLayout();
        std::cout << "Modern Toolbar tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Modern Toolbar tests failed: " << exception.what() << '\n';
        return 1;
    }
}
