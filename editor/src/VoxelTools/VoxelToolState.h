#pragma once

#include <cstdint>

namespace VoxelForge::Editor
{

enum class ActiveVoxelTool : std::uint8_t
{
    None,
    Pencil,
    Eraser,
    Fill,
    Box,
    Line,
    Sphere,
    Selection,
    Move,
    Duplicate,
    Rotate,
    Mirror,
    Scale
};

[[nodiscard]] const char* ActiveVoxelToolName(
    ActiveVoxelTool tool) noexcept;

class VoxelToolState final
{
public:
    void SetActiveTool(ActiveVoxelTool tool) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ActiveVoxelTool ActiveTool() const noexcept;
    [[nodiscard]] bool IsPencilActive() const noexcept;
    [[nodiscard]] bool IsEraserActive() const noexcept;
    [[nodiscard]] bool IsFillActive() const noexcept;
    [[nodiscard]] bool IsBoxActive() const noexcept;
    [[nodiscard]] bool IsLineActive() const noexcept;
    [[nodiscard]] bool IsSphereActive() const noexcept;
    [[nodiscard]] bool IsSelectionActive() const noexcept;
    [[nodiscard]] bool IsMoveActive() const noexcept;
    [[nodiscard]] bool IsDuplicateActive() const noexcept;
    [[nodiscard]] bool IsRotateActive() const noexcept;
    [[nodiscard]] bool IsMirrorActive() const noexcept;
    [[nodiscard]] bool IsScaleActive() const noexcept;
    [[nodiscard]] bool IsEditingToolActive() const noexcept;

private:
    ActiveVoxelTool activeTool_ = ActiveVoxelTool::Pencil;
};

[[nodiscard]] ActiveVoxelTool ResolveVoxelToolShortcut(
    ActiveVoxelTool current,
    bool pencilPressed,
    bool eraserPressed,
    bool inputAllowed) noexcept;

} // namespace VoxelForge::Editor
