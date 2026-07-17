#include "EditorDialogStyle.h"

#include <imgui.h>

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
ImVec4 AccentColor(const EditorDialogIntent intent) noexcept
{
    switch (intent)
    {
    case EditorDialogIntent::Destructive:
        return {0.78F, 0.22F, 0.20F, 1.0F};
    case EditorDialogIntent::Warning:
        return {0.90F, 0.55F, 0.16F, 1.0F};
    case EditorDialogIntent::Information:
        return {0.25F, 0.66F, 0.43F, 1.0F};
    default:
        return {0.18F, 0.46F, 0.76F, 1.0F};
    }
}

void DrawContextIcon(const EditorDialogIntent intent)
{
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 accent = AccentColor(intent);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(accent);
    const ImVec2 center(position.x + 16.0F, position.y + 16.0F);
    drawList->AddCircleFilled(center, 16.0F, color, 24);

    if (intent == EditorDialogIntent::Warning ||
        intent == EditorDialogIntent::Destructive)
    {
        drawList->AddText(
            ImVec2(center.x - 3.0F, center.y - 8.0F),
            IM_COL32(255, 255, 255, 255), "!");
    }
    else
    {
        drawList->AddRect(
            ImVec2(center.x - 7.0F, center.y - 6.0F),
            ImVec2(center.x + 7.0F, center.y + 7.0F),
            IM_COL32(255, 255, 255, 255), 2.0F, 0, 2.0F);
        drawList->AddLine(
            ImVec2(center.x - 5.0F, center.y - 9.0F),
            ImVec2(center.x + 1.0F, center.y - 9.0F),
            IM_COL32(255, 255, 255, 255), 2.0F);
    }
    ImGui::Dummy(ImVec2(40.0F, 36.0F));
}
}

bool EditorDialogStyle::BeginPopup(
    const char* popupName,
    const EditorDialogIntent intent,
    const std::string_view title,
    const std::string_view description,
    const bool hasEditableField)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const EditorDialogLayout layout =
        EditorDialogModel::CalculateLayout(viewport->WorkSize.x);
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(layout.Width, 0.0F),
        ImVec2(std::max(layout.Width, 760.0F), viewport->WorkSize.y - 32.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0F, 18.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0F, 10.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0F, 7.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    const bool open = ImGui::BeginPopupModal(
        popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(4);
    if (!open) return false;

    DrawContextIcon(intent);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextWrapped("%.*s", static_cast<int>(description.size()), description.data());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::Spacing();

    if (EditorDialogModel::ShouldFocusFirstField(
            ImGui::IsWindowAppearing(), hasEditableField))
        ImGui::SetKeyboardFocusHere();
    return true;
}

void EditorDialogStyle::EndPopup()
{
    ImGui::EndPopup();
}

void EditorDialogStyle::FullWidthField()
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
}

void EditorDialogStyle::DrawMessage(
    const std::string_view message,
    const EditorDialogIntent intent)
{
    if (message.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, AccentColor(intent));
    ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
    ImGui::PopStyleColor();
}

void EditorDialogStyle::BeginActions()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

bool EditorDialogStyle::ActionButton(
    const char* label,
    const bool primary,
    const bool enabled,
    const bool destructive)
{
    const EditorDialogLayout layout = EditorDialogModel::CalculateLayout(
        ImGui::GetMainViewport()->WorkSize.x);
    ImGui::BeginDisabled(!enabled);
    if (destructive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.66F, 0.18F, 0.17F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78F, 0.24F, 0.22F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.56F, 0.13F, 0.13F, 1.0F));
    }
    else if (primary)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.46F, 0.76F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24F, 0.55F, 0.88F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14F, 0.38F, 0.66F, 1.0F));
    }
    const bool pressed = ImGui::Button(label, ImVec2(layout.ButtonWidth, 34.0F));
    if (primary || destructive) ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    return pressed && enabled;
}

EditorDialogShortcut EditorDialogStyle::Shortcuts(
    const bool confirmationEnabled)
{
    return EditorDialogModel::ResolveShortcut(
        ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false),
        ImGui::IsKeyPressed(ImGuiKey_Escape, false),
        confirmationEnabled);
}

}
