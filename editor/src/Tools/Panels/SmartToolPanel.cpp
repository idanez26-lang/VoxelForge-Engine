#include "SmartBrushOptions.h"
#include "SmartToolPanel.h"
#include "SmartTools/BrushProfileService.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace VoxelForge::Editor
{
bool DrawSmartToolPanel(ToolContext& context)
{
    SmartTool& tool = context.Smart;
    bool changed = false;
    ImGui::TextDisabled("SMART TOOL");
    ImGui::TextDisabled("Geometry");
    ImGui::PushID("Geometry");
    int geometry = tool.Geometry() == SmartGeometry::Face ? 1
        : tool.Geometry() == SmartGeometry::Line ? 2
        : tool.Geometry() == SmartGeometry::Geometry ? 3
        : tool.Geometry() == SmartGeometry::Surface ? 4
        : tool.Geometry() == SmartGeometry::Fill ? 5 : 0;
    changed |= ImGui::RadioButton("Pencil", &geometry, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Face", &geometry, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Line", &geometry, 2);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Geometry", &geometry, 3);
    ImGui::NewLine();
    changed |= ImGui::RadioButton("Surface", &geometry, 4);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Fill", &geometry, 5);
    ImGui::PopID();
    const SmartGeometry geometryBefore = tool.Geometry();
    tool.SetGeometry(geometry == 1 ? SmartGeometry::Face
        : geometry == 2 ? SmartGeometry::Line
        : geometry == 3 ? SmartGeometry::Geometry
        : geometry == 4 ? SmartGeometry::Surface
        : geometry == 5 ? SmartGeometry::Fill : SmartGeometry::Pencil);
    changed |= tool.Geometry() != geometryBefore;

    if (tool.Geometry() == SmartGeometry::Face)
    {
        ImGui::TextDisabled("Face: connected exposed surface");
        ImGui::TextDisabled("Size and shape are not used");
    }
    else if (tool.Geometry() == SmartGeometry::Fill)
    {
        ImGui::TextDisabled("Fill Mode");
        ImGui::PushID("FillMode");
        int fillMode = static_cast<int>(tool.FillMode());
        changed |= ImGui::RadioButton("Connected", &fillMode,
            static_cast<int>(SmartFillMode::Connected));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Plane", &fillMode,
            static_cast<int>(SmartFillMode::Plane));
        ImGui::PopID();
        const SmartFillMode fillModeBefore = tool.FillMode();
        tool.SetFillMode(static_cast<SmartFillMode>(fillMode));
        changed |= tool.FillMode() != fillModeBefore;
        ImGui::TextDisabled(tool.FillMode() == SmartFillMode::Connected
            ? "Click a connected color region"
            : "Click a visible face to fill its plane");
    }
    else
    {
        ImGui::TextDisabled("Mode");
        ImGui::PushID("Mode");
        int mode = static_cast<int>(tool.Mode());
        changed |= ImGui::RadioButton("Single Voxel", &mode,
            static_cast<int>(SmartToolMode::SingleVoxel));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Cube Brush", &mode,
            static_cast<int>(SmartToolMode::CubeBrush));
        ImGui::NewLine();
        changed |= ImGui::RadioButton("Sphere Brush", &mode,
            static_cast<int>(SmartToolMode::SphereBrush));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Cylinder Brush", &mode,
            static_cast<int>(SmartToolMode::CylinderBrush));
        ImGui::PopID();
        const SmartToolMode modeBefore = tool.Mode();
        tool.SetMode(static_cast<SmartToolMode>(mode));
        changed |= tool.Mode() != modeBefore;

        if (tool.Geometry() == SmartGeometry::Pencil)
        {
            ImGui::TextDisabled("Brush Mode");
            ImGui::PushID("PencilBrushMode");
            int dimension = static_cast<int>(tool.Brush().Dimension);
            changed |= ImGui::RadioButton("3D", &dimension,
                static_cast<int>(SmartBrushDimension::Volume3D));
            ImGui::SameLine();
            changed |= ImGui::RadioButton("2D", &dimension,
                static_cast<int>(SmartBrushDimension::Surface2D));
            tool.Brush().Dimension = static_cast<SmartBrushDimension>(dimension);
            ImGui::PopID();
        }

        if (tool.Mode() == SmartToolMode::SingleVoxel)
        {
            ImGui::TextDisabled("Size: 1 voxel");
        }
        else
        {
            const int sizeBefore = tool.Brush().Size;
            ImGui::SetNextItemWidth(90.0F);
            changed |= ImGui::InputInt("Size", &tool.Brush().Size);
            tool.Brush().Size = std::clamp(tool.Brush().Size, 1,
                MaximumSmartToolBrushSize);
            changed |= tool.Brush().Size != sizeBefore;
            ImGui::TextDisabled("1-64 voxels");
        }
        if (tool.Geometry() == SmartGeometry::Line)
        {
            if (const auto axis = tool.LineConstraintAxis())
                ImGui::TextDisabled("Line | Axis %s", SmartToolLineAxisLabel(*axis));
            else
                ImGui::TextDisabled("Drag from A to B; hold Shift to constrain");
        }
        else if (tool.Geometry() == SmartGeometry::Geometry ||
            tool.Geometry() == SmartGeometry::Surface)
        {
            const bool cylinderHeightPhase =
                context.IsGeometryCylinderHeightPhase &&
                context.IsGeometryCylinderHeightPhase();
            ImGui::TextDisabled(tool.Geometry() == SmartGeometry::Surface
                    ? "Drag to extend the locked surface"
                    : tool.Mode() == SmartToolMode::SingleVoxel ||
                        tool.Mode() == SmartToolMode::CubeBrush
                    ? "Drag from first corner to opposite corner"
                    : tool.Mode() == SmartToolMode::SphereBrush
                    ? "Drag from center to set radius"
                    : cylinderHeightPhase
                    ? "Move along normal to set height, then click to confirm"
                    : "Drag from center to set radius");
        }
    }

    ImGui::TextDisabled("Action");
    ImGui::PushID("Action");
    int action = static_cast<int>(tool.Action());
    changed |= ImGui::RadioButton(
        "Create", &action, static_cast<int>(SmartAction::Add));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Remove", &action, static_cast<int>(SmartAction::Erase));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Paint", &action, static_cast<int>(SmartAction::Paint));
    ImGui::PopID();
    const SmartAction actionBefore = tool.Action();
    tool.SetAction(static_cast<SmartAction>(action));
    changed |= tool.Action() != actionBefore;

    float previewAlpha = tool.PreviewAlpha();
    if (ImGui::SliderFloat("Preview Alpha", &previewAlpha, 0.0F, 1.0F))
    {
        tool.SetPreviewAlpha(previewAlpha);
        changed = true;
    }
    if (context.BrushProfiles != nullptr)
    {
        BrushProfileService& profiles = *context.BrushProfiles;
        static char newName[96] = "Brush Profile";
        static char rename[96] = {};
        static std::string renameUuid;
        static std::string profileFeedback;
        if (context.ActivePaletteIndex)
        {
            if (const auto activePalette = context.ActivePaletteIndex())
                tool.Brush().PaletteIndex = *activePalette;
        }
        const auto applySelected = [&](const BrushProfileResult& result) {
            if (!result.Succeeded() || !result.Profile)
            {
                profileFeedback = result.Message;
                return;
            }
            changed |= BrushProfileService::Apply(*result.Profile, tool);
            if (context.SelectPaletteIndex && !context.SelectPaletteIndex(result.Profile->PaletteIndex))
                profileFeedback = "Profile selected; its palette color is unavailable.";
            else
                profileFeedback = result.Message.empty() ? "Profile selected." : result.Message;
        };
        ImGui::Separator();
        ImGui::TextDisabled("Brush Profile");
        if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr)
        {
            ImGui::TextUnformatted(active->Name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(active->Favorite ? "Favorite" : "Add Favorite"))
            {
                const bool wasFavorite = active->Favorite;
                const BrushProfileResult result = profiles.SetFavorite(active->Uuid, !wasFavorite);
                profileFeedback = result.Succeeded()
                    ? (wasFavorite ? "Profile unfavorited." : "Profile favorited.")
                    : result.Message;
            }
        }
        if (ImGui::CollapsingHeader("Manage Profiles"))
        {
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputText("Name##BrushProfile", newName, sizeof(newName));
            if (ImGui::Button("Save New"))
            {
                const BrushProfileResult result = profiles.SaveNew(newName, tool);
                applySelected(result);
            }
            if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr &&
                ImGui::Button("Duplicate Active"))
            {
                applySelected(profiles.Duplicate(active->Uuid, active->Name + " Copy"));
            }
            ImGui::TextDisabled("Saved Profiles");
            for (const BrushProfile* profile : profiles.SortedProfiles())
            {
                ImGui::PushID(profile->Uuid.c_str());
                if (ImGui::Selectable(profile->Name.c_str(), profile->Uuid == profiles.ActiveUuid()))
                    applySelected(profiles.SelectProfile(profile->Uuid));
                ImGui::PopID();
            }
            if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr &&
                active->Uuid != BrushProfileService::DefaultProfileUuid)
            {
                if (renameUuid != active->Uuid)
                {
                    renameUuid = active->Uuid;
                    std::snprintf(rename, sizeof(rename), "%s", active->Name.c_str());
                }
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::InputText("Rename##BrushProfile", rename, sizeof(rename));
                if (ImGui::Button("Rename"))
                {
                    const BrushProfileResult result = profiles.Rename(active->Uuid, rename);
                    profileFeedback = result.Succeeded() ? "Profile renamed." : result.Message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Overwrite"))
                {
                    const BrushProfileResult result = profiles.Overwrite(active->Uuid, tool);
                    profileFeedback = result.Succeeded() ? "Profile overwritten." : result.Message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete")) applySelected(profiles.Delete(active->Uuid));
            }
        }
        if (!profileFeedback.empty()) ImGui::TextDisabled("%s", profileFeedback.c_str());
    }
    ImGui::TextDisabled("Advanced");
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("More controls coming soon");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Statistics");
    if (tool.Statistics().Available)
    {
        const auto& stats = tool.Statistics();
        const bool erasing = tool.Action() == SmartAction::Erase;
        const bool painting = tool.Action() == SmartAction::Paint;
        DrawSmartBrushStatistics(
            erasing ? "Erased" : painting ? "Painted" : "New",
            stats.Changed,
            erasing || painting ? "Ignored" : "Existing",
            stats.Unchanged,
            stats.Total,
            stats.Clipped);
    }
    return changed;
}
} // namespace VoxelForge::Editor
