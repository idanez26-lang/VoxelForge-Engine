#pragma once

#include "VoxelSelection/VoxelSelectionState.h"
#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "VoxelTools/VoxelToolState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

struct InspectorLayoutInput final
{
    bool HasDocument = false;
    std::string DocumentName;
    std::uint64_t Revision = 0U;
    std::optional<VoxelRaycastHit> Hovered;
    VoxelPickingInteractionState InteractionState =
        VoxelPickingInteractionState::Unavailable;
    ActiveVoxelTool Tool = ActiveVoxelTool::None;
    std::size_t PaletteIndex = 0U;
    VoxelPlacementPreview Placement;
    std::optional<VoxelToolResult> LastPencilResult;
    std::optional<VoxelEraserResult> LastEraserResult;
};

struct InspectorDiagnosticRow final
{
    std::string Label;
    std::string Value;

    [[nodiscard]] bool operator==(
        const InspectorDiagnosticRow&) const noexcept = default;
};

class InspectorLayoutModel final
{
public:
    static constexpr std::size_t RowCount = 17U;
    static constexpr std::size_t MaximumValueCharacters = 64U;

    [[nodiscard]] static InspectorLayoutModel Build(
        const InspectorLayoutInput& input);

    [[nodiscard]] const std::array<InspectorDiagnosticRow, RowCount>&
        Rows() const noexcept;
    [[nodiscard]] std::size_t StableRowCount() const noexcept;
    [[nodiscard]] float StableHeight(
        float lineHeightWithSpacing,
        float verticalPadding) const noexcept;
    [[nodiscard]] bool RequestsViewportResize() const noexcept;

private:
    std::array<InspectorDiagnosticRow, RowCount> rows_{};
};

[[nodiscard]] std::string TruncateInspectorValue(
    std::string value,
    std::size_t maximumCharacters =
        InspectorLayoutModel::MaximumValueCharacters);

} // namespace VoxelForge::Editor
