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
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    constexpr EditorToolbarAction expected[] = {
        EditorToolbarAction::Pencil,
        EditorToolbarAction::Face,
        EditorToolbarAction::Box,
        EditorToolbarAction::Line,
        EditorToolbarAction::Selection,
        EditorToolbarAction::Transform};
    Require(buttons.size() == std::size(expected),
        "Toolbar does not expose the six Smart Tool entries.");
    for (std::size_t index = 0U; index < buttons.size(); ++index)
    {
        Require(buttons[index].Action == expected[index],
            "Toolbar action order is incorrect.");
        Require(!buttons[index].Name.empty() &&
            !buttons[index].Description.empty(),
            "A Toolbar action has no usable tooltip text.");
    }
    Require(buttons[0].Group == EditorToolbarGroup::Sculpt &&
        buttons[1].Group == EditorToolbarGroup::Construction &&
        buttons[2].Group == EditorToolbarGroup::Construction &&
        buttons[3].Group == EditorToolbarGroup::Construction &&
        buttons[4].Group == EditorToolbarGroup::Selection &&
        buttons[5].Group == EditorToolbarGroup::Transform,
        "Toolbar visual groups are incorrect.");
    Require(inputService.ShortcutLabel(buttons[0].Command) == "P" &&
        inputService.ShortcutLabel(buttons[2].Command) == "B" &&
        inputService.ShortcutLabel(buttons[3].Command) == "L" &&
        inputService.ShortcutLabel(buttons[4].Command) == "V" &&
        inputService.ShortcutLabel(buttons[5].Command) == "M",
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
        !EditorToolbarModel::IsEnabled(buttons[1], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[2], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[3], cleanDocument) &&
        EditorToolbarModel::IsEnabled(buttons[4], cleanDocument) &&
        !EditorToolbarModel::IsEnabled(buttons[5], cleanDocument),
        "Smart toolbar availability is incorrect.");
    const auto activeCount = std::count_if(buttons.begin(), buttons.end(),
        [&cleanDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsActive(button, cleanDocument);
        });
    Require(activeCount == 1,
        "Toolbar does not expose exactly one active tool.");

    cleanDocument.CanMoveSelection = true;
    cleanDocument.CanDuplicateSelection = true;
    cleanDocument.CanRotateSelection = true;
    cleanDocument.CanMirrorSelection = true;
    cleanDocument.CanScaleSelection = true;
    cleanDocument.CanAlignSelection = true;
    Require(EditorToolbarModel::IsEnabled(buttons[5], cleanDocument),
        "Transform is disabled with a valid selection.");
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
