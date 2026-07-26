#include "SmartBrushOptions.h"
#include "SmartToolPanel.h"
#include "SmartTools/BrushProfileService.h"

#include <imgui.h>

#include <cstdio>

namespace VoxelForge::Editor
{
bool DrawSmartToolPanel(ToolContext& context)
{
    SmartTool& tool = context.Smart;
    bool changed = false;
    if (tool.Geometry() == SmartGeometry::Cube ||
        tool.Geometry() == SmartGeometry::Sphere)
    {
        tool.Brush().Shape = ResolveSmartBrushShape(
            tool.Geometry(), tool.Brush().Shape);
        tool.SetGeometry(SmartGeometry::Pencil);
        changed = true;
    }
    ImGui::TextDisabled("SMART TOOL");
    ImGui::TextDisabled("Geometry");
    ImGui::PushID("Geometry");
    int geometry = static_cast<int>(SmartGeometry::Pencil);
    changed |= ImGui::RadioButton(
        "Pencil", &geometry, static_cast<int>(SmartGeometry::Pencil));
    ImGui::BeginDisabled();
    ImGui::RadioButton("Face", &geometry, static_cast<int>(SmartGeometry::Face));
    ImGui::RadioButton("Box", &geometry, static_cast<int>(SmartGeometry::Box));
    ImGui::RadioButton("Line", &geometry, static_cast<int>(SmartGeometry::Line));
    ImGui::EndDisabled();
    ImGui::PopID();
    const SmartGeometry geometryBefore = tool.Geometry();
    tool.SetGeometry(static_cast<SmartGeometry>(geometry));
    changed |= tool.Geometry() != geometryBefore;

    ImGui::TextDisabled("Action");
    ImGui::PushID("Action");
    int action = static_cast<int>(tool.Action());
    changed |= ImGui::RadioButton(
        "Add", &action, static_cast<int>(SmartAction::Add));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Paint", &action, static_cast<int>(SmartAction::Paint));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Erase", &action, static_cast<int>(SmartAction::Erase));
    ImGui::BeginDisabled();
    ImGui::SameLine();
    ImGui::RadioButton(
        "Replace", &action, static_cast<int>(SmartAction::Replace));
    ImGui::EndDisabled();
    ImGui::PopID();
    const SmartAction actionBefore = tool.Action();
    tool.SetAction(static_cast<SmartAction>(action));
    changed |= tool.Action() != actionBefore;

    changed |= DrawSmartBrushOptions(tool.Brush());
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
