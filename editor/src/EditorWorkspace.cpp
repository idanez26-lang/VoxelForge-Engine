#include "EditorWorkspace.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "EditorWindowTitle.h"

#include "VoxelForge/Project/Project.h"
#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxModelConverter.h"
#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
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
constexpr const char* DirtyConfirmationPopupName = "Unsaved Voxel Model";
constexpr const char* ImportConfirmationPopupName = "Import Models";
constexpr const char* ImportCollisionPopupName = "Model Already Exists";
constexpr const char* OpenImportedModelPopupName = "Open Imported Model";

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

std::string LowercaseExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension;
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

void DrawTooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

bool CopyPathToBuffer(
    const std::filesystem::path& path,
    std::array<char, 1024>& buffer)
{
    const std::string value = path.string();
    if (value.size() >= buffer.size()) return false;
    buffer.fill('\0');
    std::copy(value.begin(), value.end(), buffer.begin());
    return true;
}

Voxel::VoxelPalette BuildDocumentRenderPalette(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelPalette palette;
    const auto& source = document.GetPalette();
    for (std::size_t index = 0U; index < source.size(); ++index)
    {
        static_cast<void>(palette.Set(index, {
            source[index].Red,
            source[index].Green,
            source[index].Blue,
            source[index].Alpha}));
    }
    return palette;
}

Vec3 CalculateVoxelDocumentCenter(
    const Asset::Voxel::VoxelDocument& document)
{
    const auto dimensions = document.GetDimensions();
    return dimensions
        ? Vec3{
            static_cast<float>(dimensions->X) * 0.5F,
            static_cast<float>(dimensions->Y) * 0.5F,
            static_cast<float>(dimensions->Z) * 0.5F}
        : Vec3{};
}

std::optional<std::uint64_t> HashFileContents(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    constexpr std::uint64_t OffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t Prime = 1099511628211ULL;
    std::uint64_t hash = OffsetBasis;
    std::array<char, 4096U> buffer{};
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           input.gcount() > 0)
    {
        for (std::streamsize index = 0; index < input.gcount(); ++index)
        {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]);
            hash *= Prime;
        }
    }
    return input.eof() ? std::optional<std::uint64_t>(hash) : std::nullopt;
}
}

EditorWorkspace::EditorWorkspace(
    Project::ProjectManager& projectManager,
    WindowTitleCallback windowTitleCallback,
    std::filesystem::path preferencesFilePath,
    const bool simulatedFileDialogs)
    : projectManager_(projectManager),
      windowTitleCallback_(std::move(windowTitleCallback)),
      fileDialogService_(simulatedFileDialogs
          ? CreateSimulatedFileDialogService()
          : CreateSDLFileDialogService()),
      projectFolderOpener_(CreateSDLProjectFolderOpener()),
      projectDialogPreferences_(std::move(preferencesFilePath)),
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
    modelImportService_.SetRefreshCallback(
        [this]()
        {
            static_cast<void>(assetBrowser_.Refresh());
        });
    SynchronizeProjectAssets();
    if (!projectDialogPreferences_.Load())
    {
        AddConsoleMessage(
            "Preferences load warning: " + projectDialogPreferences_.LastError());
    }
}

EditorWorkspace::~EditorWorkspace()
{
    viewportRenderer_.Shutdown();
}

void EditorWorkspace::BeginFileDrop(const float x, const float y) noexcept
{
    dragDropImport_.Begin(x, y);
    dragDropImport_.UpdatePosition(
        x, y, assetBrowserDropRect_, viewportDropRect_);
}

void EditorWorkspace::UpdateFileDropPosition(
    const float x,
    const float y) noexcept
{
    dragDropImport_.UpdatePosition(
        x, y, assetBrowserDropRect_, viewportDropRect_);
}

void EditorWorkspace::AddDroppedFile(
    const std::filesystem::path& path,
    const float x,
    const float y)
{
    dragDropImport_.AddFile(
        path, x, y, assetBrowserDropRect_, viewportDropRect_);
}

void EditorWorkspace::CompleteFileDrop(const float x, const float y)
{
    dragDropImport_.UpdatePosition(
        x, y, assetBrowserDropRect_, viewportDropRect_);
    DragDropImportRequest request = dragDropImport_.Complete(
        projectManager_.HasActiveProject());
    if (request.Target == DragDropImportTarget::None)
        return;
    if (!request.Accepted)
    {
        if (!request.Message.empty()) AddConsoleMessage(request.Message);
        return;
    }
    pendingDropImportTarget_ = request.Target;
    importStartedFromDrop_ = true;
    selectedImportPaths_ = std::move(request.Paths);
    AddConsoleMessage("[Import] Dropped " +
        std::to_string(selectedImportPaths_.size()) +
        (selectedImportPaths_.size() == 1U
            ? " VOX model." : " VOX models."));
    showImportConfirmationPopup_ = true;
}

void EditorWorkspace::Draw()
{
    ConsumeFileDialogResult();
    if (!SynchronizeVoxelDocumentRendering())
        voxelViewportRenderFailed_ = true;
    HandleCommandShortcuts();
    DrawMainMenuBar();

    const ImGuiID dockspaceId = ImGui::GetID(WorkspaceDockspaceName);
    const bool layoutMissing =
        ImGui::DockBuilderGetNode(dockspaceId) == nullptr;

    DrawDockSpace(dockspaceId);

    if (layoutMissing || resetLayoutRequested_)
    {
        if (thumbnailVisualLayoutRequested_)
            BuildThumbnailVisualLayout(dockspaceId);
        else
            BuildDefaultLayout(dockspaceId);
        resetLayoutRequested_ = false;
        thumbnailVisualLayoutRequested_ = false;
    }

    if (!showScene_) viewportDropRect_ = {};
    if (!showAssetBrowser_) assetBrowserDropRect_ = {};

    if (showExplorer_) DrawExplorerPanel();
    if (showScene_) DrawScenePanel();
    if (showInspector_ && !thumbnailVisualMode_) DrawInspectorPanel();
    if (showAssetBrowser_)
    {
        if (thumbnailVisualMode_)
        {
            ImGui::SetNextWindowDockID(0U, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(8.0F, 72.0F), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(390.0F, 330.0F), ImGuiCond_Always);
        }
        DrawAssetBrowserPanel();
    }
    if (showInspector_ && thumbnailVisualMode_)
    {
        ImGui::SetNextWindowDockID(0U, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(400.0F, 72.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(325.0F, 330.0F), ImGuiCond_Always);
        DrawInspectorPanel();
    }
    if (showConsole_) DrawConsolePanel();
    if (showProfiler_) DrawProfilerPanel();

    if (showImGuiDemo_)
    {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }

    DrawStatusBar();
    DrawAboutPopup();
    DrawProjectDialogs();
    DrawModelImportDialogs();
    DrawDirtyConfirmationDialog();
}

bool EditorWorkspace::ConsumeExitRequest() noexcept
{
    return exitRequest_.ConsumeExitRequest();
}

bool EditorWorkspace::RequestApplicationExit()
{
    RequestExit();
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
        DrawTooltip("Create a project (Ctrl+N)");

        if (ImGui::MenuItem("Open Project", "Ctrl+O"))
        {
            RequestOpenProjectDialog();
        }
        DrawTooltip("Open a project (Ctrl+O)");

        const bool hasActiveProject = projectManager_.HasActiveProject();
        if (ImGui::MenuItem(
                "Import Model...", "Ctrl+I", false, hasActiveProject))
        {
            RequestImportModelDialog();
        }
        DrawTooltip("Import one or more .vox models into Assets/Models");

        std::optional<std::filesystem::path> recentImportToOpen;
        if (ImGui::BeginMenu("Recent Imports"))
        {
            const auto& recentImports = modelImportService_.RecentImports();
            if (recentImports.empty())
            {
                ImGui::MenuItem("No recent imports", nullptr, false, false);
            }
            for (const std::filesystem::path& path : recentImports)
            {
                const std::string label = path.filename().string();
                ImGui::PushID(path.string().c_str());
                if (ImGui::MenuItem(label.c_str())) recentImportToOpen = path;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.string().c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (recentImportToOpen)
            static_cast<void>(OpenVoxInViewport(*recentImportToOpen));

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
            RequestOpenProject(*recentProjectToOpen, true);
        }

        ImGui::Separator();
        if (ImGui::MenuItem(
                "Save Project",
                nullptr,
                false,
                hasActiveProject))
        {
            SaveProject();
        }
        DrawTooltip("Save the active project metadata");

        const bool hasActiveVoxelModel = activeVoxelModel_.has_value();
        if (ImGui::MenuItem(
                "Save Voxel Model",
                "Ctrl+S",
                false,
                hasActiveVoxelModel))
        {
            static_cast<void>(SaveVoxelModel());
        }
        DrawTooltip(
            "Save the active model as .vfvoxel (Ctrl+S when modified)");

        ImGui::BeginDisabled();
        ImGui::MenuItem("Save Project As...", "Ctrl+Shift+S");
        ImGui::EndDisabled();
        DrawTooltip("Not implemented yet");

        if (ImGui::MenuItem(
                "Open Project Folder", nullptr, false, hasActiveProject))
        {
            OpenProjectFolder();
        }
        DrawTooltip("Open the active project folder");

        if (ImGui::MenuItem(
                "Close Project",
                "Ctrl+W",
                false,
                hasActiveProject))
        {
            RequestCloseProject();
        }
        DrawTooltip("Close the active project (Ctrl+W)");

        ImGui::Separator();

        if (ImGui::MenuItem("Exit"))
        {
            RequestExit();
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
        DrawTooltip("Undo the last edit (Ctrl+Z)");

        const std::string redoLabel = commandHistory_.CanRedo()
            ? "Redo " + std::string(commandHistory_.RedoName())
            : "Redo";
        if (ImGui::MenuItem(
                redoLabel.c_str(), "Ctrl+Y / Ctrl+Shift+Z", false,
                commandHistory_.CanRedo()))
        {
            RedoCommand();
        }
        DrawTooltip("Redo the last edit (Ctrl+Y)");

        ImGui::Separator();

        if (ImGui::MenuItem("Preferences"))
        {
            AddConsoleMessage("Preferences are not available yet.");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Assets"))
    {
        const bool hasActiveProject = projectManager_.HasActiveProject();
        if (ImGui::MenuItem(
                "Rebuild Model Metadata", nullptr, false, hasActiveProject))
        {
            const MetadataRebuildReport report =
                modelImportService_.RebuildMetadata();
            AddConsoleMessage(
                "Model metadata rebuild: " +
                std::to_string(report.Created) + " created, " +
                std::to_string(report.Unchanged) + " unchanged, " +
                std::to_string(report.Repaired) + " repaired, " +
                std::to_string(report.Ignored) + " ignored, " +
                std::to_string(report.AnalysesCreated) + " analyzed, " +
                std::to_string(report.AnalysesUpdated) + " updated, " +
                std::to_string(report.AnalysesUnchanged) + " cached, " +
                std::to_string(report.Errors) + " errors.");
            for (const std::string& error : report.ErrorMessages)
                AddConsoleMessage("Model metadata error: " + error);
        }
        if (ImGui::MenuItem(
                "Rebuild VOX Thumbnails", nullptr, false, hasActiveProject))
        {
            const ThumbnailRebuildReport report =
                modelImportService_.RebuildThumbnails();
            AddConsoleMessage(
                "[Assets] Thumbnail rebuild completed: " +
                std::to_string(report.Generated) + " generated, " +
                std::to_string(report.Unchanged) + " unchanged, " +
                std::to_string(report.Failed) + " failed, " +
                std::to_string(report.Skipped) + " skipped, " +
                std::to_string(report.RemovedOrphans) + " orphans removed.");
            for (const std::string& error : report.Errors)
                AddConsoleMessage("Thumbnail rebuild error: " + error);
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
    const ShortcutContext context{
        io.WantTextInput,
        ImGui::IsAnyItemActive(),
        incompatiblePopupOpen,
        projectManager_.HasActiveProject()};
    if (context.TextInput || context.ActiveItem || context.PopupOpen)
    {
        return;
    }

    constexpr ImGuiInputFlags shortcutFlags = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, shortcutFlags) &&
        CanRunProjectShortcut(ProjectShortcut::NewProject, context))
    {
        RequestNewProjectDialog();
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, shortcutFlags) &&
        CanRunProjectShortcut(ProjectShortcut::OpenProject, context))
    {
        RequestOpenProjectDialog();
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_I, shortcutFlags) &&
             context.HasProject)
    {
        RequestImportModelDialog();
    }
    else if (ImGui::Shortcut(
                 ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                 shortcutFlags) &&
             CanRunProjectShortcut(ProjectShortcut::SaveAs, context))
    {
        AddConsoleMessage("Save As is not implemented yet.");
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, shortcutFlags))
    {
        if (activeVoxelModel_ && voxelSaveState_.IsDirty())
        {
            static_cast<void>(SaveVoxelModel());
        }
        else if (CanRunProjectShortcut(ProjectShortcut::SaveProject, context))
        {
            SaveProject();
        }
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, shortcutFlags) &&
             CanRunProjectShortcut(ProjectShortcut::CloseProject, context))
    {
        RequestCloseProject();
    }
    else if (ImGui::Shortcut(
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

void EditorWorkspace::BuildThumbnailVisualLayout(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - StatusBarHeight));
    ImGui::DockBuilderRemoveNode(dockspaceId);
    const ImGuiDockNodeFlags flags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockBuilderAddNode(dockspaceId, flags);
    ImGui::DockBuilderSetNodeSize(dockspaceId, workspaceSize);
    ImGuiID visibleId = dockspaceId;
    static_cast<void>(ImGui::DockBuilderSplitNode(
        visibleId, ImGuiDir_Right, 0.43F, nullptr, &visibleId));
    ImGuiID browserId = visibleId;
    const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(
        browserId, ImGuiDir_Right, 0.36F, nullptr, &browserId);
    ImGui::DockBuilderDockWindow("Asset Browser", browserId);
    ImGui::DockBuilderDockWindow("Inspector", inspectorId);
    ImGui::DockBuilderFinish(dockspaceId);
    showExplorer_ = false;
    showScene_ = false;
    showInspector_ = true;
    showAssetBrowser_ = true;
    showConsole_ = false;
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

    const ImVec2 scenePosition = ImGui::GetWindowPos();
    const ImVec2 sceneSize = ImGui::GetWindowSize();
    viewportDropRect_ = {
        scenePosition.x, scenePosition.y,
        scenePosition.x + sceneSize.x, scenePosition.y + sceneSize.y};

    if (!projectManager_.HasActiveProject())
    {
        DrawWelcomeScreen();
        DrawFileDropOverlay(
            viewportDropRect_, DragDropImportTarget::Viewport);
        ImGui::End();
        return;
    }

    const bool hasModel = viewportState_.HasModel();
    Voxel::VoxelGrid* viewportGrid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    const std::string modelLabel = hasModel
        ? viewportState_.Name() + (voxelSaveState_.IsDirty() ? " *" : "")
        : "No voxel model loaded.";
    ImGui::TextUnformatted(modelLabel.c_str());
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const bool narrowToolbar = toolbarWidth < 520.0F;
    if (ImGui::Button("Frame Model"))
    {
        FrameVoxelViewport();
    }
    DrawTooltip("Frame the active voxel model (F)");
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
    DrawTooltip("Show or hide the ground grid");
    ImGui::SameLine();
    bool showAxes = viewportState_.AreAxesVisible();
    if (ImGui::Checkbox("Axes", &showAxes))
        viewportState_.SetAxesVisible(showAxes);
    DrawTooltip("Show or hide the world axes");
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
    DrawTooltip("Choose the viewport background");

    const auto drawToolButton = [this](
        const char* inactiveLabel,
        const char* activeLabel,
        const char* tooltip,
        const ActiveVoxelTool tool,
        const ImVec4 activeColor)
    {
        const bool active = voxelToolState_.ActiveTool() == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        const bool clicked = ImGui::Button(active ? activeLabel : inactiveLabel);
        if (active) ImGui::PopStyleColor();
        DrawTooltip(tooltip);
        if (!clicked) return;
        voxelToolState_.SetActiveTool(active ? ActiveVoxelTool::None : tool);
        voxelToolInput_.Reset();
        UpdateVoxelHighlights();
    };
    drawToolButton(
        "Pencil", "Pencil [Active]", "Activate the Pencil tool (P)",
        ActiveVoxelTool::Pencil, ImVec4(0.20F, 0.48F, 0.28F, 1.0F));
    ImGui::SameLine();
    drawToolButton(
        "Eraser", "Eraser [Active]", "Activate the Eraser tool (E)",
        ActiveVoxelTool::Eraser, ImVec4(0.58F, 0.20F, 0.08F, 1.0F));

    bool eraseRequested = false;
    bool addRequested = false;
    const AddVoxelTarget addTarget = FindAddVoxelTarget(
        viewportGrid, voxelSelection_.Selected());
    ImGui::BeginDisabled(!addTarget);
    if (ImGui::Button("Add Adjacent"))
        addRequested = true;
    ImGui::EndDisabled();
    DrawTooltip("Add a voxel next to the selected face (A)");
    ImGui::SameLine();
    const bool canErase = hasModel && voxelSelection_.Selected().has_value();
    ImGui::BeginDisabled(!canErase);
    if (ImGui::Button("Erase Selected"))
        eraseRequested = true;
    ImGui::EndDisabled();
    DrawTooltip("Erase the selected voxel (Delete)");
    ImGui::SameLine();
    ImGui::TextDisabled("Delete");
    if (voxelSelection_.Selected() && !addTarget)
    {
        ImGui::TextDisabled(
            "%s", AddVoxelTargetStatusMessage(addTarget.Status));
    }
    if (voxelSaveState_.IsDirty())
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
            ImGui::TextDisabled(
                "Sub-model: %zu | Face: %s | Distance: %.3f | Color: %u",
                detail->SubModelIndex, VoxelHitFaceName(detail->Face),
                detail->Distance, detail->ColorIndex);
            ImGui::TextDisabled(
                "Adjacent: %d, %d, %d%s",
                detail->AdjacentPosition.X,
                detail->AdjacentPosition.Y,
                detail->AdjacentPosition.Z,
                detail->AdjacentWithinBounds ? "" : " (outside bounds)");
        }
        ImGui::TextDisabled("Picking: %s",
            VoxelPickingInteractionStateName(
                voxelSelection_.InteractionState()));
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
        DrawTooltip("Click a voxel to select it");
        ImGui::GetWindowDrawList()->AddRect(
            imageOrigin,
            ImVec2(imageOrigin.x + available.x, imageOrigin.y + available.y),
            IM_COL32(55, 64, 78, 255));

        const bool imageHovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        const bool sceneFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const VoxelCameraInteraction cameraInteraction =
            ImGui::IsMouseDown(ImGuiMouseButton_Right)
                ? VoxelCameraInteraction::Orbit
                : ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                ? VoxelCameraInteraction::Pan
                : (imageHovered && io.MouseWheel != 0.0F)
                ? VoxelCameraInteraction::Zoom
                : VoxelCameraInteraction::None;
        const bool cameraControl =
            cameraInteraction != VoxelCameraInteraction::None;
        const bool incompatiblePopupOpen = ImGui::IsPopupOpen(
            nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool inputBlocked = ImGui::IsAnyItemActive() ||
            io.WantTextInput || incompatiblePopupOpen;
        const bool selectionInputAvailable = imageHovered && sceneFocused &&
            !inputBlocked && !cameraControl;
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        std::optional<VoxelRaycastHit> hoveredHit;
        VoxelPickingInteractionState pickingState =
            VoxelPickingInteractionState::Unavailable;
        if (!imageHovered)
            pickingState = VoxelPickingInteractionState::OutsideViewport;
        else if (inputBlocked || !sceneFocused)
            pickingState = VoxelPickingInteractionState::Blocked;
        else if (cameraControl)
            pickingState = VoxelPickingInteractionState::CameraInteraction;
        else if (document == nullptr)
            pickingState = VoxelPickingInteractionState::NoDocument;
        else if (hasModel)
        {
            const ViewportRayBuildResult ray = BuildViewportRay(
                {io.MousePos.x, io.MousePos.y},
                {imageOrigin.x, imageOrigin.y, available.x, available.y},
                viewportCamera_.GetViewProjection(),
                viewportCamera_.GetPosition());
            if (ray.Succeeded())
            {
                VoxelRaycastOptions options;
                options.Transform =
                    CenteredVoxelModelTransform(voxelModelCenter_);
                hoveredHit = RaycastVoxelDocument(
                    *document, *ray.Ray, options);
                pickingState = hoveredHit
                    ? VoxelPickingInteractionState::Hit
                    : VoxelPickingInteractionState::NoHit;
            }
        }
        if (voxelSelection_.SetHovered(pickingState, std::move(hoveredHit)))
        {
            UpdateVoxelHighlights();
        }
        const DragDropImportState dropState = dragDropImport_.State();
        const bool dragDropActive =
            dropState != DragDropImportState::Idle &&
            dropState != DragDropImportState::Completed &&
            dropState != DragDropImportState::Cancelled;
        const VoxelToolInputDecision toolDecision =
            voxelToolInput_.Update({
                ImGui::IsMouseDown(ImGuiMouseButton_Left),
                voxelToolState_.IsEditingToolActive(),
                document != nullptr,
                imageHovered,
                sceneFocused,
                io.WantCaptureMouse && (!imageHovered || inputBlocked),
                incompatiblePopupOpen,
                dragDropActive,
                cameraInteraction,
                voxelEditInProgress_,
                voxelDocumentSession_.Generation()});
        if (toolDecision == VoxelToolInputDecision::Apply)
        {
            if (voxelToolState_.IsPencilActive())
                static_cast<void>(ApplyVoxelPencil());
            else if (voxelToolState_.IsEraserActive())
                static_cast<void>(ApplyVoxelEraser());
        }

        if (!voxelToolState_.IsEditingToolActive() && selectionInputAvailable &&
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
        const bool shortcutsEnabled = sceneActive &&
            !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
            !incompatiblePopupOpen;
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_F, false))
            FrameVoxelViewport();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Home, false))
            viewportCamera_.Reset();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            eraseRequested = true;
        const ActiveVoxelTool shortcutTool = ResolveVoxelToolShortcut(
            voxelToolState_.ActiveTool(),
            ImGui::IsKeyPressed(ImGuiKey_P, false),
            ImGui::IsKeyPressed(ImGuiKey_E, false),
            shortcutsEnabled);
        if (shortcutTool != voxelToolState_.ActiveTool())
        {
            voxelToolState_.SetActiveTool(shortcutTool);
            voxelToolInput_.Reset();
            UpdateVoxelHighlights();
        }
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_A, false))
            addRequested = true;
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
    if (addRequested) static_cast<void>(AddAdjacentVoxel());
    DrawFileDropOverlay(viewportDropRect_, DragDropImportTarget::Viewport);
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
    DrawTooltip("Create a project (Ctrl+N)");

    ImGui::SameLine(0.0F, ActionSpacing);

    if (ImGui::Button("Open Project", ImVec2(ActionWidth, 42.0F)))
    {
        RequestOpenProjectDialog();
    }
    DrawTooltip("Open a project (Ctrl+O)");

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
        RequestOpenProject(*recentProjectToOpen, true);
    }

    ImGui::EndChild();
}

void EditorWorkspace::DrawInspectorPanel()
{
    ImGui::Begin("Inspector", &showInspector_);
    if (thumbnailVisualMode_)
        ImGui::SetScrollY(180.0F);
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    if (const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument())
    {
        ImGui::TextUnformatted("Active Voxel Document");
        if (const auto dimensions = document->GetDimensions())
        {
            ImGui::Text(
                "Dimensions: %u x %u x %u",
                dimensions->X,
                dimensions->Y,
                dimensions->Z);
        }
        ImGui::Text("Voxel Count: %llu",
            static_cast<unsigned long long>(document->GetVoxelCount()));
        ImGui::Text("Sub-models: %zu", document->GetModelCount());
        ImGui::Text("Dirty: %s", document->IsDirty() ? "Yes" : "No");
        ImGui::Text("Revision: %llu",
            static_cast<unsigned long long>(document->GetRevision()));
        ImGui::Text("Active Tool: %s",
            ActiveVoxelToolName(voxelToolState_.ActiveTool()));
        if (voxelToolState_.IsPencilActive())
            ImGui::Text("Palette Index: %zu",
                voxelToolState_.ActivePaletteIndex());
        ImGui::Text("Placement State: %s",
            VoxelPlacementPreviewStatusName(voxelPlacementPreview_.Status));
        if (voxelPlacementPreview_.Position)
        {
            ImGui::Text("Target Position: %d, %d, %d",
                voxelPlacementPreview_.Position->X,
                voxelPlacementPreview_.Position->Y,
                voxelPlacementPreview_.Position->Z);
        }
        if (voxelToolState_.IsEraserActive() && lastVoxelEraserResult_)
        {
            ImGui::Text("Last Operation: %s",
                VoxelEraserResultCodeName(lastVoxelEraserResult_->Code));
            ImGui::Text("Last Position: %d, %d, %d",
                lastVoxelEraserResult_->Position.X,
                lastVoxelEraserResult_->Position.Y,
                lastVoxelEraserResult_->Position.Z);
            ImGui::Text("Removed Palette Index: %u",
                lastVoxelEraserResult_->RemovedPaletteIndex);
        }
        else if (lastVoxelToolResult_)
        {
            ImGui::Text("Last Operation: %s",
                VoxelToolResultCodeName(lastVoxelToolResult_->Code));
            ImGui::Text("Last Position: %d, %d, %d",
                lastVoxelToolResult_->Position.X,
                lastVoxelToolResult_->Position.Y,
                lastVoxelToolResult_->Position.Z);
        }
        ImGui::Separator();
    }

    static_cast<void>(
        assetInspector_.UpdateSelection(assetBrowser_.SelectedEntry()));
    const AssetInspectorState& assetState = assetInspector_.State();
    if (assetState.Kind != AssetInspectorKind::None)
    {
        ImGui::TextUnformatted(assetState.Name.c_str());
        ImGui::TextDisabled("%s", assetState.TypeLabel.c_str());
        ImGui::Separator();
        if (assetState.Kind == AssetInspectorKind::Folder ||
            assetState.Kind == AssetInspectorKind::OtherFile)
        {
            ImGui::Text("Name: %s", assetState.Name.c_str());
            ImGui::TextWrapped("Relative Path: %s",
                assetState.RelativePath.generic_string().c_str());
            ImGui::End();
            return;
        }

        if (!assetState.AnalysisError.empty())
        {
            ImGui::TextUnformatted("Analysis Error");
            DrawErrorMessage(assetState.AnalysisError);
        }
        else
        {
            ImGui::TextUnformatted("Model");
            ImGui::Text("Dimensions: %s", assetState.Dimensions.c_str());
            ImGui::Text("Voxel Count: %s", assetState.VoxelCount.c_str());
            ImGui::Text("Model Count: %s", assetState.ModelCount.c_str());
            ImGui::Text("Palette Colors: %s", assetState.PaletteColors.c_str());
            ImGui::Text("Custom Palette: %s", assetState.CustomPalette.c_str());
            ImGui::Text("VOX Version: %s", assetState.VoxVersion.c_str());
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Asset");
        ImGui::TextWrapped("Asset ID: %s", assetState.AssetId.c_str());
        ImGui::Text("Importer: %s", assetState.Importer.c_str());
        ImGui::Text("Importer Version: %s",
            assetState.ImporterVersion.c_str());
        ImGui::TextWrapped("Relative Path: %s",
            assetState.RelativePath.generic_string().c_str());
        ImGui::Text("File Size: %s", assetState.FileSize.c_str());
        ImGui::Text("Last Modified: %s", assetState.LastModified.c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted("Thumbnail");
        ImGui::Text("Status: %s", assetState.ThumbnailStatus.c_str());
        ImGui::Text("Resolution: %s",
            assetState.ThumbnailResolution.c_str());
        ImGui::Text("Generator Version: %s",
            assetState.ThumbnailGeneratorVersion.c_str());
        ImGui::TextWrapped("Cached File: %s",
            assetState.ThumbnailFile.empty() ? "Unavailable" :
                assetState.ThumbnailFile.generic_string().c_str());
        if (!assetState.ThumbnailError.empty())
            DrawErrorMessage(assetState.ThumbnailError);
        if (ImGui::Button("Regenerate Thumbnail"))
        {
            const std::string assetName = assetState.Name;
            const bool succeeded = assetInspector_.RegenerateThumbnail();
            assetBrowser_.InvalidateThumbnails();
            AddConsoleMessage(succeeded
                ? "Thumbnail regenerated: " + assetName
                : "Thumbnail regeneration failed: " + assetName);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!assetState.CanRevealThumbnail);
        if (ImGui::Button("Reveal Cached File"))
        {
            std::string revealError;
            if (!projectFolderOpener_->Open(
                    assetState.ThumbnailFile.parent_path(), revealError))
                AddConsoleMessage(
                    "Reveal thumbnail failed: " + revealError);
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        if (ImGui::Button("Open in Viewport") && assetInspector_.Selection())
            static_cast<void>(OpenVoxInViewport(
                assetInspector_.Selection()->AbsolutePath()));
        ImGui::SameLine();
        if (ImGui::Button("Reveal in Asset Browser") &&
            assetInspector_.Selection())
            static_cast<void>(assetBrowser_.RevealEntry(
                assetInspector_.Selection()->RelativePath()));
        if (ImGui::Button("Reanalyze"))
        {
            const bool succeeded = assetInspector_.Reanalyze();
            AddConsoleMessage(succeeded
                ? "VOX analysis updated: " + assetState.Name
                : "VOX analysis failed: " +
                    assetInspector_.State().AnalysisError);
            assetBrowser_.InvalidateThumbnails();
        }
        ImGui::End();
        return;
    }

    if (!activeVoxelModel_)
    {
        ImGui::TextDisabled("No asset selected.");
        ImGui::End();
        return;
    }

    const auto colorToImGui = [](const Voxel::VoxelColor& color)
    {
        constexpr float ByteScale = 1.0F / 255.0F;
        return ImVec4(
            static_cast<float>(color.Red) * ByteScale,
            static_cast<float>(color.Green) * ByteScale,
            static_cast<float>(color.Blue) * ByteScale,
            1.0F);
    };

    const Voxel::VoxelGrid* grid = activeVoxelModel_->GetGrid(0U);
    const std::optional<VoxelRaycastHit>& selected = voxelSelection_.Selected();
    const Voxel::Voxel* selectedVoxel = selected && grid
        ? grid->Get(
            selected->Coordinates.X,
            selected->Coordinates.Y,
            selected->Coordinates.Z)
        : nullptr;
    const bool hasOccupiedSelection =
        selectedVoxel != nullptr && selectedVoxel->IsOccupied();

    ImGui::TextUnformatted("Selected voxel");
    if (hasOccupiedSelection)
    {
        ImGui::Text(
            "Coordinates: %u, %u, %u",
            selected->Coordinates.X,
            selected->Coordinates.Y,
            selected->Coordinates.Z);
        ImGui::Text("Current Color: %u", selectedVoxel->ColorIndex);
        const Voxel::VoxelColor* currentColor =
            activeVoxelModel_->Palette().Get(selectedVoxel->ColorIndex);
        if (currentColor)
        {
            ImGui::SameLine();
            ImGui::ColorButton(
                "##CurrentVoxelColor",
                colorToImGui(*currentColor),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                ImVec2(18.0F, 18.0F));
        }
    }
    else
    {
        ImGui::TextDisabled("Nothing selected.");
    }

    ImGui::Spacing();
    ImGui::Text("Paint Color: %u", paintPaletteSelection_.Index());
    const Voxel::VoxelColor* paintPreview =
        paintPaletteSelection_.SelectedColor(&activeVoxelModel_->Palette());
    if (paintPreview)
    {
        ImGui::SameLine();
        ImGui::ColorButton(
            "##PaintColorPreview",
            colorToImGui(*paintPreview),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
            ImVec2(18.0F, 18.0F));
    }
    const float swatchSize = std::max(14.0F, ImGui::GetFrameHeight() * 0.70F);
    for (std::size_t index = 0U;
         index < Voxel::VoxelPalette::Size();
         ++index)
    {
        const Voxel::VoxelColor* color = activeVoxelModel_->Palette().Get(index);
        if (color == nullptr) continue;

        ImGui::PushID(static_cast<int>(index));
        const bool chosen = index == paintPaletteSelection_.Index();
        if (chosen)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Border, ImVec4(1.0F, 0.85F, 0.25F, 1.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0F);
        }
        if (ImGui::ColorButton(
                "##PaintColor",
                colorToImGui(*color),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                ImVec2(swatchSize, swatchSize)))
        {
            static_cast<void>(paintPaletteSelection_.SetIndex(index));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Index %zu | RGBA %u, %u, %u, %u",
                index, color->Red, color->Green, color->Blue, color->Alpha);
        }
        if (chosen)
        {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        if ((index + 1U) % 16U != 0U) ImGui::SameLine();
    }

    const bool wouldChange = hasOccupiedSelection &&
        selectedVoxel->ColorIndex != paintPaletteSelection_.Index();
    ImGui::BeginDisabled(!wouldChange);
    if (ImGui::Button("Paint Selected"))
        static_cast<void>(PaintSelectedVoxel());
    ImGui::EndDisabled();
    DrawTooltip(
        wouldChange
            ? "Paint the selected voxel with the active color"
            : "Select an occupied voxel and choose a different color.");
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
    if (const ImGuiWindow* window = ImGui::FindWindowByName("Asset Browser"))
    {
        assetBrowserDropRect_ = {
            window->Pos.x, window->Pos.y,
            window->Pos.x + window->Size.x,
            window->Pos.y + window->Size.y};
        DrawFileDropOverlay(
            assetBrowserDropRect_, DragDropImportTarget::AssetBrowser);
    }
}

void EditorWorkspace::DrawFileDropOverlay(
    const DragDropRect& rect,
    const DragDropImportTarget target) const
{
    if (dragDropImport_.Target() != target || !rect.IsValid()) return;
    const std::string message = dragDropImport_.HoverMessage();
    if (message.empty()) return;
    const bool invalid =
        dragDropImport_.State() == DragDropImportState::HoverInvalid;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImU32 fill = invalid
        ? IM_COL32(135, 40, 40, 155)
        : IM_COL32(35, 85, 145, 155);
    const ImU32 border = invalid
        ? IM_COL32(245, 95, 85, 255)
        : IM_COL32(90, 170, 255, 255);
    const ImVec2 minimum(rect.Left, rect.Top);
    const ImVec2 maximum(rect.Right, rect.Bottom);
    drawList->AddRectFilled(minimum, maximum, fill, 6.0F);
    drawList->AddRect(minimum, maximum, border, 6.0F, 0, 3.0F);
    const ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
    drawList->AddText(
        ImVec2(
            rect.Left + (rect.Right - rect.Left - textSize.x) * 0.5F,
            rect.Top + (rect.Bottom - rect.Top - textSize.y) * 0.5F),
        IM_COL32(255, 255, 255, 255), message.c_str());
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
                (voxelSaveState_.IsDirty() ? " *" : "")
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

void EditorWorkspace::DrawModelImportDialogs()
{
    if (showImportConfirmationPopup_)
    {
        ImGui::OpenPopup(ImportConfirmationPopupName);
        showImportConfirmationPopup_ = false;
    }
    if (ImGui::BeginPopupModal(
            ImportConfirmationPopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (importStartedFromDrop_)
        {
            if (selectedImportPaths_.size() == 1U)
                ImGui::Text("Import %s ?",
                    selectedImportPaths_.front().filename().string().c_str());
            else
                ImGui::Text("Import %zu VOX models ?",
                    selectedImportPaths_.size());
        }
        else
        {
            ImGui::TextUnformatted(selectedImportPaths_.size() == 1U
                ? "Import this model into Assets/Models?"
                : "Import these models into Assets/Models?");
        }
        ImGui::Separator();
        for (const std::filesystem::path& path : selectedImportPaths_)
        {
            ImGui::BulletText("%s", path.filename().string().c_str());
        }
        const char* importLabel = selectedImportPaths_.size() == 1U
            ? "Import" : "Import All";
        if (ImGui::Button(importLabel))
        {
            std::vector<std::filesystem::path> paths =
                std::move(selectedImportPaths_);
            if (importStartedFromDrop_) dragDropImport_.MarkImporting();
            ImGui::CloseCurrentPopup();
            BeginModelImport(std::move(paths));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            selectedImportPaths_.clear();
            if (importStartedFromDrop_) dragDropImport_.Cancel();
            importStartedFromDrop_ = false;
            pendingDropImportTarget_ = DragDropImportTarget::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showImportCollisionPopup_)
    {
        ImGui::OpenPopup(ImportCollisionPopupName);
        showImportCollisionPopup_ = false;
    }
    if (ImGui::BeginPopupModal(
            ImportCollisionPopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Le fichier existe déjà.");
        if (pendingImportCollision_)
        {
            ImGui::TextWrapped("%s",
                pendingImportCollision_->DestinationPath.string().c_str());
        }
        if (ImGui::Button("Remplacer"))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Replace);
        }
        ImGui::SameLine();
        if (ImGui::Button("Renommer"))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Rename);
        }
        ImGui::SameLine();
        if (ImGui::Button("Ignorer"))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Skip);
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler"))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Cancel);
        }
        ImGui::EndPopup();
    }

    if (showOpenImportedModelPopup_)
    {
        ImGui::OpenPopup(OpenImportedModelPopupName);
        showOpenImportedModelPopup_ = false;
    }
    if (ImGui::BeginPopupModal(
            OpenImportedModelPopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Open in Viewport?");
        if (importedModelToOpen_)
            ImGui::TextDisabled("%s",
                importedModelToOpen_->filename().string().c_str());
        if (ImGui::Button("Oui"))
        {
            const std::filesystem::path path =
                importedModelToOpen_.value_or(std::filesystem::path{});
            importedModelToOpen_.reset();
            ImGui::CloseCurrentPopup();
            if (!path.empty()) static_cast<void>(OpenVoxInViewport(path));
        }
        ImGui::SameLine();
        if (ImGui::Button("Non"))
        {
            importedModelToOpen_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorWorkspace::ConsumeFileDialogResult()
{
    const std::optional<FileDialogResult> result =
        fileDialogService_->ConsumeResult();
    if (!result || result->Status == FileDialogStatus::Cancelled) return;

    if (result->Status == FileDialogStatus::Error)
    {
        projectDialogError_ = result->Error.empty()
            ? "The system file dialog failed." : result->Error;
        AddConsoleMessage("File dialog failed: " + projectDialogError_);
        return;
    }

    if (result->Kind == FileDialogKind::ModelFiles)
    {
        importStartedFromDrop_ = false;
        pendingDropImportTarget_ = DragDropImportTarget::None;
        selectedImportPaths_ = result->Paths;
        if (selectedImportPaths_.empty() && !result->Path.empty())
            selectedImportPaths_.push_back(result->Path);
        if (selectedImportPaths_.empty()) return;
        showImportConfirmationPopup_ = true;
        return;
    }

    std::array<char, 1024>& destination =
        result->Kind == FileDialogKind::ProjectParentFolder
        ? newProjectParentPath_ : openProjectFilePath_;
    if (!CopyPathToBuffer(result->Path, destination))
    {
        projectDialogError_ = "The selected path is too long.";
        return;
    }
    projectDialogError_.clear();
}

void EditorWorkspace::DrawDirtyConfirmationDialog()
{
    if (showDirtyConfirmationPopup_)
    {
        ImGui::OpenPopup(DirtyConfirmationPopupName);
        showDirtyConfirmationPopup_ = false;
    }
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal(
            DirtyConfirmationPopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextWrapped("The active voxel model has unsaved changes.");
    ImGui::TextUnformatted("Discard changes?");
    ImGui::Spacing();
    if (ImGui::Button("Discard"))
    {
        const auto action = dirtyActionConfirmation_.Discard();
        ImGui::CloseCurrentPopup();
        if (action) ExecutePendingDirtyAction(*action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        dirtyActionConfirmation_.Cancel();
        pendingProjectPath_.clear();
        pendingVoxelPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorWorkspace::DrawNewProjectDialog()
{
    constexpr const char* PopupName = "New VoxelForge Project";

    if (showNewProjectPopup_)
    {
        ImGui::OpenPopup(PopupName);
        showNewProjectPopup_ = false;
    }

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(440.0F, 0.0F), ImVec2(760.0F, 600.0F));
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal(PopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::TextDisabled("Create a project folder and its Assets directory.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(std::max(260.0F, ImGui::GetContentRegionAvail().x));
    bool fieldsChanged = ImGui::InputText(
        "##NewProjectName", newProjectName_.data(), newProjectName_.size());
    ImGui::TextDisabled("Project Name");
    ImGui::SetNextItemWidth(std::max(
        180.0F, ImGui::GetContentRegionAvail().x - 92.0F));
    fieldsChanged = DrawPathInput(
        "##NewProjectParent", newProjectParentPath_) || fieldsChanged;
    ImGui::SameLine();
    ImGui::BeginDisabled(fileDialogService_->IsPending());
    if (ImGui::Button("Browse...##NewProject"))
    {
        const std::filesystem::path current(newProjectParentPath_.data());
        const std::filesystem::path initial = current.empty()
            ? projectDialogPreferences_.LastCreateParent() : current;
        if (!fileDialogService_->ChooseProjectParentFolder(initial))
            projectDialogError_ = "A file dialog is already open.";
    }
    ImGui::EndDisabled();
    DrawTooltip("Choose the project parent folder");
    ImGui::TextDisabled("Parent Folder");

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
        ImGui::BeginChild("##FinalProjectPath", ImVec2(0.0F, 42.0F), true);
        ImGui::TextWrapped("%s", validation.DestinationPath.string().c_str());
        ImGui::EndChild();
    }

    const std::string_view displayedError = projectDialogError_.empty()
        ? std::string_view(validation.Error)
        : std::string_view(projectDialogError_);
    DrawErrorMessage(displayedError);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(!validation.IsValid());

    if (ImGui::Button("Create"))
    {
        CreateProject();
        if (projectDialogError_.empty() &&
            !dirtyActionConfirmation_.IsPending())
            ImGui::CloseCurrentPopup();
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

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(440.0F, 0.0F), ImVec2(760.0F, 500.0F));
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal(PopupName, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::TextDisabled("Select a VoxelForge project file.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(std::max(
        180.0F, ImGui::GetContentRegionAvail().x - 92.0F));
    if (DrawPathInput("##OpenProjectFile", openProjectFilePath_))
    {
        projectDialogError_.clear();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(fileDialogService_->IsPending());
    if (ImGui::Button("Browse...##OpenProject"))
    {
        const std::filesystem::path current(openProjectFilePath_.data());
        const std::filesystem::path initial = current.empty()
            ? projectDialogPreferences_.LastOpenDirectory()
            : current.parent_path();
        if (!fileDialogService_->ChooseProjectFile(initial))
            projectDialogError_ = "A file dialog is already open.";
    }
    ImGui::EndDisabled();
    DrawTooltip("Choose a .vfproject file");
    ImGui::TextDisabled("Project File");

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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(!inputError.empty());

    if (ImGui::Button("Open"))
    {
        if (voxelSaveState_.IsDirty())
        {
            RequestOpenProject(projectFilePath, false);
            ImGui::CloseCurrentPopup();
        }
        else if (OpenProject(projectFilePath, false))
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
    if (newProjectParentPath_[0] == '\0')
        static_cast<void>(CopyPathToBuffer(
            projectDialogPreferences_.LastCreateParent(),
            newProjectParentPath_));
    showNewProjectPopup_ = true;
}

void EditorWorkspace::RequestOpenProjectDialog()
{
    projectDialogError_.clear();
    showOpenProjectPopup_ = true;
}

void EditorWorkspace::RequestImportModelDialog()
{
    if (!projectManager_.HasActiveProject())
    {
        AddConsoleMessage("Model import failed: no project is loaded.");
        return;
    }
    const std::filesystem::path initial = modelImportService_.ModelsDirectory();
    if (!fileDialogService_->ChooseModelFiles(initial))
        AddConsoleMessage("Model import failed: a file dialog is already open.");
}

void EditorWorkspace::CreateProject()
{
    if (!dirtyActionConfirmation_.Request(
            DestructiveAction::CreateProject, voxelSaveState_.IsDirty()))
    {
        showDirtyConfirmationPopup_ = true;
        ImGui::CloseCurrentPopup();
        return;
    }
    CreateProjectNow();
}

void EditorWorkspace::CreateProjectNow()
{
    const std::filesystem::path parent(newProjectParentPath_.data());
    const auto project = projectManager_.CreateProject(
        newProjectName_.data(),
        parent);

    if (!project)
    {
        voxelDocumentSession_.ClearProject();
        projectDialogError_ = projectManager_.LastError();
        AddConsoleMessage(
            "Project creation failed: " + projectDialogError_);
        return;
    }

    ClearVoxelViewport();
    SynchronizeProjectAssets();
    AddConsoleMessage("Project created: " + project->Name());
    projectDialogError_.clear();
    welcomeError_.clear();
    failedRecentProjectPath_.reset();
    if (!projectDialogPreferences_.SetLastCreateParent(parent))
        AddConsoleMessage(
            "Preferences save warning: " + projectDialogPreferences_.LastError());
    newProjectName_.fill('\0');
    newProjectParentPath_.fill('\0');
    UpdateWindowTitle();
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
    SynchronizeProjectAssets();
    if (!projectDialogPreferences_.SetLastOpenDirectory(
            projectFilePath.parent_path()))
        AddConsoleMessage(
            "Preferences save warning: " + projectDialogPreferences_.LastError());
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

bool EditorWorkspace::SaveVoxelModel()
{
    if (!activeVoxelModel_ || voxelSaveState_.SavePath().empty())
    {
        AddConsoleMessage("Voxel model save failed: no model is loaded.");
        return false;
    }

    const Voxel::VoxelSerializationResult result =
        Voxel::VoxelModelSerializer::Save(
            voxelSaveState_.SavePath(), *activeVoxelModel_);
    if (!result)
    {
        AddConsoleMessage("Voxel model save failed: " + result.Message);
        return false;
    }

    voxelSaveState_.MarkSaved();
    static_cast<void>(assetBrowser_.Refresh());
    AddConsoleMessage(
        "Voxel model saved: " + voxelSaveState_.SavePath().string());
    return true;
}

void EditorWorkspace::RequestExit()
{
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::ExitApplication, voxelSaveState_.IsDirty()))
    {
        exitRequest_.RequestExit();
        return;
    }
    showDirtyConfirmationPopup_ = true;
}

void EditorWorkspace::RequestCloseProject()
{
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::CloseProject, voxelSaveState_.IsDirty()))
    {
        CloseProject();
        return;
    }
    showDirtyConfirmationPopup_ = true;
}

void EditorWorkspace::RequestOpenProject(
    std::filesystem::path projectFilePath,
    const bool recentProject)
{
    if (dirtyActionConfirmation_.IsPending()) return;
    pendingProjectPath_ = std::move(projectFilePath);
    pendingRecentProject_ = recentProject;
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::OpenProject, voxelSaveState_.IsDirty()))
    {
        static_cast<void>(OpenProject(
            pendingProjectPath_, pendingRecentProject_));
        pendingProjectPath_.clear();
        return;
    }
    showDirtyConfirmationPopup_ = true;
}

void EditorWorkspace::RequestReplaceVoxelModel(std::filesystem::path filePath)
{
    if (dirtyActionConfirmation_.IsPending()) return;
    pendingVoxelPath_ = std::move(filePath);
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::ReplaceVoxelModel, voxelSaveState_.IsDirty()))
    {
        static_cast<void>(OpenVoxInViewportNow(pendingVoxelPath_));
        pendingVoxelPath_.clear();
        return;
    }
    showDirtyConfirmationPopup_ = true;
}

void EditorWorkspace::ExecutePendingDirtyAction(const DestructiveAction action)
{
    switch (action)
    {
    case DestructiveAction::CloseProject:
        CloseProject();
        break;
    case DestructiveAction::OpenProject:
        static_cast<void>(OpenProject(
            pendingProjectPath_, pendingRecentProject_));
        pendingProjectPath_.clear();
        break;
    case DestructiveAction::CreateProject:
        CreateProjectNow();
        break;
    case DestructiveAction::ExitApplication:
        exitRequest_.RequestExit();
        break;
    case DestructiveAction::ReplaceVoxelModel:
        static_cast<void>(OpenVoxInViewportNow(pendingVoxelPath_));
        pendingVoxelPath_.clear();
        break;
    }
}

void EditorWorkspace::OpenProjectFolder()
{
    const auto& project = projectManager_.ActiveProject();
    if (!project) return;
    std::string error;
    if (!projectFolderOpener_->Open(project->RootPath(), error))
        AddConsoleMessage("Open project folder failed: " + error);
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
    SynchronizeProjectAssets();
    AddConsoleMessage("Project closed: " + projectName);
    welcomeError_.clear();
    failedRecentProjectPath_.reset();
    UpdateWindowTitle();
}

void EditorWorkspace::SynchronizeProjectAssets()
{
    const auto& project = projectManager_.ActiveProject();
    if (!project)
    {
        dragDropImport_.Reset();
        pendingDropImportTarget_ = DragDropImportTarget::None;
        importStartedFromDrop_ = false;
        selectedImportPaths_.clear();
        pendingImportPaths_.clear();
        successfulImportPaths_.clear();
        pendingImportCollision_.reset();
        showImportConfirmationPopup_ = false;
        showImportCollisionPopup_ = false;
        modelImportService_.SetRefreshCallback(
            [this]()
            {
                static_cast<void>(assetBrowser_.Refresh());
            });
        modelImportService_.ClearProjectRoot();
        assetBrowser_.ClearAssetsRoot();
        assetInspector_.ClearProject();
        return;
    }

    if (!modelImportService_.SetProjectRoot(project->RootPath()))
    {
        AddConsoleMessage(
            "Model import setup failed: " + modelImportService_.LastError());
    }
    if (!assetBrowser_.SetAssetsRoot(project->RootPath() / "Assets"))
    {
        AddConsoleMessage("Asset Browser refresh failed.");
    }
    if (!assetInspector_.SetAssetsRoot(project->RootPath() / "Assets"))
        AddConsoleMessage("Asset Inspector setup failed.");
    if (!voxelDocumentSession_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("Voxel document session setup failed.");
}

void EditorWorkspace::BeginModelImport(
    std::vector<std::filesystem::path> sourcePaths)
{
    modelImportService_.SetRefreshCallback({});
    pendingImportPaths_ = std::move(sourcePaths);
    pendingImportIndex_ = 0U;
    requestedImportCount_ = pendingImportPaths_.size();
    completedImportCount_ = 0U;
    skippedImportCount_ = 0U;
    failedImportCount_ = 0U;
    successfulImportPaths_.clear();
    pendingImportCollision_.reset();
    ContinueModelImport(ModelImportCollisionAction::Ask);
}

void EditorWorkspace::ContinueModelImport(
    ModelImportCollisionAction collisionAction)
{
    while (pendingImportIndex_ < pendingImportPaths_.size())
    {
        const std::filesystem::path source =
            pendingImportPaths_[pendingImportIndex_];
        const ModelImportResult result =
            modelImportService_.ImportModel(source, collisionAction);
        collisionAction = ModelImportCollisionAction::Ask;

        if (result.Status == ModelImportStatus::Collision)
        {
            pendingImportCollision_ = result;
            showImportCollisionPopup_ = true;
            return;
        }
        if (result.Status == ModelImportStatus::Cancelled)
        {
            AddConsoleMessage("Model import cancelled.");
            FinishModelImport(true);
            return;
        }

        if (result.Succeeded())
        {
            ++completedImportCount_;
            successfulImportPaths_.push_back(result.DestinationPath);
            LogModelImport(result);
            if (importStartedFromDrop_)
            {
                std::string message = "[Import] Imported " +
                    result.SourcePath.filename().string();
                if (result.Status == ModelImportStatus::Renamed)
                    message += " as " +
                        result.DestinationPath.filename().string();
                AddConsoleMessage(message + ".");
            }
        }
        else if (result.Status == ModelImportStatus::Skipped)
        {
            ++skippedImportCount_;
            AddConsoleMessage(
                "Import skipped: " + source.filename().string());
        }
        else
        {
            ++failedImportCount_;
            AddConsoleMessage(
                "Import failed: " + source.filename().string() +
                " - " + result.Message);
        }

        ++pendingImportIndex_;
        pendingImportCollision_.reset();
    }
    FinishModelImport();
}

void EditorWorkspace::FinishModelImport(const bool cancelled)
{
    modelImportService_.SetRefreshCallback(
        [this]()
        {
            static_cast<void>(assetBrowser_.Refresh());
        });
    if (!successfulImportPaths_.empty())
    {
        const std::filesystem::path relativeToAssets =
            successfulImportPaths_.back().lexically_relative(
                modelImportService_.ProjectRoot() / "Assets");
        static_cast<void>(assetBrowser_.RevealEntry(relativeToAssets));
        static_cast<void>(assetInspector_.UpdateSelection(
            assetBrowser_.SelectedEntry()));
    }

    if (importStartedFromDrop_)
    {
        AddConsoleMessage("[Import] Completed:\n" +
            std::to_string(completedImportCount_) + " imported\n" +
            std::to_string(skippedImportCount_) + " skipped\n" +
            std::to_string(failedImportCount_) + " failed");
        if (!cancelled &&
            pendingDropImportTarget_ == DragDropImportTarget::Viewport &&
            requestedImportCount_ == 1U &&
            successfulImportPaths_.size() == 1U)
            static_cast<void>(OpenVoxInViewportNow(
                successfulImportPaths_.front()));
        if (cancelled)
            dragDropImport_.Cancel();
        else
            dragDropImport_.MarkCompleted();
    }
    else if (requestedImportCount_ > 1U)
    {
        AddConsoleMessage(
            std::to_string(successfulImportPaths_.size()) +
            " models imported.");
    }
    else if (requestedImportCount_ == 1U &&
             successfulImportPaths_.size() == 1U)
    {
        importedModelToOpen_ = successfulImportPaths_.front();
        showOpenImportedModelPopup_ = true;
    }
    pendingImportPaths_.clear();
    pendingImportCollision_.reset();
    pendingImportIndex_ = 0U;
    requestedImportCount_ = 0U;
    pendingDropImportTarget_ = DragDropImportTarget::None;
    importStartedFromDrop_ = false;
}

void EditorWorkspace::LogModelImport(const ModelImportResult& result)
{
    const std::filesystem::path relative = result.DestinationPath
        .lexically_relative(modelImportService_.ProjectRoot());
    AddConsoleMessage("Import");
    AddConsoleMessage(result.SourcePath.filename().string());
    AddConsoleMessage("-> " + relative.generic_string());
    AddConsoleMessage("Done");
    AddConsoleMessage(result.ThumbnailStatus ==
            ThumbnailGenerationStatus::Generated
        ? "Thumbnail generated."
        : "Thumbnail warning: " + result.ThumbnailMessage);
}

bool EditorWorkspace::OpenVoxInViewport(
    const std::filesystem::path& filePath)
{
    if (voxelSaveState_.IsDirty())
    {
        RequestReplaceVoxelModel(filePath);
        return true;
    }
    return OpenVoxInViewportNow(filePath);
}

bool EditorWorkspace::OpenVoxInViewportNow(
    const std::filesystem::path& filePath)
{
    std::optional<Voxel::VoxelModel> model;
    std::optional<Asset::Vox::VoxModel> inspectedVoxModel;
    const std::string extension = LowercaseExtension(filePath);
    if (extension == ".vfvoxel")
    {
        Voxel::VoxelDeserializationResult loaded =
            Voxel::VoxelModelSerializer::Load(filePath);
        if (!loaded.Model)
        {
            AddConsoleMessage(
                "VFVOXEL viewport load failed: " + loaded.Message);
            return false;
        }
        model = std::move(*loaded.Model);
    }
    else if (extension == ".vox")
    {
        Asset::Vox::VoxImporter importer;
        const auto imported = importer.Inspect(filePath);
        if (!imported.Result.Succeeded || !imported.Asset)
        {
            AddConsoleMessage(
                "VOX viewport import failed: " + imported.Result.Message);
            return false;
        }
        inspectedVoxModel = std::move(*imported.Asset);
        auto converted = Voxel::VoxModelConverter::Convert(
            *inspectedVoxModel, filePath.stem().string());
        if (!converted.Succeeded || !converted.Model)
        {
            AddConsoleMessage(
                "VOX viewport conversion failed: " + converted.Message);
            return false;
        }
        model = std::move(*converted.Model);
    }
    else
    {
        AddConsoleMessage(
            "Voxel viewport load failed: unsupported file extension.");
        return false;
    }

    const Voxel::VoxelGrid* grid = model->GetGrid(0U);
    if (grid == nullptr)
    {
        AddConsoleMessage("Voxel viewport load failed: no grid is available.");
        return false;
    }

    std::optional<Mesh::MeshData> legacyMesh;
    const Mesh::MeshData* renderMesh = nullptr;
    Voxel::VoxelPalette renderPalette;
    Vec3 modelCenter{};
    if (inspectedVoxModel)
    {
        const VoxelDocumentSessionResult opened =
            voxelDocumentSession_.OpenInspected(filePath, *inspectedVoxModel);
        if (!opened.Succeeded())
        {
            AddConsoleMessage("Voxel document load failed: " + opened.Message);
            return false;
        }
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const auto synchronized = voxelDocumentMeshCache_.Synchronize(
            *document, voxelDocumentSession_.Generation());
        if (!synchronized.Succeeded || voxelDocumentMeshCache_.Mesh() == nullptr)
        {
            ClearVoxelViewport();
            AddConsoleMessage(
                "Voxel document mesh failed: " + synchronized.Message);
            return false;
        }
        renderMesh = voxelDocumentMeshCache_.Mesh();
        renderPalette = BuildDocumentRenderPalette(*document);
        modelCenter = CalculateVoxelDocumentCenter(*document);
    }
    else
    {
        voxelDocumentSession_.Close();
        voxelDocumentMeshCache_.Clear();
        uploadedDocumentIdentity_.reset();
        uploadedDocumentRevision_.reset();
        Mesh::MeshBuildResult built = Mesh::VoxelMeshBuilder::Build(*grid);
        if (!built.Succeeded || !built.Mesh)
        {
            AddConsoleMessage("Voxel viewport mesh failed: " + built.Message);
            return false;
        }
        legacyMesh = std::move(*built.Mesh);
        renderMesh = &*legacyMesh;
        renderPalette = model->Palette();
        modelCenter = CalculateVoxelGridCenter(*grid);
    }

    if (!viewportRenderer_.Upload(*renderMesh, renderPalette, modelCenter))
    {
        const std::string error = viewportRenderer_.LastError();
        ClearVoxelViewport();
        AddConsoleMessage("Voxel viewport GPU upload failed: " + error);
        return false;
    }
    commandHistory_.Clear();
    activeVoxelModel_ = std::move(*model);
    paintPaletteSelection_.OnModelLoaded();
    ++voxelModelGeneration_;
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const bool stateReplaced = document
        ? viewportState_.ReplaceDocument(
            filePath.filename().string(), *document, *renderMesh)
        : viewportState_.Replace(
            filePath.filename().string(), *activeVoxelModel_, *renderMesh);
    if (!stateReplaced)
    {
        ClearVoxelViewport();
        AddConsoleMessage(
            "Voxel viewport load failed: no first grid is available.");
        return false;
    }
    if (document)
    {
        uploadedDocumentIdentity_ = voxelDocumentSession_.Generation();
        uploadedDocumentRevision_ = document->GetRevision();
    }
    voxelModelCenter_ = modelCenter;
    voxelSaveState_.OnModelLoaded(filePath);
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
    if (activeVoxelModel_->GridCount() > 1U)
    {
        AddConsoleMessage("Viewport v1 displays only the first voxel grid.");
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
        std::optional<VoxelRaycastHit> hit;
        if (const Asset::Voxel::VoxelDocument* document =
                voxelDocumentSession_.ActiveDocument())
        {
            VoxelRaycastOptions options;
            options.Transform = CenteredVoxelModelTransform(voxelModelCenter_);
            hit = RaycastVoxelDocument(
                *document,
                {VoxelGridToViewport(
                     {-1.0F, 1.5F, 1.5F}, voxelModelCenter_),
                 {1.0F, 0.0F, 0.0F}},
                options);
        }
        else
        {
            hit = RaycastVoxelGrid(
                *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        }
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
            !voxelSelection_.Selected() && voxelSaveState_.IsDirty();
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

bool EditorWorkspace::RunPaintVoxelSmokeStep(const std::size_t frame)
{
    const Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    if (grid == nullptr) return false;

    if (frame == 0U)
    {
        const auto hit = RaycastVoxelGrid(
            *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        paintSmokeSelected_ = hit.has_value();
        if (!hit) return false;
        paintSmokeX_ = hit->Coordinates.X;
        paintSmokeY_ = hit->Coordinates.Y;
        paintSmokeZ_ = hit->Coordinates.Z;
        const Voxel::Voxel* voxel =
            grid->Get(paintSmokeX_, paintSmokeY_, paintSmokeZ_);
        if (voxel == nullptr || !voxel->IsOccupied()) return false;
        paintSmokeInitialColor_ = voxel->ColorIndex;
        paintSmokeNewColor_ = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(paintSmokeInitialColor_) + 1U) %
            Voxel::VoxelPalette::Size());
        paintSmokeInitialVoxelCount_ = grid->OccupiedVoxelCount();
        paintSmokeInitialTriangleCount_ =
            viewportState_.Statistics().TriangleCount;
        static_cast<void>(paintPaletteSelection_.SetIndex(paintSmokeNewColor_));
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        UpdateVoxelHighlights();
    }
    else if (frame == 1U)
    {
        paintSmokeExecuteRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();
        const bool painted = PaintSelectedVoxel();
        const Voxel::Voxel* voxel =
            grid->Get(paintSmokeX_, paintSmokeY_, paintSmokeZ_);
        paintSmokeExecuted_ = painted && voxel != nullptr &&
            voxel->IsOccupied() &&
            voxel->ColorIndex == paintSmokeNewColor_ &&
            grid->OccupiedVoxelCount() == paintSmokeInitialVoxelCount_ &&
            viewportState_.Statistics().TriangleCount ==
                paintSmokeInitialTriangleCount_ &&
            !voxelSelection_.Selected() && voxelSaveState_.IsDirty();
    }
    else if (frame == 10U)
    {
        const bool paintedFramesRendered =
            viewportRenderer_.ModelRenderCount() >
            paintSmokeExecuteRenderBaseline_;
        UndoCommand();
        const Voxel::Voxel* voxel =
            grid->Get(paintSmokeX_, paintSmokeY_, paintSmokeZ_);
        paintSmokeUndone_ = paintedFramesRendered && voxel != nullptr &&
            voxel->IsOccupied() &&
            voxel->ColorIndex == paintSmokeInitialColor_ &&
            grid->OccupiedVoxelCount() == paintSmokeInitialVoxelCount_ &&
            viewportState_.Statistics().TriangleCount ==
                paintSmokeInitialTriangleCount_;
        paintSmokeUndoRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    else if (frame == 20U)
    {
        const bool undoFramesRendered =
            viewportRenderer_.ModelRenderCount() >
            paintSmokeUndoRenderBaseline_;
        RedoCommand();
        const Voxel::Voxel* voxel =
            grid->Get(paintSmokeX_, paintSmokeY_, paintSmokeZ_);
        paintSmokeRedone_ = undoFramesRendered && voxel != nullptr &&
            voxel->IsOccupied() &&
            voxel->ColorIndex == paintSmokeNewColor_ &&
            grid->OccupiedVoxelCount() == paintSmokeInitialVoxelCount_ &&
            viewportState_.Statistics().TriangleCount ==
                paintSmokeInitialTriangleCount_;
        paintSmokeRedoRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    return true;
}

bool EditorWorkspace::PaintVoxelSmokePassed() const noexcept
{
    return paintSmokeSelected_ && paintSmokeExecuted_ &&
        paintSmokeUndone_ && paintSmokeRedone_ &&
        viewportRenderer_.ModelRenderCount() > paintSmokeRedoRenderBaseline_ &&
        viewportState_.Statistics().OccupiedVoxelCount ==
            paintSmokeInitialVoxelCount_ &&
        viewportState_.Statistics().TriangleCount ==
            paintSmokeInitialTriangleCount_;
}

bool EditorWorkspace::RunVoxelSaveSmokeStep(const std::size_t frame)
{
    Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    if (grid == nullptr)
    {
        return false;
    }

    if (frame == 0U)
    {
        const auto hit = RaycastVoxelGrid(
            *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit)
        {
            return false;
        }
        voxelSaveSmokeX_ = hit->Coordinates.X;
        voxelSaveSmokeY_ = hit->Coordinates.Y;
        voxelSaveSmokeZ_ = hit->Coordinates.Z;
        const Voxel::Voxel* voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        if (voxel == nullptr || !voxel->IsOccupied())
        {
            return false;
        }
        voxelSaveSmokeColor_ = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(voxel->ColorIndex) + 17U) %
            Voxel::VoxelPalette::Size());
        voxelSaveSmokeInitialVoxelCount_ = grid->OccupiedVoxelCount();
        static_cast<void>(paintPaletteSelection_.SetIndex(voxelSaveSmokeColor_));
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        UpdateVoxelHighlights();
    }
    else if (frame == 1U)
    {
        const bool painted = PaintSelectedVoxel();
        voxelSaveSmokePath_ = voxelSaveState_.SavePath();
        const bool saved = painted && voxelSaveState_.IsDirty() &&
            SaveVoxelModel();
        const Voxel::Voxel* voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        voxelSaveSmokePaintedAndSaved_ = saved && voxel != nullptr &&
            voxel->ColorIndex == voxelSaveSmokeColor_ &&
            !voxelSaveState_.IsDirty() && commandHistory_.CanUndo() &&
            std::filesystem::is_regular_file(voxelSaveSmokePath_);
    }
    else if (frame == 5U)
    {
        ClearVoxelViewport();
        if (!OpenVoxInViewportNow(voxelSaveSmokePath_))
        {
            return false;
        }
        grid = activeVoxelModel_->GetGrid(0U);
        const Voxel::Voxel* voxel = grid ? grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_) : nullptr;
        voxelSaveSmokePaintReloaded_ = voxel != nullptr &&
            voxel->IsOccupied() && voxel->ColorIndex == voxelSaveSmokeColor_ &&
            !voxelSaveState_.IsDirty() && !commandHistory_.CanUndo() &&
            !commandHistory_.CanRedo() && !voxelSelection_.Selected();
    }
    else if (frame == 10U)
    {
        const Voxel::Voxel* voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        if (voxel == nullptr || !voxel->IsOccupied())
        {
            return false;
        }
        const VoxelRaycastHit hit{
            {voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_},
            VoxelHitFace::NegativeX,
            0.0F,
            {},
            voxel->ColorIndex};
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        const bool erased = EraseSelectedVoxel();
        const bool saved = erased && voxelSaveState_.IsDirty() &&
            SaveVoxelModel();
        voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        voxelSaveSmokeErasedAndSaved_ = saved && voxel != nullptr &&
            !voxel->IsOccupied() && !voxelSaveState_.IsDirty() &&
            commandHistory_.CanUndo();
    }
    else if (frame == 15U)
    {
        ClearVoxelViewport();
        if (!OpenVoxInViewportNow(voxelSaveSmokePath_))
        {
            return false;
        }
        grid = activeVoxelModel_->GetGrid(0U);
        const Voxel::Voxel* voxel = grid ? grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_) : nullptr;
        const auto temporary =
            std::filesystem::path(voxelSaveSmokePath_.string() + ".tmp");
        const auto backup =
            std::filesystem::path(voxelSaveSmokePath_.string() + ".bak");
        voxelSaveSmokeEraseReloaded_ = voxel != nullptr &&
            !voxel->IsOccupied() && grid->OccupiedVoxelCount() + 1U ==
                voxelSaveSmokeInitialVoxelCount_ &&
            !voxelSaveState_.IsDirty() && !commandHistory_.CanUndo() &&
            !commandHistory_.CanRedo() && !voxelSelection_.Selected() &&
            !std::filesystem::exists(temporary) &&
            !std::filesystem::exists(backup);
        voxelSaveSmokeRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    return true;
}

bool EditorWorkspace::VoxelSaveSmokePassed() const noexcept
{
    return voxelSaveSmokePaintedAndSaved_ &&
        voxelSaveSmokePaintReloaded_ &&
        voxelSaveSmokeErasedAndSaved_ &&
        voxelSaveSmokeEraseReloaded_ &&
        viewportRenderer_.ModelRenderCount() > voxelSaveSmokeRenderBaseline_;
}

bool EditorWorkspace::RunAddVoxelSmokeStep(const std::size_t frame)
{
    Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    if (grid == nullptr)
    {
        return false;
    }

    if (frame == 0U)
    {
        if (grid->OccupiedVoxelCount() != 1U ||
            viewportState_.Statistics().TriangleCount != 12U)
        {
            return false;
        }
        const auto hit = RaycastVoxelGrid(
            *grid, {{-1.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit)
        {
            return false;
        }
        static_cast<void>(paintPaletteSelection_.SetIndex(255U));
        addVoxelSmokePreviewUploadBaseline_ =
            viewportRenderer_.HighlightUploadCount();
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        const AddVoxelTarget target = FindAddVoxelTarget(grid, hit);
        if (!target)
        {
            return false;
        }
        addVoxelSmokeTarget_ = *target.Coordinates;
        UpdateVoxelHighlights();
        addVoxelSmokeSelected_ = true;
    }
    else if (frame == 1U)
    {
        addVoxelSmokeExecuteRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();
        const bool added = AddAdjacentVoxel();
        const Voxel::Voxel* voxel = grid->Get(
            addVoxelSmokeTarget_.X,
            addVoxelSmokeTarget_.Y,
            addVoxelSmokeTarget_.Z);
        addVoxelSmokeExecuted_ = added && voxel != nullptr &&
            voxel->IsOccupied() && voxel->ColorIndex == 255U &&
            voxel->Flags == Voxel::Voxel::OccupiedFlag &&
            grid->OccupiedVoxelCount() == 2U &&
            viewportState_.Statistics().TriangleCount == 20U &&
            voxelSaveState_.IsDirty() && !voxelSelection_.Selected() &&
            viewportRenderer_.HighlightUploadCount() >
                addVoxelSmokePreviewUploadBaseline_;
    }
    else if (frame == 10U)
    {
        const bool rendered = viewportRenderer_.ModelRenderCount() >
            addVoxelSmokeExecuteRenderBaseline_;
        UndoCommand();
        addVoxelSmokeUndone_ = rendered &&
            !grid->Get(
                addVoxelSmokeTarget_.X,
                addVoxelSmokeTarget_.Y,
                addVoxelSmokeTarget_.Z)->IsOccupied() &&
            grid->OccupiedVoxelCount() == 1U &&
            viewportState_.Statistics().TriangleCount == 12U;
        addVoxelSmokeUndoRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();
    }
    else if (frame == 20U)
    {
        const bool rendered = viewportRenderer_.ModelRenderCount() >
            addVoxelSmokeUndoRenderBaseline_;
        RedoCommand();
        const Voxel::Voxel* voxel = grid->Get(
            addVoxelSmokeTarget_.X,
            addVoxelSmokeTarget_.Y,
            addVoxelSmokeTarget_.Z);
        addVoxelSmokeSavePath_ = voxelSaveState_.SavePath();
        const bool saved = voxel != nullptr && voxel->IsOccupied() &&
            SaveVoxelModel();
        addVoxelSmokeRedoneAndSaved_ = rendered && saved &&
            voxel->ColorIndex == 255U && grid->OccupiedVoxelCount() == 2U &&
            viewportState_.Statistics().TriangleCount == 20U &&
            !voxelSaveState_.IsDirty() && commandHistory_.CanUndo();
    }
    else if (frame == 25U)
    {
        ClearVoxelViewport();
        if (!OpenVoxInViewportNow(addVoxelSmokeSavePath_))
        {
            return false;
        }
        grid = activeVoxelModel_->GetGrid(0U);
        const Voxel::Voxel* voxel = grid ? grid->Get(
            addVoxelSmokeTarget_.X,
            addVoxelSmokeTarget_.Y,
            addVoxelSmokeTarget_.Z) : nullptr;
        addVoxelSmokeReloaded_ = voxel != nullptr && voxel->IsOccupied() &&
            voxel->ColorIndex == 255U && grid->OccupiedVoxelCount() == 2U &&
            viewportState_.Statistics().TriangleCount == 20U &&
            !voxelSaveState_.IsDirty() && !commandHistory_.CanUndo() &&
            !commandHistory_.CanRedo();
        addVoxelSmokeReloadRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();
    }
    return true;
}

bool EditorWorkspace::AddVoxelSmokePassed() const noexcept
{
    return addVoxelSmokeSelected_ && addVoxelSmokeExecuted_ &&
        addVoxelSmokeUndone_ && addVoxelSmokeRedoneAndSaved_ &&
        addVoxelSmokeReloaded_ &&
        viewportRenderer_.ModelRenderCount() >
            addVoxelSmokeReloadRenderBaseline_;
}

bool EditorWorkspace::RunModelImportSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath,
    const bool importVisualSet)
{
    if (frame == 0U)
    {
        resetLayoutRequested_ = true;
        thumbnailVisualLayoutRequested_ = importVisualSet;
        thumbnailVisualMode_ = importVisualSet;
        const std::size_t refreshBaseline = assetBrowser_.RefreshCount();
        std::vector<std::filesystem::path> sources{sourcePath};
        if (importVisualSet)
        {
            std::error_code sourceError;
            for (const auto& entry : std::filesystem::directory_iterator(
                     sourcePath.parent_path(), sourceError))
            {
                if (entry.is_regular_file(sourceError) && !sourceError &&
                    entry.path() != sourcePath &&
                    entry.path().extension() == ".vox")
                    sources.push_back(entry.path());
                sourceError.clear();
            }
            std::sort(sources.begin(), sources.end());
        }
        const std::vector<ModelImportResult> results =
            modelImportService_.ImportModels(sources);
        const ModelImportResult& result = results.front();
        modelImportSmokeDestination_ = result.DestinationPath;
        modelImportSmokeImported_ = result.Succeeded() &&
            result.DestinationPath.parent_path() ==
                modelImportService_.ModelsDirectory() &&
            std::filesystem::is_regular_file(result.DestinationPath) &&
            (importVisualSet
                ? results.size() >= 3U && std::all_of(
                    results.begin(), results.end(),
                    [](const ModelImportResult& item)
                    {
                        return item.Succeeded();
                    })
                : modelImportService_.RecentImports().size() == 1U);
        const MetadataReadResult metadata =
            modelImportService_.ReadMetadataForModel(result.DestinationPath);
        modelImportSmokeMetadata_ = metadata.Succeeded &&
            std::filesystem::is_regular_file(
                result.DestinationPath.string() + ".vfmeta");
        if (metadata.Succeeded)
        {
            modelImportSmokeAssetId_ = metadata.Metadata.AssetId;
            if (metadata.Metadata.Thumbnail)
            {
                modelImportSmokeThumbnailPath_ =
                    modelImportService_.ProjectRoot() / "Cache" /
                    "Thumbnails" / metadata.Metadata.Thumbnail->File;
                ThumbnailImage thumbnail;
                std::string thumbnailError;
                modelImportSmokeThumbnailGenerated_ =
                    metadata.Metadata.Thumbnail->Status ==
                        ThumbnailStatus::Valid &&
                    std::filesystem::is_regular_file(
                        modelImportSmokeThumbnailPath_);
                modelImportSmokeThumbnailLoaded_ =
                    ReadThumbnailImage(modelImportSmokeThumbnailPath_,
                        thumbnail, thumbnailError) &&
                    thumbnail.Width == VoxThumbnailWidth &&
                    thumbnail.Height == VoxThumbnailHeight;
            }
        }
        modelImportSmokeRefreshed_ =
            assetBrowser_.RefreshCount() > refreshBaseline;
        if (modelImportSmokeImported_)
        {
            const std::filesystem::path relativeToAssets =
                result.DestinationPath.lexically_relative(
                    modelImportService_.ProjectRoot() / "Assets");
            modelImportSmokeRefreshed_ = modelImportSmokeRefreshed_ &&
                assetBrowser_.RevealEntry(relativeToAssets) &&
                assetBrowser_.SelectedRelativePath() == relativeToAssets;
            const auto visibleEntries = assetBrowser_.VisibleEntries();
            modelImportSmokeMetadata_ = modelImportSmokeMetadata_ &&
                std::none_of(
                    visibleEntries.begin(), visibleEntries.end(),
                    [](const AssetEntry* entry)
                    {
                        return entry && entry->Extension() == ".vfmeta";
                    });
            static_cast<void>(assetInspector_.UpdateSelection(
                assetBrowser_.SelectedEntry()));
            const AssetInspectorState& inspector = assetInspector_.State();
            modelImportSmokeInspected_ =
                inspector.Kind == AssetInspectorKind::VoxModel &&
                inspector.Analysis && inspector.Analysis->Valid &&
                !inspector.Dimensions.empty() &&
                !inspector.VoxelCount.empty() &&
                inspector.AssetId == modelImportSmokeAssetId_;
            modelImportSmokeInspected_ = modelImportSmokeInspected_ &&
                inspector.ThumbnailStatus == "valid" &&
                inspector.CanRevealThumbnail;
        }
    }
    else if (frame == 1U)
    {
        modelImportSmokeOpened_ = modelImportSmokeImported_ &&
            OpenVoxInViewportNow(modelImportSmokeDestination_);
        modelImportSmokeRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    else if (!importVisualSet && frame == 2U)
    {
        const AssetOperationResult renamed =
            assetBrowser_.RenameSelectedEntry("renamed-model.vox");
        if (renamed.Succeeded && renamed.ResultingRelativePath)
        {
            modelImportSmokeDestination_ =
                modelImportService_.ProjectRoot() / "Assets" /
                *renamed.ResultingRelativePath;
            const MetadataReadResult metadata =
                modelImportService_.ReadMetadataForModel(
                    modelImportSmokeDestination_);
            modelImportSmokeRenamed_ = metadata.Succeeded &&
                metadata.Metadata.AssetId == modelImportSmokeAssetId_ &&
                metadata.Metadata.SourceFile == "renamed-model.vox" &&
                std::filesystem::is_regular_file(
                    modelImportSmokeDestination_.string() + ".vfmeta");
            modelImportSmokeThumbnailPreserved_ = metadata.Succeeded &&
                metadata.Metadata.Thumbnail &&
                metadata.Metadata.Thumbnail->File ==
                    modelImportSmokeThumbnailPath_.filename().string() &&
                std::filesystem::is_regular_file(
                    modelImportSmokeThumbnailPath_);
            static_cast<void>(assetInspector_.UpdateSelection(
                assetBrowser_.SelectedEntry()));
            modelImportSmokeReanalyzed_ = assetInspector_.Reanalyze() &&
                assetInspector_.State().Name == "renamed-model.vox" &&
                assetInspector_.State().AssetId == modelImportSmokeAssetId_;
            modelImportSmokeThumbnailRegenerated_ =
                assetInspector_.RegenerateThumbnail() &&
                std::filesystem::is_regular_file(
                    modelImportSmokeThumbnailPath_);
        }
    }
    else if (!importVisualSet && frame == 3U)
    {
        const AssetOperationResult deleted = assetBrowser_.DeleteSelectedEntry();
        modelImportSmokeDeleted_ = deleted.Succeeded &&
            !std::filesystem::exists(modelImportSmokeDestination_) &&
            !std::filesystem::exists(
                modelImportSmokeDestination_.string() + ".vfmeta");
        modelImportSmokeThumbnailRemoved_ =
            !std::filesystem::exists(modelImportSmokeThumbnailPath_);
        static_cast<void>(assetInspector_.UpdateSelection(
            assetBrowser_.SelectedEntry()));
        modelImportSmokeInspectorCleared_ =
            assetInspector_.State().Kind == AssetInspectorKind::None;
        modelImportSmokeClean_ = true;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(
                 modelImportService_.ModelsDirectory(), error))
        {
            const std::string name = entry.path().filename().string();
            if (name.ends_with(".tmp") || name.ends_with(".bak"))
                modelImportSmokeClean_ = false;
        }
        modelImportSmokeClean_ = modelImportSmokeClean_ && !error;
        const std::filesystem::path thumbnailDirectory =
            modelImportService_.ProjectRoot() / "Cache" / "Thumbnails";
        for (const auto& entry : std::filesystem::directory_iterator(
                 thumbnailDirectory, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.ends_with(".tmp") || name.ends_with(".bak") ||
                VoxThumbnailService::IsRecognizedCacheFile(entry.path()))
                modelImportSmokeClean_ = false;
        }
        modelImportSmokeClean_ = modelImportSmokeClean_ && !error;
        resetLayoutRequested_ = true;
    }
    else if (importVisualSet && frame == 30U)
    {
        VoxThumbnailService thumbnailService;
        std::string thumbnailError;
        if (thumbnailService.SetProjectRoot(modelImportService_.ProjectRoot()))
            static_cast<void>(thumbnailService.RemoveByAssetId(
                modelImportSmokeAssetId_, thumbnailError));
        assetBrowser_.InvalidateThumbnails();
        static_cast<void>(assetInspector_.UpdateSelection(
            assetBrowser_.SelectedEntry()));
    }
    else if (importVisualSet && frame == 90U)
    {
        static_cast<void>(assetInspector_.RegenerateThumbnail());
        assetBrowser_.InvalidateThumbnails();
    }
    return ModelImportSmokePassed();
}

bool EditorWorkspace::ModelImportSmokePassed() const noexcept
{
    return modelImportSmokeImported_ && modelImportSmokeMetadata_ &&
        modelImportSmokeInspected_ && modelImportSmokeReanalyzed_ &&
        modelImportSmokeInspectorCleared_ &&
        modelImportSmokeThumbnailGenerated_ &&
        modelImportSmokeThumbnailLoaded_ &&
        modelImportSmokeThumbnailPreserved_ &&
        modelImportSmokeThumbnailRegenerated_ &&
        modelImportSmokeThumbnailRemoved_ &&
        modelImportSmokeRefreshed_ && modelImportSmokeOpened_ &&
        modelImportSmokeRenamed_ && modelImportSmokeDeleted_ &&
        modelImportSmokeClean_ && HasRenderedVoxelViewport() &&
        viewportRenderer_.ModelRenderCount() > modelImportSmokeRenderBaseline_;
}

bool EditorWorkspace::RunDragDropImportSmokeStep(
    const std::size_t frame,
    const std::vector<std::filesystem::path>& sourcePaths)
{
    if (sourcePaths.size() < 3U) return false;
    assetBrowserDropRect_ = {0.0F, 0.0F, 100.0F, 100.0F};
    viewportDropRect_ = {110.0F, 0.0F, 210.0F, 100.0F};
    const auto confirmDrop = [this]()
    {
        if (dragDropImport_.State() !=
            DragDropImportState::AwaitingConfirmation)
            return false;
        dragDropImport_.MarkImporting();
        showImportConfirmationPopup_ = false;
        std::vector<std::filesystem::path> paths =
            std::move(selectedImportPaths_);
        BeginModelImport(std::move(paths));
        return true;
    };

    if (frame == 0U)
    {
        const std::size_t refreshBaseline = assetBrowser_.RefreshCount();
        BeginFileDrop(50.0F, 50.0F);
        AddDroppedFile(sourcePaths[0], 50.0F, 50.0F);
        CompleteFileDrop(50.0F, 50.0F);
        if (!confirmDrop()) return false;
        const auto imported = modelImportService_.ModelsDirectory() /
            sourcePaths[0].filename();
        dragDropSmokeImportedPaths_.push_back(imported);
        const MetadataReadResult metadata =
            modelImportService_.ReadMetadataForModel(imported);
        dragDropSmokeAssetImported_ =
            std::filesystem::is_regular_file(imported) &&
            metadata.Succeeded && metadata.Metadata.Analysis &&
            metadata.Metadata.Analysis->Valid && metadata.Metadata.Thumbnail &&
            metadata.Metadata.Thumbnail->Status == ThumbnailStatus::Valid;
        dragDropSmokeAssetDidNotOpen_ = !viewportState_.HasModel();
        dragDropSmokeRefreshControlled_ =
            assetBrowser_.RefreshCount() == refreshBaseline + 1U;
        static_cast<void>(assetInspector_.UpdateSelection(
            assetBrowser_.SelectedEntry()));
        dragDropSmokeInspected_ =
            assetInspector_.State().Kind == AssetInspectorKind::VoxModel &&
            assetInspector_.State().ThumbnailStatus == "valid";
    }
    else if (frame == 1U)
    {
        const std::size_t refreshBaseline = assetBrowser_.RefreshCount();
        BeginFileDrop(150.0F, 50.0F);
        AddDroppedFile(sourcePaths[1], 150.0F, 50.0F);
        CompleteFileDrop(150.0F, 50.0F);
        if (!confirmDrop()) return false;
        const auto imported = modelImportService_.ModelsDirectory() /
            sourcePaths[1].filename();
        dragDropSmokeImportedPaths_.push_back(imported);
        dragDropSmokeViewportOpened_ =
            viewportState_.HasModel() &&
            std::filesystem::is_regular_file(imported);
        dragDropSmokeRefreshControlled_ =
            dragDropSmokeRefreshControlled_ &&
            assetBrowser_.RefreshCount() == refreshBaseline + 1U;
    }
    else if (frame == 2U)
    {
        const std::size_t refreshBaseline = assetBrowser_.RefreshCount();
        BeginFileDrop(150.0F, 50.0F);
        AddDroppedFile(sourcePaths[2], 150.0F, 50.0F);
        CompleteFileDrop(150.0F, 50.0F);
        if (!confirmDrop() || !pendingImportCollision_) return false;
        showImportCollisionPopup_ = false;
        ContinueModelImport(ModelImportCollisionAction::Rename);
        const auto renamed = modelImportService_.ModelsDirectory() /
            (sourcePaths[2].stem().string() + " (1).vox");
        dragDropSmokeImportedPaths_.push_back(renamed);
        dragDropSmokeCollisionRenamed_ =
            std::filesystem::is_regular_file(renamed) &&
            assetBrowser_.SelectedRelativePath() ==
                renamed.lexically_relative(
                    modelImportService_.ProjectRoot() / "Assets");
        dragDropSmokeRefreshControlled_ =
            dragDropSmokeRefreshControlled_ &&
            assetBrowser_.RefreshCount() == refreshBaseline + 1U;
    }
    else if (frame == 6U)
    {
        dragDropSmokeClean_ = true;
        for (auto iterator = dragDropSmokeImportedPaths_.rbegin();
             iterator != dragDropSmokeImportedPaths_.rend(); ++iterator)
        {
            const auto relative = iterator->lexically_relative(
                modelImportService_.ProjectRoot() / "Assets");
            if (!assetBrowser_.RevealEntry(relative) ||
                !assetBrowser_.DeleteSelectedEntry().Succeeded)
                dragDropSmokeClean_ = false;
        }
        std::error_code error;
        const auto thumbnailDirectory = modelImportService_.ProjectRoot() /
            "Cache" / "Thumbnails";
        for (const auto& entry : std::filesystem::directory_iterator(
                 thumbnailDirectory, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.ends_with(".tmp") || name.ends_with(".bak") ||
                VoxThumbnailService::IsRecognizedCacheFile(entry.path()))
                dragDropSmokeClean_ = false;
        }
        dragDropSmokeClean_ = dragDropSmokeClean_ && !error;
        dragDropSmokeClean_ = dragDropSmokeClean_ &&
            projectManager_.HasActiveProject();
    }
    return DragDropImportSmokePassed();
}

bool EditorWorkspace::DragDropImportSmokePassed() const noexcept
{
    return dragDropSmokeAssetImported_ && dragDropSmokeAssetDidNotOpen_ &&
        dragDropSmokeInspected_ && dragDropSmokeViewportOpened_ &&
        dragDropSmokeCollisionRenamed_ &&
        dragDropSmokeRefreshControlled_ && dragDropSmokeClean_;
}

bool EditorWorkspace::RunVoxelDocumentSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    using Asset::Voxel::VoxelDocument;
    using Asset::Voxel::VoxelPosition;

    if (frame == 0U)
    {
        std::error_code error;
        voxelDocumentSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        voxelDocumentSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        if (error) return false;
        const auto hash = HashFileContents(sourcePath);
        if (!hash) return false;
        voxelDocumentSmokeSourceHash_ = *hash;
        const VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        voxelDocumentSmokeInitialState_ = document != nullptr &&
            document->SourcePath() ==
                std::filesystem::weakly_canonical(sourcePath, error) &&
            !error && document->GetModelCount() == 1U &&
            document->GetVoxelCount() == 7U &&
            document->HasVoxel({1, 1, 1}) && !document->IsDirty() &&
            document->GetRevision() == 0U;
    }
    else if (frame == 1U)
    {
        VoxelDocument* document = voxelDocumentSession_.ActiveDocument();
        if (document == nullptr) return false;
        const VoxelPosition editPosition{0, 0, 0};
        const auto added = document->SetVoxel(editPosition, 8U);
        const auto replaced = document->ReplaceVoxelColor(editPosition, 9U);
        const auto removed = document->RemoveVoxel(editPosition);
        voxelDocumentSmokeEdited_ = added.Changed && replaced.Changed &&
            removed.Changed && document->GetVoxelCount() == 7U &&
            !document->HasVoxel(editPosition) && document->IsDirty() &&
            document->GetRevision() == 3U;
    }
    else if (frame == 2U)
    {
        VoxelDocument* document = voxelDocumentSession_.ActiveDocument();
        if (document == nullptr) return false;
        const std::uint64_t revision = document->GetRevision();
        document->MarkSaved();
        voxelDocumentSmokeSaved_ = !document->IsDirty() &&
            document->GetRevision() == revision && revision == 3U;
    }
    else if (frame == 3U)
    {
        voxelDocumentSession_.Close();
        voxelDocumentSmokeClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            voxelDocumentSession_.ActiveDocument() == nullptr;
    }
    else if (frame == 4U)
    {
        std::error_code error;
        const std::uintmax_t size =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        const auto modified =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        voxelDocumentSmokeSourcePreserved_ = !error &&
            size == voxelDocumentSmokeSourceSize_ &&
            modified == voxelDocumentSmokeSourceTime_ && hash &&
            *hash == voxelDocumentSmokeSourceHash_;
    }
    return VoxelDocumentSmokePassed();
}

bool EditorWorkspace::VoxelDocumentSmokePassed() const noexcept
{
    return voxelDocumentSmokeInitialState_ && voxelDocumentSmokeEdited_ &&
        voxelDocumentSmokeSaved_ && voxelDocumentSmokeClosed_ &&
        voxelDocumentSmokeSourcePreserved_ &&
        !voxelDocumentSession_.HasActiveDocument();
}

bool EditorWorkspace::RunVoxelRenderSyncSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        if (document == nullptr) return false;
        std::error_code error;
        voxelRenderSyncSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        voxelRenderSyncSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        if (error || !hash) return false;
        voxelRenderSyncSmokeSourceHash_ = *hash;
        voxelRenderSyncInitialBuildCount_ =
            voxelDocumentMeshCache_.BuildCount();
        voxelRenderSyncInitialUploadCount_ =
            viewportRenderer_.ModelUploadCount();
        voxelRenderSyncInitialBuilt_ =
            voxelDocumentMeshCache_.HasMesh() &&
            viewportRenderer_.HasModelMesh() &&
            voxelDocumentMeshCache_.DocumentRevision() == 0U &&
            uploadedDocumentRevision_ == 0U &&
            viewportState_.Statistics().TriangleCount == 60U;
    }
    else if (frame == 1U)
    {
        if (document == nullptr) return false;
        voxelRenderSyncSetRebuilt_ =
            document->SetVoxel({0, 0, 0}, 8U).Changed;
    }
    else if (frame == 2U)
    {
        if (document == nullptr) return false;
        voxelRenderSyncSetRebuilt_ = voxelRenderSyncSetRebuilt_ &&
            document->GetRevision() == 1U &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelRenderSyncInitialBuildCount_ + 1U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelRenderSyncInitialUploadCount_ + 1U &&
            uploadedDocumentRevision_ == 1U;
        voxelRenderSyncRemoveRebuilt_ =
            document->RemoveVoxel({0, 0, 0}).Changed;
    }
    else if (frame == 3U)
    {
        if (document == nullptr) return false;
        voxelRenderSyncRemoveRebuilt_ = voxelRenderSyncRemoveRebuilt_ &&
            document->GetRevision() == 2U &&
            document->GetVoxelCount() == 7U &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelRenderSyncInitialBuildCount_ + 2U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelRenderSyncInitialUploadCount_ + 2U &&
            uploadedDocumentRevision_ == 2U;
        voxelRenderSyncStableBuildCount_ =
            voxelDocumentMeshCache_.BuildCount();
    }
    else if (frame == 5U)
    {
        voxelRenderSyncUnchangedSkipped_ =
            voxelDocumentMeshCache_.BuildCount() ==
                voxelRenderSyncStableBuildCount_ &&
            viewportRenderer_.ModelUploadCount() ==
                voxelRenderSyncInitialUploadCount_ + 2U;
    }
    else if (frame == 6U)
    {
        std::error_code error;
        const auto hash = HashFileContents(sourcePath);
        voxelRenderSyncSourcePreserved_ = hash &&
            *hash == voxelRenderSyncSmokeSourceHash_ &&
            std::filesystem::file_size(sourcePath, error) ==
                voxelRenderSyncSmokeSourceSize_ && !error &&
            std::filesystem::last_write_time(sourcePath, error) ==
                voxelRenderSyncSmokeSourceTime_ && !error;
        ClearVoxelViewport();
        voxelRenderSyncClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportState_.HasModel();
    }
    return VoxelRenderSyncSmokePassed();
}

bool EditorWorkspace::VoxelRenderSyncSmokePassed() const noexcept
{
    return voxelRenderSyncInitialBuilt_ && voxelRenderSyncSetRebuilt_ &&
        voxelRenderSyncRemoveRebuilt_ &&
        voxelRenderSyncUnchangedSkipped_ && voxelRenderSyncClosed_ &&
        voxelRenderSyncSourcePreserved_;
}

bool EditorWorkspace::RunVoxelRayPickingSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    constexpr ViewportRectangle SmokeViewport{100.0F, 50.0F, 640.0F, 480.0F};
    if (frame == 0U)
    {
        if (document == nullptr) return false;
        std::error_code error;
        voxelRayPickingSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        voxelRayPickingSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        if (error || !hash) return false;
        voxelRayPickingSmokeSourceHash_ = *hash;
        voxelRayPickingInitialRevision_ = document->GetRevision();
        voxelRayPickingInitialDirty_ = document->IsDirty();
        voxelRayPickingHighlightUploadBaseline_ =
            viewportRenderer_.HighlightUploadCount();

        viewportCamera_.Frame(3.0F, 3.0F, 3.0F);
        viewportCamera_.SetView(EditorCameraView::Front);
        viewportCamera_.SetAspectRatio(4.0F / 3.0F);
        const ViewportRayBuildResult ray = BuildViewportRay(
            {420.0F, 290.0F}, SmokeViewport,
            viewportCamera_.GetViewProjection(), viewportCamera_.GetPosition());
        voxelRayPickingRayBuilt_ = ray.Succeeded() &&
            std::abs(Length(ray.Ray->Direction) - 1.0F) <= 0.001F;
        if (!voxelRayPickingRayBuilt_) return false;
        VoxelRaycastOptions options;
        options.Transform = CenteredVoxelModelTransform(voxelModelCenter_);
        const auto hit = RaycastVoxelDocument(*document, *ray.Ray, options);
        voxelRayPickingHitVerified_ = hit &&
            hit->Coordinates == VoxelCoordinates{1U, 1U, 2U} &&
            hit->Face == VoxelHitFace::PositiveZ &&
            hit->AdjacentPosition == Asset::Voxel::VoxelPosition{1, 1, 3} &&
            !hit->AdjacentWithinBounds && std::isfinite(hit->Distance) &&
            hit->Distance > 0.0F;
        if (!voxelRayPickingHitVerified_) return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
    }
    else if (frame == 1U)
    {
        if (document == nullptr) return false;
        voxelRayPickingHighlightRendered_ =
            viewportRenderer_.HighlightUploadCount() >
                voxelRayPickingHighlightUploadBaseline_ &&
            viewportRenderer_.HighlightRenderCount() > 0U;
        voxelRayPickingDocumentUnchanged_ =
            document->GetRevision() == voxelRayPickingInitialRevision_ &&
            document->IsDirty() == voxelRayPickingInitialDirty_;
        const ViewportRayBuildResult emptyRay = BuildViewportRay(
            {100.0F, 50.0F}, SmokeViewport,
            viewportCamera_.GetViewProjection(), viewportCamera_.GetPosition());
        if (!emptyRay.Succeeded()) return false;
        VoxelRaycastOptions options;
        options.Transform = CenteredVoxelModelTransform(voxelModelCenter_);
        const auto miss = RaycastVoxelDocument(
            *document, *emptyRay.Ray, options);
        if (miss) return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::NoHit));
        UpdateVoxelHighlights();
    }
    else if (frame == 2U)
    {
        if (document == nullptr) return false;
        voxelRayPickingMissCleared_ = !voxelSelection_.Hovered() &&
            !viewportRenderer_.HasHighlightMesh();
        voxelRayPickingDocumentUnchanged_ =
            voxelRayPickingDocumentUnchanged_ &&
            document->GetRevision() == voxelRayPickingInitialRevision_ &&
            document->IsDirty() == voxelRayPickingInitialDirty_;
        std::error_code error;
        const auto hash = HashFileContents(sourcePath);
        voxelRayPickingSourcePreserved_ = hash &&
            *hash == voxelRayPickingSmokeSourceHash_ &&
            std::filesystem::file_size(sourcePath, error) ==
                voxelRayPickingSmokeSourceSize_ && !error &&
            std::filesystem::last_write_time(sourcePath, error) ==
                voxelRayPickingSmokeSourceTime_ && !error;
        ClearVoxelViewport();
        voxelRayPickingClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel();
    }
    return VoxelRayPickingSmokePassed();
}

bool EditorWorkspace::VoxelRayPickingSmokePassed() const noexcept
{
    return voxelRayPickingRayBuilt_ && voxelRayPickingHitVerified_ &&
        voxelRayPickingHighlightRendered_ && voxelRayPickingMissCleared_ &&
        voxelRayPickingDocumentUnchanged_ && voxelRayPickingClosed_ &&
        voxelRayPickingSourcePreserved_;
}

bool EditorWorkspace::RunVoxelPencilSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    const auto inputFrame = [this, document](const bool leftDown)
    {
        return VoxelPencilInputFrame{
            leftDown,
            voxelToolState_.IsPencilActive(),
            document != nullptr,
            true,
            true,
            false,
            false,
            false,
            VoxelCameraInteraction::None,
            voxelEditInProgress_,
            voxelDocumentSession_.Generation()};
    };

    if (frame == 0U)
    {
        if (document == nullptr || grid == nullptr ||
            document->GetVoxelCount() != 1U) return false;
        std::error_code error;
        voxelPencilSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        voxelPencilSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        if (error || !hash) return false;
        voxelPencilSmokeSourceHash_ = *hash;
        voxelPencilSmokeInitialRevision_ = document->GetRevision();
        voxelPencilSmokeInitialVoxelCount_ = document->GetVoxelCount();
        voxelPencilSmokeInitialBuildCount_ =
            voxelDocumentMeshCache_.BuildCount();
        voxelPencilSmokeInitialUploadCount_ =
            viewportRenderer_.ModelUploadCount();
        voxelPencilSmokeHighlightUploadBaseline_ =
            viewportRenderer_.HighlightUploadCount();
        voxelPencilSmokeRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();

        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        static_cast<void>(voxelToolState_.SetActivePaletteIndex(1U));
        VoxelRaycastOptions options;
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}},
            options);
        if (!hit || hit->AdjacentPosition !=
                Asset::Voxel::VoxelPosition{0, 1, 1}) return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        voxelPencilSmokeTarget_ = hit->AdjacentPosition;
        voxelPencilSmokePreviewValid_ =
            voxelPlacementPreview_.IsValid() &&
            voxelPlacementPreview_.Position == hit->AdjacentPosition &&
            GetBackendDisplayName() == "Direct3D 12" &&
            viewportRenderer_.HasModelMesh();
        voxelToolSmokeInput_.Reset();
    }
    else if (frame == 1U)
    {
        if (document == nullptr || grid == nullptr) return false;
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit) return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        const bool firstClick = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelPencilInputDecision::Apply;
        const bool applied = firstClick && ApplyVoxelPencil();
        const auto voxel = document->GetVoxel(voxelPencilSmokeTarget_);
        const Voxel::Voxel* compatible = grid->Get(
            static_cast<std::uint32_t>(voxelPencilSmokeTarget_.X),
            static_cast<std::uint32_t>(voxelPencilSmokeTarget_.Y),
            static_cast<std::uint32_t>(voxelPencilSmokeTarget_.Z));
        voxelPencilSmokeApplied_ = applied && voxel &&
            voxel->PaletteIndex == 1U && compatible &&
            compatible->IsOccupied() && compatible->ColorIndex == 1U &&
            document->IsDirty() &&
            document->GetRevision() == voxelPencilSmokeInitialRevision_ + 1U &&
            document->GetVoxelCount() == voxelPencilSmokeInitialVoxelCount_ + 1U &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelPencilSmokeInitialBuildCount_ + 1U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelPencilSmokeInitialUploadCount_ + 1U &&
            viewportRenderer_.HighlightUploadCount() >
                voxelPencilSmokeHighlightUploadBaseline_ &&
            viewportRenderer_.HighlightRenderCount() > 0U &&
            viewportRenderer_.HasModelMesh() && lastVoxelToolResult_ &&
            lastVoxelToolResult_->Code == VoxelToolResultCode::Applied;
    }
    else if (frame == 2U)
    {
        if (document == nullptr) return false;
        const std::uint64_t revision = document->GetRevision();
        const std::uint64_t count = document->GetVoxelCount();
        const std::size_t builds = voxelDocumentMeshCache_.BuildCount();
        const std::size_t uploads = viewportRenderer_.ModelUploadCount();
        const bool repeated = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelPencilInputDecision::Apply;
        voxelPencilSmokeHeldWithoutRepeat_ = !repeated &&
            document->GetRevision() == revision &&
            document->GetVoxelCount() == count &&
            voxelDocumentMeshCache_.BuildCount() == builds &&
            viewportRenderer_.ModelUploadCount() == uploads;
        voxelPencilSmokeRendered_ =
            viewportRenderer_.ModelRenderCount() >
                voxelPencilSmokeRenderBaseline_ &&
            viewportRenderer_.HasModelMesh();
    }
    else if (frame == 3U)
    {
        static_cast<void>(voxelToolSmokeInput_.Update(inputFrame(false)));
    }
    else if (frame == 4U)
    {
        if (document == nullptr) return false;
        VoxelRaycastHit outside;
        outside.Coordinates = {0U, 1U, 1U};
        outside.Face = VoxelHitFace::NegativeX;
        outside.SubModelIndex = 0U;
        outside.AdjacentPosition = {-1, 1, 1};
        outside.AdjacentWithinBounds = false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, outside));
        UpdateVoxelHighlights();
        const std::uint64_t revision = document->GetRevision();
        const std::uint64_t count = document->GetVoxelCount();
        const std::size_t builds = voxelDocumentMeshCache_.BuildCount();
        const std::size_t uploads = viewportRenderer_.ModelUploadCount();
        const bool click = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelPencilInputDecision::Apply;
        const bool applied = click && ApplyVoxelPencil();
        voxelPencilSmokeOutOfBoundsRefused_ = !applied &&
            lastVoxelToolResult_ && lastVoxelToolResult_->Code ==
                VoxelToolResultCode::TargetOutOfBounds &&
            voxelPlacementPreview_.Status ==
                VoxelPlacementPreviewStatus::OutOfBounds &&
            document->GetRevision() == revision &&
            document->GetVoxelCount() == count &&
            voxelDocumentMeshCache_.BuildCount() == builds &&
            viewportRenderer_.ModelUploadCount() == uploads;
    }
    else if (frame == 5U)
    {
        if (document == nullptr) return false;
        std::error_code error;
        const auto hash = HashFileContents(sourcePath);
        voxelPencilSmokeSourcePreserved_ = hash &&
            *hash == voxelPencilSmokeSourceHash_ &&
            std::filesystem::file_size(sourcePath, error) ==
                voxelPencilSmokeSourceSize_ && !error &&
            std::filesystem::last_write_time(sourcePath, error) ==
                voxelPencilSmokeSourceTime_ && !error;
        ClearVoxelViewport();
        voxelPencilSmokeClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() &&
            !voxelToolInput_.WaitingForRelease() &&
            !voxelToolSmokeInput_.WaitingForRelease();
    }
    return VoxelPencilSmokePassed();
}

bool EditorWorkspace::VoxelPencilSmokePassed() const noexcept
{
    return voxelPencilSmokePreviewValid_ && voxelPencilSmokeApplied_ &&
        voxelPencilSmokeHeldWithoutRepeat_ &&
        voxelPencilSmokeOutOfBoundsRefused_ && voxelPencilSmokeRendered_ &&
        voxelPencilSmokeClosed_ && voxelPencilSmokeSourcePreserved_;
}

bool EditorWorkspace::RunVoxelEraserSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    const auto inputFrame = [this, document](const bool leftDown)
    {
        return VoxelToolInputFrame{
            leftDown,
            voxelToolState_.IsEditingToolActive(),
            document != nullptr,
            true,
            true,
            false,
            false,
            false,
            VoxelCameraInteraction::None,
            voxelEditInProgress_,
            voxelDocumentSession_.Generation()};
    };

    if (frame == 0U)
    {
        if (document == nullptr || grid == nullptr ||
            document->GetVoxelCount() != 1U) return false;
        std::error_code error;
        voxelEraserSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        if (error) return false;
        voxelEraserSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        if (error || !hash) return false;
        voxelEraserSmokeSourceHash_ = *hash;
        voxelEraserSmokeInitialRevision_ = document->GetRevision();
        voxelEraserSmokeInitialVoxelCount_ = document->GetVoxelCount();
        voxelEraserSmokeInitialBuildCount_ =
            voxelDocumentMeshCache_.BuildCount();
        voxelEraserSmokeInitialUploadCount_ =
            viewportRenderer_.ModelUploadCount();
        voxelEraserSmokeHighlightUploadBaseline_ =
            viewportRenderer_.HighlightUploadCount();
        voxelEraserSmokeRenderBaseline_ =
            viewportRenderer_.ModelRenderCount();

        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        static_cast<void>(voxelToolState_.SetActivePaletteIndex(1U));
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit || hit->AdjacentPosition !=
                Asset::Voxel::VoxelPosition{0, 1, 1}) return false;
        voxelEraserSmokeAddedTarget_ = hit->AdjacentPosition;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        const bool added = ApplyVoxelPencil();
        voxelEraserSmokePencilAdded_ = added &&
            document->HasVoxel(voxelEraserSmokeAddedTarget_) &&
            document->GetRevision() == voxelEraserSmokeInitialRevision_ + 1U &&
            document->GetVoxelCount() == voxelEraserSmokeInitialVoxelCount_ + 1U &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelEraserSmokeInitialBuildCount_ + 1U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelEraserSmokeInitialUploadCount_ + 1U;
    }
    else if (frame == 1U)
    {
        if (document == nullptr || !voxelEraserSmokePencilAdded_) return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Eraser);
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit || hit->Coordinates != VoxelCoordinates{0U, 1U, 1U})
            return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        voxelEraserSmokePreviewValid_ =
            voxelPlacementPreview_.IsValid() &&
            voxelPlacementPreview_.Tool == VoxelPreviewTool::Eraser &&
            voxelPlacementPreview_.Position == voxelEraserSmokeAddedTarget_ &&
            GetBackendDisplayName() == "Direct3D 12" &&
            viewportRenderer_.HasModelMesh();
        voxelToolSmokeInput_.Reset();
    }
    else if (frame == 2U)
    {
        if (document == nullptr || grid == nullptr) return false;
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit) return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        const bool firstClick = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelToolInputDecision::Apply;
        const bool removed = firstClick && ApplyVoxelEraser();
        const Voxel::Voxel* compatible = grid->Get(
            static_cast<std::uint32_t>(voxelEraserSmokeAddedTarget_.X),
            static_cast<std::uint32_t>(voxelEraserSmokeAddedTarget_.Y),
            static_cast<std::uint32_t>(voxelEraserSmokeAddedTarget_.Z));
        voxelEraserSmokeRemovedPaletteIndex_ = lastVoxelEraserResult_
            ? lastVoxelEraserResult_->RemovedPaletteIndex : 0U;
        voxelEraserSmokeRemoved_ = removed &&
            !document->HasVoxel(voxelEraserSmokeAddedTarget_) &&
            compatible && !compatible->IsOccupied() && document->IsDirty() &&
            voxelEraserSmokeRemovedPaletteIndex_ == 1U &&
            document->GetRevision() == voxelEraserSmokeInitialRevision_ + 2U &&
            document->GetVoxelCount() == voxelEraserSmokeInitialVoxelCount_ &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelEraserSmokeInitialBuildCount_ + 2U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelEraserSmokeInitialUploadCount_ + 2U &&
            viewportRenderer_.HasModelMesh() && lastVoxelEraserResult_ &&
            lastVoxelEraserResult_->Code == VoxelEraserResultCode::Applied;
        voxelEraserSmokeRendered_ =
            viewportRenderer_.ModelRenderCount() >
                voxelEraserSmokeRenderBaseline_ &&
            viewportRenderer_.HighlightUploadCount() >
                voxelEraserSmokeHighlightUploadBaseline_ &&
            viewportRenderer_.HighlightRenderCount() > 0U;
    }
    else if (frame == 3U)
    {
        if (document == nullptr) return false;
        const std::uint64_t revision = document->GetRevision();
        const std::uint64_t count = document->GetVoxelCount();
        const std::size_t builds = voxelDocumentMeshCache_.BuildCount();
        const std::size_t uploads = viewportRenderer_.ModelUploadCount();
        const bool repeated = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelToolInputDecision::Apply;
        voxelEraserSmokeHeldWithoutRepeat_ = !repeated &&
            document->GetRevision() == revision &&
            document->GetVoxelCount() == count &&
            voxelDocumentMeshCache_.BuildCount() == builds &&
            viewportRenderer_.ModelUploadCount() == uploads;
    }
    else if (frame == 4U)
    {
        static_cast<void>(voxelToolSmokeInput_.Update(inputFrame(false)));
    }
    else if (frame == 5U)
    {
        if (document == nullptr || document->GetVoxelCount() != 1U)
            return false;
        const auto hit = RaycastVoxelDocument(
            *document,
            {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit || hit->Coordinates != VoxelCoordinates{1U, 1U, 1U})
            return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        UpdateVoxelHighlights();
        const bool click = voxelToolSmokeInput_.Update(
            inputFrame(true)) == VoxelToolInputDecision::Apply;
        const bool removed = click && ApplyVoxelEraser();
        const auto bounds = document->GetBounds();
        const Mesh::MeshData* mesh = voxelDocumentMeshCache_.Mesh();
        voxelEraserSmokeLastRemoved_ = removed &&
            document->GetVoxelCount() == 0U && bounds && !bounds->HasValue &&
            document->GetRevision() == voxelEraserSmokeInitialRevision_ + 3U &&
            voxelDocumentMeshCache_.BuildCount() ==
                voxelEraserSmokeInitialBuildCount_ + 3U &&
            viewportRenderer_.ModelUploadCount() ==
                voxelEraserSmokeInitialUploadCount_ + 3U &&
            mesh != nullptr && mesh->Empty() &&
            !viewportRenderer_.HasModelMesh();
    }
    else if (frame == 6U)
    {
        if (document == nullptr) return false;
        std::error_code error;
        const auto hash = HashFileContents(sourcePath);
        voxelEraserSmokeSourcePreserved_ = hash &&
            *hash == voxelEraserSmokeSourceHash_ &&
            std::filesystem::file_size(sourcePath, error) ==
                voxelEraserSmokeSourceSize_ && !error &&
            std::filesystem::last_write_time(sourcePath, error) ==
                voxelEraserSmokeSourceTime_ && !error;
        ClearVoxelViewport();
        voxelEraserSmokeClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() &&
            !voxelToolInput_.WaitingForRelease() &&
            !voxelToolSmokeInput_.WaitingForRelease();
    }
    return VoxelEraserSmokePassed();
}

bool EditorWorkspace::VoxelEraserSmokePassed() const noexcept
{
    return voxelEraserSmokePencilAdded_ && voxelEraserSmokePreviewValid_ &&
        voxelEraserSmokeRemoved_ && voxelEraserSmokeHeldWithoutRepeat_ &&
        voxelEraserSmokeRendered_ && voxelEraserSmokeLastRemoved_ &&
        voxelEraserSmokeClosed_ && voxelEraserSmokeSourcePreserved_;
}

bool EditorWorkspace::RunQualityOfLifeSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& parentDirectory)
{
    if (frame == 0U)
    {
        CloseProject();
        RequestNewProjectDialog();
        constexpr std::string_view Name = "QualityOfLifeCreated";
        std::copy(Name.begin(), Name.end(), newProjectName_.begin());
        if (!fileDialogService_->ChooseProjectParentFolder(parentDirectory) ||
            !fileDialogService_->InjectSimulatedResult({
                FileDialogStatus::Success,
                FileDialogKind::ProjectParentFolder,
                parentDirectory,
                {}}))
            return false;
    }
    else if (frame == 1U)
    {
        ConsumeFileDialogResult();
        CreateProjectNow();
        if (!projectManager_.HasActiveProject()) return false;
        const std::filesystem::path projectFile =
            projectManager_.ActiveProject()->ProjectFilePath();
        RequestOpenProjectDialog();
        if (!fileDialogService_->ChooseProjectFile(parentDirectory) ||
            !fileDialogService_->InjectSimulatedResult({
                FileDialogStatus::Success,
                FileDialogKind::ProjectFile,
                projectFile,
                {}}))
            return false;
    }
    else if (frame == 2U)
    {
        ConsumeFileDialogResult();
        RequestOpenProject(openProjectFilePath_.data(), false);
        if (!projectManager_.HasActiveProject()) return false;
        voxelSaveState_.MarkModified();
        RequestCloseProject();
        if (!dirtyActionConfirmation_.IsPending()) return false;
    }
    else if (frame == 3U)
    {
        dirtyActionConfirmation_.Cancel();
        if (!projectManager_.HasActiveProject() || !voxelSaveState_.IsDirty())
            return false;
        RequestCloseProject();
        if (!dirtyActionConfirmation_.IsPending()) return false;
    }
    else if (frame == 4U)
    {
        const auto action = dirtyActionConfirmation_.Discard();
        if (!action) return false;
        ExecutePendingDirtyAction(*action);
        qualityOfLifeSmokePassed_ =
            !projectManager_.HasActiveProject() && !voxelSaveState_.IsDirty();
    }
    return qualityOfLifeSmokePassed_;
}

bool EditorWorkspace::QualityOfLifeSmokePassed() const noexcept
{
    return qualityOfLifeSmokePassed_;
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

bool EditorWorkspace::PaintSelectedVoxel()
{
    if (!activeVoxelModel_)
    {
        AddConsoleMessage("Paint failed: no voxel model is loaded.");
        return false;
    }
    const std::optional<VoxelRaycastHit> selected = voxelSelection_.Selected();
    if (!selected)
    {
        AddConsoleMessage("Paint failed: no voxel is selected.");
        return false;
    }

    const VoxelCoordinates coordinates = selected->Coordinates;
    const std::uint8_t colorIndex = paintPaletteSelection_.Index();
    // Paint v1 deliberately uses the shared full mesh rebuild because palette
    // indices are currently baked into mesh vertices.
    CommandResult result = commandHistory_.Execute(
        std::make_unique<PaintVoxelCommand>(
            static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
            coordinates.X, coordinates.Y, coordinates.Z, colorIndex));
    if (!result)
    {
        AddConsoleMessage("Paint failed: " + result.Message);
        return false;
    }

    AddConsoleMessage(
        "Painted voxel " + std::to_string(coordinates.X) + ", " +
        std::to_string(coordinates.Y) + ", " +
        std::to_string(coordinates.Z) + " with color " +
        std::to_string(colorIndex));
    return true;
}

bool EditorWorkspace::AddAdjacentVoxel()
{
    Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    const AddVoxelTarget target =
        FindAddVoxelTarget(grid, voxelSelection_.Selected());
    if (!target)
    {
        AddConsoleMessage(
            std::string("Add voxel failed: ") +
            AddVoxelTargetStatusMessage(target.Status));
        return false;
    }

    const VoxelCoordinates destination = *target.Coordinates;
    CommandResult result = commandHistory_.Execute(
        std::make_unique<AddVoxelCommand>(
            static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
            destination.X, destination.Y, destination.Z,
            paintPaletteSelection_.Index()));
    if (!result)
    {
        AddConsoleMessage("Add voxel failed: " + result.Message);
        return false;
    }

    // The shared edit completion rule clears hover and selection after every
    // successful Execute/Undo/Redo. This is the safest deterministic v1 rule.
    AddConsoleMessage(
        "Added voxel: " + std::to_string(destination.X) + ", " +
        std::to_string(destination.Y) + ", " +
        std::to_string(destination.Z));
    return true;
}

bool EditorWorkspace::ApplyVoxelPencil()
{
    if (voxelEditInProgress_) return false;
    voxelEditInProgress_ = true;
    VoxelToolResult result;
    try
    {
        result = VoxelPencilTool::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            voxelToolState_.ActivePaletteIndex(),
            !voxelToolState_.IsPencilActive()});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelToolResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelToolResultCode::Failed;
        result.Error = "Unknown Pencil failure.";
    }
    voxelEditInProgress_ = false;
    lastVoxelToolResult_ = result;

    if (result.Code == VoxelToolResultCode::Applied)
    {
        AddConsoleMessage(
            "[Edit] Added voxel at (" +
            std::to_string(result.Position.X) + ", " +
            std::to_string(result.Position.Y) + ", " +
            std::to_string(result.Position.Z) +
            ") using palette index " +
            std::to_string(voxelToolState_.ActivePaletteIndex()) + ".");
        return true;
    }
    if (result.Code == VoxelToolResultCode::Failed)
    {
        AddConsoleMessage(
            "[Edit] Failed to add voxel at (" +
            std::to_string(result.Position.X) + ", " +
            std::to_string(result.Position.Y) + ", " +
            std::to_string(result.Position.Z) + "): " + result.Error);
    }
    return false;
}

bool EditorWorkspace::ApplyVoxelEraser()
{
    if (voxelEditInProgress_) return false;
    voxelEditInProgress_ = true;
    VoxelEraserResult result;
    try
    {
        result = VoxelEraserTool::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            !voxelToolState_.IsEraserActive()});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelEraserResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelEraserResultCode::Failed;
        result.Error = "Unknown Eraser failure.";
    }
    voxelEditInProgress_ = false;
    lastVoxelEraserResult_ = result;

    if (result.Code == VoxelEraserResultCode::Applied)
    {
        AddConsoleMessage(
            "[Edit] Removed voxel at (" +
            std::to_string(result.Position.X) + ", " +
            std::to_string(result.Position.Y) + ", " +
            std::to_string(result.Position.Z) + ").");
        return true;
    }
    if (result.Code == VoxelEraserResultCode::Failed)
    {
        AddConsoleMessage(
            "[Edit] Failed to remove voxel at (" +
            std::to_string(result.Position.X) + ", " +
            std::to_string(result.Position.Y) + ", " +
            std::to_string(result.Position.Z) + "): " + result.Error);
    }
    return false;
}

std::uint64_t EditorWorkspace::VoxelModelGeneration() const noexcept
{
    return voxelModelGeneration_;
}

Voxel::VoxelModel* EditorWorkspace::ActiveVoxelModel() noexcept
{
    return activeVoxelModel_ ? &*activeVoxelModel_ : nullptr;
}

Asset::Voxel::VoxelDocument*
EditorWorkspace::ActiveVoxelDocument() noexcept
{
    return voxelDocumentSession_.ActiveDocument();
}

bool EditorWorkspace::SynchronizeVoxelDocumentRendering()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr)
    {
        if (voxelDocumentMeshCache_.HasMesh()) ClearVoxelViewport();
        return true;
    }

    const std::uint64_t identity = voxelDocumentSession_.Generation();
    const std::uint64_t revision = document->GetRevision();
    const Mesh::VoxelDocumentMeshSyncResult synchronized =
        voxelDocumentMeshCache_.Synchronize(*document, identity);
    if (!synchronized.Succeeded || voxelDocumentMeshCache_.Mesh() == nullptr)
    {
        if (failedDocumentIdentity_ != identity ||
            failedDocumentRevision_ != revision)
        {
            AddConsoleMessage(
                "Voxel render synchronization failed: " +
                synchronized.Message);
            failedDocumentIdentity_ = identity;
            failedDocumentRevision_ = revision;
        }
        return false;
    }
    if (!synchronized.Rebuilt() &&
        uploadedDocumentIdentity_ == identity &&
        uploadedDocumentRevision_ == revision)
    {
        return true;
    }

    const Vec3 modelCenter = CalculateVoxelDocumentCenter(*document);
    const Voxel::VoxelPalette palette = BuildDocumentRenderPalette(*document);
    const Mesh::MeshData& mesh = *voxelDocumentMeshCache_.Mesh();
    if (!viewportRenderer_.Upload(mesh, palette, modelCenter))
    {
        if (failedDocumentIdentity_ != identity ||
            failedDocumentRevision_ != revision)
        {
            AddConsoleMessage(
                "Voxel render GPU synchronization failed: " +
                viewportRenderer_.LastError());
            failedDocumentIdentity_ = identity;
            failedDocumentRevision_ = revision;
        }
        return false;
    }
    if (viewportState_.HasModel())
    {
        if (!viewportState_.UpdateDocumentStatistics(*document, mesh))
            return false;
    }
    else if (!viewportState_.ReplaceDocument(
                 document->SourcePath().filename().string(),
                 *document,
                 mesh))
    {
        return false;
    }

    const auto hitStillExists = [document](
        const std::optional<VoxelRaycastHit>& hit)
    {
        return !hit || document->HasVoxel({
            static_cast<std::int32_t>(hit->Coordinates.X),
            static_cast<std::int32_t>(hit->Coordinates.Y),
            static_cast<std::int32_t>(hit->Coordinates.Z)},
            hit->SubModelIndex);
    };
    bool highlightChanged = false;
    if (!hitStillExists(voxelSelection_.Hovered()))
    {
        highlightChanged |= voxelSelection_.SetHovered(
            VoxelPickingInteractionState::NoHit);
    }
    if (!hitStillExists(voxelSelection_.Selected()))
        highlightChanged |= voxelSelection_.ClearSelection();
    if (highlightChanged) UpdateVoxelHighlights();

    voxelModelCenter_ = modelCenter;
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    viewportRenderer_.ConfigureGuides(
        static_cast<float>(statistics.Width),
        static_cast<float>(statistics.Height),
        static_cast<float>(statistics.Depth));
    uploadedDocumentIdentity_ = identity;
    uploadedDocumentRevision_ = revision;
    failedDocumentIdentity_.reset();
    failedDocumentRevision_.reset();
    voxelViewportRendered_ = false;
    voxelViewportRenderFailed_ = false;
    return true;
}

CommandResult EditorWorkspace::RebuildActiveVoxelMesh()
{
    if (voxelDocumentSession_.HasActiveDocument())
    {
        return SynchronizeVoxelDocumentRendering()
            ? CommandResult::Success()
            : CommandResult::Failure(
                "VoxelDocument render synchronization failed.");
    }
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
    // All successful voxel edits use the same predictable v1 rule: clear both
    // selection and hover after the rebuilt mesh has replaced the old one.
    static_cast<void>(voxelSelection_.Clear());
    UpdateVoxelHighlights();
    voxelSaveState_.MarkModified();
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
    std::optional<VoxelCoordinates> hoveredCoordinates =
        coordinates(voxelSelection_.Hovered());
    std::optional<Asset::Voxel::VoxelPosition> placementPosition;
    VoxelPlacementPreviewStyle placementStyle =
        VoxelPlacementPreviewStyle::PencilInvalid;
    if (voxelToolState_.IsPencilActive())
    {
        voxelPlacementPreview_ = EvaluateVoxelPencilPreview(
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            true);
        placementPosition = voxelPlacementPreview_.IsVisible()
            ? voxelPlacementPreview_.Position : std::nullopt;
        placementStyle = voxelPlacementPreview_.IsValid()
            ? VoxelPlacementPreviewStyle::PencilValid
            : VoxelPlacementPreviewStyle::PencilInvalid;
    }
    else if (voxelToolState_.IsEraserActive())
    {
        const VoxelPickingInteractionState interaction =
            voxelSelection_.InteractionState();
        const bool blocked =
            interaction == VoxelPickingInteractionState::CameraInteraction ||
            interaction == VoxelPickingInteractionState::Blocked;
        voxelPlacementPreview_ = EvaluateVoxelEraserPreview(
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            true,
            blocked);
        placementPosition = voxelPlacementPreview_.IsVisible()
            ? voxelPlacementPreview_.Position : std::nullopt;
        placementStyle = VoxelPlacementPreviewStyle::Eraser;
        if (placementPosition) hoveredCoordinates.reset();
    }
    else
    {
        voxelPlacementPreview_ = {};
        const Voxel::VoxelGrid* grid = activeVoxelModel_
            ? activeVoxelModel_->GetGrid(0U) : nullptr;
        const AddVoxelTarget addTarget =
            FindAddVoxelTarget(grid, voxelSelection_.Selected());
        if (addTarget)
        {
            placementPosition = Asset::Voxel::VoxelPosition{
                static_cast<std::int32_t>(addTarget.Coordinates->X),
                static_cast<std::int32_t>(addTarget.Coordinates->Y),
                static_cast<std::int32_t>(addTarget.Coordinates->Z)};
            placementStyle = VoxelPlacementPreviewStyle::PencilValid;
        }
    }
    viewportRenderer_.ConfigureHighlights(
        hoveredCoordinates,
        coordinates(voxelSelection_.Selected()),
        placementPosition,
        placementStyle,
        voxelModelCenter_);
}

void EditorWorkspace::ClearVoxelViewport() noexcept
{
    commandHistory_.Clear();
    ++voxelModelGeneration_;
    viewportRenderer_.ClearModel();
    viewportRenderer_.ConfigureGuides(0.0F, 0.0F, 0.0F);
    viewportState_.Clear();
    voxelDocumentSession_.Close();
    voxelDocumentMeshCache_.Clear();
    uploadedDocumentIdentity_.reset();
    uploadedDocumentRevision_.reset();
    failedDocumentIdentity_.reset();
    failedDocumentRevision_.reset();
    activeVoxelModel_.reset();
    static_cast<void>(voxelSelection_.Clear());
    voxelToolState_.Reset();
    voxelToolInput_.Reset();
    voxelToolSmokeInput_.Reset();
    voxelPlacementPreview_ = {};
    lastVoxelToolResult_.reset();
    lastVoxelEraserResult_.reset();
    voxelEditInProgress_ = false;
    voxelModelCenter_ = {};
    voxelViewportRendered_ = false;
    voxelViewportRenderFailed_ = false;
    voxelSaveState_.Clear();
    eraseSmokeSelected_ = false;
    eraseSmokeExecuted_ = false;
    eraseSmokeUndone_ = false;
    eraseSmokeRedone_ = false;
    eraseSmokeInitialVoxelCount_ = 0U;
    eraseSmokeEraseRenderBaseline_ = 0U;
    eraseSmokeUndoRenderBaseline_ = 0U;
    eraseSmokeRedoRenderBaseline_ = 0U;
    paintSmokeSelected_ = false;
    paintSmokeExecuted_ = false;
    paintSmokeUndone_ = false;
    paintSmokeRedone_ = false;
    paintSmokeInitialVoxelCount_ = 0U;
    paintSmokeInitialTriangleCount_ = 0U;
    paintSmokeExecuteRenderBaseline_ = 0U;
    paintSmokeUndoRenderBaseline_ = 0U;
    paintSmokeRedoRenderBaseline_ = 0U;
    paintSmokeX_ = 0U;
    paintSmokeY_ = 0U;
    paintSmokeZ_ = 0U;
    paintSmokeInitialColor_ = 0U;
    paintSmokeNewColor_ = 0U;
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
