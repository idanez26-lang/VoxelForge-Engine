#pragma once

#include "EditorWorkspace.h"
#include "Layout/EditorLayoutPersistence.h"
#include "VoxelForge/Core/Layer/Layer.h"
#include "VoxelForge/Scene/Scene.h"

#include <cstddef>
#include <functional>
#include <filesystem>
#include <memory>
#include <vector>

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
        std::filesystem::path qualityOfLifeParent = {},
        bool dragDropImportSmokeTest = false,
        std::vector<std::filesystem::path> dragDropSmokePaths = {},
        bool voxelDocumentSmokeTest = false,
        bool voxelRenderSyncSmokeTest = false,
        bool voxelRayPickingSmokeTest = false,
        bool voxelPencilSmokeTest = false,
        bool voxelEraserSmokeTest = false,
        bool voxelUndoRedoSmokeTest = false,
        bool firstCreationExperienceSmokeTest = false,
        bool layoutStabilitySmokeTest = false,
        bool doubleClickCameraSmokeTest = false,
        bool persistentWorkplaneSmokeTest = false,
        bool projectSessionRestoreSmokeTest = false,
        bool directCreationFlowSmokeTest = false,
        bool paletteUiSmokeTest = false,
        bool voxelFillSmokeTest = false,
        bool voxelBoxSmokeTest = false,
        bool voxelLineSmokeTest = false,
        bool voxelSphereSmokeTest = false,
        bool modernToolbarSmokeTest = false,
        bool keyboardShortcutsSmokeTest = false,
        bool voxelMoveSmokeTest = false,
        bool voxelDuplicateSmokeTest = false,
        std::filesystem::path imguiIniPathOverride = {});

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnEvent(VoxelForge::Event& event) override;
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

    EditorLayoutPersistence layoutPersistence_;
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
    bool dragDropImportSmokeTest_ = false;
    std::vector<std::filesystem::path> dragDropSmokePaths_;
    bool voxelDocumentSmokeTest_ = false;
    std::filesystem::path voxelDocumentSmokeSourcePath_;
    bool voxelRenderSyncSmokeTest_ = false;
    std::filesystem::path voxelRenderSyncSmokeSourcePath_;
    bool voxelRayPickingSmokeTest_ = false;
    std::filesystem::path voxelRayPickingSmokeSourcePath_;
    bool voxelPencilSmokeTest_ = false;
    std::filesystem::path voxelPencilSmokeSourcePath_;
    bool voxelEraserSmokeTest_ = false;
    std::filesystem::path voxelEraserSmokeSourcePath_;
    bool voxelUndoRedoSmokeTest_ = false;
    std::filesystem::path voxelUndoRedoSmokeSourcePath_;
    bool firstCreationExperienceSmokeTest_ = false;
    bool layoutStabilitySmokeTest_ = false;
    bool doubleClickCameraSmokeTest_ = false;
    bool persistentWorkplaneSmokeTest_ = false;
    bool projectSessionRestoreSmokeTest_ = false;
    bool directCreationFlowSmokeTest_ = false;
    bool paletteUiSmokeTest_ = false;
    bool voxelFillSmokeTest_ = false;
    bool voxelBoxSmokeTest_ = false;
    bool voxelLineSmokeTest_ = false;
    bool voxelSphereSmokeTest_ = false;
    bool modernToolbarSmokeTest_ = false;
    bool keyboardShortcutsSmokeTest_ = false;
    bool voxelMoveSmokeTest_ = false;
    bool voxelDuplicateSmokeTest_ = false;
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
