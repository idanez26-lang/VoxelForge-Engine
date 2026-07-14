#include "EditorLayer.h"

#include "VoxelForge/Core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace VoxelForge::Editor
{

EditorLayer::EditorLayer()
    : Layer("VoxelForge Editor Layer")
{
}

void EditorLayer::OnAttach()
{
    Core::Logger::Instance().Info("Editor default workspace attached.");
}

void EditorLayer::OnDetach()
{
    Core::Logger::Instance().Info("Editor default workspace detached.");
}

void EditorLayer::OnImGuiRender()
{
    DrawMainMenuBar();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
        0,
        viewport,
        ImGuiDockNodeFlags_PassthruCentralNode);

    ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);

    const bool workspaceIsEmpty =
        dockNode == nullptr ||
        (dockNode->ChildNodes[0] == nullptr &&
         dockNode->ChildNodes[1] == nullptr);

    if (resetWorkspaceRequested_ || workspaceIsEmpty)
    {
        BuildDefaultWorkspace(dockspaceId, *viewport);
        resetWorkspaceRequested_ = false;
    }

    if (showHierarchy_)
    {
        DrawHierarchyPanel();
    }

    if (showInspector_)
    {
        DrawInspectorPanel();
    }

    if (showViewport_)
    {
        DrawViewportPanel();
    }

    if (showAssetBrowser_)
    {
        DrawAssetBrowserPanel();
    }

    if (showConsole_)
    {
        DrawConsolePanel();
    }

    DrawStatusBar();

    if (showAboutPopup_)
    {
        ImGui::OpenPopup("A propos de VoxelForge");
        showAboutPopup_ = false;
    }

    if (ImGui::BeginPopupModal(
            "A propos de VoxelForge",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("VoxelForge Editor v0.0.8");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Plateforme professionnelle de creation voxel assistee par IA.");
        ImGui::TextWrapped(
            "Principe : l'IA accelere le travail, l'artiste garde le controle.");

        if (ImGui::Button("Fermer"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorLayer::BuildDefaultWorkspace(
    const ImGuiID dockspaceId,
    const ImGuiViewport& viewport)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(
        dockspaceId,
        ImGuiDockNodeFlags_DockSpace |
            ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport.WorkSize);

    ImGuiID centerId = dockspaceId;

    const ImGuiID leftId = ImGui::DockBuilderSplitNode(
        centerId,
        ImGuiDir_Left,
        0.20F,
        nullptr,
        &centerId);

    const ImGuiID rightId = ImGui::DockBuilderSplitNode(
        centerId,
        ImGuiDir_Right,
        0.24F,
        nullptr,
        &centerId);

    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        centerId,
        ImGuiDir_Down,
        0.28F,
        nullptr,
        &centerId);

    ImGuiID bottomLeftId = bottomId;
    const ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(
        bottomLeftId,
        ImGuiDir_Right,
        0.48F,
        nullptr,
        &bottomLeftId);

    ImGui::DockBuilderDockWindow("Hierarchy", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Viewport", centerId);
    ImGui::DockBuilderDockWindow("Asset Browser", bottomLeftId);
    ImGui::DockBuilderDockWindow("Console", bottomRightId);

    ImGui::DockBuilderFinish(dockspaceId);

    showHierarchy_ = true;
    showInspector_ = true;
    showViewport_ = true;
    showAssetBrowser_ = true;
    showConsole_ = true;

    Core::Logger::Instance().Info(
        "VoxelForge default editor workspace created.");
}

void EditorLayer::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("Fichier"))
    {
        ImGui::MenuItem("Nouveau projet", "Ctrl+N");
        ImGui::MenuItem("Ouvrir un projet", "Ctrl+O");
        ImGui::Separator();
        ImGui::MenuItem("Enregistrer", "Ctrl+S");
        ImGui::MenuItem("Enregistrer sous...");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edition"))
    {
        ImGui::MenuItem("Annuler", "Ctrl+Z");
        ImGui::MenuItem("Retablir", "Ctrl+Y");
        ImGui::Separator();
        ImGui::MenuItem("Preferences");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Affichage"))
    {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Console", nullptr, &showConsole_);

        ImGui::Separator();

        if (ImGui::MenuItem("Reinitialiser l'espace de travail"))
        {
            resetWorkspaceRequested_ = true;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Outils"))
    {
        ImGui::MenuItem("Forge AI");
        ImGui::MenuItem("Editeur de palette");
        ImGui::MenuItem("Generateur procedural");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Aide"))
    {
        if (ImGui::MenuItem("A propos"))
        {
            showAboutPopup_ = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorLayer::DrawHierarchyPanel()
{
    ImGui::Begin("Hierarchy", &showHierarchy_);

    ImGui::TextDisabled("Scene : DemoProject");
    ImGui::Separator();

    if (ImGui::TreeNodeEx(
            "DemoProject",
            ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        ImGui::Selectable("Camera");
        ImGui::Selectable("Directional Light");
        ImGui::Selectable("Voxel Object");

        ImGui::TreePop();
    }

    ImGui::End();
}

void EditorLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector", &showInspector_);

    ImGui::TextUnformatted("Aucun objet selectionne");
    ImGui::Separator();
    ImGui::TextDisabled(
        "Les proprietes de l'objet selectionne apparaitront ici.");

    ImGui::End();
}

void EditorLayer::DrawViewportPanel()
{
    ImGui::Begin("Viewport", &showViewport_);

    const ImVec2 availableSize = ImGui::GetContentRegionAvail();

    const float centeredX =
        ImGui::GetCursorPosX() +
        (availableSize.x * 0.5F) -
        90.0F;

    const float centeredY =
        ImGui::GetCursorPosY() +
        (availableSize.y * 0.5F) -
        10.0F;

    ImGui::SetCursorPos(ImVec2(centeredX, centeredY));
    ImGui::TextDisabled("Viewport 3D - bientot disponible");

    ImGui::End();
}

void EditorLayer::DrawAssetBrowserPanel()
{
    ImGui::Begin("Asset Browser", &showAssetBrowser_);

    ImGui::TextUnformatted("Assets/");
    ImGui::Separator();
    ImGui::Selectable("Models");
    ImGui::SameLine();
    ImGui::Selectable("Materials");
    ImGui::SameLine();
    ImGui::Selectable("Textures");

    ImGui::End();
}

void EditorLayer::DrawConsolePanel()
{
    ImGui::Begin("Console", &showConsole_);

    if (ImGui::Button("Effacer"))
    {
        // Le branchement au Logger sera ajoute dans un prochain sprint.
    }

    ImGui::Separator();
    ImGui::TextColored(
        ImVec4(0.45F, 0.85F, 0.55F, 1.0F),
        "[INFO] VoxelForge Editor est pret.");
    ImGui::TextDisabled(
        "[INFO] La vraie console sera connectee au Logger.");

    ImGui::End();
}

void EditorLayer::DrawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float statusBarHeight = 24.0F;

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x,
            viewport->WorkPos.y +
                viewport->WorkSize.y -
                statusBarHeight));

    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x, statusBarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(8.0F, 4.0F));

    if (ImGui::Begin("##VoxelForgeStatusBar", nullptr, flags))
    {
        const ImGuiIO& io = ImGui::GetIO();

        ImGui::Text(
            "FPS : %.1f | Projet : DemoProject | Branche : feature/imgui",
            io.Framerate);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace VoxelForge::Editor
