#pragma once

#include "EditorCamera.h"
#include "EditorWorkspace.h"
#include "ViewportRenderer.h"
#include "VoxelForge/Core/Layer/Layer.h"
#include "VoxelForge/Scene/Scene.h"

#include <cstddef>
#include <functional>
#include <memory>

struct ImGuiViewport;
using ImGuiID = unsigned int;

namespace VoxelForge::Editor
{

class EditorLayer final : public Core::Layer
{
public:
    using ApplicationCloseCallback = std::function<void()>;

    explicit EditorLayer(
        ApplicationCloseCallback applicationCloseCallback,
        std::size_t smokeTestFrameLimit = 0);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnImGuiRender() override;

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

    EditorCamera editorCamera_{};
    ViewportRenderer viewportRenderer_{};
    EditorWorkspace workspace_{};
    ApplicationCloseCallback applicationCloseCallback_;

    std::size_t smokeTestFrameLimit_ = 0;
    std::size_t renderedFrameCount_ = 0;
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
