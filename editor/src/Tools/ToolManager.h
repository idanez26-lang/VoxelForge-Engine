#pragma once

#include "Input/EditorInputService.h"
#include "VoxelTools/VoxelToolState.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace VoxelForge::Editor
{

enum class ToolPanelKind : std::uint8_t
{
    Smart,
    Selection,
    Move,
    Rotate,
    Scale,
    Generic
};

enum class ToolCursor : std::uint8_t
{
    Default,
    Crosshair,
    Eraser,
    Paint,
    Selection,
    Move,
    Rotate,
    Scale
};

struct ToolDescriptor final
{
    ActiveVoxelTool Tool = ActiveVoxelTool::None;
    std::string_view Name;
    EditorInputCommand Command = EditorInputCommand::None;
    ToolPanelKind Panel = ToolPanelKind::Generic;
    ToolCursor Cursor = ToolCursor::Default;
};

class ToolManager final
{
public:
    static constexpr std::size_t ToolCount = 14U;
    static constexpr std::size_t PrimaryToolCount = 6U;

    explicit ToolManager(VoxelToolState& state) noexcept;

    [[nodiscard]] ActiveVoxelTool ActiveTool() const noexcept;
    void SetActiveTool(ActiveVoxelTool tool) noexcept;
    [[nodiscard]] const ToolDescriptor& ActiveDescriptor() const noexcept;
    [[nodiscard]] static const ToolDescriptor& Descriptor(
        ActiveVoxelTool tool) noexcept;
    [[nodiscard]] static std::span<const ActiveVoxelTool,
        PrimaryToolCount> PrimaryToolOrder() noexcept;
    [[nodiscard]] static std::span<const ToolDescriptor, ToolCount>
        Tools() noexcept;

private:
    VoxelToolState& state_;
};

} // namespace VoxelForge::Editor
