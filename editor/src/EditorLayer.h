#pragma once

#include "EditorWorkspace.h"
#include "VoxelForge/Core/Layer/Layer.h"
#include "VoxelForge/Scene/Scene.h"

#include <cstddef>
#include <functional>
#include <filesystem>
#include <memory>

struct ImGuiViewport;
using ImGuiID = unsigned int;

namespace VoxelForge::Project
{
class ProjectManager;
}

namespace VoxelForge::Editor
{

class EditorLayer final : public Core::Layer
{
public:
    using ApplicationCloseCallback = std::function<void()>;
    using WindowTitleCallback = EditorWorkspace::WindowTitleCallback;

    explicit EditorLayer(
        Project::ProjectManager& projectManager,
        WindowTitleCallback windowTitleCallback,
        ApplicationCloseCallback applicationCloseCallback,
        std::size_t smokeTestFrameLimit = 0,
        std::filesystem::path startupVoxPath = {},
        bool requireVoxelViewportRender = false,
        bool voxelSelectionSmokeTest = false,
        bool voxelSelectionVisualTest = false,
        bool eraseVoxelSmokeTest = false,
        bool eraseVoxelVisualTest = false,
        bool paintVoxelSmokeTest = false,
        bool paintVoxelVisualTest = false,
        bool voxelSaveSmokeTest = false,
        bool addVoxelSmokeTest = false,
        bool modelImportSmokeTest = false,
        bool modelImportVisualTest = false,
        bool qualityOfLifeSmokeTest = false,
        std::filesystem::path qualityOfLifeParent = {});

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnImGuiRender() override;
    [[nodiscard]] bool RequestWindowClose();

private:
    void RequestApplicationClose();

    void BuildDefaultWorkspace(
        ImGuiID dockspaceId,
        const ImGuiViewport& viewport);

    void CreateDefaultScene();
    void DrawMainMenuBar();
    void DrawHierarchyPanel();
    void DrawInspectorPanel();
    void DrawViewportPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawStatusBar();

    std::unique_ptr<Scene::Scene> scene_;
    Scene::Entity* selectedEntity_ = nullptr;

    EditorWorkspace workspace_;
    ApplicationCloseCallback applicationCloseCallback_;

    std::size_t smokeTestFrameLimit_ = 0;
    std::size_t renderedFrameCount_ = 0;
    std::filesystem::path startupVoxPath_;
    bool requireVoxelViewportRender_ = false;
    bool voxelSelectionSmokeTest_ = false;
    bool voxelSelectionVisualTest_ = false;
    bool eraseVoxelSmokeTest_ = false;
    bool eraseVoxelVisualTest_ = false;
    bool paintVoxelSmokeTest_ = false;
    bool paintVoxelVisualTest_ = false;
    bool voxelSaveSmokeTest_ = false;
    bool addVoxelSmokeTest_ = false;
    bool modelImportSmokeTest_ = false;
    bool modelImportVisualTest_ = false;
    bool qualityOfLifeSmokeTest_ = false;
    std::filesystem::path qualityOfLifeParent_;
    bool voxelSelectionRayHit_ = false;
    bool applicationCloseRequested_ = false;

    float deltaTime_ = 1.0F / 60.0F;
    bool viewportHovered_ = false;

    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showViewport_ = true;
    bool showAssetBrowser_ = true;
    bool showConsole_ = true;
    bool showAboutPopup_ = false;
    bool resetWorkspaceRequested_ = false;
};

} // namespace VoxelForge::Editor
