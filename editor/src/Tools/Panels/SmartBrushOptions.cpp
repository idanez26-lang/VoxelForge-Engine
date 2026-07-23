#include "SmartBrushOptions.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace VoxelForge::Editor
{
namespace
{
SmartBrushState GeometryState(const SmartBrushState& state) noexcept
{
    SmartBrushState normalized = state;
    normalized.Mode = SmartBrushMode::Add;
    return normalized;
}

template <std::size_t Count>
bool FitsOnOneLine(const std::array<const char*, Count>& labels)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    float requiredWidth = 0.0F;
    for (std::size_t index = 0U; index < labels.size(); ++index)
    {
        requiredWidth += ImGui::CalcTextSize(labels[index]).x +
            ImGui::GetFrameHeight();
        if (index != 0U) requiredWidth += style.ItemSpacing.x;
    }
    return requiredWidth <= ImGui::GetContentRegionAvail().x;
}
}

bool DrawSmartBrushOptions(SmartBrushState& state, const bool allowShape)
{
    bool changed = false;
    if (allowShape)
    {
        ImGui::TextDisabled("Shape");
        ImGui::PushID("Shape");
        int shape = static_cast<int>(state.Shape);
        changed |= ImGui::RadioButton("Cube", &shape,
            static_cast<int>(SmartBrushShape::Cube));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Sphere", &shape,
            static_cast<int>(SmartBrushShape::Sphere));
        state.Shape = static_cast<SmartBrushShape>(shape);
        ImGui::PopID();
    }

    ImGui::TextDisabled("Dimension");
    ImGui::PushID("Dimension");
    int dimension = static_cast<int>(state.Dimension);
    const bool dimensionsFit = FitsOnOneLine(
        std::array{"3D Volume", "2D Surface"});
    changed |= ImGui::RadioButton("3D Volume", &dimension,
        static_cast<int>(SmartBrushDimension::Volume3D));
    if (dimensionsFit) ImGui::SameLine();
    changed |= ImGui::RadioButton("2D Surface", &dimension,
        static_cast<int>(SmartBrushDimension::Surface2D));
    state.Dimension = static_cast<SmartBrushDimension>(dimension);
    ImGui::PopID();

    ImGui::TextDisabled("Orientation");
    ImGui::PushID("Orientation");
    int orientation = static_cast<int>(state.Orientation);
    const bool orientationsFit = FitsOnOneLine(
        std::array{"Auto", "X", "Y", "Z"});
    changed |= ImGui::RadioButton("Auto", &orientation,
        static_cast<int>(SmartBrushOrientation::Auto));
    if (orientationsFit) ImGui::SameLine();
    changed |= ImGui::RadioButton("X", &orientation,
        static_cast<int>(SmartBrushOrientation::X));
    if (orientationsFit) ImGui::SameLine();
    changed |= ImGui::RadioButton("Y", &orientation,
        static_cast<int>(SmartBrushOrientation::Y));
    if (orientationsFit) ImGui::SameLine();
    changed |= ImGui::RadioButton("Z", &orientation,
        static_cast<int>(SmartBrushOrientation::Z));
    state.Orientation = static_cast<SmartBrushOrientation>(orientation);
    ImGui::PopID();

    ImGui::PushID("Size");
    const int sizeBefore = state.Size;
    ImGui::SetNextItemWidth(90.0F);
    changed |= ImGui::InputInt("Size", &state.Size);
    state.Size = std::clamp(state.Size, 1, SmartBrushEngine::MaximumSize());
    changed |= state.Size != sizeBefore;
    ImGui::PopID();
    ImGui::TextDisabled("1-16 voxels");
    return changed;
}

void DrawSmartBrushStatistics(
    const char* const primaryLabel,
    const std::size_t primaryValue,
    const char* const secondaryLabel,
    const std::size_t secondaryValue,
    const std::size_t total,
    const std::size_t clipped)
{
    ImGui::TextDisabled("Total: %zu", total);
    ImGui::TextDisabled("%s: %zu", primaryLabel, primaryValue);
    ImGui::TextDisabled("%s: %zu", secondaryLabel, secondaryValue);
    ImGui::TextDisabled("Clipped: %zu", clipped);
}

std::size_t EstimateSmartBrushGeometry(const SmartBrushState& state) noexcept
{
    return SmartBrushEngine::EstimateTotal(GeometryState(state));
}

} // namespace VoxelForge::Editor
