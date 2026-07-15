#include "EditorWorkspace.h"

#include "VoxelForge/Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
constexpr float StatusBarHeight = 26.0F;
constexpr const char* WorkspaceDockspaceName = "VoxelForgeStudioDockSpace";
constexpr const char* AboutPopupName = "About VoxelForge Studio";
}

EditorWorkspace::EditorWorkspace()
    : consoleMessages_{
          "Console ready",
          "VoxelForge Studio initialized"}
{
}

void EditorWorkspace::Draw()
{
    DrawMainMenuBar();

    const ImGuiID dockspaceId = ImGui::GetID(WorkspaceDockspaceName);
    const bool layoutMissing =
        ImGui::DockBuilderGetNode(dockspaceId) == nullptr;

    DrawDockSpace(dockspaceId);

    if (layoutMissing || resetLayoutRequested_)
    {
        BuildDefaultLayout(dockspaceId);
        resetLayoutRequested_ = false;
    }

    if (showExplorer_) DrawExplorerPanel();
    if (showScene_) DrawScenePanel();
    if (showInspector_) DrawInspectorPanel();
    if (showAssetBrowser_) DrawAssetBrowserPanel();
    if (showConsole_) DrawConsolePanel();
    if (showProfiler_) DrawProfilerPanel();

    if (showImGuiDemo_)
    {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }

    DrawStatusBar();
    DrawAboutPopup();
}

void EditorWorkspace::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Project", "Ctrl+N"))
        {
            AddConsoleMessage("New Project is not available yet.");
        }

        if (ImGui::MenuItem("Open Project", "Ctrl+O"))
        {
            AddConsoleMessage("Open Project is not available yet.");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
            AddConsoleMessage("Save is not available yet.");
        }

        if (ImGui::MenuItem("Save As", "Ctrl+Shift+S"))
        {
            AddConsoleMessage("Save As is not available yet.");
        }

        ImGui::Separator();
        ImGui::MenuItem("Exit", nullptr, false, false);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(
                "Exit is disabled until the editor owns a direct close request.");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z"))
        {
            AddConsoleMessage("Undo is not available yet.");
        }

        if (ImGui::MenuItem("Redo", "Ctrl+Y"))
        {
            AddConsoleMessage("Redo is not available yet.");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Preferences"))
        {
            AddConsoleMessage("Preferences are not available yet.");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Explorer", nullptr, &showExplorer_);
        ImGui::MenuItem("Scene", nullptr, &showScene_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Console", nullptr, &showConsole_);
        ImGui::MenuItem("Profiler", nullptr, &showProfiler_);
        ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
        ImGui::Separator();

        if (ImGui::MenuItem("Reset Layout"))
        {
            resetLayoutRequested_ = true;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Voxel Editor"))
        {
            AddConsoleMessage("Voxel Editor is not available yet.");
        }

        if (ImGui::MenuItem("Material Editor"))
        {
            AddConsoleMessage("Material Editor is not available yet.");
        }

        if (ImGui::MenuItem("Asset Processor"))
        {
            AddConsoleMessage("Asset Processor is not available yet.");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About VoxelForge Studio"))
        {
            showAboutPopup_ = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorWorkspace::DrawDockSpace(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - StatusBarHeight));

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(workspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    ImGui::Begin("##VoxelForgeWorkspaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGui::DockSpace(
        dockspaceId,
        ImVec2(0.0F, 0.0F),
        ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void EditorWorkspace::BuildDefaultLayout(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - StatusBarHeight));

    ImGui::DockBuilderRemoveNode(dockspaceId);

    const ImGuiDockNodeFlags dockNodeFlags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockBuilderAddNode(dockspaceId, dockNodeFlags);
    ImGui::DockBuilderSetNodeSize(dockspaceId, workspaceSize);

    ImGuiID topId = dockspaceId;
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Down,
        0.28F,
        nullptr,
        &topId);

    const ImGuiID rightId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Right,
        0.22F,
        nullptr,
        &topId);

    const ImGuiID leftId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Left,
        0.26F,
        nullptr,
        &topId);

    ImGuiID bottomLeftId = bottomId;
    const ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(
        bottomLeftId,
        ImGuiDir_Right,
        0.40F,
        nullptr,
        &bottomLeftId);

    ImGui::DockBuilderDockWindow("Explorer", leftId);
    ImGui::DockBuilderDockWindow("Scene", topId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Asset Browser", bottomLeftId);
    ImGui::DockBuilderDockWindow("Console", bottomRightId);
    ImGui::DockBuilderFinish(dockspaceId);

    showExplorer_ = true;
    showScene_ = true;
    showInspector_ = true;
    showAssetBrowser_ = true;
    showConsole_ = true;
}

void EditorWorkspace::DrawExplorerPanel()
{
    ImGui::Begin("Explorer", &showExplorer_);
    ImGui::TextUnformatted("Project Explorer");
    ImGui::Separator();
    ImGui::TextDisabled("No project loaded");
    ImGui::End();
}

void EditorWorkspace::DrawScenePanel()
{
    ImGui::Begin("Scene", &showScene_);
    ImGui::TextUnformatted("Scene");
    ImGui::Separator();
    ImGui::TextDisabled("3D viewport will appear here");
    ImGui::End();
}

void EditorWorkspace::DrawInspectorPanel()
{
    ImGui::Begin("Inspector", &showInspector_);
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();
    ImGui::TextDisabled("No object selected");
    ImGui::End();
}

void EditorWorkspace::DrawAssetBrowserPanel()
{
    ImGui::Begin("Asset Browser", &showAssetBrowser_);
    ImGui::TextUnformatted("Asset Browser");
    ImGui::Separator();
    ImGui::TextDisabled("No assets available");
    ImGui::End();
}

void EditorWorkspace::DrawConsolePanel()
{
    ImGui::Begin("Console", &showConsole_);

    for (const std::string& message : consoleMessages_)
    {
        ImGui::TextUnformatted(message.c_str());
    }

    ImGui::End();
}

void EditorWorkspace::DrawProfilerPanel()
{
    ImGui::Begin("Profiler", &showProfiler_);

    const ImGuiIO& io = ImGui::GetIO();
    const float frameTime =
        io.Framerate > 0.0F ? 1000.0F / io.Framerate : 0.0F;
    const std::string backendName = GetBackendDisplayName();

    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("Frame time: %.2f ms", frameTime);
    ImGui::Text("Graphics backend: %s", backendName.c_str());
    ImGui::Text("ImGui version: %s", IMGUI_VERSION);

    ImGui::End();
}

void EditorWorkspace::DrawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(
        viewport->WorkPos.x,
        viewport->WorkPos.y + viewport->WorkSize.y - StatusBarHeight));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x, StatusBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 5.0F));

    if (ImGui::Begin("##VoxelForgeStatusBar", nullptr, windowFlags))
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float frameTime =
            io.Framerate > 0.0F ? 1000.0F / io.Framerate : 0.0F;
        const std::string backendName = GetBackendDisplayName();

        ImGui::Text(
            "Ready | FPS %.1f | %.2f ms | Backend: %s | ImGui %s",
            io.Framerate,
            frameTime,
            backendName.c_str(),
            IMGUI_VERSION);
        ImGui::SameLine();
        ImGui::TextDisabled("| Créer plus vite. Rester l'artisan.");
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void EditorWorkspace::DrawAboutPopup()
{
    if (showAboutPopup_)
    {
        ImGui::OpenPopup(AboutPopupName);
        showAboutPopup_ = false;
    }

    if (ImGui::BeginPopupModal(
            AboutPopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("VoxelForge Studio");
        ImGui::Separator();
        ImGui::TextUnformatted("Créer plus vite. Rester l'artisan.");
        ImGui::Spacing();
        ImGui::TextDisabled("Early development build");
        ImGui::Spacing();

        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorWorkspace::AddConsoleMessage(std::string message)
{
    consoleMessages_.push_back(std::move(message));
    showConsole_ = true;
}

std::string EditorWorkspace::GetBackendDisplayName() const
{
    const std::string& backendName = Renderer::Renderer::GetBackendName();

    if (backendName == "direct3d12" || backendName == "d3d12")
    {
        return "Direct3D 12";
    }

    return backendName.empty() ? "Unavailable" : backendName;
}

} // namespace VoxelForge::Editor
