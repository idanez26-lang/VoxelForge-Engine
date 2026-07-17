#include "EditorWorkspace.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "EditorWindowTitle.h"
#include "Layout/PalettePanelLayout.h"

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
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <optional>
#include <memory>
#include <string_view>
#include <sstream>
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

ProjectSessionCameraView ToSessionView(const EditorCameraView view) noexcept
{
    switch (view)
    {
    case EditorCameraView::Front: return ProjectSessionCameraView::Front;
    case EditorCameraView::Back: return ProjectSessionCameraView::Back;
    case EditorCameraView::Left: return ProjectSessionCameraView::Left;
    case EditorCameraView::Right: return ProjectSessionCameraView::Right;
    case EditorCameraView::Top: return ProjectSessionCameraView::Top;
    case EditorCameraView::Bottom: return ProjectSessionCameraView::Bottom;
    case EditorCameraView::Perspective:
        return ProjectSessionCameraView::Perspective;
    }
    return ProjectSessionCameraView::Perspective;
}

EditorCameraView FromSessionView(const ProjectSessionCameraView view) noexcept
{
    switch (view)
    {
    case ProjectSessionCameraView::Front: return EditorCameraView::Front;
    case ProjectSessionCameraView::Back: return EditorCameraView::Back;
    case ProjectSessionCameraView::Left: return EditorCameraView::Left;
    case ProjectSessionCameraView::Right: return EditorCameraView::Right;
    case ProjectSessionCameraView::Top: return EditorCameraView::Top;
    case ProjectSessionCameraView::Bottom: return EditorCameraView::Bottom;
    case ProjectSessionCameraView::Perspective:
        return EditorCameraView::Perspective;
    }
    return EditorCameraView::Perspective;
}

ProjectSessionVector3 ToSessionVector(const Vec3& value) noexcept
{
    return {value.X, value.Y, value.Z};
}

Vec3 FromSessionVector(const ProjectSessionVector3& value) noexcept
{
    return {value.X, value.Y, value.Z};
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

ImVec4 ToImGuiColor(const Asset::Voxel::VoxelColor& color) noexcept
{
    constexpr float ByteScale = 1.0F / 255.0F;
    return {
        static_cast<float>(color.Red) * ByteScale,
        static_cast<float>(color.Green) * ByteScale,
        static_cast<float>(color.Blue) * ByteScale,
        static_cast<float>(color.Alpha) * ByteScale};
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

std::string FormatSaveTime(
    const std::chrono::system_clock::time_point time)
{
    if (time == std::chrono::system_clock::time_point{}) return "Never";
    const std::time_t value = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &value) != 0) return "Unavailable";
#else
    if (localtime_r(&value, &local) == nullptr) return "Unavailable";
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void DrawInspectorDiagnostics(const InspectorLayoutModel& model)
{
    ImGui::TextUnformatted("Voxel Diagnostics");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float childHeight = model.StableHeight(
        ImGui::GetTextLineHeightWithSpacing(), style.WindowPadding.y);
    constexpr ImGuiWindowFlags childFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild(
            "##StableVoxelDiagnostics", ImVec2(0.0F, childHeight),
            true, childFlags))
    {
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("##StableVoxelDiagnosticRows", 2, tableFlags))
        {
            ImGui::TableSetupColumn(
                "Label", ImGuiTableColumnFlags_WidthFixed, 112.0F);
            ImGui::TableSetupColumn(
                "Value", ImGuiTableColumnFlags_WidthStretch);
            for (const InspectorDiagnosticRow& row : model.Rows())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", row.Label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.Value.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", row.Value.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
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
    voxelDocumentSaveService_.SetRefreshCallback(
        [this]()
        {
            static_cast<void>(assetBrowser_.Refresh());
        });
    voxelDocumentSaveService_.SetThumbnailInvalidationCallback(
        [this]()
        {
            assetBrowser_.InvalidateThumbnails();
        });
    voxelModelCreationService_.SetThumbnailCallback(
        [this](const std::filesystem::path& path)
        {
            const ThumbnailGenerationResult result =
                modelImportService_.GenerateThumbnail(path, true);
            return VoxelModelCreationStepResult{
                result.Succeeded(), result.Message};
        });
    voxelModelCreationService_.SetAssetBrowserCallback(
        [this](const std::filesystem::path& path)
        {
            const auto& project = projectManager_.ActiveProject();
            return project && assetBrowser_.RevealEntry(
                path.lexically_relative(project->RootPath() / "Assets"));
        });
    voxelModelCreationService_.SetOpenCallback(
        [this](const std::filesystem::path& path)
        {
            return OpenVoxInViewportNow(path);
        });
    directCreationFlowService_.SetViewportPreparationCallback(
        [this](const std::filesystem::path& path)
        {
            const Asset::Voxel::VoxelDocument* document =
                voxelDocumentSession_.ActiveDocument();
            if (document == nullptr ||
                document->SourcePath().lexically_normal() !=
                    path.lexically_normal() || !viewportState_.HasModel())
            {
                return DirectCreationStepResult{
                    false, "Created document is not active in the viewport."};
            }
            showScene_ = true;
            viewportState_.SetGridVisible(true);
            viewportState_.SetAxesVisible(true);
            FrameVoxelViewport();
            return DirectCreationStepResult{true, {}};
        });
    directCreationFlowService_.SetPencilActivationCallback(
        [this](const std::filesystem::path&)
        {
            voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
            voxelToolInput_.Reset();
            UpdateVoxelHighlights();
            return DirectCreationStepResult{
                voxelToolState_.IsPencilActive(),
                voxelToolState_.IsPencilActive()
                    ? std::string{} : "Pencil could not be activated."};
        });
    directCreationFlowService_.SetWorkplanePreparationCallback(
        [this](const std::filesystem::path&)
        {
            const Asset::Voxel::VoxelDocument* document =
                voxelDocumentSession_.ActiveDocument();
            const bool ready = document != nullptr &&
                workplaneService_.Grid(*document).has_value() &&
                viewportState_.IsGridVisible();
            return DirectCreationStepResult{ready,
                ready ? std::string{} : "Workplane is not ready."};
        });
    directCreationFlowService_.SetViewportFocusCallback(
        [this](const std::filesystem::path&)
        {
            viewportFocusRequested_ = true;
            viewportFocusApplied_ = false;
            return DirectCreationStepResult{true, {}};
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

    bool layoutRebuilt = false;
    if (layoutMissing || resetLayoutRequested_)
    {
        if (thumbnailVisualLayoutRequested_)
            BuildThumbnailVisualLayout(dockspaceId);
        else
            BuildDefaultLayout(dockspaceId);
        resetLayoutRequested_ = false;
        thumbnailVisualLayoutRequested_ = false;
        layoutRebuilt = true;
    }

    if (!showScene_) viewportDropRect_ = {};
    if (!showAssetBrowser_) assetBrowserDropRect_ = {};

    if (showExplorer_) DrawExplorerPanel();
    if (showScene_) DrawScenePanel();
    if (showInspector_ && !thumbnailVisualMode_) DrawInspectorPanel();
    if (showPalette_ && !thumbnailVisualMode_) DrawPalettePanel();
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
    DrawVoxelModelCreationDialogs();
    DrawModelImportDialogs();
    DrawDirtyConfirmationDialog();
    DrawFirstCreationOverlay();
    if (layoutRebuilt && ImGui::GetIO().IniFilename != nullptr)
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
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
                "New Voxel Model...", "Ctrl+Shift+N", false,
                hasActiveProject))
        {
            RequestNewVoxelModelDialog();
        }
        DrawTooltip("Create an empty voxel model in Assets/Models");

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

        const Asset::Voxel::VoxelDocument* activeDocument =
            voxelDocumentSession_.ActiveDocument();
        const bool canSaveVoxel = activeDocument != nullptr &&
            activeDocument->IsDirty() &&
            !voxelDocumentSaveService_.IsBusy();
        if (ImGui::MenuItem(
                "Save",
                "Ctrl+S",
                false,
                canSaveVoxel))
        {
            static_cast<void>(SaveVoxelModel());
        }
        DrawTooltip(
            "Save the active VOX model");

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
        const bool documentHistory =
            voxelDocumentSession_.HasActiveDocument();
        const bool canUndo = documentHistory
            ? voxelEditHistory_.CanUndo() : commandHistory_.CanUndo();
        const bool canRedo = documentHistory
            ? voxelEditHistory_.CanRedo() : commandHistory_.CanRedo();
        const std::string_view undoName = documentHistory
            ? voxelEditHistory_.UndoLabel() : commandHistory_.UndoName();
        const std::string_view redoName = documentHistory
            ? voxelEditHistory_.RedoLabel() : commandHistory_.RedoName();
        const std::string undoLabel = canUndo
            ? "Undo " + std::string(undoName)
            : "Undo";
        if (ImGui::MenuItem(
                undoLabel.c_str(), "Ctrl+Z", false,
                canUndo && !voxelEditInProgress_))
        {
            UndoCommand();
        }
        DrawTooltip("Undo the last edit (Ctrl+Z)");

        const std::string redoLabel = canRedo
            ? "Redo " + std::string(redoName)
            : "Redo";
        if (ImGui::MenuItem(
                redoLabel.c_str(), "Ctrl+Y / Ctrl+Shift+Z", false,
                canRedo && !voxelEditInProgress_))
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
        ImGui::MenuItem("Palette", nullptr, &showPalette_);
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
    const VoxelHistoryInputDecision historyDecision =
        voxelHistoryInput_.Update({
            io.KeyCtrl,
            io.KeyShift,
            ImGui::IsKeyDown(ImGuiKey_Z),
            ImGui::IsKeyDown(ImGuiKey_Y),
            context.TextInput || context.ActiveItem,
            context.PopupOpen,
            io.WantCaptureKeyboard && context.ActiveItem,
            voxelDocumentSession_.HasActiveDocument(),
            voxelEditInProgress_,
            voxelEditHistory_.IsBusy()});
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
                 ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N,
                 shortcutFlags) && context.HasProject)
    {
        RequestNewVoxelModelDialog();
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
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (document)
        {
            if (document->IsDirty() &&
                !voxelDocumentSaveService_.IsBusy())
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
    else if (historyDecision == VoxelHistoryInputDecision::Undo &&
             voxelEditHistory_.CanUndo())
        UndoCommand();
    else if (historyDecision == VoxelHistoryInputDecision::Redo &&
             voxelEditHistory_.CanRedo())
        RedoCommand();
}

void EditorWorkspace::UndoCommand()
{
    if (voxelDocumentSession_.HasActiveDocument())
    {
        if (voxelEditInProgress_ || voxelEditHistory_.IsBusy()) return;
        voxelEditInProgress_ = true;
        const VoxelEditHistoryResult result = voxelEditHistory_.Undo(*this);
        voxelEditInProgress_ = false;
        if (result)
        {
            AddConsoleMessage("[Edit] Undo: " + result.Label);
            firstCreationExperience_.OnUndo();
        }
        else if (result.Code != VoxelEditHistoryResultCode::NothingToUndo)
            AddConsoleMessage("[Edit] Undo failed: " + result.Message);
        return;
    }
    const std::string name(commandHistory_.UndoName());
    const CommandResult result = commandHistory_.Undo();
    AddConsoleMessage(result
        ? "Undo: " + name
        : "Undo failed: " + result.Message);
}

void EditorWorkspace::RedoCommand()
{
    if (voxelDocumentSession_.HasActiveDocument())
    {
        if (voxelEditInProgress_ || voxelEditHistory_.IsBusy()) return;
        voxelEditInProgress_ = true;
        const VoxelEditHistoryResult result = voxelEditHistory_.Redo(*this);
        voxelEditInProgress_ = false;
        if (result)
        {
            AddConsoleMessage("[Edit] Redo: " + result.Label);
            firstCreationExperience_.OnRedo();
        }
        else if (result.Code != VoxelEditHistoryResultCode::NothingToRedo)
            AddConsoleMessage("[Edit] Redo failed: " + result.Message);
        return;
    }
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

    ImGuiID rightId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Right,
        0.22F,
        nullptr,
        &topId);

    const ImGuiID paletteId = ImGui::DockBuilderSplitNode(
        rightId,
        ImGuiDir_Down,
        0.64F,
        nullptr,
        &rightId);

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
    ImGui::DockBuilderDockWindow("Palette", paletteId);
    ImGui::DockBuilderDockWindow("Asset Browser", bottomLeftId);
    ImGui::DockBuilderDockWindow("Console", bottomRightId);
    ImGui::DockBuilderFinish(dockspaceId);

    showExplorer_ = true;
    showScene_ = true;
    showInspector_ = true;
    showPalette_ = true;
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
    const bool focusRequested = std::exchange(
        viewportFocusRequested_, false);
    if (focusRequested) ImGui::SetNextWindowFocus();
    const bool visible = ImGui::Begin("Scene", &showScene_);
    if (focusRequested)
    {
        viewportFocusApplied_ = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
    }
    if (!visible)
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

    const Asset::Voxel::VoxelDocument* activeDocument =
        voxelDocumentSession_.ActiveDocument();
    const bool canSave = activeDocument != nullptr &&
        activeDocument->IsDirty() && !voxelDocumentSaveService_.IsBusy();
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save")) static_cast<void>(SaveVoxelModel());
    ImGui::EndDisabled();
    DrawTooltip("Save the active VOX model");
    ImGui::SameLine();

    const auto drawToolButton = [this](
        const char* inactiveLabel,
        const char* activeLabel,
        const char* tooltip,
        const ActiveVoxelTool tool,
        const ImVec4 activeColor)
    {
        const bool active = voxelToolState_.ActiveTool() == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        constexpr ImVec2 buttonSize(104.0F, 0.0F);
        const bool clicked = ImGui::Button(
            active ? activeLabel : inactiveLabel, buttonSize);
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
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanUndo() || voxelEditInProgress_);
    if (ImGui::Button("Undo")) UndoCommand();
    ImGui::EndDisabled();
    DrawTooltip("Undo the last edit (Ctrl+Z)");
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanRedo() || voxelEditInProgress_);
    if (ImGui::Button("Redo")) RedoCommand();
    ImGui::EndDisabled();
    DrawTooltip("Redo the last undone edit (Ctrl+Y or Ctrl+Shift+Z)");

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
        ImGui::TextDisabled(
            "Voxel hover and tool diagnostics are shown in Inspector.");
    }
    else
    {
        ImGui::TextDisabled(
            "Right-click a .vox file and choose Open in Viewport.");
    }
    ImGui::TextDisabled(
        "Right: orbit | Middle: pan | Wheel: zoom | F: frame | Home: reset");

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0F);
    available.y = std::max(available.y, 1.0F);
    const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    currentViewportRectangle_ = {
        imageOrigin.x, imageOrigin.y, available.x, available.y};
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
        const auto previousWorkplaneHit = workplaneHit_;
        workplaneHit_.reset();
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
                currentViewportRectangle_,
                viewportCamera_.GetViewProjection(),
                viewportCamera_.GetPosition());
            if (ray.Succeeded())
            {
                VoxelRaycastOptions options;
                options.Transform =
                    CenteredVoxelModelTransform(voxelModelCenter_);
                hoveredHit = RaycastVoxelDocument(
                    *document, *ray.Ray, options);
                if (!hoveredHit && voxelToolState_.IsPencilActive())
                {
                    workplaneHit_ = workplaneService_.Intersect(
                        *document, 0U, *ray.Ray, voxelModelCenter_);
                }
                pickingState = hoveredHit
                    ? VoxelPickingInteractionState::Hit
                    : VoxelPickingInteractionState::NoHit;
            }
        }
        if (layoutStabilitySmokePickingOverride_)
        {
            pickingState = *layoutStabilitySmokePickingOverride_;
            hoveredHit = layoutStabilitySmokeHitOverride_;
            workplaneHit_ = layoutStabilitySmokeWorkplaneOverride_
                ? std::optional<WorkplaneHit>(WorkplaneHit{
                    WorkplaneHitStatus::Valid,
                    layoutStabilitySmokeWorkplaneOverride_,
                    0.0F})
                : std::nullopt;
        }
        if (voxelSelection_.SetHovered(pickingState, std::move(hoveredHit)) ||
            previousWorkplaneHit != workplaneHit_)
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
        const std::uint8_t leftClickCount =
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                ? 2U
                : ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? 1U : 0U;
        const ViewportCameraActions cameraActions =
            ResolveViewportCameraActions({
                shortcutsEnabled,
                ImGui::IsKeyPressed(ImGuiKey_F, false),
                ImGui::IsKeyPressed(ImGuiKey_Home, false),
                leftClickCount});
        if (cameraActions.FrameRequested)
            FrameVoxelViewport();
        if (cameraActions.ResetRequested)
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
    {
        const InspectorLayoutModel emptyDiagnostics =
            InspectorLayoutModel::Build({});
        ImGui::SetScrollY(
            180.0F + emptyDiagnostics.StableHeight(
                ImGui::GetTextLineHeightWithSpacing(),
                ImGui::GetStyle().WindowPadding.y));
    }
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document != nullptr)
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
        ImGui::Text("Undo Available: %s",
            voxelEditHistory_.CanUndo() ? "Yes" : "No");
        ImGui::Text("Redo Available: %s",
            voxelEditHistory_.CanRedo() ? "Yes" : "No");
        ImGui::Text("Undo Count: %zu", voxelEditHistory_.UndoCount());
        ImGui::Text("Redo Count: %zu", voxelEditHistory_.RedoCount());
        ImGui::Text("Next Undo: %s", voxelEditHistory_.CanUndo()
            ? std::string(voxelEditHistory_.UndoLabel()).c_str() : "None");
        ImGui::Text("Next Redo: %s", voxelEditHistory_.CanRedo()
            ? std::string(voxelEditHistory_.RedoLabel()).c_str() : "None");
        ImGui::Text("History Memory: %zu bytes",
            voxelEditHistory_.EstimatedMemory());
        ImGui::Text("Saved State: %s",
            voxelEditHistory_.IsAtSavedState() ? "Current" : "Different");
        const VoxelDocumentSaveResult& saveResult =
            voxelDocumentSaveService_.LastResult();
        ImGui::Text("Save Status: %s",
            VoxelDocumentSaveStageName(voxelDocumentSaveService_.Stage()));
        ImGui::Text("Last Save Time: %s",
            FormatSaveTime(
                voxelDocumentSaveService_.LastSaveTime()).c_str());
        ImGui::TextWrapped("Last Save Result: %s",
            saveResult.Message.empty() ? "None" : saveResult.Message.c_str());
        ImGui::TextWrapped("Saved Path: %s",
            saveResult.SavedPath.empty() ? "None"
                : saveResult.SavedPath.string().c_str());
    }
    const std::optional<PaletteColorSelection> activePaletteColor =
        paletteService_.ActiveColor();
    ImGui::Text("Palette Active: %s",
        paletteService_.HasActivePalette()
            ? paletteService_.ActivePaletteName().c_str() : "--");
    if (activePaletteColor)
    {
        ImGui::Text("Couleur Active: #%02X%02X%02X%02X",
            activePaletteColor->Color.Red,
            activePaletteColor->Color.Green,
            activePaletteColor->Color.Blue,
            activePaletteColor->Color.Alpha);
        ImGui::Text("Index: %zu", activePaletteColor->Index);
    }
    else
    {
        ImGui::TextUnformatted("Couleur Active: --");
        ImGui::TextUnformatted("Index: --");
    }
    DrawInspectorDiagnostics(InspectorLayoutModel::Build({
        document != nullptr,
        document ? document->SourcePath().filename().string() : std::string{},
        document ? document->GetRevision() : 0U,
        voxelSelection_.Hovered(),
        voxelSelection_.InteractionState(),
        voxelToolState_.ActiveTool(),
        paletteService_.ActiveIndex().value_or(0U),
        voxelPlacementPreview_,
        lastVoxelToolResult_,
        lastVoxelEraserResult_}));
    ImGui::Separator();

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

void EditorWorkspace::DrawPalettePanel()
{
    if (!ImGui::Begin("Palette", &showPalette_))
    {
        ImGui::End();
        return;
    }

    const PaletteService::Palette* palette = paletteService_.ActivePalette();
    const std::optional<PaletteColorSelection> active =
        paletteService_.ActiveColor();
    if (palette == nullptr || !active)
    {
        ImGui::TextUnformatted("No active palette.");
        ImGui::TextDisabled("Open a voxel model to display its colors.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Active Color");
    const float activePreviewWidth =
        std::max(1.0F, ImGui::GetContentRegionAvail().x);
    static_cast<void>(ImGui::ColorButton(
        "##ActivePaletteColor", ToImGuiColor(active->Color),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
            ImGuiColorEditFlags_NoPicker,
        ImVec2(activePreviewWidth, 32.0F)));
    ImGui::Text("Index: %zu", active->Index);
    ImGui::SameLine();
    ImGui::TextDisabled("#%02X%02X%02X%02X",
        active->Color.Red, active->Color.Green,
        active->Color.Blue, active->Color.Alpha);

    const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::Spacing();
    ImGui::TextDisabled("Recent Colors");
    const auto& recent = paletteService_.RecentColors();
    constexpr std::size_t RecentSlotCount = PaletteService::RecentColorLimit;
    const PaletteGridLayout recentLayout = CalculatePaletteGridLayout(
        ImGui::GetContentRegionAvail().x,
        itemSpacing,
        24.0F,
        RecentSlotCount);
    for (std::size_t index = 0U; index < RecentSlotCount; ++index)
    {
        if (index % recentLayout.ColumnCount != 0U)
            ImGui::SameLine(0.0F, itemSpacing);
        ImGui::PushID(static_cast<int>(index));
        const bool populated = index < recent.size();
        const ImVec4 color = populated
            ? ToImGuiColor(recent[index].Color)
            : ImVec4(0.18F, 0.18F, 0.18F, 0.45F);
        ImGui::BeginDisabled(!populated);
        static_cast<void>(ImGui::ColorButton(
                "##RecentColor", color,
                ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop |
                    ImGuiColorEditFlags_NoPicker,
                ImVec2(recentLayout.SwatchSize, recentLayout.SwatchSize)));
        if (populated && ImGui::IsItemHovered())
            ImGui::SetTooltip("Used palette index %zu", recent[index].Index);
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Palette");
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() +
        ImGui::GetTextLineHeightWithSpacing() +
        ImGui::GetStyle().ItemSpacing.y;
    const float gridHeight = std::max(
        96.0F, ImGui::GetContentRegionAvail().y - footerHeight);
    bool openColorEditor = false;
    if (ImGui::BeginChild(
            "##PaletteGridRegion", ImVec2(0.0F, gridHeight), true))
    {
        constexpr float MinimumSwatchSize = 20.0F;
        const PaletteGridLayout gridLayout = CalculatePaletteGridLayout(
            ImGui::GetContentRegionAvail().x,
            itemSpacing,
            MinimumSwatchSize,
            palette->size());
        for (std::size_t index = 0U; index < palette->size(); ++index)
        {
            if (index % gridLayout.ColumnCount != 0U)
                ImGui::SameLine(0.0F, itemSpacing);
            ImGui::PushID(static_cast<int>(index));
            const bool selectable = PaletteService::IsSelectableIndex(index);
            const bool selected = index == active->Index;
            ImGui::BeginDisabled(!selectable);
            if (ImGui::ColorButton(
                    "##Color", ToImGuiColor((*palette)[index]),
                    ImGuiColorEditFlags_NoTooltip |
                        ImGuiColorEditFlags_NoDragDrop,
                    ImVec2(gridLayout.SwatchSize, gridLayout.SwatchSize)))
            {
                static_cast<void>(paletteService_.SelectColor(index));
            }
            if (selectable && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                paletteColorEditorIndex_ = index;
                openColorEditor = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(selectable
                    ? "Index %zu - double-click: color editor preview"
                    : "Index 0 is reserved by VOX", index);
            }
            ImGui::EndDisabled();
            if (selected)
            {
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRect(
                    minimum, maximum,
                    IM_COL32(0, 0, 0, 255), 2.0F, 0, 3.0F);
                drawList->AddRect(
                    ImVec2(minimum.x + 2.0F, minimum.y + 2.0F),
                    ImVec2(maximum.x - 2.0F, maximum.y - 2.0F),
                    IM_COL32(255, 255, 255, 255), 1.0F, 0, 1.0F);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled("Tool Options");
    ImGui::SameLine();
    ImGui::TextDisabled("(coming later)");

    if (openColorEditor) ImGui::OpenPopup("Color Editor Preview");
    if (ImGui::BeginPopupModal(
            "Color Editor Preview", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Palette Index: %zu",
            paletteColorEditorIndex_.value_or(active->Index));
        ImGui::TextUnformatted(
            "Color editing is prepared but intentionally disabled in v1.");
        if (ImGui::Button("Close"))
        {
            paletteColorEditorIndex_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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

    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::string fileName = document
        ? document->SourcePath().filename().string() : "active model";
    ImGui::TextWrapped("Save changes to %s?", fileName.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(voxelDocumentSaveService_.IsBusy());
    if (ImGui::Button("Save"))
    {
        if (SaveVoxelModel())
        {
            const auto action =
                dirtyActionConfirmation_.ContinueAfterSuccessfulSave();
            ImGui::CloseCurrentPopup();
            if (action) ExecutePendingDirtyAction(*action);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Don't Save"))
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
        pendingVoxelModelCreation_ = {};
        pendingVoxelModelCollisionAction_ =
            VoxelModelCreationCollisionAction::Ask;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorWorkspace::DrawVoxelModelCreationDialogs()
{
    constexpr const char* CreatePopup = "New Voxel Model";
    constexpr const char* CollisionPopup = "Voxel Model Already Exists";
    if (showNewVoxelModelPopup_)
    {
        ImGui::OpenPopup(CreatePopup);
        showNewVoxelModelPopup_ = false;
    }
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (ImGui::BeginPopupModal(
            CreatePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Create an empty VOX model.");
        ImGui::InputText(
            "Name", newVoxelModelName_.data(), newVoxelModelName_.size());
        ImGui::InputInt3("Dimensions", newVoxelModelDimensions_.data());
        ImGui::TextDisabled("Valid range: 1..256. Default: 64 x 64 x 64.");
        DrawErrorMessage(voxelModelCreationError_);
        if (ImGui::Button("Create"))
        {
            VoxelModelCreationRequest request;
            request.Name = newVoxelModelName_.data();
            request.Dimensions = {
                newVoxelModelDimensions_[0] > 0
                    ? static_cast<std::uint32_t>(newVoxelModelDimensions_[0]) : 0U,
                newVoxelModelDimensions_[1] > 0
                    ? static_cast<std::uint32_t>(newVoxelModelDimensions_[1]) : 0U,
                newVoxelModelDimensions_[2] > 0
                    ? static_cast<std::uint32_t>(newVoxelModelDimensions_[2]) : 0U};
            std::string validation;
            if (!VoxelModelCreationService::ValidateModelName(
                    request.Name, validation) ||
                !VoxelModelCreationService::ValidateDimensions(
                    request.Dimensions, validation))
            {
                voxelModelCreationError_ = std::move(validation);
            }
            else
            {
                voxelModelCreationError_.clear();
                RequestCreateVoxelModel(
                    std::move(request),
                    VoxelModelCreationCollisionAction::Ask);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            voxelModelCreationError_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showVoxelModelCollisionPopup_)
    {
        ImGui::OpenPopup(CollisionPopup);
        showVoxelModelCollisionPopup_ = false;
    }
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (ImGui::BeginPopupModal(
            CollisionPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("The model already exists.");
        if (ImGui::Button("Rename"))
        {
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Rename;
            CreateVoxelModelNow();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace"))
        {
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Replace;
            CreateVoxelModelNow();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pendingVoxelModelCreation_ = {};
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Ask;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorWorkspace::DrawFirstCreationOverlay()
{
    if (!firstCreationExperience_.Visible()) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const bool hasViewportRectangle =
        currentViewportRectangle_.Width > 1.0F &&
        currentViewportRectangle_.Height > 1.0F;
    const ImVec2 overlayPosition = hasViewportRectangle
        ? ImVec2(
            currentViewportRectangle_.X +
                currentViewportRectangle_.Width * 0.5F,
            currentViewportRectangle_.Y + 16.0F)
        : ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
            viewport->WorkPos.y + 72.0F);
    ImGui::SetNextWindowPos(
        overlayPosition,
        ImGuiCond_Always, ImVec2(0.5F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(360.0F, 112.0F), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94F);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("First Creation", nullptr, flags))
    {
        ImGui::TextWrapped("%s", firstCreationExperience_.Message());
        if (firstCreationExperience_.Stage() == FirstCreationStage::Welcome &&
            ImGui::Button("Compris"))
            firstCreationExperience_.Acknowledge();
    }
    ImGui::End();
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
        if (HasUnsavedVoxelChanges())
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

void EditorWorkspace::RequestNewVoxelModelDialog()
{
    if (!projectManager_.HasActiveProject())
    {
        AddConsoleMessage("Voxel model creation failed: no project is loaded.");
        return;
    }
    newVoxelModelName_.fill('\0');
    constexpr std::string_view defaultName = "MyModel";
    std::copy(defaultName.begin(), defaultName.end(),
        newVoxelModelName_.begin());
    newVoxelModelDimensions_ = {64, 64, 64};
    voxelModelCreationError_.clear();
    showNewVoxelModelPopup_ = true;
}

void EditorWorkspace::RequestCreateVoxelModel(
    VoxelModelCreationRequest request,
    const VoxelModelCreationCollisionAction collisionAction)
{
    if (dirtyActionConfirmation_.IsPending()) return;
    pendingVoxelModelCreation_ = std::move(request);
    pendingVoxelModelCollisionAction_ = collisionAction;
    if (!dirtyActionConfirmation_.Request(
            DestructiveAction::CreateVoxelModel,
            HasUnsavedVoxelChanges()))
    {
        showDirtyConfirmationPopup_ = true;
        return;
    }
    CreateVoxelModelNow();
}

void EditorWorkspace::CreateProject()
{
    if (!dirtyActionConfirmation_.Request(
            DestructiveAction::CreateProject, HasUnsavedVoxelChanges()))
    {
        showDirtyConfirmationPopup_ = true;
        ImGui::CloseCurrentPopup();
        return;
    }
    CreateProjectNow();
}

void EditorWorkspace::CreateProjectNow()
{
    static_cast<void>(SaveActiveProjectSession());
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
    static_cast<void>(SaveActiveProjectSession());
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
    RestoreActiveProjectSession();
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

void EditorWorkspace::CreateVoxelModelNow()
{
    const DirectCreationFlowResult flow =
        directCreationFlowService_.Create(
            voxelModelCreationService_,
            pendingVoxelModelCreation_, pendingVoxelModelCollisionAction_);
    const VoxelModelCreationResult& result = flow.Creation;
    if (result.Status == VoxelModelCreationStatus::Collision)
    {
        showVoxelModelCollisionPopup_ = true;
        return;
    }
    if (!result.Succeeded())
    {
        if (result.Status != VoxelModelCreationStatus::Cancelled)
        {
            voxelModelCreationError_ = result.Message;
            AddConsoleMessage(
                "Voxel model creation failed: " + result.Message);
        }
        return;
    }
    AddConsoleMessage(
        "[Create] Created " + result.ModelPath.filename().string() + ".");
    if (!flow.Warning.empty())
        AddConsoleMessage("[Create] Warning: " + flow.Warning);
    if (flow.Ready())
    {
        firstCreationExperience_.Start(
            projectDialogPreferences_.FirstCreationCompleted());
        AddConsoleMessage("[Create] Viewport ready for the first voxel.");
    }
    else
    {
        AddConsoleMessage("[Create] Direct creation incomplete: " +
            flow.Message);
    }
    pendingVoxelModelCreation_ = {};
    pendingVoxelModelCollisionAction_ =
        VoxelModelCreationCollisionAction::Ask;
    voxelModelCreationError_.clear();
}

bool EditorWorkspace::SaveVoxelModel()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr)
    {
        AddConsoleMessage("[Save] Failed: no active VOX document.");
        return false;
    }
    const VoxelDocumentSaveResult result =
        voxelDocumentSaveService_.Save(*document, voxelEditHistory_);
    if (!result.Succeeded())
    {
        AddConsoleMessage("[Save] Failed to save " +
            document->SourcePath().filename().string() + ": " +
            result.Message);
        return false;
    }
    voxelSaveState_.MarkSaved();
    if (firstCreationExperience_.OnSave() &&
        !projectDialogPreferences_.SetFirstCreationCompleted(true))
    {
        AddConsoleMessage("First creation preference warning: " +
            projectDialogPreferences_.LastError());
    }
    AddConsoleMessage("[Save] Saved " +
        result.SavedPath.filename().string() + ".");
    if (result.Warning.empty())
        AddConsoleMessage("[Save] Updated metadata and thumbnail for " +
            result.SavedPath.filename().string() + ".");
    else
        AddConsoleMessage("[Save] Saved " +
            result.SavedPath.filename().string() + ", but " +
            result.Warning);
    return true;
}

bool EditorWorkspace::HasUnsavedVoxelChanges() const noexcept
{
    return voxelDocumentSession_.HasActiveDocument()
        ? voxelDocumentSession_.IsDirty()
        : voxelSaveState_.IsDirty();
}

void EditorWorkspace::RequestExit()
{
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::ExitApplication, HasUnsavedVoxelChanges()))
    {
        static_cast<void>(SaveActiveProjectSession());
        exitRequest_.RequestExit();
        return;
    }
    showDirtyConfirmationPopup_ = true;
}

void EditorWorkspace::RequestCloseProject()
{
    if (dirtyActionConfirmation_.Request(
            DestructiveAction::CloseProject, HasUnsavedVoxelChanges()))
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
            DestructiveAction::OpenProject, HasUnsavedVoxelChanges()))
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
            DestructiveAction::ReplaceVoxelModel, HasUnsavedVoxelChanges()))
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
    case DestructiveAction::CreateVoxelModel:
        CreateVoxelModelNow();
        break;
    case DestructiveAction::ExitApplication:
        static_cast<void>(SaveActiveProjectSession());
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
    static_cast<void>(SaveActiveProjectSession());
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
        voxelDocumentSaveService_.ClearProject();
        voxelModelCreationService_.ClearProject();
        projectSessionService_.ClearProject();
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
    if (!voxelDocumentSaveService_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("VOX save service setup failed.");
    if (!voxelModelCreationService_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("Voxel model creation service setup failed: " +
            voxelModelCreationService_.LastError());
    if (!projectSessionService_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("Project session setup failed.");
}

bool EditorWorkspace::SaveActiveProjectSession()
{
    const auto& project = projectManager_.ActiveProject();
    if (!project || projectSessionService_.ProjectRoot().empty()) return true;

    ProjectSessionData session;
    if (voxelDocumentSession_.HasActiveDocument())
    {
        std::error_code error;
        session.LastModel = std::filesystem::relative(
            voxelDocumentSession_.SourcePath(), project->RootPath(), error);
        if (error || !ProjectSessionService::IsValidModelPath(session.LastModel))
        {
            AddConsoleMessage("Project session save warning: active model path "
                "is not a valid project model.");
            return false;
        }
    }

    const EditorCameraState camera = viewportCamera_.CaptureState();
    session.Camera = {
        ToSessionVector(camera.Position),
        ToSessionVector(camera.RotationDegrees),
        camera.Distance,
        ToSessionVector(camera.Target),
        ToSessionView(camera.View)};
    session.ActiveTool = voxelToolState_.IsEraserActive()
        ? ProjectSessionTool::Eraser : ProjectSessionTool::Pencil;
    session.ActivePaletteIndex =
        paletteService_.ActiveIndex().value_or(
            PaletteService::FirstSelectableIndex);

    std::string error;
    if (!projectSessionService_.Save(session, error))
    {
        AddConsoleMessage("Project session save warning: " + error);
        return false;
    }
    return true;
}

void EditorWorkspace::RestoreActiveProjectSession()
{
    const auto& project = projectManager_.ActiveProject();
    if (!project || projectSessionService_.ProjectRoot().empty()) return;

    const ProjectSessionLoadResult loaded = projectSessionService_.Load();
    if (loaded.Status == ProjectSessionLoadStatus::NotFound) return;
    if (!loaded.Loaded())
    {
        AddConsoleMessage("Project session ignored: " + loaded.Message);
        return;
    }

    voxelToolState_.SetActiveTool(
        loaded.Session.ActiveTool == ProjectSessionTool::Eraser
            ? ActiveVoxelTool::Eraser : ActiveVoxelTool::Pencil);
    if (loaded.Session.LastModel.empty()) return;

    const std::filesystem::path modelPath =
        project->RootPath() / loaded.Session.LastModel;
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(modelPath, error);
    if (error || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status))
    {
        AddConsoleMessage("Last model not found.");
        return;
    }
    if (!OpenVoxInViewportNow(modelPath))
    {
        AddConsoleMessage("Last model could not be restored.");
        return;
    }
    if (!paletteService_.SelectColor(loaded.Session.ActivePaletteIndex))
    {
        static_cast<void>(paletteService_.SelectColor(
            PaletteService::FirstSelectableIndex));
        AddConsoleMessage(
            "Project session palette index invalid; index 1 used.");
    }

    const std::filesystem::path browserPath =
        loaded.Session.LastModel.lexically_relative("Assets");
    if (!assetBrowser_.RevealEntry(browserPath))
        AddConsoleMessage("Last model could not be selected in Asset Browser.");

    if (!loaded.CameraValid || !viewportCamera_.RestoreState({
            FromSessionVector(loaded.Session.Camera.Position),
            FromSessionVector(loaded.Session.Camera.RotationDegrees),
            loaded.Session.Camera.Distance,
            FromSessionVector(loaded.Session.Camera.Target),
            FromSessionView(loaded.Session.Camera.View)}))
    {
        AddConsoleMessage("Project session camera invalid; framed model used.");
    }
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
    if (HasUnsavedVoxelChanges())
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
        voxelEditHistory_.Clear();
        voxelHistoryInput_.Reset();
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
    voxelEditHistory_.Clear();
    voxelHistoryInput_.Reset();
    activeVoxelModel_ = std::move(*model);
    paintPaletteSelection_.OnModelLoaded();
    ++voxelModelGeneration_;
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document)
    {
        paletteService_.SetPalette(
            document->GetPalette(),
            document->HasCustomPalette()
                ? "Custom VOX Palette" : "Default VOX Palette");
    }
    else
    {
        paletteService_.Clear();
    }
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
        voxelEditHistory_.MarkSavedState(
            *voxelDocumentSession_.ActiveDocument());
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
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        voxelSaveSmokeRevisionAfterEdit_ = document
            ? document->GetRevision() : 0U;
        const bool saved = painted && voxelSaveState_.IsDirty() &&
            SaveVoxelModel();
        const Voxel::Voxel* voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        voxelSaveSmokePaintedAndSaved_ = saved && voxel != nullptr &&
            voxel->ColorIndex == voxelSaveSmokeColor_ &&
            !voxelSaveState_.IsDirty() && voxelEditHistory_.CanUndo() &&
            std::filesystem::is_regular_file(voxelSaveSmokePath_) &&
            voxelSaveSmokePath_.extension() == ".vox";
        document = voxelDocumentSession_.ActiveDocument();
        voxelSaveSmokeRevisionPreserved_ = document && saved &&
            document->GetRevision() == voxelSaveSmokeRevisionAfterEdit_;
        const MetadataReadResult metadata =
            modelImportService_.ReadMetadataForModel(voxelSaveSmokePath_);
        voxelSaveSmokeMetadataUpdated_ = metadata.Succeeded &&
            metadata.Metadata.Analysis && metadata.Metadata.Analysis->Valid &&
            metadata.Metadata.Analysis->VoxelCount ==
                voxelSaveSmokeInitialVoxelCount_ &&
            metadata.Metadata.SourceFile ==
                voxelSaveSmokePath_.filename().string();
        voxelSaveSmokeThumbnailUpdated_ = metadata.Succeeded &&
            metadata.Metadata.Thumbnail &&
            metadata.Metadata.Thumbnail->Status == ThumbnailStatus::Valid &&
            std::filesystem::is_regular_file(
                modelImportService_.ProjectRoot() / "Cache" / "Thumbnails" /
                metadata.Metadata.Thumbnail->File);
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
            !voxelSaveState_.IsDirty() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo() && !voxelSelection_.Selected();
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
        const bool dirtyAfterErase = erased && voxelSaveState_.IsDirty();
        UndoCommand();
        const Voxel::Voxel* restored = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        const bool saved = dirtyAfterErase && restored &&
            restored->IsOccupied() &&
            restored->ColorIndex == voxelSaveSmokeColor_ &&
            SaveVoxelModel();
        voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        voxelSaveSmokeErasedAndSaved_ = saved && voxel != nullptr &&
            voxel->IsOccupied() &&
            voxel->ColorIndex == voxelSaveSmokeColor_ &&
            !voxelSaveState_.IsDirty() && voxelEditHistory_.CanRedo();
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
        const auto temporary = VoxelDocumentSaveService::TemporaryPathFor(
            voxelSaveSmokePath_);
        const auto backup = VoxelDocumentSaveService::BackupPathFor(
            voxelSaveSmokePath_);
        voxelSaveSmokeEraseReloaded_ = voxel != nullptr &&
            voxel->IsOccupied() &&
            voxel->ColorIndex == voxelSaveSmokeColor_ &&
            grid->OccupiedVoxelCount() == voxelSaveSmokeInitialVoxelCount_ &&
            !voxelSaveState_.IsDirty() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo() && !voxelSelection_.Selected() &&
            !std::filesystem::exists(temporary) &&
            !std::filesystem::exists(backup);
        voxelSaveSmokeRenderBaseline_ = viewportRenderer_.ModelRenderCount();
    }
    else if (frame == 20U)
    {
        const auto baselineHash = HashFileContents(voxelSaveSmokePath_);
        if (!baselineHash) return false;
        voxelSaveSmokeFailureBaselineHash_ = *baselineHash;
        const Voxel::Voxel* voxel = grid->Get(
            voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_);
        if (voxel == nullptr || !voxel->IsOccupied()) return false;
        const std::uint8_t failureColor = static_cast<std::uint8_t>(
            voxelSaveSmokeColor_ == 255U ? 254U : voxelSaveSmokeColor_ + 1U);
        static_cast<void>(paintPaletteSelection_.SetIndex(failureColor));
        const VoxelRaycastHit hit{
            {voxelSaveSmokeX_, voxelSaveSmokeY_, voxelSaveSmokeZ_},
            VoxelHitFace::NegativeX, 0.0F, {}, voxel->ColorIndex};
        static_cast<void>(voxelSelection_.SetHovered(hit));
        static_cast<void>(voxelSelection_.SelectHovered());
        if (!PaintSelectedVoxel() || !voxelSaveState_.IsDirty()) return false;
        const std::filesystem::path temporary =
            VoxelDocumentSaveService::TemporaryPathFor(voxelSaveSmokePath_);
        {
            std::ofstream blocker(temporary, std::ios::binary | std::ios::trunc);
            blocker << "controlled smoke transaction blocker";
        }
        const bool confirmationRequested = !dirtyActionConfirmation_.Request(
            DestructiveAction::CloseProject, true);
        const bool saveFailed = !SaveVoxelModel();
        const auto afterFailureHash = HashFileContents(voxelSaveSmokePath_);
        voxelSaveSmokeFailureRolledBack_ = confirmationRequested &&
            saveFailed && dirtyActionConfirmation_.IsPending() &&
            projectManager_.HasActiveProject() &&
            voxelDocumentSession_.HasActiveDocument() &&
            voxelSaveState_.IsDirty() && afterFailureHash &&
            *afterFailureHash == voxelSaveSmokeFailureBaselineHash_;
        dirtyActionConfirmation_.Cancel();
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        if (cleanup) return false;
    }
    else if (frame == 22U)
    {
        const bool cancelRequested = !dirtyActionConfirmation_.Request(
            DestructiveAction::CloseProject, voxelSaveState_.IsDirty());
        dirtyActionConfirmation_.Cancel();
        const bool cancelKeptDocument =
            projectManager_.HasActiveProject() &&
            voxelDocumentSession_.HasActiveDocument() &&
            voxelSaveState_.IsDirty();
        const bool discardRequested = !dirtyActionConfirmation_.Request(
            DestructiveAction::CloseProject, voxelSaveState_.IsDirty());
        const auto action = dirtyActionConfirmation_.Discard();
        if (action) ExecutePendingDirtyAction(*action);
        voxelSaveSmokeDirtyCloseProtected_ = cancelRequested &&
            cancelKeptDocument && discardRequested &&
            action == DestructiveAction::CloseProject;
        voxelSaveSmokeCleanupComplete_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !activeVoxelModel_ && !viewportState_.HasModel() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                VoxelDocumentSaveService::TemporaryPathFor(
                    voxelSaveSmokePath_)) &&
            !std::filesystem::exists(
                VoxelDocumentSaveService::BackupPathFor(
                    voxelSaveSmokePath_));
    }
    return true;
}

bool EditorWorkspace::VoxelSaveSmokePassed() const noexcept
{
    return voxelSaveSmokePaintedAndSaved_ &&
        voxelSaveSmokePaintReloaded_ &&
        voxelSaveSmokeErasedAndSaved_ &&
        voxelSaveSmokeEraseReloaded_ &&
        voxelSaveSmokeRevisionPreserved_ &&
        voxelSaveSmokeMetadataUpdated_ &&
        voxelSaveSmokeThumbnailUpdated_ &&
        voxelSaveSmokeFailureRolledBack_ &&
        voxelSaveSmokeDirtyCloseProtected_ &&
        voxelSaveSmokeCleanupComplete_;
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
            !voxelSaveState_.IsDirty() && voxelEditHistory_.CanUndo();
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
            !voxelSaveState_.IsDirty() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo();
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
        static_cast<void>(paletteService_.SelectColor(1U));
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
        static_cast<void>(paletteService_.SelectColor(1U));
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

bool EditorWorkspace::RunVoxelUndoRedoSmokeStep(
    const std::size_t frame,
    const std::filesystem::path& sourcePath)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const auto metrics = [this, document]()
    {
        return std::array<std::uint64_t, 3U>{
            document ? document->GetRevision() : 0U,
            static_cast<std::uint64_t>(voxelDocumentMeshCache_.BuildCount()),
            static_cast<std::uint64_t>(viewportRenderer_.ModelUploadCount())};
    };
    const auto advancedOnce = [this, document](
        const std::array<std::uint64_t, 3U>& before)
    {
        return document != nullptr &&
            document->GetRevision() == before[0] + 1U &&
            voxelDocumentMeshCache_.BuildCount() == before[1] + 1U &&
            viewportRenderer_.ModelUploadCount() == before[2] + 1U;
    };

    if (frame == 0U)
    {
        if (document == nullptr || document->GetVoxelCount() != 1U ||
            !voxelEditHistory_.IsAtSavedState() ||
            voxelEditHistory_.CanUndo() || document->IsDirty()) return false;
        std::error_code error;
        voxelUndoRedoSmokeSourceSize_ =
            std::filesystem::file_size(sourcePath, error);
        voxelUndoRedoSmokeSourceTime_ =
            std::filesystem::last_write_time(sourcePath, error);
        const auto hash = HashFileContents(sourcePath);
        if (error || !hash || GetBackendDisplayName() != "Direct3D 12")
            return false;
        voxelUndoRedoSmokeSourceHash_ = *hash;
        voxelUndoRedoSmokeInitialRevision_ = document->GetRevision();
        voxelUndoRedoSmokeInitialVoxelCount_ = document->GetVoxelCount();
        voxelUndoRedoSmokeInitialBuildCount_ =
            voxelDocumentMeshCache_.BuildCount();
        voxelUndoRedoSmokeInitialUploadCount_ =
            viewportRenderer_.ModelUploadCount();

        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        static_cast<void>(paletteService_.SelectColor(1U));
        const auto hit = RaycastVoxelDocument(
            *document, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit) return false;
        voxelUndoRedoSmokePencilTarget_ = hit->AdjacentPosition;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        const auto before = metrics();
        voxelUndoRedoSmokePencilExecuted_ = ApplyVoxelPencil() &&
            advancedOnce(before) &&
            document->HasVoxel(voxelUndoRedoSmokePencilTarget_) &&
            document->IsDirty() && voxelEditHistory_.UndoCount() == 1U &&
            voxelEditHistory_.UndoLabel() == "Add Voxel" &&
            !voxelEditHistory_.CanRedo();
    }
    else if (frame == 1U)
    {
        if (document == nullptr || !voxelUndoRedoSmokePencilExecuted_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult undone = voxelEditHistory_.Undo(*this);
        voxelUndoRedoSmokePencilUndone_ = undone && advancedOnce(before) &&
            !document->HasVoxel(voxelUndoRedoSmokePencilTarget_) &&
            document->GetVoxelCount() == voxelUndoRedoSmokeInitialVoxelCount_ &&
            !document->IsDirty() && voxelEditHistory_.IsAtSavedState() &&
            voxelEditHistory_.CanRedo();
    }
    else if (frame == 2U)
    {
        if (document == nullptr || !voxelUndoRedoSmokePencilUndone_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult redone = voxelEditHistory_.Redo(*this);
        voxelUndoRedoSmokePencilRedone_ = redone && advancedOnce(before) &&
            document->HasVoxel(voxelUndoRedoSmokePencilTarget_) &&
            document->IsDirty() && !voxelEditHistory_.CanRedo();
    }
    else if (frame == 3U)
    {
        if (document == nullptr || !voxelUndoRedoSmokePencilRedone_)
            return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Eraser);
        const auto hit = RaycastVoxelDocument(
            *document, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
        if (!hit || hit->Coordinates != VoxelCoordinates{0U, 1U, 1U})
            return false;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        const auto before = metrics();
        const bool erased = ApplyVoxelEraser();
        voxelUndoRedoSmokeEraserCycle_ = erased && advancedOnce(before) &&
            lastVoxelEraserResult_ &&
            lastVoxelEraserResult_->RemovedPaletteIndex == 1U &&
            !document->HasVoxel(voxelUndoRedoSmokePencilTarget_) &&
            voxelEditHistory_.UndoLabel() == "Remove Voxel";
    }
    else if (frame == 4U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeEraserCycle_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult undone = voxelEditHistory_.Undo(*this);
        const auto restored = document->GetVoxel(voxelUndoRedoSmokePencilTarget_);
        voxelUndoRedoSmokeEraserCycle_ = undone && advancedOnce(before) &&
            restored && restored->PaletteIndex == 1U;
    }
    else if (frame == 5U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeEraserCycle_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult redone = voxelEditHistory_.Redo(*this);
        voxelUndoRedoSmokeEraserCycle_ = redone && advancedOnce(before) &&
            !document->HasVoxel(voxelUndoRedoSmokePencilTarget_);
    }
    else if (frame == 6U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeEraserCycle_)
            return false;
        const auto undoBefore = metrics();
        const VoxelEditHistoryResult undone = voxelEditHistory_.Undo(*this);
        if (!undone || !advancedOnce(undoBefore)) return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        const auto hit = RaycastVoxelDocument(
            *document, {{4.0F, 1.5F, 1.5F}, {-1.0F, 0.0F, 0.0F}});
        if (!hit || hit->AdjacentPosition !=
                Asset::Voxel::VoxelPosition{2, 1, 1}) return false;
        voxelUndoRedoSmokeBranchTarget_ = hit->AdjacentPosition;
        static_cast<void>(voxelSelection_.SetHovered(
            VoxelPickingInteractionState::Hit, hit));
        const auto pencilBefore = metrics();
        voxelUndoRedoSmokeBranchClearedRedo_ = ApplyVoxelPencil() &&
            advancedOnce(pencilBefore) &&
            document->HasVoxel(voxelUndoRedoSmokeBranchTarget_) &&
            !voxelEditHistory_.CanRedo() &&
            voxelEditHistory_.UndoLabel() == "Add Voxel";
    }
    else if (frame == 7U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeBranchClearedRedo_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult executed = voxelEditHistory_.Execute(
            *this,
            VoxelEditOperation{
                "Synthetic Multi Edit",
                {
                    VoxelChange{0U, {0, 0, 0}, false, 0U, true, 2U},
                    VoxelChange{0U, {2, 2, 2}, false, 0U, true, 3U},
                    VoxelChange{0U, voxelUndoRedoSmokeBranchTarget_,
                        true, 1U, false, 0U}
                }});
        voxelUndoRedoSmokeMultiExecuted_ = executed && advancedOnce(before) &&
            document->HasVoxel({0, 0, 0}) &&
            document->HasVoxel({2, 2, 2}) &&
            !document->HasVoxel(voxelUndoRedoSmokeBranchTarget_) &&
            voxelEditHistory_.UndoLabel() == "Synthetic Multi Edit";
    }
    else if (frame == 8U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeMultiExecuted_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult undone = voxelEditHistory_.Undo(*this);
        voxelUndoRedoSmokeMultiUndone_ = undone && advancedOnce(before) &&
            !document->HasVoxel({0, 0, 0}) &&
            !document->HasVoxel({2, 2, 2}) &&
            document->HasVoxel(voxelUndoRedoSmokeBranchTarget_);
    }
    else if (frame == 9U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeMultiUndone_)
            return false;
        const auto before = metrics();
        const VoxelEditHistoryResult redone = voxelEditHistory_.Redo(*this);
        voxelUndoRedoSmokeMultiRedone_ = redone && advancedOnce(before) &&
            document->HasVoxel({0, 0, 0}) &&
            document->HasVoxel({2, 2, 2}) &&
            !document->HasVoxel(voxelUndoRedoSmokeBranchTarget_) &&
            document->GetRevision() == voxelUndoRedoSmokeInitialRevision_ + 11U;
    }
    else if (frame == 10U)
    {
        if (document == nullptr || !voxelUndoRedoSmokeMultiRedone_)
            return false;
        std::error_code error;
        const auto hash = HashFileContents(sourcePath);
        voxelUndoRedoSmokeSourcePreserved_ = hash &&
            *hash == voxelUndoRedoSmokeSourceHash_ &&
            std::filesystem::file_size(sourcePath, error) ==
                voxelUndoRedoSmokeSourceSize_ && !error &&
            std::filesystem::last_write_time(sourcePath, error) ==
                voxelUndoRedoSmokeSourceTime_ && !error;
        ClearVoxelViewport();
        voxelUndoRedoSmokeClosed_ =
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo() &&
            voxelEditHistory_.EstimatedMemory() == 0U;
    }
    return VoxelUndoRedoSmokePassed();
}

bool EditorWorkspace::VoxelUndoRedoSmokePassed() const noexcept
{
    return voxelUndoRedoSmokePencilExecuted_ &&
        voxelUndoRedoSmokePencilUndone_ &&
        voxelUndoRedoSmokePencilRedone_ &&
        voxelUndoRedoSmokeEraserCycle_ &&
        voxelUndoRedoSmokeBranchClearedRedo_ &&
        voxelUndoRedoSmokeMultiExecuted_ &&
        voxelUndoRedoSmokeMultiUndone_ &&
        voxelUndoRedoSmokeMultiRedone_ &&
        voxelUndoRedoSmokeClosed_ && voxelUndoRedoSmokeSourcePreserved_;
}

bool EditorWorkspace::RunFirstCreationExperienceSmokeStep(
    const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const VoxelModelCreationResult created =
            voxelModelCreationService_.CreateModel({
                "Maison", {64U, 64U, 64U}});
        document = voxelDocumentSession_.ActiveDocument();
        firstCreationSmokePath_ = created.ModelPath;
        const std::filesystem::path expectedSelection =
            std::filesystem::path("Models") / "Maison.vox";
        firstCreationSmokeCreated_ = created.Succeeded() &&
            created.ThumbnailGenerated && created.AssetBrowserRefreshed &&
            created.Opened && document != nullptr &&
            document->SourcePath() == created.ModelPath &&
            document->GetDimensions() ==
                Asset::Voxel::VoxelDimensions{64U, 64U, 64U} &&
            document->GetVoxelCount() == 0U && !document->IsDirty() &&
            voxelEditHistory_.IsAtSavedState() &&
            assetBrowser_.SelectedRelativePath() == expectedSelection &&
            created.Metadata.Analysis && created.Metadata.Analysis->Valid &&
            created.Metadata.Analysis->VoxelCount == 0U &&
            created.Metadata.Thumbnail &&
            created.Metadata.Thumbnail->Status == ThumbnailStatus::Valid;
        if (!firstCreationSmokeCreated_) return false;
        firstCreationExperience_.Start(false);
        firstCreationExperience_.Acknowledge();
    }
    else if (frame == 1U)
    {
        if (document == nullptr || !firstCreationSmokeCreated_) return false;
        const std::uint64_t revision = document->GetRevision();
        firstCreationSmokeTarget_ = {32, 0, 32};
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid, firstCreationSmokeTarget_, 0.0F};
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        static_cast<void>(paletteService_.SelectColor(1U));
        firstCreationSmokePencilled_ = ApplyVoxelPencil() &&
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(firstCreationSmokeTarget_) &&
            document->GetRevision() == revision + 1U && document->IsDirty() &&
            voxelEditHistory_.CanUndo() &&
            firstCreationExperience_.Stage() == FirstCreationStage::Undo;
    }
    else if (frame == 2U)
    {
        if (document == nullptr || !firstCreationSmokePencilled_) return false;
        const std::uint64_t revision = document->GetRevision();
        UndoCommand();
        firstCreationSmokeUndone_ = document->GetVoxelCount() == 0U &&
            !document->HasVoxel(firstCreationSmokeTarget_) &&
            document->GetRevision() == revision + 1U && !document->IsDirty() &&
            voxelEditHistory_.CanRedo() &&
            firstCreationExperience_.Stage() == FirstCreationStage::Redo;
    }
    else if (frame == 3U)
    {
        if (document == nullptr || !firstCreationSmokeUndone_) return false;
        const std::uint64_t revision = document->GetRevision();
        RedoCommand();
        firstCreationSmokeRedone_ = document->GetVoxelCount() == 1U &&
            document->HasVoxel(firstCreationSmokeTarget_) &&
            document->GetRevision() == revision + 1U && document->IsDirty() &&
            firstCreationExperience_.Stage() == FirstCreationStage::Save;
    }
    else if (frame == 4U)
    {
        if (document == nullptr || !firstCreationSmokeRedone_) return false;
        const std::uint64_t revision = document->GetRevision();
        firstCreationSmokeSaved_ = SaveVoxelModel() &&
            !document->IsDirty() && document->GetRevision() == revision &&
            voxelEditHistory_.CanUndo() &&
            firstCreationExperience_.Stage() == FirstCreationStage::Completed &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfsave.bak");
    }
    else if (frame == 5U)
    {
        if (!firstCreationSmokeSaved_) return false;
        ClearVoxelViewport();
        if (!OpenVoxInViewportNow(firstCreationSmokePath_)) return false;
        document = voxelDocumentSession_.ActiveDocument();
        firstCreationSmokeReopened_ = document != nullptr &&
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(firstCreationSmokeTarget_) &&
            !document->IsDirty() && voxelEditHistory_.IsAtSavedState();
    }
    else if (frame == 6U)
    {
        if (!firstCreationSmokeReopened_) return false;
        CloseProject();
        firstCreationSmokeCleaned_ =
            !projectManager_.ActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfcreate.tmp") &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfcreate.bak") &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                firstCreationSmokePath_.string() + ".vfsave.bak");
    }
    return FirstCreationExperienceSmokePassed();
}

bool EditorWorkspace::FirstCreationExperienceSmokePassed() const noexcept
{
    return firstCreationSmokeCreated_ && firstCreationSmokePencilled_ &&
        firstCreationSmokeUndone_ && firstCreationSmokeRedone_ &&
        firstCreationSmokeSaved_ && firstCreationSmokeReopened_ &&
        firstCreationSmokeCleaned_;
}

bool EditorWorkspace::RunPersistentWorkplaneSmokeStep(
    const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const VoxelModelCreationResult created =
            voxelModelCreationService_.CreateModel({
                "PersistentWorkplane", {64U, 64U, 64U}});
        document = voxelDocumentSession_.ActiveDocument();
        persistentWorkplaneSmokePath_ = created.ModelPath;
        persistentWorkplaneSmokeFirst_ = {4, 0, 4};
        persistentWorkplaneSmokeSecond_ = {56, 0, 56};
        persistentWorkplaneSmokeCreated_ = created.Succeeded() &&
            created.Opened && document != nullptr &&
            document->GetVoxelCount() == 0U && !document->IsDirty() &&
            workplaneService_.Grid(*document) == WorkplaneGrid{
                {WorkplaneAxis::Y, 0}, 64U, 64U};
        if (!persistentWorkplaneSmokeCreated_) return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        static_cast<void>(paletteService_.SelectColor(1U));
    }
    else if (frame == 1U)
    {
        if (document == nullptr || !persistentWorkplaneSmokeCreated_)
            return false;
        const std::uint64_t revision = document->GetRevision();
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid,
            persistentWorkplaneSmokeFirst_,
            1.0F};
        persistentWorkplaneSmokeFirstAdded_ = ApplyVoxelPencil() &&
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(persistentWorkplaneSmokeFirst_) &&
            document->GetRevision() == revision + 1U &&
            voxelEditHistory_.UndoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (document == nullptr || !persistentWorkplaneSmokeFirstAdded_)
            return false;
        const std::uint64_t revision = document->GetRevision();
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid,
            persistentWorkplaneSmokeSecond_,
            1.0F};
        persistentWorkplaneSmokeSecondAdded_ = ApplyVoxelPencil() &&
            document->GetVoxelCount() == 2U &&
            document->HasVoxel(persistentWorkplaneSmokeFirst_) &&
            document->HasVoxel(persistentWorkplaneSmokeSecond_) &&
            document->GetRevision() == revision + 1U &&
            voxelEditHistory_.UndoCount() == 2U;
    }
    else if (frame == 3U)
    {
        if (document == nullptr || !persistentWorkplaneSmokeSecondAdded_)
            return false;
        UndoCommand();
        persistentWorkplaneSmokeUndone_ =
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(persistentWorkplaneSmokeFirst_) &&
            !document->HasVoxel(persistentWorkplaneSmokeSecond_) &&
            voxelEditHistory_.CanRedo();
    }
    else if (frame == 4U)
    {
        if (document == nullptr || !persistentWorkplaneSmokeUndone_)
            return false;
        RedoCommand();
        persistentWorkplaneSmokeRedone_ =
            document->GetVoxelCount() == 2U &&
            document->HasVoxel(persistentWorkplaneSmokeFirst_) &&
            document->HasVoxel(persistentWorkplaneSmokeSecond_) &&
            !voxelEditHistory_.CanRedo();
    }
    else if (frame == 5U)
    {
        if (document == nullptr || !persistentWorkplaneSmokeRedone_)
            return false;
        const std::uint64_t revision = document->GetRevision();
        persistentWorkplaneSmokeSaved_ = SaveVoxelModel() &&
            !document->IsDirty() && document->GetRevision() == revision &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfsave.bak");
    }
    else if (frame == 6U)
    {
        if (!persistentWorkplaneSmokeSaved_) return false;
        ClearVoxelViewport();
        if (!OpenVoxInViewportNow(persistentWorkplaneSmokePath_)) return false;
        document = voxelDocumentSession_.ActiveDocument();
        persistentWorkplaneSmokeReopened_ = document != nullptr &&
            document->GetVoxelCount() == 2U &&
            document->HasVoxel(persistentWorkplaneSmokeFirst_) &&
            document->HasVoxel(persistentWorkplaneSmokeSecond_) &&
            !document->IsDirty();
    }
    else if (frame == 7U)
    {
        if (!persistentWorkplaneSmokeReopened_) return false;
        CloseProject();
        persistentWorkplaneSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() && !workplaneHit_ &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfcreate.tmp") &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfcreate.bak") &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                persistentWorkplaneSmokePath_.string() + ".vfsave.bak");
    }
    return PersistentWorkplaneSmokePassed();
}

bool EditorWorkspace::PersistentWorkplaneSmokePassed() const noexcept
{
    return persistentWorkplaneSmokeCreated_ &&
        persistentWorkplaneSmokeFirstAdded_ &&
        persistentWorkplaneSmokeSecondAdded_ &&
        persistentWorkplaneSmokeUndone_ &&
        persistentWorkplaneSmokeRedone_ &&
        persistentWorkplaneSmokeSaved_ &&
        persistentWorkplaneSmokeReopened_ &&
        persistentWorkplaneSmokeCleaned_;
}

bool EditorWorkspace::RunProjectSessionRestoreSmokeStep(
    const std::size_t frame)
{
    if (frame == 0U)
    {
        const auto& project = projectManager_.ActiveProject();
        if (!project) return false;
        projectSessionSmokeProjectFile_ = project->ProjectFilePath();
        const VoxelModelCreationResult created =
            voxelModelCreationService_.CreateModel({
                "SessionRestore", {16U, 16U, 16U}});
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        projectSessionSmokeModelPath_ = created.ModelPath;
        viewportCamera_.Orbit(53.0F, -21.0F);
        viewportCamera_.Pan(17.0F, -8.0F, 720.0F);
        viewportCamera_.Zoom(2.0F);
        projectSessionSmokeCamera_ = viewportCamera_.CaptureState();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Eraser);
        projectSessionSmokeRevision_ = document
            ? document->GetRevision() : 0U;
        projectSessionSmokeRefreshCount_ = assetBrowser_.RefreshCount();
        projectSessionSmokeCreated_ = created.Succeeded() && created.Opened &&
            document != nullptr && !document->IsDirty() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo();
    }
    else if (frame == 1U)
    {
        if (!projectSessionSmokeCreated_) return false;
        const std::filesystem::path sessionPath =
            projectSessionService_.SessionPath();
        CloseProject();
        projectSessionSmokeSavedOnClose_ =
            !projectManager_.HasActiveProject() &&
            std::filesystem::is_regular_file(sessionPath) &&
            !std::filesystem::exists(sessionPath.string() + ".tmp") &&
            !std::filesystem::exists(sessionPath.string() + ".bak");
    }
    else if (frame == 2U)
    {
        if (!projectSessionSmokeSavedOnClose_ ||
            !OpenProject(projectSessionSmokeProjectFile_, false))
            return false;
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        projectSessionSmokeRestored_ = document != nullptr &&
            document->SourcePath().lexically_normal() ==
                projectSessionSmokeModelPath_.lexically_normal() &&
            viewportCamera_.CaptureState() == projectSessionSmokeCamera_ &&
            voxelToolState_.IsEraserActive() && !document->IsDirty() &&
            document->GetRevision() == projectSessionSmokeRevision_ &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            assetBrowser_.SelectedRelativePath() ==
                std::optional<std::filesystem::path>(
                    "Models/SessionRestore.vox") &&
            assetBrowser_.RefreshCount() ==
                projectSessionSmokeRefreshCount_ + 1U;
    }
    else if (frame == 3U)
    {
        if (!projectSessionSmokeRestored_) return false;
        const std::filesystem::path sessionPath =
            projectSessionService_.SessionPath();
        CloseProject();
        projectSessionSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(sessionPath.string() + ".tmp") &&
            !std::filesystem::exists(sessionPath.string() + ".bak");
    }
    return ProjectSessionRestoreSmokePassed();
}

bool EditorWorkspace::ProjectSessionRestoreSmokePassed() const noexcept
{
    return projectSessionSmokeCreated_ &&
        projectSessionSmokeSavedOnClose_ && projectSessionSmokeRestored_ &&
        projectSessionSmokeCleaned_;
}

bool EditorWorkspace::RunDirectCreationFlowSmokeStep(
    const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Eraser);
        const std::size_t refreshBaseline = assetBrowser_.RefreshCount();
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_,
            {"DirectCreation", {16U, 16U, 16U}});
        document = voxelDocumentSession_.ActiveDocument();
        directCreationSmokePath_ = flow.Creation.ModelPath;
        directCreationSmokeTarget_ = {8, 0, 8};
        EditorCamera expectedCamera;
        expectedCamera.Frame(16.0F, 16.0F, 16.0F);
        directCreationSmokeCreated_ = flow.Ready() &&
            flow.Creation.ThumbnailGenerated && document != nullptr &&
            document->SourcePath().lexically_normal() ==
                directCreationSmokePath_.lexically_normal() &&
            document->GetVoxelCount() == 0U && !document->IsDirty() &&
            voxelToolState_.IsPencilActive() && viewportState_.HasModel() &&
            viewportState_.IsGridVisible() &&
            workplaneService_.Grid(*document).has_value() &&
            viewportCamera_.CaptureState() == expectedCamera.CaptureState() &&
            assetBrowser_.SelectedRelativePath() ==
                std::optional<std::filesystem::path>(
                    "Models/DirectCreation.vox") &&
            assetBrowser_.RefreshCount() == refreshBaseline + 1U &&
            viewportFocusRequested_ &&
            std::filesystem::is_regular_file(directCreationSmokePath_) &&
            std::filesystem::is_regular_file(
                directCreationSmokePath_.string() + ".vfmeta");
    }
    else if (frame == 1U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!directCreationSmokeCreated_ || document == nullptr) return false;
        directCreationSmokeFocused_ = viewportFocusApplied_;
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid, directCreationSmokeTarget_, 1.0F};
        const std::uint64_t revision = document->GetRevision();
        directCreationSmokePencilled_ = directCreationSmokeFocused_ &&
            ApplyVoxelPencil() && document->GetVoxelCount() == 1U &&
            document->HasVoxel(directCreationSmokeTarget_) &&
            document->GetRevision() == revision + 1U &&
            document->IsDirty() && voxelEditHistory_.CanUndo();
    }
    else if (frame == 2U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!directCreationSmokePencilled_ || document == nullptr) return false;
        const std::uint64_t revision = document->GetRevision();
        directCreationSmokeSaved_ = SaveVoxelModel() &&
            !document->IsDirty() && document->GetRevision() == revision;
    }
    else if (frame == 3U)
    {
        if (!directCreationSmokeSaved_) return false;
        CloseProject();
        directCreationSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() && !workplaneHit_ &&
            !viewportFocusRequested_ && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                directCreationSmokePath_.string() + ".vfcreate.tmp") &&
            !std::filesystem::exists(
                directCreationSmokePath_.string() + ".vfcreate.bak") &&
            !std::filesystem::exists(
                directCreationSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                directCreationSmokePath_.string() + ".vfsave.bak");
    }
    return DirectCreationFlowSmokePassed();
}

bool EditorWorkspace::DirectCreationFlowSmokePassed() const noexcept
{
    return directCreationSmokeCreated_ && directCreationSmokeFocused_ &&
        directCreationSmokePencilled_ && directCreationSmokeSaved_ &&
        directCreationSmokeCleaned_;
}

bool EditorWorkspace::RunPaletteUiSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        resetLayoutRequested_ = true;
    }
    else if (frame == 1U)
    {
        const auto& project = projectManager_.ActiveProject();
        if (!project) return false;
        paletteSmokeProjectFile_ = project->ProjectFilePath();
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"PaletteSmoke", {16U, 16U, 16U}});
        paletteSmokeModelPath_ = flow.Creation.ModelPath;
        paletteSmokeTarget_ = {7, 0, 7};
        document = voxelDocumentSession_.ActiveDocument();
        const bool selected = paletteService_.SelectColor(paletteSmokeIndex_);
        const std::optional<PaletteColorSelection> active =
            paletteService_.ActiveColor();
        if (active) paletteSmokeColor_ = active->Color;
        paletteSmokeCreated_ = flow.Ready() && document != nullptr &&
            selected && active && active->Index == paletteSmokeIndex_ &&
            paletteService_.ActivePalette() != nullptr &&
            paletteService_.ActivePalette()->size() ==
                PaletteService::PaletteSize &&
            paletteService_.RecentColors().empty();
        if (!paletteSmokeCreated_) return false;
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid, paletteSmokeTarget_, 1.0F};
        paletteSmokePencilled_ = ApplyVoxelPencil() &&
            document->GetVoxel(paletteSmokeTarget_).has_value() &&
            document->GetVoxel(paletteSmokeTarget_)->PaletteIndex ==
                paletteSmokeIndex_ &&
            paletteService_.RecentColors().size() == 1U &&
            paletteService_.RecentColors().front().Index == paletteSmokeIndex_;
    }
    else if (frame == 2U)
    {
        const ImGuiWindow* inspectorWindow =
            ImGui::FindWindowByName("Inspector");
        const ImGuiWindow* paletteWindow = ImGui::FindWindowByName("Palette");
        paletteSmokeLayoutValid_ = inspectorWindow != nullptr &&
            paletteWindow != nullptr && inspectorWindow->DockNode != nullptr &&
            paletteWindow->DockNode != nullptr &&
            inspectorWindow->DockNode != paletteWindow->DockNode &&
            paletteWindow->Pos.y > inspectorWindow->Pos.y &&
            std::abs(paletteWindow->Pos.x - inspectorWindow->Pos.x) <= 1.0F &&
            std::abs(paletteWindow->Size.x - inspectorWindow->Size.x) <= 1.0F;
        if (!paletteSmokePencilled_ || !SaveVoxelModel()) return false;
        CloseProject();
        paletteSmokeSavedAndClosed_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !paletteService_.HasActivePalette() &&
            paletteService_.RecentColors().empty();
    }
    else if (frame == 3U)
    {
        if (!paletteSmokeSavedAndClosed_ ||
            !OpenProject(paletteSmokeProjectFile_, false))
            return false;
        document = voxelDocumentSession_.ActiveDocument();
        const std::optional<PaletteColorSelection> active =
            paletteService_.ActiveColor();
        const std::optional<Asset::Voxel::Voxel> voxel = document
            ? document->GetVoxel(paletteSmokeTarget_) : std::nullopt;
        paletteSmokeRestored_ = document != nullptr && active && voxel &&
            active->Index == paletteSmokeIndex_ &&
            active->Color == paletteSmokeColor_ &&
            voxel->PaletteIndex == paletteSmokeIndex_ &&
            paletteService_.RecentColors().empty() &&
            !document->IsDirty() && !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo();
    }
    else if (frame == 4U)
    {
        if (!paletteSmokeRestored_) return false;
        CloseProject();
        const std::filesystem::path session =
            paletteSmokeProjectFile_.parent_path() / ".vfsession";
        paletteSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !paletteService_.HasActivePalette() &&
            paletteService_.RecentColors().empty() &&
            !std::filesystem::exists(session.string() + ".tmp") &&
            !std::filesystem::exists(session.string() + ".bak") &&
            !std::filesystem::exists(
                paletteSmokeModelPath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                paletteSmokeModelPath_.string() + ".vfsave.bak");
    }
    return PaletteUiSmokePassed();
}

bool EditorWorkspace::PaletteUiSmokePassed() const noexcept
{
    return paletteSmokeLayoutValid_ && paletteSmokeCreated_ &&
        paletteSmokePencilled_ &&
        paletteSmokeSavedAndClosed_ && paletteSmokeRestored_ &&
        paletteSmokeCleaned_;
}

bool EditorWorkspace::RunLayoutStabilitySmokeStep(const std::size_t frame)
{
    if (frame == 1U) return false;
    const std::size_t scenarioFrame = frame > 1U ? frame - 1U : frame;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const auto recordRectangle = [this]()
    {
        const ViewportRectangle& rectangle = currentViewportRectangle_;
        if (rectangle.Width <= 1.0F || rectangle.Height <= 1.0F)
            return false;
        layoutStabilitySmokeRectangles_.push_back(rectangle);
        const ViewportRectangle& baseline =
            layoutStabilitySmokeRectangles_.front();
        layoutStabilitySmokeRectanglesStable_ =
            layoutStabilitySmokeRectanglesStable_ &&
            rectangle.X == baseline.X && rectangle.Y == baseline.Y &&
            rectangle.Width == baseline.Width &&
            rectangle.Height == baseline.Height;
        return layoutStabilitySmokeRectanglesStable_;
    };
    const auto setPickingState = [this](
        const VoxelPickingInteractionState state,
        std::optional<VoxelRaycastHit> hit = std::nullopt,
        std::optional<Asset::Voxel::VoxelPosition> construction = std::nullopt)
    {
        layoutStabilitySmokePickingOverride_ = state;
        layoutStabilitySmokeHitOverride_ = std::move(hit);
        layoutStabilitySmokeWorkplaneOverride_ = construction;
    };
    const auto hitFor = [this, document](const VoxelHitFace face)
    {
        VoxelRaycastHit hit;
        hit.Coordinates = {
            static_cast<std::uint32_t>(layoutStabilitySmokeTarget_.X),
            static_cast<std::uint32_t>(layoutStabilitySmokeTarget_.Y),
            static_cast<std::uint32_t>(layoutStabilitySmokeTarget_.Z)};
        hit.Face = face;
        hit.Distance = 8.0F;
        hit.ColorIndex = 1U;
        hit.SubModelIndex = 0U;
        const Asset::Voxel::VoxelPosition normal =
            VoxelHitFaceIntegerNormal(face);
        hit.AdjacentPosition = {
            layoutStabilitySmokeTarget_.X + normal.X,
            layoutStabilitySmokeTarget_.Y + normal.Y,
            layoutStabilitySmokeTarget_.Z + normal.Z};
        hit.AdjacentWithinBounds = true;
        hit.DocumentRevision = document ? document->GetRevision() : 0U;
        return hit;
    };

    if (scenarioFrame == 0U)
    {
        const VoxelModelCreationResult created =
            voxelModelCreationService_.CreateModel({
                "LayoutStability", {64U, 64U, 64U}});
        document = voxelDocumentSession_.ActiveDocument();
        layoutStabilitySmokePath_ = created.ModelPath;
        layoutStabilitySmokeTarget_ = {32, 0, 32};
        layoutStabilitySmokeRectanglesStable_ = true;
        layoutStabilitySmokeCreated_ = created.Succeeded() &&
            created.Opened && document != nullptr &&
            document->GetVoxelCount() == 0U && !document->IsDirty();
        if (!layoutStabilitySmokeCreated_) return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        setPickingState(
            VoxelPickingInteractionState::NoHit, std::nullopt,
            layoutStabilitySmokeTarget_);
    }
    else if (scenarioFrame == 1U)
    {
        if (!layoutStabilitySmokeCreated_ || document == nullptr ||
            !recordRectangle() ||
            voxelPlacementPreview_.Status !=
                VoxelPlacementPreviewStatus::Valid)
            return false;
        layoutStabilitySmokePencilled_ = ApplyVoxelPencil() &&
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(layoutStabilitySmokeTarget_) &&
            document->IsDirty() && voxelEditHistory_.CanUndo();
        setPickingState(
            VoxelPickingInteractionState::Hit,
            hitFor(VoxelHitFace::PositiveY));
    }
    else if (scenarioFrame == 2U)
    {
        if (!layoutStabilitySmokePencilled_ || !recordRectangle()) return false;
        setPickingState(
            VoxelPickingInteractionState::Hit,
            hitFor(VoxelHitFace::PositiveX));
    }
    else if (scenarioFrame == 3U)
    {
        if (!recordRectangle()) return false;
        setPickingState(VoxelPickingInteractionState::NoHit);
    }
    else if (scenarioFrame == 4U)
    {
        if (document == nullptr || !recordRectangle()) return false;
        UndoCommand();
        layoutStabilitySmokeUndone_ = document->GetVoxelCount() == 0U &&
            !document->HasVoxel(layoutStabilitySmokeTarget_) &&
            voxelEditHistory_.CanRedo();
        setPickingState(
            VoxelPickingInteractionState::NoHit, std::nullopt,
            layoutStabilitySmokeTarget_);
    }
    else if (scenarioFrame == 5U)
    {
        if (!layoutStabilitySmokeUndone_ || !recordRectangle()) return false;
        RedoCommand();
        layoutStabilitySmokeRedone_ = document != nullptr &&
            document->GetVoxelCount() == 1U &&
            document->HasVoxel(layoutStabilitySmokeTarget_);
        setPickingState(
            VoxelPickingInteractionState::Hit,
            hitFor(VoxelHitFace::NegativeZ));
    }
    else if (scenarioFrame == 6U)
    {
        if (!layoutStabilitySmokeRedone_ || !recordRectangle()) return false;
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Eraser);
        setPickingState(
            VoxelPickingInteractionState::Hit,
            hitFor(VoxelHitFace::NegativeX));
    }
    else if (scenarioFrame == 7U)
    {
        if (!recordRectangle() ||
            voxelToolState_.ActiveTool() != ActiveVoxelTool::Eraser)
            return false;
        setPickingState(VoxelPickingInteractionState::OutsideViewport);
    }
    else if (scenarioFrame == 8U)
    {
        if (!recordRectangle()) return false;
        layoutStabilitySmokeRectanglesStable_ =
            layoutStabilitySmokeRectanglesStable_ &&
            layoutStabilitySmokeRectangles_.size() == 8U;
    }
    else if (scenarioFrame == 9U)
    {
        if (!layoutStabilitySmokeRectanglesStable_) return false;
        layoutStabilitySmokeReadyForShutdown_ =
            projectManager_.ActiveProject() &&
            voxelDocumentSession_.HasActiveDocument() &&
            !std::filesystem::exists(
                layoutStabilitySmokePath_.string() + ".vfcreate.tmp") &&
            !std::filesystem::exists(
                layoutStabilitySmokePath_.string() + ".vfcreate.bak");
    }
    return LayoutStabilitySmokePassed();
}

bool EditorWorkspace::LayoutStabilitySmokePassed() const noexcept
{
    return layoutStabilitySmokeCreated_ && layoutStabilitySmokePencilled_ &&
        layoutStabilitySmokeUndone_ && layoutStabilitySmokeRedone_ &&
        layoutStabilitySmokeRectanglesStable_ &&
        layoutStabilitySmokeReadyForShutdown_;
}

bool EditorWorkspace::RunDoubleClickCameraSmokeStep(const std::size_t frame)
{
    const auto applyActions = [this](const ViewportCameraActions& actions)
    {
        if (actions.FrameRequested) FrameVoxelViewport();
        if (actions.ResetRequested) viewportCamera_.Reset();
    };
    const auto verifyDoubleClick = [this, &applyActions]()
    {
        const ViewportCameraActions actions =
            ResolveViewportCameraActions({true, false, false, 2U});
        applyActions(actions);
        return !actions.FrameRequested && !actions.ResetRequested &&
            viewportCamera_.CaptureState() ==
                doubleClickCameraSmokeReference_;
    };

    switch (frame)
    {
    case 0U:
        if (!projectManager_.HasActiveProject() ||
            voxelDocumentSession_.ActiveDocument() == nullptr ||
            !viewportState_.HasModel())
        {
            return false;
        }
        FrameVoxelViewport();
        viewportCamera_.Orbit(23.0F, -11.0F);
        viewportCamera_.Pan(17.0F, -9.0F, 720.0F);
        viewportCamera_.Zoom(1.0F);
        doubleClickCameraSmokeReference_ = viewportCamera_.CaptureState();
        return true;
    case 1U:
        doubleClickCameraSmokeGridStable_ = verifyDoubleClick();
        return doubleClickCameraSmokeGridStable_;
    case 2U:
        doubleClickCameraSmokeVoxelStable_ = verifyDoubleClick();
        return doubleClickCameraSmokeVoxelStable_;
    case 3U:
        doubleClickCameraSmokeEmptyStable_ = verifyDoubleClick();
        return doubleClickCameraSmokeEmptyStable_;
    case 4U:
    {
        const EditorCameraState before = viewportCamera_.CaptureState();
        viewportCamera_.Orbit(12.0F, -8.0F);
        const EditorCameraState after = viewportCamera_.CaptureState();
        doubleClickCameraSmokeOrbitWorked_ =
            after.RotationDegrees != before.RotationDegrees &&
            after.Target == before.Target && after.Distance == before.Distance;
        return doubleClickCameraSmokeOrbitWorked_;
    }
    case 5U:
    {
        const EditorCameraState before = viewportCamera_.CaptureState();
        viewportCamera_.Pan(-14.0F, 7.0F, 720.0F);
        const EditorCameraState after = viewportCamera_.CaptureState();
        doubleClickCameraSmokePanWorked_ =
            after.Target != before.Target &&
            after.RotationDegrees == before.RotationDegrees &&
            after.Distance == before.Distance;
        return doubleClickCameraSmokePanWorked_;
    }
    case 6U:
    {
        const EditorCameraState before = viewportCamera_.CaptureState();
        viewportCamera_.Zoom(-1.0F);
        const EditorCameraState after = viewportCamera_.CaptureState();
        doubleClickCameraSmokeZoomWorked_ =
            after.Distance != before.Distance &&
            after.RotationDegrees == before.RotationDegrees &&
            after.Target == before.Target;
        return doubleClickCameraSmokeZoomWorked_;
    }
    case 7U:
    {
        const ViewportCameraActions actions =
            ResolveViewportCameraActions({true, true, false, 0U});
        applyActions(actions);
        doubleClickCameraSmokeShortcutsWorked_ =
            actions.FrameRequested && !actions.ResetRequested &&
            viewportCamera_.GetTarget() == Vec3{};
        return doubleClickCameraSmokeShortcutsWorked_;
    }
    case 8U:
    {
        const ViewportCameraActions actions =
            ResolveViewportCameraActions({true, false, true, 0U});
        applyActions(actions);
        EditorCamera defaultCamera;
        doubleClickCameraSmokeShortcutsWorked_ =
            doubleClickCameraSmokeShortcutsWorked_ &&
            !actions.FrameRequested && actions.ResetRequested &&
            viewportCamera_.CaptureState() == defaultCamera.CaptureState();
        return doubleClickCameraSmokeShortcutsWorked_;
    }
    case 9U:
        CloseProject();
        doubleClickCameraSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasHighlightMesh() &&
            !viewportState_.HasModel() &&
            !voxelPlacementPreview_.IsVisible() &&
            !workplaneHit_ &&
            !voxelEditHistory_.CanUndo() &&
            !voxelEditHistory_.CanRedo();
        return doubleClickCameraSmokeCleaned_;
    default:
        return DoubleClickCameraSmokePassed();
    }
}

bool EditorWorkspace::DoubleClickCameraSmokePassed() const noexcept
{
    return doubleClickCameraSmokeGridStable_ &&
        doubleClickCameraSmokeVoxelStable_ &&
        doubleClickCameraSmokeEmptyStable_ &&
        doubleClickCameraSmokeOrbitWorked_ &&
        doubleClickCameraSmokePanWorked_ &&
        doubleClickCameraSmokeZoomWorked_ &&
        doubleClickCameraSmokeShortcutsWorked_ &&
        doubleClickCameraSmokeCleaned_;
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
    CommandResult result;
    if (Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument())
    {
        const Asset::Voxel::VoxelPosition position{
            static_cast<std::int32_t>(coordinates.X),
            static_cast<std::int32_t>(coordinates.Y),
            static_cast<std::int32_t>(coordinates.Z)};
        const auto voxel = document->GetVoxel(position, selected->SubModelIndex);
        if (!voxel)
            result = CommandResult::Failure(
                "Selected voxel is absent from the active document.");
        else
        {
            const VoxelEditHistoryResult historyResult =
                voxelEditHistory_.Execute(*this, {
                    "Erase Voxel",
                    {VoxelChange{
                        selected->SubModelIndex, position,
                        true, voxel->PaletteIndex, false, 0U}}});
            result = historyResult
                ? CommandResult::Success()
                : CommandResult::Failure(historyResult.Message);
        }
    }
    else
    {
        result = commandHistory_.Execute(
            std::make_unique<EraseVoxelCommand>(
                static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
                coordinates.X, coordinates.Y, coordinates.Z));
    }
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
    CommandResult result;
    if (Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument())
    {
        const Asset::Voxel::VoxelPosition position{
            static_cast<std::int32_t>(coordinates.X),
            static_cast<std::int32_t>(coordinates.Y),
            static_cast<std::int32_t>(coordinates.Z)};
        const auto voxel = document->GetVoxel(position, selected->SubModelIndex);
        if (!voxel)
            result = CommandResult::Failure(
                "Selected voxel is absent from the active document.");
        else
        {
            const VoxelEditHistoryResult historyResult =
                voxelEditHistory_.Execute(*this, {
                    "Paint Voxel",
                    {VoxelChange{
                        selected->SubModelIndex, position,
                        true, voxel->PaletteIndex, true, colorIndex}}});
            result = historyResult
                ? CommandResult::Success()
                : CommandResult::Failure(historyResult.Message);
        }
    }
    else
    {
        result = commandHistory_.Execute(
            std::make_unique<PaintVoxelCommand>(
                static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
                coordinates.X, coordinates.Y, coordinates.Z, colorIndex));
    }
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
    CommandResult result;
    if (voxelDocumentSession_.HasActiveDocument())
    {
        const VoxelEditHistoryResult historyResult =
            voxelEditHistory_.Execute(*this, {
                "Add Voxel",
                {VoxelChange{
                    0U,
                    {
                        static_cast<std::int32_t>(destination.X),
                        static_cast<std::int32_t>(destination.Y),
                        static_cast<std::int32_t>(destination.Z)},
                    false,
                    0U,
                    true,
                    paintPaletteSelection_.Index()}}});
        result = historyResult
            ? CommandResult::Success()
            : CommandResult::Failure(historyResult.Message);
    }
    else
    {
        result = commandHistory_.Execute(
            std::make_unique<AddVoxelCommand>(
                static_cast<VoxelEditSession&>(*this), voxelModelGeneration_,
                destination.X, destination.Y, destination.Z,
                paintPaletteSelection_.Index()));
    }
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
    const std::uint64_t voxelCountBefore =
        voxelDocumentSession_.ActiveDocument()
        ? voxelDocumentSession_.ActiveDocument()->GetVoxelCount() : 0U;
    voxelEditInProgress_ = true;
    VoxelToolResult result;
    const std::optional<PaletteColorSelection> activeColor =
        paletteService_.ActiveColor();
    try
    {
        result = VoxelPencilTool::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            activeColor ? activeColor->Index : 0U,
            !voxelToolState_.IsPencilActive(),
            &voxelEditHistory_,
            workplaneHit_ ? workplaneHit_->Position : std::nullopt});
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
    viewportFocusRequested_ = false;
    viewportFocusApplied_ = false;
    lastVoxelToolResult_ = result;

    if (result.Code == VoxelToolResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        workplaneHit_.reset();
        if (voxelCountBefore == 0U)
            firstCreationExperience_.OnFirstVoxelCreated();
        AddConsoleMessage(
            "[Edit] Added voxel at (" +
            std::to_string(result.Position.X) + ", " +
            std::to_string(result.Position.Y) + ", " +
            std::to_string(result.Position.Z) +
            ") using palette index " +
            std::to_string(activeColor ? activeColor->Index : 0U) + ".");
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
            !voxelToolState_.IsEraserActive(),
            &voxelEditHistory_});
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

void EditorWorkspace::UpdateVoxelEditSavedState(
    const bool isAtSavedState) noexcept
{
    voxelSaveState_.UpdateFromHistory(isAtSavedState);
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
            true,
            workplaneHit_ ? workplaneHit_->Position : std::nullopt);
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
    voxelEditHistory_.Clear();
    voxelHistoryInput_.Reset();
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
    paletteService_.Clear();
    paletteColorEditorIndex_.reset();
    voxelToolInput_.Reset();
    voxelToolSmokeInput_.Reset();
    voxelPlacementPreview_ = {};
    workplaneHit_.reset();
    currentViewportRectangle_ = {};
    layoutStabilitySmokePickingOverride_.reset();
    layoutStabilitySmokeHitOverride_.reset();
    layoutStabilitySmokeWorkplaneOverride_.reset();
    firstCreationExperience_.Hide();
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
