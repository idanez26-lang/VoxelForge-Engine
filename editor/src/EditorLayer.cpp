#include "EditorLayer.h"

#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string>
#include <stdexcept>
#include <utility>

namespace VoxelForge::Editor
{

EditorLayer::EditorLayer(
    Project::ProjectManager& projectManager,
    WindowTitleCallback windowTitleCallback,
    ApplicationCloseCallback applicationCloseCallback,
    const std::size_t smokeTestFrameLimit,
    std::filesystem::path startupVoxPath,
    const bool requireVoxelViewportRender,
    const bool voxelSelectionSmokeTest,
    const bool voxelSelectionVisualTest,
    const bool eraseVoxelSmokeTest,
    const bool eraseVoxelVisualTest)
    : Layer("VoxelForge Editor Layer"),
      workspace_(projectManager, std::move(windowTitleCallback)),
      applicationCloseCallback_(std::move(applicationCloseCallback)),
      smokeTestFrameLimit_(smokeTestFrameLimit),
      startupVoxPath_(std::move(startupVoxPath)),
      requireVoxelViewportRender_(requireVoxelViewportRender),
      voxelSelectionSmokeTest_(voxelSelectionSmokeTest),
      voxelSelectionVisualTest_(voxelSelectionVisualTest),
      eraseVoxelSmokeTest_(eraseVoxelSmokeTest),
      eraseVoxelVisualTest_(eraseVoxelVisualTest)
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
    if (!startupVoxPath_.empty())
    {
        if (!workspace_.OpenVoxInViewport(startupVoxPath_))
        {
            throw std::runtime_error("Viewport startup model failed to load.");
        }
        startupVoxPath_.clear();
    }
    if (requireVoxelViewportRender_ && renderedFrameCount_ == 5U)
        workspace_.SetVoxelViewportView(EditorCameraView::Front);
    if (requireVoxelViewportRender_ && renderedFrameCount_ == 15U)
        workspace_.SetVoxelViewportView(EditorCameraView::Top);
    if (voxelSelectionSmokeTest_ ||
        (voxelSelectionVisualTest_ && renderedFrameCount_ == 0U))
    {
        voxelSelectionRayHit_ |=
            workspace_.RunVoxelSelectionSmokeStep(renderedFrameCount_);
    }
    if (eraseVoxelSmokeTest_ ||
        (eraseVoxelVisualTest_ && renderedFrameCount_ <= 1U))
    {
        static_cast<void>(
            workspace_.RunEraseVoxelSmokeStep(renderedFrameCount_));
    }
    workspace_.Draw();

    ++renderedFrameCount_;

    const bool smokeTestComplete =
        smokeTestFrameLimit_ > 0 &&
        renderedFrameCount_ >= smokeTestFrameLimit_;

    if (requireVoxelViewportRender_ &&
        (workspace_.HasVoxelViewportRenderError() ||
         (smokeTestComplete && !workspace_.HasRenderedVoxelViewport())))
    {
        throw std::runtime_error("Viewport smoke test did not render a GPU frame.");
    }
    if (voxelSelectionSmokeTest_ && smokeTestComplete &&
        (!voxelSelectionRayHit_ ||
         workspace_.VoxelHighlightUploadCount() == 0U ||
         workspace_.VoxelHighlightRenderCount() < 5U))
    {
        throw std::runtime_error(
            "Voxel selection smoke test did not raycast, upload, and render highlights.");
    }
    if (eraseVoxelSmokeTest_ && smokeTestComplete &&
        !workspace_.EraseVoxelSmokePassed())
    {
        throw std::runtime_error(
            "Erase voxel smoke test did not execute, undo, redo, and render.");
    }

    if (workspace_.ConsumeExitRequest() || smokeTestComplete)
    {
        RequestApplicationClose();
    }
}

void EditorLayer::RequestApplicationClose()
{
    if (applicationCloseRequested_)
    {
        return;
    }

    applicationCloseRequested_ = true;

    if (applicationCloseCallback_)
    {
        applicationCloseCallback_();
    }
}

void EditorLayer::BuildDefaultWorkspace(
    const ImGuiID dockspaceId,
    const ImGuiViewport& viewport)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);

    const ImGuiDockNodeFlags dockNodeFlags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockBuilderAddNode(
        dockspaceId,
        dockNodeFlags);
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

    ImGui::TextDisabled("The active viewport is hosted by the Scene panel.");

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
