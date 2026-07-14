#include "EditorLayer.h"

#include "VoxelForge/Core/Logger.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

EditorLayer::EditorLayer()
    : Layer("VoxelForge Editor Layer")
{
}

void EditorLayer::OnAttach()
{
    Core::Logger::Instance().Info("Editor layer attached.");
}

void EditorLayer::OnDetach()
{
    Core::Logger::Instance().Info("Editor layer detached.");
}

void EditorLayer::OnImGuiRender()
{
    ImGui::SetNextWindowSize(
        ImVec2(520.0F, 300.0F),
        ImGuiCond_FirstUseEver);

    if (ImGui::Begin("VoxelForge Editor"))
    {
        ImGui::TextUnformatted("VoxelForge Engine v0.0.7");
        ImGui::Separator();
        ImGui::TextWrapped(
            "La premiere interface Dear ImGui de VoxelForge fonctionne.");
        ImGui::Spacing();

        ImGui::Checkbox(
            "Afficher la demonstration Dear ImGui",
            &showDemoWindow_);

        if (ImGui::Button("A propos de VoxelForge"))
        {
            showAboutWindow_ = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "Prochaine etape : menu principal, dockspace et panneaux.");
    }

    ImGui::End();

    if (showAboutWindow_)
    {
        ImGui::OpenPopup("A propos");
        showAboutWindow_ = false;
    }

    if (ImGui::BeginPopupModal(
            "A propos",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("VoxelForge Engine");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Une plateforme de creation voxel assistee par IA, "
            "avec le controle creatif conserve par l'artiste.");

        if (ImGui::Button("Fermer"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (showDemoWindow_)
    {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }
}

} // namespace VoxelForge::Editor
