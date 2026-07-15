#include "EditorWorkspace.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "EditorWindowTitle.h"

#include "VoxelForge/Project/Project.h"
#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxModelConverter.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <memory>
#include <string_view>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
constexpr float StatusBarHeight = 26.0F;
constexpr std::size_t MaximumConsoleMessageCount = 200;
constexpr const char* WorkspaceDockspaceName = "VoxelForgeStudioDockSpace";
constexpr const char* AboutPopupName = "About VoxelForge Studio";

bool HasProjectExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ".vfproject";
}

bool IsVisibleRecentProject(const std::filesystem::path& path)
{
    std::error_code error;
    return HasProjectExtension(path) &&
        std::filesystem::is_regular_file(path, error) && !error;
}

void DrawErrorMessage(const std::string_view error)
{
    if (error.empty())
    {
        return;
    }

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(0.95F, 0.35F, 0.30F, 1.0F));
    ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
    ImGui::PopStyleColor();
}
}

EditorWorkspace::EditorWorkspace(
    Project::ProjectManager& projectManager,
    WindowTitleCallback windowTitleCallback)
    : projectManager_(projectManager),
      windowTitleCallback_(std::move(windowTitleCallback)),
      consoleMessages_{
          "Console ready",
          "VoxelForge Studio initialized"}
{
    assetBrowser_.SetMessageCallback(
        [this](std::string message)
        {
            AddConsoleMessage(std::move(message));
        });
    assetBrowser_.SetOpenVoxCallback(
        [this](std::filesystem::path filePath)
        {
            static_cast<void>(OpenVoxInViewport(filePath));
        });
}

EditorWorkspace::~EditorWorkspace()
{
    viewportRenderer_.Shutdown();
}

void EditorWorkspace::Draw()
{
    HandleCommandShortcuts();
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
    DrawProjectDialogs();
}

bool EditorWorkspace::ConsumeExitRequest() noexcept
{
    return exitRequest_.ConsumeExitRequest();
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
            RequestNewProjectDialog();
        }

        if (ImGui::MenuItem("Open Project", "Ctrl+O"))
        {
            RequestOpenProjectDialog();
        }

        std::optional<std::filesystem::path> recentProjectToOpen;

        if (ImGui::BeginMenu("Recent Projects"))
        {
            const auto& recentProjects = projectManager_.RecentProjectPaths();
            bool displayedRecentProject = false;

            for (const std::filesystem::path& projectPath : recentProjects)
            {
                if (!IsVisibleRecentProject(projectPath))
                {
                    continue;
                }

                displayedRecentProject = true;
                const std::string pathText = projectPath.string();
                const std::string label = projectPath.stem().string();
                ImGui::PushID(pathText.c_str());

                if (ImGui::MenuItem(label.c_str()))
                {
                    recentProjectToOpen = projectPath;
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", pathText.c_str());
                }

                ImGui::PopID();
            }

            if (!displayedRecentProject)
            {
                ImGui::MenuItem("No recent projects", nullptr, false, false);
            }

            ImGui::EndMenu();
        }

        if (recentProjectToOpen)
        {
            static_cast<void>(OpenProject(*recentProjectToOpen, true));
        }

        ImGui::Separator();
        const bool hasActiveProject = projectManager_.HasActiveProject();

        if (ImGui::MenuItem(
                "Save Project",
                "Ctrl+S",
                false,
                hasActiveProject))
        {
            SaveProject();
        }

        if (ImGui::MenuItem(
                "Close Project",
                nullptr,
                false,
                hasActiveProject))
        {
            CloseProject();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit"))
        {
            exitRequest_.RequestExit();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        const std::string undoLabel = commandHistory_.CanUndo()
            ? "Undo " + std::string(commandHistory_.UndoName())
            : "Undo";
        if (ImGui::MenuItem(
                undoLabel.c_str(), "Ctrl+Z", false,
                commandHistory_.CanUndo()))
        {
            UndoCommand();
        }

        const std::string redoLabel = commandHistory_.CanRedo()
            ? "Redo " + std::string(commandHistory_.RedoName())
            : "Redo";
        if (ImGui::MenuItem(
                redoLabel.c_str(), "Ctrl+Y / Ctrl+Shift+Z", false,
                commandHistory_.CanRedo()))
        {
            RedoCommand();
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

void EditorWorkspace::HandleCommandShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool incompatiblePopupOpen = ImGui::IsPopupOpen(
        nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (io.WantTextInput || ImGui::IsAnyItemActive() || incompatiblePopupOpen)
    {
        return;
    }

    constexpr ImGuiInputFlags shortcutFlags = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(
            ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, shortcutFlags))
    {
        if (commandHistory_.CanRedo()) RedoCommand();
    }
    else if (ImGui::Shortcut(
                 ImGuiMod_Ctrl | ImGuiKey_Z, shortcutFlags))
    {
        if (commandHistory_.CanUndo()) UndoCommand();
    }
    else if (ImGui::Shortcut(
                 ImGuiMod_Ctrl | ImGuiKey_Y, shortcutFlags))
    {
        if (commandHistory_.CanRedo()) RedoCommand();
    }
}

void EditorWorkspace::UndoCommand()
{
    const std::string name(commandHistory_.UndoName());
    const CommandResult result = commandHistory_.Undo();
    AddConsoleMessage(result
        ? "Undo: " + name
        : "Undo failed: " + result.Message);
}

void EditorWorkspace::RedoCommand()
{
    const std::string name(commandHistory_.RedoName());
    const CommandResult result = commandHistory_.Redo();
    AddConsoleMessage(result
        ? "Redo: " + name
        : "Redo failed: " + result.Message);
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

    const auto& activeProject = projectManager_.ActiveProject();

    if (!activeProject)
    {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", activeProject->Name().c_str());
    ImGui::TextWrapped("%s", activeProject->RootPath().string().c_str());
    ImGui::Separator();
    ImGui::BulletText("Assets");
    ImGui::BulletText("Scenes");
    ImGui::BulletText("Cache");
    ImGui::End();
}

void EditorWorkspace::DrawScenePanel()
{
    if (!ImGui::Begin("Scene", &showScene_))
    {
        ImGui::End();
        return;
    }

    if (!projectManager_.HasActiveProject())
    {
        DrawWelcomeScreen();
        ImGui::End();
        return;
    }

    const bool hasModel = viewportState_.HasModel();
    Voxel::VoxelGrid* viewportGrid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    const std::string modelLabel = hasModel
        ? viewportState_.Name() + (voxelModelModified_ ? " *" : "")
        : "No voxel model loaded.";
    ImGui::TextUnformatted(modelLabel.c_str());
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const bool narrowToolbar = toolbarWidth < 520.0F;
    if (ImGui::Button("Frame Model"))
    {
        FrameVoxelViewport();
    }
    ImGui::SameLine();
    const char* viewNames[] = {
        "Perspective", "Front", "Back", "Left", "Right", "Top", "Bottom"};
    int selectedView = static_cast<int>(viewportCamera_.GetView());
    ImGui::SetNextItemWidth(narrowToolbar ? 90.0F : 110.0F);
    if (ImGui::Combo("##ViewportView", &selectedView, viewNames, 7))
    {
        viewportCamera_.SetView(static_cast<EditorCameraView>(selectedView));
    }
    if (!narrowToolbar) ImGui::SameLine();
    bool showGrid = viewportState_.IsGridVisible();
    if (ImGui::Checkbox("Grid", &showGrid))
        viewportState_.SetGridVisible(showGrid);
    ImGui::SameLine();
    bool showAxes = viewportState_.AreAxesVisible();
    if (ImGui::Checkbox("Axes", &showAxes))
        viewportState_.SetAxesVisible(showAxes);
    if (toolbarWidth >= 300.0F) ImGui::SameLine();
    const char* backgroundNames[] = {"Dark", "Neutral", "Light"};
    int selectedBackground = static_cast<int>(viewportState_.Background());
    ImGui::SetNextItemWidth(90.0F);
    if (ImGui::Combo(
            "##ViewportBackground", &selectedBackground,
            backgroundNames, 3))
    {
        viewportState_.SetBackground(
            static_cast<ViewportBackground>(selectedBackground));
    }

    bool eraseRequested = false;
    const bool canErase = hasModel && voxelSelection_.Selected().has_value();
    ImGui::BeginDisabled(!canErase);
    if (ImGui::Button("Erase Selected"))
        eraseRequested = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Delete");
    if (voxelModelModified_)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Unsaved changes");
    }

    if (hasModel)
    {
        ImGui::TextDisabled(
            "%u x %u x %u | %zu voxels | %zu faces | %zu triangles",
            statistics.Width, statistics.Height, statistics.Depth,
            statistics.OccupiedVoxelCount, statistics.TriangleCount / 2U,
            statistics.TriangleCount);
        const auto drawHit = [](const char* label,
            const std::optional<VoxelRaycastHit>& hit)
        {
            if (!hit)
            {
                ImGui::TextDisabled("%s: None", label);
                return;
            }
            ImGui::Text("%s: %u, %u, %u", label,
                hit->Coordinates.X, hit->Coordinates.Y, hit->Coordinates.Z);
        };
        drawHit("Hovered", voxelSelection_.Hovered());
        ImGui::SameLine();
        drawHit("Selected", voxelSelection_.Selected());
        const auto& detail = voxelSelection_.Hovered()
            ? voxelSelection_.Hovered() : voxelSelection_.Selected();
        if (detail)
        {
            ImGui::TextDisabled("Face: %s | Color index: %u",
                VoxelHitFaceName(detail->Face), detail->ColorIndex);
        }
    }
    else
    {
        ImGui::TextDisabled(
            "Right-click a .vox file and choose Open in Viewport.");
    }
    ImGui::TextDisabled(
        "Right: orbit | Middle: pan | Wheel: zoom | F/double-click: frame | Home: reset");

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0F);
    available.y = std::max(available.y, 1.0F);
    viewportCamera_.SetAspectRatio(available.x / available.y);
    const auto width = static_cast<std::uint32_t>(available.x);
    const auto height = static_cast<std::uint32_t>(available.y);
    if (viewportRenderer_.Render(
            width, height, viewportCamera_,
            viewportState_.IsGridVisible(),
            viewportState_.AreAxesVisible(),
            viewportState_.BackgroundColor()))
    {
        voxelViewportRendered_ = true;
        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        ImGui::Image(
            reinterpret_cast<ImTextureID>(viewportRenderer_.Texture()),
            available,
            ImVec2(0.0F, 0.0F),
            ImVec2(1.0F, 1.0F));
        ImGui::GetWindowDrawList()->AddRect(
            imageOrigin,
            ImVec2(imageOrigin.x + available.x, imageOrigin.y + available.y),
            IM_COL32(55, 64, 78, 255));

        const bool imageHovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        const bool sceneFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const bool selectionInputAvailable = imageHovered && sceneFocused &&
            !ImGui::IsAnyItemActive() && !io.WantTextInput;
        const bool cameraControl =
            ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (selectionInputAvailable && hasModel && viewportGrid != nullptr &&
            !cameraControl)
        {
            const float normalizedX =
                2.0F * (io.MousePos.x - imageOrigin.x) / available.x - 1.0F;
            const float normalizedY =
                1.0F - 2.0F * (io.MousePos.y - imageOrigin.y) / available.y;
            const VoxelRay ray = ViewportToVoxelGrid(
                viewportCamera_.CreateViewportRay(normalizedX, normalizedY),
                voxelModelCenter_);
            if (voxelSelection_.SetHovered(
                    RaycastVoxelGrid(*viewportGrid, ray)))
                UpdateVoxelHighlights();
        }
        else if (voxelSelection_.SetHovered(std::nullopt))
        {
            UpdateVoxelHighlights();
        }
        if (selectionInputAvailable &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            voxelSelectionClickCandidate_ = !cameraControl &&
                !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (voxelSelectionClickCandidate_ &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || !sceneFocused ||
             io.WantTextInput))
            voxelSelectionClickCandidate_ = false;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (voxelSelectionClickCandidate_ &&
                voxelSelection_.SelectHovered())
                UpdateVoxelHighlights();
            voxelSelectionClickCandidate_ = false;
        }
        viewportCamera_.Update(imageHovered, available.y);
        const bool sceneActive = imageHovered || sceneFocused;
        const bool incompatiblePopupOpen = ImGui::IsPopupOpen(
            nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool shortcutsEnabled = sceneActive &&
            !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
            !incompatiblePopupOpen;
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_F, false))
            FrameVoxelViewport();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Home, false))
            viewportCamera_.Reset();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            eraseRequested = true;
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            voxelSelection_.ClearSelection())
            UpdateVoxelHighlights();
        if (imageHovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FrameVoxelViewport();
    }
    else if (!viewportRenderer_.LastError().empty())
    {
        voxelViewportRenderFailed_ = true;
        DrawErrorMessage(viewportRenderer_.LastError());
    }
    if (eraseRequested) static_cast<void>(EraseSelectedVoxel());
    ImGui::End();
}

void EditorWorkspace::DrawWelcomeScreen()
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float contentWidth = std::min(720.0F, std::max(1.0F, available.x));
    const float horizontalOffset = std::max(
        0.0F,
        (available.x - contentWidth) * 0.5F);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizontalOffset);

    ImGui::BeginChild(
        "##VoxelForgeWelcome",
        ImVec2(contentWidth, available.y),
        false);
    ImGui::Spacing();

    const char* title = "VoxelForge Studio";
    const float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (contentWidth - titleWidth) * 0.5F));
    ImGui::TextUnformatted(title);

    const char* motto = "Cr\303\251er plus vite. Rester l'artisan.";
    const float mottoWidth = ImGui::CalcTextSize(motto).x;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (contentWidth - mottoWidth) * 0.5F));
    ImGui::TextDisabled("%s", motto);
    ImGui::Spacing();
    ImGui::Spacing();

    constexpr float ActionWidth = 180.0F;
    constexpr float ActionSpacing = 12.0F;
    const float actionsWidth = ActionWidth * 2.0F + ActionSpacing;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (contentWidth - actionsWidth) * 0.5F));

    if (ImGui::Button("New Project", ImVec2(ActionWidth, 42.0F)))
    {
        RequestNewProjectDialog();
    }

    ImGui::SameLine(0.0F, ActionSpacing);

    if (ImGui::Button("Open Project", ImVec2(ActionWidth, 42.0F)))
    {
        RequestOpenProjectDialog();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Recent Projects");
    ImGui::Spacing();

    std::optional<std::filesystem::path> recentProjectToOpen;
    std::optional<std::filesystem::path> recentProjectToRemove;
    bool displayedRecentProject = false;

    for (const std::filesystem::path& projectPath :
         projectManager_.RecentProjectPaths())
    {
        if (!IsVisibleRecentProject(projectPath))
        {
            continue;
        }

        displayedRecentProject = true;
        const std::string pathText = projectPath.string();
        const std::string projectName = projectPath.stem().string();
        ImGui::PushID(pathText.c_str());

        if (ImGui::Button(projectName.c_str(), ImVec2(220.0F, 0.0F)))
        {
            recentProjectToOpen = projectPath;
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Remove from list"))
        {
            recentProjectToRemove = projectPath;
        }

        ImGui::TextDisabled("%s", pathText.c_str());
        ImGui::Spacing();
        ImGui::PopID();
    }

    if (!displayedRecentProject)
    {
        ImGui::TextDisabled("No recent projects yet.");
    }

    if (!welcomeError_.empty())
    {
        ImGui::Spacing();
        DrawErrorMessage(welcomeError_);
    }

    if (failedRecentProjectPath_ &&
        ImGui::Button("Remove unavailable project from list"))
    {
        recentProjectToRemove = *failedRecentProjectPath_;
    }

    if (recentProjectToRemove)
    {
        RemoveRecentProject(*recentProjectToRemove);
    }
    else if (recentProjectToOpen)
    {
        static_cast<void>(OpenProject(*recentProjectToOpen, true));
    }

    ImGui::EndChild();
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
    const auto& activeProject = projectManager_.ActiveProject();

    if (activeProject)
    {
        static_cast<void>(assetBrowser_.SetAssetsRoot(
            activeProject->RootPath() / "Assets"));
    }
    else
    {
        assetBrowser_.ClearAssetsRoot();
    }

    assetBrowser_.Draw(&showAssetBrowser_);
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
        const auto& activeProject = projectManager_.ActiveProject();
        const std::string projectStatus = activeProject
            ? "Project: " + activeProject->Name()
            : "No project loaded";
        const std::string modelStatus = viewportState_.HasModel()
            ? " | Model: " + viewportState_.Name() +
                (voxelModelModified_ ? " *" : "")
            : "";

        ImGui::Text(
            "Ready | %s%s | FPS %.1f | %.2f ms | Backend: %s | ImGui %s",
            projectStatus.c_str(),
            modelStatus.c_str(),
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

void EditorWorkspace::DrawProjectDialogs()
{
    DrawNewProjectDialog();
    DrawOpenProjectDialog();
}

void EditorWorkspace::DrawNewProjectDialog()
{
    constexpr const char* PopupName = "New VoxelForge Project";

    if (showNewProjectPopup_)
    {
        ImGui::OpenPopup(PopupName);
        showNewProjectPopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            PopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    bool fieldsChanged = ImGui::InputText(
        "Project Name",
        newProjectName_.data(),
        newProjectName_.size());
    fieldsChanged = DrawPathInput(
        "Parent Folder",
        newProjectParentPath_) || fieldsChanged;

    if (fieldsChanged)
    {
        projectDialogError_.clear();
    }

    const std::string projectName(newProjectName_.data());
    const std::filesystem::path parentDirectory(
        newProjectParentPath_.data());
    const Project::ProjectCreationValidation validation =
        projectManager_.ValidateProjectCreation(
            projectName,
            parentDirectory);

    ImGui::TextUnformatted("Final Path");

    if (projectName.empty() || parentDirectory.empty())
    {
        ImGui::TextDisabled("Enter a project name and parent folder.");
    }
    else
    {
        ImGui::TextWrapped(
            "%s",
            validation.DestinationPath.string().c_str());
    }

    const std::string_view displayedError = projectDialogError_.empty()
        ? std::string_view(validation.Error)
        : std::string_view(projectDialogError_);
    DrawErrorMessage(displayedError);

    ImGui::BeginDisabled(!validation.IsValid());

    if (ImGui::Button("Create"))
    {
        CreateProject();
    }

    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        projectDialogError_.clear();
        newProjectName_.fill('\0');
        newProjectParentPath_.fill('\0');
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorWorkspace::DrawOpenProjectDialog()
{
    constexpr const char* PopupName = "Open VoxelForge Project";

    if (showOpenProjectPopup_)
    {
        ImGui::OpenPopup(PopupName);
        showOpenProjectPopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            PopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (DrawPathInput("Project File", openProjectFilePath_))
    {
        projectDialogError_.clear();
    }

    ImGui::TextDisabled("Expected extension: .vfproject");

    const std::filesystem::path projectFilePath(
        openProjectFilePath_.data());
    std::string inputError;

    if (projectFilePath.empty())
    {
        inputError = "Project file cannot be empty.";
    }
    else if (!HasProjectExtension(projectFilePath))
    {
        inputError = "Project file must use the .vfproject extension.";
    }

    const std::string_view displayedError = projectDialogError_.empty()
        ? std::string_view(inputError)
        : std::string_view(projectDialogError_);
    DrawErrorMessage(displayedError);

    ImGui::BeginDisabled(!inputError.empty());

    if (ImGui::Button("Open"))
    {
        if (OpenProject(projectFilePath, false))
        {
            openProjectFilePath_.fill('\0');
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        projectDialogError_.clear();
        openProjectFilePath_.fill('\0');
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool EditorWorkspace::DrawPathInput(
    const char* label,
    std::array<char, 1024>& buffer)
{
    // A future lifetime-safe Window file-dialog service can be connected here
    // without duplicating project creation or opening logic in the UI.
    return ImGui::InputText(label, buffer.data(), buffer.size());
}

void EditorWorkspace::RequestNewProjectDialog()
{
    projectDialogError_.clear();
    showNewProjectPopup_ = true;
}

void EditorWorkspace::RequestOpenProjectDialog()
{
    projectDialogError_.clear();
    showOpenProjectPopup_ = true;
}

void EditorWorkspace::CreateProject()
{
    const auto project = projectManager_.CreateProject(
        newProjectName_.data(),
        std::filesystem::path(newProjectParentPath_.data()));

    if (!project)
    {
        projectDialogError_ = projectManager_.LastError();
        AddConsoleMessage(
            "Project creation failed: " + projectDialogError_);
        return;
    }

    ClearVoxelViewport();
    AddConsoleMessage("Project created: " + project->Name());
    projectDialogError_.clear();
    welcomeError_.clear();
    failedRecentProjectPath_.reset();
    newProjectName_.fill('\0');
    newProjectParentPath_.fill('\0');
    UpdateWindowTitle();
    ImGui::CloseCurrentPopup();
}

bool EditorWorkspace::OpenProject(
    const std::filesystem::path& projectFilePath,
    const bool recentProject)
{
    const auto project = projectManager_.OpenProject(projectFilePath);

    if (!project)
    {
        projectDialogError_ = projectManager_.LastError();
        AddConsoleMessage("Project open failed: " + projectDialogError_);

        if (recentProject)
        {
            welcomeError_ =
                "Unable to open recent project: " + projectDialogError_;
            failedRecentProjectPath_ = projectFilePath;
        }

        return false;
    }

    projectDialogError_.clear();
    welcomeError_.clear();
    failedRecentProjectPath_.reset();
    ClearVoxelViewport();
    AddConsoleMessage("Project opened: " + project->Name());
    UpdateWindowTitle();
    return true;
}

void EditorWorkspace::RemoveRecentProject(
    const std::filesystem::path& projectFilePath)
{
    if (!projectManager_.RemoveRecentProject(projectFilePath))
    {
        welcomeError_ =
            "Unable to remove recent project: " +
            projectManager_.LastError();
        AddConsoleMessage(welcomeError_);
        return;
    }

    welcomeError_.clear();

    if (failedRecentProjectPath_ &&
        *failedRecentProjectPath_ == projectFilePath)
    {
        failedRecentProjectPath_.reset();
    }

    AddConsoleMessage(
        "Removed from recent projects: " + projectFilePath.string());
}

void EditorWorkspace::SaveProject()
{
    if (!projectManager_.SaveActiveProject())
    {
        AddConsoleMessage(
            "Project save failed: " + projectManager_.LastError());
        return;
    }

    AddConsoleMessage(
        "Project saved: " + projectManager_.ActiveProject()->Name());
}

void EditorWorkspace::CloseProject()
{
    const auto& activeProject = projectManager_.ActiveProject();

    if (!activeProject)
    {
        return;
    }

    const std::string projectName = activeProject->Name();
    ClearVoxelViewport();
    projectManager_.CloseProject();
    AddConsoleMessage("Project closed: " + projectName);
    welcomeError_.clear();
    failedRecentProjectPath_.reset();
    UpdateWindowTitle();
}

bool EditorWorkspace::OpenVoxInViewport(
    const std::filesystem::path& filePath)
{
    Asset::Vox::VoxImporter importer;
    const auto imported = importer.Inspect(filePath);
    if (!imported.Result.Succeeded || !imported.Asset)
    {
        AddConsoleMessage("VOX viewport import failed: " + imported.Result.Message);
        return false;
    }

    auto converted = Voxel::VoxModelConverter::Convert(
        *imported.Asset, filePath.stem().string());
    if (!converted.Succeeded || !converted.Model)
    {
        AddConsoleMessage("VOX viewport conversion failed: " + converted.Message);
        return false;
    }

    const Voxel::VoxelGrid* grid = converted.Model->GetGrid(0U);
    if (grid == nullptr)
    {
        AddConsoleMessage("VOX viewport load failed: no grid is available.");
        return false;
    }
    Mesh::MeshBuildResult built = Mesh::VoxelMeshBuilder::Build(*grid);
    if (!built.Succeeded || !built.Mesh)
    {
        AddConsoleMessage("VOX viewport mesh failed: " + built.Message);
        return false;
    }
    const Vec3 modelCenter = CalculateVoxelGridCenter(*grid);
    if (!viewportRenderer_.Upload(
            *built.Mesh, converted.Model->Palette(), modelCenter))
    {
        const std::string error = viewportRenderer_.LastError();
        AddConsoleMessage("VOX viewport GPU upload failed: " + error);
        return false;
    }
    commandHistory_.Clear();
    activeVoxelModel_ = std::move(*converted.Model);
    ++voxelModelGeneration_;
    if (!viewportState_.Replace(
            filePath.filename().string(), *activeVoxelModel_, *built.Mesh))
    {
        ClearVoxelViewport();
        AddConsoleMessage("VOX viewport load failed: no first grid is available.");
        return false;
    }
    voxelModelCenter_ = modelCenter;
    voxelModelModified_ = false;
    static_cast<void>(voxelSelection_.Clear());
    UpdateVoxelHighlights();
    voxelViewportRendered_ = false;
    voxelViewportRenderFailed_ = false;
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    viewportRenderer_.ConfigureGuides(
        static_cast<float>(statistics.Width),
        static_cast<float>(statistics.Height),
        static_cast<float>(statistics.Depth));
    viewportCamera_.Frame(
        static_cast<float>(statistics.Width),
        static_cast<float>(statistics.Height),
        static_cast<float>(statistics.Depth));
    AddConsoleMessage("Opened in viewport: " + filePath.filename().string());
    if (converted.Model->GridCount() > 1U)
    {
        AddConsoleMessage("Viewport v1 displays only the first VOX grid.");
    }
    return true;
}

bool EditorWorkspace::HasRenderedVoxelViewport() const noexcept
{
    return voxelViewportRendered_;
}

bool EditorWorkspace::HasVoxelViewportRenderError() const noexcept
{
    return voxelViewportRenderFailed_;
}

void EditorWorkspace::SetVoxelViewportView(
    const EditorCameraView view) noexcept
{
    viewportCamera_.SetView(view);
}

bool EditorWorkspace::RunVoxelSelectionSmokeStep(const std::size_t frame)
{
    const Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    if (grid == nullptr) return false;
    if (frame == 0U)
    {
        const auto hit = RaycastVoxelGrid(
            *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit) return false;
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        UpdateVoxelHighlights();
    }
    else if (frame == 20U)
    {
        static_cast<void>(voxelSelection_.Clear());
        UpdateVoxelHighlights();
    }
    return true;
}

bool EditorWorkspace::RunEraseVoxelSmokeStep(const std::size_t frame)
{
    const Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    if (grid == nullptr) return false;

    if (frame == 0U)
    {
        eraseSmokeInitialVoxelCount_ = grid->OccupiedVoxelCount();
        const auto hit = RaycastVoxelGrid(
            *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        eraseSmokeSelected_ = hit.has_value();
        if (!hit) return false;
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        UpdateVoxelHighlights();
    }
    else if (frame == 1U)
    {
        eraseSmokeEraseRenderBaseline_ = viewportRenderer_.ModelRenderCount();
        eraseSmokeExecuted_ = EraseSelectedVoxel() &&
            grid->OccupiedVoxelCount() + 1U == eraseSmokeInitialVoxelCount_ &&
            !voxelSelection_.Selected() && voxelModelModified_;
    }
    else if (frame == 10U)
    {
        const bool erasedFramesRendered =
            viewportRenderer_.ModelRenderCount() > eraseSmokeEraseRenderBaseline_;
        UndoCommand();
        eraseSmokeUndone_ = erasedFramesRendered &&
            grid->OccupiedVoxelCount() == eraseSmokeInitialVoxelCount_;
        eraseSmokeUndoRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    else if (frame == 20U)
    {
        const bool undoFramesRendered =
            viewportRenderer_.ModelRenderCount() > eraseSmokeUndoRenderBaseline_;
        RedoCommand();
        eraseSmokeRedone_ = undoFramesRendered &&
            grid->OccupiedVoxelCount() + 1U == eraseSmokeInitialVoxelCount_;
        eraseSmokeRedoRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    return true;
}

bool EditorWorkspace::EraseVoxelSmokePassed() const noexcept
{
    return eraseSmokeSelected_ && eraseSmokeExecuted_ &&
        eraseSmokeUndone_ && eraseSmokeRedone_ &&
        viewportRenderer_.ModelRenderCount() > eraseSmokeRedoRenderBaseline_ &&
        viewportState_.Statistics().OccupiedVoxelCount + 1U ==
            eraseSmokeInitialVoxelCount_;
}

bool EditorWorkspace::EraseSelectedVoxel()
{
    if (!activeVoxelModel_)
    {
        AddConsoleMessage("Erase failed: no voxel model is loaded.");
        return false;
    }
    const std::optional<VoxelRaycastHit> selected = voxelSelection_.Selected();
    if (!selected)
    {
        AddConsoleMessage("Erase failed: no voxel is selected.");
        return false;
    }

    const VoxelCoordinates coordinates = selected->Coordinates;
    CommandResult result = commandHistory_.Execute(
        std::make_unique<EraseVoxelCommand>(
            static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
            coordinates.X, coordinates.Y, coordinates.Z));
    if (!result)
    {
        AddConsoleMessage("Erase failed: " + result.Message);
        return false;
    }

    AddConsoleMessage(
        "Erased voxel: " + std::to_string(coordinates.X) + ", " +
        std::to_string(coordinates.Y) + ", " +
        std::to_string(coordinates.Z));
    return true;
}

std::uint64_t EditorWorkspace::VoxelModelGeneration() const noexcept
{
    return voxelModelGeneration_;
}

Voxel::VoxelModel* EditorWorkspace::ActiveVoxelModel() noexcept
{
    return activeVoxelModel_ ? &*activeVoxelModel_ : nullptr;
}

CommandResult EditorWorkspace::RebuildActiveVoxelMesh()
{
    if (!activeVoxelModel_)
        return CommandResult::Failure("No active voxel model is available.");
    Voxel::VoxelGrid* grid = activeVoxelModel_->GetGrid(0U);
    if (grid == nullptr)
        return CommandResult::Failure("The active voxel model has no grid.");

    Mesh::MeshBuildResult built = Mesh::VoxelMeshBuilder::Build(*grid);
    if (!built.Succeeded || !built.Mesh)
        return CommandResult::Failure(
            "Voxel mesh rebuild failed: " + built.Message);

    const Vec3 modelCenter = CalculateVoxelGridCenter(*grid);
    if (!viewportRenderer_.Upload(
            *built.Mesh, activeVoxelModel_->Palette(), modelCenter))
        return CommandResult::Failure(
            "Voxel mesh GPU upload failed: " + viewportRenderer_.LastError());

    static_cast<void>(
        viewportState_.UpdateStatistics(*activeVoxelModel_, *built.Mesh));
    voxelModelCenter_ = modelCenter;
    voxelViewportRendered_ = false;
    voxelViewportRenderFailed_ = false;
    return CommandResult::Success();
}

void EditorWorkspace::CompleteVoxelEdit() noexcept
{
    static_cast<void>(voxelSelection_.Clear());
    UpdateVoxelHighlights();
    voxelModelModified_ = true;
}

std::size_t EditorWorkspace::VoxelHighlightUploadCount() const noexcept
{
    return viewportRenderer_.HighlightUploadCount();
}

std::size_t EditorWorkspace::VoxelHighlightRenderCount() const noexcept
{
    return viewportRenderer_.HighlightRenderCount();
}

void EditorWorkspace::UpdateVoxelHighlights() noexcept
{
    const auto coordinates = [](const std::optional<VoxelRaycastHit>& hit)
        -> std::optional<VoxelCoordinates>
    {
        return hit ? std::optional<VoxelCoordinates>(hit->Coordinates)
                   : std::nullopt;
    };
    viewportRenderer_.ConfigureHighlights(
        coordinates(voxelSelection_.Hovered()),
        coordinates(voxelSelection_.Selected()), voxelModelCenter_);
}

void EditorWorkspace::ClearVoxelViewport() noexcept
{
    commandHistory_.Clear();
    ++voxelModelGeneration_;
    viewportRenderer_.ClearModel();
    viewportRenderer_.ConfigureGuides(0.0F, 0.0F, 0.0F);
    viewportState_.Clear();
    activeVoxelModel_.reset();
    static_cast<void>(voxelSelection_.Clear());
    voxelModelCenter_ = {};
    voxelViewportRendered_ = false;
    voxelViewportRenderFailed_ = false;
    voxelModelModified_ = false;
    eraseSmokeSelected_ = false;
    eraseSmokeExecuted_ = false;
    eraseSmokeUndone_ = false;
    eraseSmokeRedone_ = false;
    eraseSmokeInitialVoxelCount_ = 0U;
    eraseSmokeEraseRenderBaseline_ = 0U;
    eraseSmokeUndoRenderBaseline_ = 0U;
    eraseSmokeRedoRenderBaseline_ = 0U;
}

void EditorWorkspace::FrameVoxelViewport() noexcept
{
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    viewportCamera_.Frame(
        viewportState_.HasModel() ? static_cast<float>(statistics.Width) : 1.0F,
        viewportState_.HasModel() ? static_cast<float>(statistics.Height) : 1.0F,
        viewportState_.HasModel() ? static_cast<float>(statistics.Depth) : 1.0F);
}

void EditorWorkspace::UpdateWindowTitle()
{
    if (!windowTitleCallback_)
    {
        return;
    }

    const auto& activeProject = projectManager_.ActiveProject();
    const std::string title = FormatEditorWindowTitle(
        activeProject ? std::string_view(activeProject->Name())
                      : std::string_view{});

    if (!windowTitleCallback_(title))
    {
        AddConsoleMessage("Unable to update the editor window title.");
    }
}

void EditorWorkspace::AddConsoleMessage(std::string message)
{
    if (consoleMessages_.size() >= MaximumConsoleMessageCount)
    {
        consoleMessages_.erase(consoleMessages_.begin());
    }

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
