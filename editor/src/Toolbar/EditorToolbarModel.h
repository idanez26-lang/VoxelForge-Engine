#pragma once

#include "Input/EditorInputService.h"
#include "VoxelTools/VoxelToolState.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace VoxelForge::Editor
{

enum class EditorToolbarAction : std::uint8_t
{
    Save,
    Pencil,
    Eraser,
    Fill,
    Box,
    Line,
    Sphere,
    Selection
};

enum class EditorToolbarGroup : std::uint8_t
{
    File,
    DirectEdit,
    Construction,
    Manipulation
};

struct EditorToolbarButton final
{
    EditorToolbarAction Action = EditorToolbarAction::Save;
    EditorToolbarGroup Group = EditorToolbarGroup::File;
    std::string_view Name;
    std::string_view Description;
    EditorInputCommand Command = EditorInputCommand::None;
    ActiveVoxelTool Tool = ActiveVoxelTool::None;
};

struct EditorToolbarState final
{
    bool HasDocument = false;
    bool CanSave = false;
    ActiveVoxelTool ActiveTool = ActiveVoxelTool::None;
};

struct EditorToolbarLayout final
{
    float ButtonSize = 34.0F;
    float ItemSpacing = 5.0F;
    float GroupSpacing = 12.0F;
    bool WrapGroups = false;
};

class EditorToolbarModel final
{
public:
    static constexpr std::size_t ButtonCount = 8U;

    [[nodiscard]] static const std::array<EditorToolbarButton, ButtonCount>&
        Buttons() noexcept;
    [[nodiscard]] static bool IsEnabled(
        const EditorToolbarButton& button,
        const EditorToolbarState& state) noexcept;
    [[nodiscard]] static bool IsActive(
        const EditorToolbarButton& button,
        const EditorToolbarState& state) noexcept;
    [[nodiscard]] static EditorToolbarLayout CalculateLayout(
        float availableWidth,
        float fontSize) noexcept;
};

} // namespace VoxelForge::Editor
