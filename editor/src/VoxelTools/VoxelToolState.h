#pragma once

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{

enum class ActiveVoxelTool : std::uint8_t
{
    None,
    Pencil,
    Eraser
};

[[nodiscard]] const char* ActiveVoxelToolName(
    ActiveVoxelTool tool) noexcept;

class VoxelToolState final
{
public:
    static constexpr std::size_t DefaultPaletteIndex = 1U;

    void SetActiveTool(ActiveVoxelTool tool) noexcept;
    [[nodiscard]] bool SetActivePaletteIndex(std::size_t paletteIndex) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ActiveVoxelTool ActiveTool() const noexcept;
    [[nodiscard]] std::size_t ActivePaletteIndex() const noexcept;
    [[nodiscard]] bool IsPencilActive() const noexcept;
    [[nodiscard]] bool IsEraserActive() const noexcept;
    [[nodiscard]] bool IsEditingToolActive() const noexcept;

private:
    ActiveVoxelTool activeTool_ = ActiveVoxelTool::Pencil;
    std::size_t activePaletteIndex_ = DefaultPaletteIndex;
};

[[nodiscard]] ActiveVoxelTool ResolveVoxelToolShortcut(
    ActiveVoxelTool current,
    bool pencilPressed,
    bool eraserPressed,
    bool inputAllowed) noexcept;

} // namespace VoxelForge::Editor
