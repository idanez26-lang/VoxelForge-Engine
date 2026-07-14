#include "EditorLayer.h"

#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string>

namespace VoxelForge::Editor
{

EditorLayer::EditorLayer()
    : Layer("VoxelForge Editor Layer")
{
}

void EditorLayer::OnAttach()
{
    CreateDefaultScene();
    Core::Logger::Instance().Info("Viewport 3D prototype attached.");
}

void EditorLayer::OnDetach()
{
    selectedEntity_ = nullptr;
    scene_.reset();
    Core::Logger::Instance().Info("Viewport 3D prototype detached.");
}

void EditorLayer::OnUpdate()
{
    const ImGuiIO& io = ImGui::GetIO();
    deltaTime_ = std::clamp(io.DeltaTime, 0.0001F, 0.1F);
    editorCamera_.Update(deltaTime_, viewportHovered_);
}

void EditorLayer::CreateDefaultScene()
{
    scene_ = std::make_unique<Scene::Scene>("DemoScene");

    Scene::Entity& camera = scene_->CreateEntity("Camera");
    camera.GetMetadata().Category = "Camera";
    camera.GetForgeDNA().Purpose = "Editor View";
    camera.GetTransform().Position = {8.0F, 6.0F, 10.0F};

    Scene::Entity& light = scene_->CreateEntity("Directional Light");
    light.GetMetadata().Category = "Light";
    light.GetForgeDNA().Purpose = "Lighting";
    light.GetTransform().Rotation = {45.0F, -30.0F, 0.0F};

    Scene::Entity& voxelObject = scene_->CreateEntity("Voxel Object");
    voxelObject.GetMetadata().Category = "Voxel";
    voxelObject.GetMetadata().Tags = {"Destructible", "Prototype"};
    voxelObject.GetForgeDNA().Style = "VoxelForge Default";
    voxelObject.GetForgeDNA().Palette = "Forge Neutral";
    voxelObject.GetForgeDNA().Purpose = "Creation";
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

    if (showHierarchy_) DrawHierarchyPanel();
    if (showInspector_) DrawInspectorPanel();
    if (showViewport_) DrawViewportPanel();
    if (showAssetBrowser_) DrawAssetBrowserPanel();
    if (showConsole_) DrawConsolePanel();

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
        ImGui::TextUnformatted("VoxelForge Studio v0.1.2");
        ImGui::Separator();
        ImGui::TextUnformatted("Créer plus vite. Rester l'artisan.");

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
        centerId, ImGuiDir_Left, 0.20F, nullptr, &centerId);
    const ImGuiID rightId = ImGui::DockBuilderSplitNode(
        centerId, ImGuiDir_Right, 0.24F, nullptr, &centerId);
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        centerId, ImGuiDir_Down, 0.28F, nullptr, &centerId);

    ImGuiID bottomLeftId = bottomId;
    const ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(
        bottomLeftId, ImGuiDir_Right, 0.48F, nullptr, &bottomLeftId);

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
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edition"))
    {
        ImGui::MenuItem("Annuler", "Ctrl+Z");
        ImGui::MenuItem("Retablir", "Ctrl+Y");
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

    if (scene_ == nullptr)
    {
        ImGui::TextDisabled("Aucune scene chargee.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(
        "Scene : %s (%zu objets)",
        scene_->GetName().c_str(),
        scene_->GetEntityCount());
    ImGui::Separator();

    for (const auto& entity : scene_->GetEntities())
    {
        const bool selected = selectedEntity_ == entity.get();

        if (ImGui::Selectable(
                entity->GetName().c_str(),
                selected,
                ImGuiSelectableFlags_SpanAvailWidth))
        {
            selectedEntity_ = entity.get();
        }
    }

    ImGui::End();
}

void EditorLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector", &showInspector_);

    if (selectedEntity_ == nullptr)
    {
        ImGui::TextUnformatted("Aucun objet selectionne");
        ImGui::TextDisabled("Selectionne un objet dans la Hierarchy.");
        ImGui::End();
        return;
    }

    char nameBuffer[128]{};
    const std::string& currentName = selectedEntity_->GetName();
    const std::size_t copyLength =
        currentName.size() < 127 ? currentName.size() : 127;
    currentName.copy(nameBuffer, copyLength);

    if (ImGui::InputText("Nom", nameBuffer, sizeof(nameBuffer)))
    {
        selectedEntity_->SetName(nameBuffer);
    }

    ImGui::TextDisabled(
        "UUID : %s",
        selectedEntity_->GetId().ToString().c_str());

    if (ImGui::CollapsingHeader(
            "Transform",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& transform = selectedEntity_->GetTransform();

        ImGui::DragFloat3(
            "Position",
            transform.Position.data(),
            0.1F);
        ImGui::DragFloat3(
            "Rotation",
            transform.Rotation.data(),
            0.5F);
        ImGui::DragFloat3(
            "Echelle",
            transform.Scale.data(),
            0.05F,
            0.01F,
            100.0F);
    }

    if (ImGui::CollapsingHeader(
            "Metadata",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& metadata = selectedEntity_->GetMetadata();
        ImGui::Text("Auteur : %s", metadata.Author.c_str());
        ImGui::Text("Categorie : %s", metadata.Category.c_str());

        for (const std::string& tag : metadata.Tags)
        {
            ImGui::BulletText("%s", tag.c_str());
        }
    }

    if (ImGui::CollapsingHeader(
            "Forge DNA",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& dna = selectedEntity_->GetForgeDNA();
        ImGui::Text("Style : %s", dna.Style.c_str());
        ImGui::Text("Palette : %s", dna.Palette.c_str());
        ImGui::Text("Etat : %s", dna.State.c_str());
        ImGui::Text("Usage : %s", dna.Purpose.c_str());
    }

    ImGui::End();
}

void EditorLayer::DrawViewportPanel()
{
    ImGui::Begin(
        "Viewport",
        &showViewport_,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

    viewportHovered_ = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const ImVec2 viewportOrigin = ImGui::GetCursorScreenPos();
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    viewportSize.x = viewportSize.x < 1.0F ? 1.0F : viewportSize.x;
    viewportSize.y = viewportSize.y < 1.0F ? 1.0F : viewportSize.y;

    Vec3 cubePosition{0.0F, 1.0F, 0.0F};

    if (scene_ != nullptr)
    {
        for (const auto& entity : scene_->GetEntities())
        {
            if (entity->GetMetadata().Category == "Voxel")
            {
                const auto& position = entity->GetTransform().Position;
                cubePosition = {position[0], position[1] + 1.0F, position[2]};
                break;
            }
        }
    }

    viewportRenderer_.Draw(
        *ImGui::GetWindowDrawList(),
        viewportOrigin,
        viewportSize,
        editorCamera_,
        cubePosition);

    ImGui::InvisibleButton(
        "##ViewportInteraction",
        viewportSize,
        ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetCursorScreenPos(
        ImVec2(viewportOrigin.x + 10.0F, viewportOrigin.y + 10.0F));

    ImGui::BeginGroup();

    ImGui::TextUnformatted("Viewport 3D Prototype");
    ImGui::TextDisabled("Clic droit + souris : regarder");
    ImGui::TextDisabled("WASD : bouger | Q/E : descendre/monter");
    ImGui::TextDisabled("Shift : acceleration | Molette : zoom");

    const Vec3& cameraPosition = editorCamera_.GetPosition();
    ImGui::Text(
        "Camera %.1f / %.1f / %.1f",
        cameraPosition.X,
        cameraPosition.Y,
        cameraPosition.Z);

    ImGui::EndGroup();

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

    if (Renderer::Renderer::HasGPUDevice())
    {
        ImGui::TextColored(
            ImVec4(0.45F, 0.85F, 0.55F, 1.0F),
            "[INFO] Interface rendue avec SDL GPU.");

        ImGui::Text(
            "Backend : %s",
            Renderer::Renderer::GetBackendName().c_str());

        ImGui::Text(
            "Shaders : %s",
            Renderer::Renderer::GetShaderFormatsDescription().c_str());
    }
    else
    {
        ImGui::TextColored(
            ImVec4(0.95F, 0.45F, 0.35F, 1.0F),
            "[ERROR] Aucun peripherique SDL GPU actif.");
    }

    ImGui::End();
}

void EditorLayer::DrawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float statusBarHeight = 24.0F;

    ImGui::SetNextWindowPos(ImVec2(
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
        const std::size_t entityCount =
            scene_ == nullptr ? 0 : scene_->GetEntityCount();

        ImGui::Text(
            "FPS : %.1f | Scene : %zu objets | GPU UI : %s | VF-0242",
            io.Framerate,
            entityCount,
            Renderer::Renderer::GetBackendName().c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace VoxelForge::Editor
