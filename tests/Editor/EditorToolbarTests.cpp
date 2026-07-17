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
    const auto& buttons = EditorToolbarModel::Buttons();
    constexpr EditorToolbarAction expected[] = {
        EditorToolbarAction::Save,
        EditorToolbarAction::Pencil,
        EditorToolbarAction::Eraser,
        EditorToolbarAction::Fill,
        EditorToolbarAction::Box,
        EditorToolbarAction::Line,
        EditorToolbarAction::Sphere};
    Require(buttons.size() == std::size(expected),
        "Toolbar does not expose exactly seven actions.");
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
        buttons[6].Group == EditorToolbarGroup::Construction,
        "Toolbar visual groups are incorrect.");
    Require(buttons[0].Shortcut == "Ctrl+S" &&
        buttons[1].Shortcut == "P" && buttons[2].Shortcut == "E" &&
        buttons[3].Shortcut.empty() && buttons[4].Shortcut.empty() &&
        buttons[5].Shortcut.empty() && buttons[6].Shortcut.empty(),
        "Toolbar advertises a shortcut that is not implemented.");
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
    for (std::size_t index = 1U; index < buttons.size(); ++index)
        Require(EditorToolbarModel::IsEnabled(buttons[index], cleanDocument),
            "A voxel tool is disabled with an active document.");
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
}

void TestVoxelToolStatePipeline()
{
    VoxelToolState state;
    for (const EditorToolbarButton& button : EditorToolbarModel::Buttons())
    {
        if (button.Tool == ActiveVoxelTool::None) continue;
        state.SetActiveTool(button.Tool);
        Require(state.ActiveTool() == button.Tool &&
            state.IsEditingToolActive(),
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
