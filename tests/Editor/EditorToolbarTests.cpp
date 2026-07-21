#include "Toolbar/EditorToolbarModel.h"

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
    const auto& buttons = EditorToolbarModel::Buttons();
    constexpr EditorToolbarAction expected[] = {
        EditorToolbarAction::Save,
        EditorToolbarAction::Pencil,
        EditorToolbarAction::Eraser,
        EditorToolbarAction::Fill,
        EditorToolbarAction::Box,
        EditorToolbarAction::Line,
        EditorToolbarAction::Sphere,
        EditorToolbarAction::Selection,
        EditorToolbarAction::Move,
        EditorToolbarAction::Duplicate,
        EditorToolbarAction::Rotate,
        EditorToolbarAction::Mirror,
        EditorToolbarAction::Scale,
        EditorToolbarAction::Align};
    Require(buttons.size() == std::size(expected),
        "Toolbar does not expose exactly fourteen actions.");
    for (std::size_t index = 0U; index < buttons.size(); ++index)
    {
        Require(buttons[index].Action == expected[index],
            "Toolbar action order is incorrect.");
        Require(!buttons[index].Name.empty() &&
            !buttons[index].Description.empty(),
            "A Toolbar action has no usable tooltip text.");
    }
    Require(buttons[0].Group == EditorToolbarGroup::File &&
        buttons[1].Group == EditorToolbarGroup::DirectEdit &&
        buttons[2].Group == EditorToolbarGroup::DirectEdit &&
        buttons[3].Group == EditorToolbarGroup::DirectEdit &&
        buttons[4].Group == EditorToolbarGroup::Construction &&
        buttons[5].Group == EditorToolbarGroup::Construction &&
        buttons[6].Group == EditorToolbarGroup::Construction &&
        buttons[7].Group == EditorToolbarGroup::Manipulation &&
        buttons[8].Group == EditorToolbarGroup::Manipulation &&
        buttons[9].Group == EditorToolbarGroup::Manipulation &&
        buttons[10].Group == EditorToolbarGroup::Manipulation &&
        buttons[11].Group == EditorToolbarGroup::Manipulation &&
        buttons[12].Group == EditorToolbarGroup::Manipulation &&
        buttons[13].Group == EditorToolbarGroup::Manipulation,
        "Toolbar visual groups are incorrect.");
    Require(inputService.ShortcutLabel(buttons[0].Command) == "Ctrl+S" &&
        inputService.ShortcutLabel(buttons[1].Command) == "P" &&
        inputService.ShortcutLabel(buttons[2].Command) == "E" &&
        inputService.ShortcutLabel(buttons[3].Command) == "Shift+F" &&
        inputService.ShortcutLabel(buttons[4].Command) == "B" &&
        inputService.ShortcutLabel(buttons[5].Command) == "L" &&
        inputService.ShortcutLabel(buttons[6].Command) == "S" &&
        inputService.ShortcutLabel(buttons[7].Command) == "V" &&
        inputService.ShortcutLabel(buttons[8].Command) == "M" &&
        inputService.ShortcutLabel(buttons[9].Command) == "D" &&
        inputService.ShortcutLabel(buttons[10].Command) == "R" &&
        inputService.ShortcutLabel(buttons[11].Command) == "H" &&
        inputService.ShortcutLabel(buttons[12].Command) == "K" &&
        inputService.ShortcutLabel(buttons[13].Command) == "Shift+A",
        "Toolbar does not use the centralized shortcut bindings.");
}

void TestAvailabilityAndSingleActiveTool()
{
    const auto& buttons = EditorToolbarModel::Buttons();
    const EditorToolbarState noDocument{};
    Require(std::none_of(buttons.begin(), buttons.end(),
        [&noDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsEnabled(button, noDocument) ||
                EditorToolbarModel::IsActive(button, noDocument);
        }), "Toolbar is actionable without a voxel document.");

    EditorToolbarState cleanDocument{true, false, ActiveVoxelTool::Pencil};
    Require(!EditorToolbarModel::IsEnabled(buttons[0], cleanDocument),
        "Save is enabled for a clean document.");
    for (std::size_t index = 1U; index + 6U < buttons.size(); ++index)
        Require(EditorToolbarModel::IsEnabled(buttons[index], cleanDocument),
            "A voxel tool is disabled with an active document.");
    Require(!EditorToolbarModel::IsEnabled(buttons[8], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[9], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[10], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[11], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[12], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[13], cleanDocument),
        "A transform tool is enabled without a valid selection.");
    const auto activeCount = std::count_if(buttons.begin(), buttons.end(),
        [&cleanDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsActive(button, cleanDocument);
        });
    Require(activeCount == 1,
        "Toolbar does not expose exactly one active tool.");

    cleanDocument.CanSave = true;
    Require(EditorToolbarModel::IsEnabled(buttons[0], cleanDocument),
        "Save is disabled for a saveable dirty document.");
    cleanDocument.CanMoveSelection = true;
    cleanDocument.CanDuplicateSelection = true;
    cleanDocument.CanRotateSelection = true;
    cleanDocument.CanMirrorSelection = true;
    cleanDocument.CanScaleSelection = true;
    cleanDocument.CanAlignSelection = true;
    Require(EditorToolbarModel::IsEnabled(buttons[8], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[9], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[10], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[11], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[12], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[13], cleanDocument),
        "A transform tool is disabled with a valid selection.");
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
