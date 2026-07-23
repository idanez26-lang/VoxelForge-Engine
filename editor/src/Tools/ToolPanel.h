#pragma once

#include "ToolContext.h"
#include "ToolManager.h"

#include <cstdint>

namespace VoxelForge::Editor
{

enum class ToolPanelHost : std::uint8_t
{
    IntegratedWorkspace
};

class ToolPanel final
{
public:
    // The host owns the ImGui window. ToolPanel only renders content into the
    // already active workspace region, so it cannot create a floating tool UI.
    [[nodiscard]] static constexpr ToolPanelHost Host() noexcept
    {
        return ToolPanelHost::IntegratedWorkspace;
    }
    [[nodiscard]] static bool Draw(
        const ToolManager& manager, ToolContext& context);
    [[nodiscard]] static float PreferredHeight(
        ToolPanelKind panel) noexcept;
};

} // namespace VoxelForge::Editor
