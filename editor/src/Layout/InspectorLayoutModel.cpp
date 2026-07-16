#include "InspectorLayoutModel.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
constexpr const char* EmptyValue = "--";

std::string PositionValue(const Asset::Voxel::VoxelPosition position)
{
    return std::to_string(position.X) + ", " +
        std::to_string(position.Y) + ", " + std::to_string(position.Z);
}

std::string CoordinatesValue(const VoxelCoordinates coordinates)
{
    return std::to_string(coordinates.X) + ", " +
        std::to_string(coordinates.Y) + ", " +
        std::to_string(coordinates.Z);
}

std::string DistanceValue(const float distance)
{
    std::ostringstream value;
    value << std::fixed << std::setprecision(3) << distance;
    return value.str();
}

InspectorDiagnosticRow Row(std::string label, std::string value)
{
    return {std::move(label), TruncateInspectorValue(std::move(value))};
}
}

InspectorLayoutModel InspectorLayoutModel::Build(
    const InspectorLayoutInput& input)
{
    const VoxelRaycastHit* hit = input.Hovered
        ? &*input.Hovered : nullptr;
    const bool pencil = input.Tool == ActiveVoxelTool::Pencil;
    const bool eraser = input.Tool == ActiveVoxelTool::Eraser;
    const VoxelToolResult* pencilResult = input.LastPencilResult
        ? &*input.LastPencilResult : nullptr;
    const VoxelEraserResult* eraserResult = input.LastEraserResult
        ? &*input.LastEraserResult : nullptr;

    InspectorLayoutModel result;
    result.rows_ = {{
        Row("Document", input.HasDocument
            ? (input.DocumentName.empty() ? "Open" : input.DocumentName)
            : EmptyValue),
        Row("Revision", input.HasDocument
            ? std::to_string(input.Revision) : EmptyValue),
        Row("Hovered Voxel", hit ? "Hit" : EmptyValue),
        Row("Sub-model", hit
            ? std::to_string(hit->SubModelIndex) : EmptyValue),
        Row("Position", hit
            ? CoordinatesValue(hit->Coordinates) : EmptyValue),
        Row("Face", hit ? VoxelHitFaceName(hit->Face) : EmptyValue),
        Row("Adjacent", hit
            ? PositionValue(hit->AdjacentPosition) : EmptyValue),
        Row("Distance", hit
            ? DistanceValue(hit->Distance) : EmptyValue),
        Row("Color", hit
            ? std::to_string(hit->ColorIndex) : EmptyValue),
        Row("Active Tool", ActiveVoxelToolName(input.Tool)),
        Row("Palette Index", pencil
            ? std::to_string(input.PaletteIndex) : EmptyValue),
        Row("Placement", VoxelPlacementPreviewStatusName(
            input.Placement.Status)),
        Row("Tool Target", input.Placement.Position
            ? PositionValue(*input.Placement.Position) : EmptyValue),
        Row("Last Operation", eraser && eraserResult
            ? VoxelEraserResultCodeName(eraserResult->Code)
            : pencilResult
            ? VoxelToolResultCodeName(pencilResult->Code) : EmptyValue),
        Row("Last Position", eraser && eraserResult
            ? PositionValue(eraserResult->Position)
            : pencilResult
            ? PositionValue(pencilResult->Position) : EmptyValue),
        Row("Removed Palette", eraser && eraserResult
            ? std::to_string(eraserResult->RemovedPaletteIndex) : EmptyValue),
        Row("Status", VoxelPickingInteractionStateName(
            input.InteractionState))
    }};
    return result;
}

const std::array<InspectorDiagnosticRow, InspectorLayoutModel::RowCount>&
InspectorLayoutModel::Rows() const noexcept
{
    return rows_;
}

std::size_t InspectorLayoutModel::StableRowCount() const noexcept
{
    return rows_.size();
}

float InspectorLayoutModel::StableHeight(
    const float lineHeightWithSpacing,
    const float verticalPadding) const noexcept
{
    return static_cast<float>(rows_.size()) * lineHeightWithSpacing +
        verticalPadding * 2.0F + 4.0F;
}

bool InspectorLayoutModel::RequestsViewportResize() const noexcept
{
    return false;
}

std::string TruncateInspectorValue(
    std::string value,
    const std::size_t maximumCharacters)
{
    if (value.size() <= maximumCharacters) return value;
    if (maximumCharacters <= 3U) return value.substr(0U, maximumCharacters);
    value.resize(maximumCharacters - 3U);
    value += "...";
    return value;
}

} // namespace VoxelForge::Editor
