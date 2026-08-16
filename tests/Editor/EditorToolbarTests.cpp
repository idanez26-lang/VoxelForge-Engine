#include "Toolbar/EditorToolbarModel.h"
#include "SmartTools/SmartTool.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

// VF-UX-TOOLS : la barre expose desormais les FAMILLES. Les modes et options
// vivent dans le panneau contextuel juste en dessous.
constexpr std::size_t kPencil = 0U;
constexpr std::size_t kGeometry = 1U;
constexpr std::size_t kFace = 2U;
constexpr std::size_t kSurface = 3U;
constexpr std::size_t kFill = 4U;
constexpr std::size_t kSelection = 5U;
constexpr std::size_t kTransform = 6U;

void TestOrderGroupsAndTooltips()
{
    const EditorInputService inputService;
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    constexpr EditorToolbarAction expected[] = {
        EditorToolbarAction::Pencil,
        EditorToolbarAction::Geometry,
        EditorToolbarAction::Face,
        EditorToolbarAction::Surface,
        EditorToolbarAction::Fill,
        EditorToolbarAction::Selection,
        EditorToolbarAction::Transform};
    Require(buttons.size() == std::size(expected),
        "Toolbar does not expose the five families plus Selection and Transform.");
    for (std::size_t index = 0U; index < buttons.size(); ++index)
    {
        Require(buttons[index].Action == expected[index],
            "Toolbar action order is incorrect.");
        Require(!buttons[index].Name.empty() &&
            !buttons[index].Description.empty(),
            "A Toolbar action has no usable tooltip text.");
    }
    constexpr std::string_view names[] = {
        "Pencil", "Geometry", "Face", "Surface", "Fill",
        "Selection", "Transform"};
    for (std::size_t index = 0U; index < buttons.size(); ++index)
        Require(buttons[index].Name == names[index],
            "A Toolbar family carries an unexpected label.");
    for (std::size_t index = kPencil; index <= kFill; ++index)
        Require(buttons[index].Group == EditorToolbarGroup::Sculpt &&
            buttons[index].HasFamily,
            "A sculpt family is missing its group or its family binding.");
    Require(buttons[kSelection].Group == EditorToolbarGroup::Selection &&
        buttons[kTransform].Group == EditorToolbarGroup::Transform &&
        !buttons[kSelection].HasFamily && !buttons[kTransform].HasFamily,
        "Toolbar visual groups are incorrect.");
    Require(inputService.ShortcutLabel(buttons[kSelection].Command) == "V" &&
        inputService.ShortcutLabel(buttons[kTransform].Command) == "M",
        "Toolbar does not use the centralized shortcut bindings.");
    // Les familles n'ont volontairement pas de raccourci : les touches
    // historiques P / E / Shift+F restent des selecteurs outil + action, et
    // leur redonner un sens different serait un changement non demande.
    for (std::size_t index = kPencil; index <= kFill; ++index)
        Require(inputService.ShortcutLabel(buttons[index].Command).empty(),
            "A family button claims a keyboard shortcut it was not given.");
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
    for (std::size_t index = kPencil; index <= kSelection; ++index)
        Require(EditorToolbarModel::IsEnabled(buttons[index], cleanDocument),
            "A family or Selection button is unavailable with a document.");
    Require(!EditorToolbarModel::IsEnabled(buttons[kTransform], cleanDocument),
        "Transform is available without a selection.");
    const auto activeCount = std::count_if(buttons.begin(), buttons.end(),
        [&cleanDocument](const EditorToolbarButton& button)
        {
            return EditorToolbarModel::IsActive(button, cleanDocument);
        });
    Require(activeCount == 1,
        "Toolbar does not expose exactly one active tool.");

    // Invariant central de la nouvelle barre : une famille, et une seule,
    // s'allume — pour chaque geometrie et chaque action. Sans le test sur
    // ActiveGeometry, les cinq familles s'allumeraient ensemble.
    SmartTool smartTool;
    constexpr std::pair<SmartGeometry, std::size_t> families[] = {
        {SmartGeometry::Pencil, kPencil},
        {SmartGeometry::Geometry, kGeometry},
        {SmartGeometry::Line, kGeometry},
        {SmartGeometry::Face, kFace},
        {SmartGeometry::Surface, kSurface},
        {SmartGeometry::Fill, kFill}};
    constexpr SmartToolMode smartModes[] = {
        SmartToolMode::SingleVoxel, SmartToolMode::CubeBrush,
        SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush};
    constexpr SmartAction smartActions[] = {
        SmartAction::Add, SmartAction::Erase, SmartAction::Paint};
    for (const auto& [geometry, expectedIndex] : families)
    {
        for (const SmartToolMode mode : smartModes)
        {
            for (const SmartAction action : smartActions)
            {
                smartTool.SetGeometry(geometry);
                smartTool.SetMode(mode);
                smartTool.SetAction(action);
                EditorToolbarState smartState{
                    true, false, ActiveVoxelTool::Pencil};
                smartState.ActiveGeometry = geometry;
                const auto active = std::count_if(
                    buttons.begin(), buttons.end(),
                    [&smartState](const EditorToolbarButton& button)
                    {
                        return EditorToolbarModel::IsActive(button, smartState);
                    });
                Require(smartTool.IsOperational() && active == 1 &&
                    EditorToolbarModel::IsActive(
                        buttons[expectedIndex], smartState),
                    "Exactly one family must light up for a supported "
                    "configuration.");
            }
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
    Require(EditorToolbarModel::IsActive(buttons[kSelection], selectionState) &&
        std::count_if(buttons.begin(), buttons.end(),
            [&selectionState](const EditorToolbarButton& button)
            {
                return EditorToolbarModel::IsActive(button, selectionState);
            }) == 1,
        "Selection is not the only active toolbar button for Selection.");

    cleanDocument.CanMoveSelection = true;
    Require(EditorToolbarModel::IsEnabled(buttons[kTransform], cleanDocument),
        "Transform is disabled with a valid selection.");

    constexpr ActiveVoxelTool transformTools[] = {
        ActiveVoxelTool::Move, ActiveVoxelTool::Duplicate,
        ActiveVoxelTool::Rotate, ActiveVoxelTool::Mirror,
        ActiveVoxelTool::Scale, ActiveVoxelTool::Align};
    for (const ActiveVoxelTool tool : transformTools)
    {
        cleanDocument.ActiveTool = tool;
        Require(EditorToolbarModel::IsActive(buttons[kTransform], cleanDocument) &&
            std::count_if(buttons.begin(), buttons.end(),
                [&cleanDocument](const EditorToolbarButton& button)
                {
                    return EditorToolbarModel::IsActive(button, cleanDocument);
                }) == 1,
            "Transform is not the only active button for a transform-family tool.");
    }
}

void TestHiddenToolbarActionsAndLegacyShortcuts()
{
    const auto buttons = EditorToolbarModel::PrimaryButtons();
    // Face est desormais une famille visible. Les outils herites Box / Line /
    // Sphere et les boutons Eraser / Paint restent hors de la barre : leur
    // sort est une dette distincte, volontairement non traitee ici.
    constexpr EditorToolbarAction hiddenActions[] = {
        EditorToolbarAction::Eraser, EditorToolbarAction::Paint,
        EditorToolbarAction::Box,
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
