#include "EditorWorkspace.h"
#include "EditorWorkspaceUiHelpers.h"
#include "Layout/EditorPanelNames.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelSelection/VoxelRayTransform.h"
#include "EditorWindowTitle.h"
#include "Layout/EditorDockLayout.h"
#include "Layout/PalettePanelLayout.h"
#include "ProjectSession/ProjectSessionMapping.h"
#include "Dialogs/EditorDialogStyle.h"
#include "Toolbar/EditorToolbar.h"
#include "Tools/ToolPanel.h"
#include "TransformGizmo/GizmoStyle.h"

#include "VoxelForge/Project/Project.h"
#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
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
#include <functional>
#include <iomanip>
#include <optional>
#include <memory>
#include <limits>
#include <string_view>
#include <sstream>
#include <span>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
// VF-0260 (nettoyage) : les noms de panneaux viennent de la source de
// vérité partagée Layout/EditorPanelNames.h — plus de littéraux locaux.
constexpr float StatusBarHeight = PanelNames::StatusBarHeight;
constexpr std::size_t MaximumConsoleMessageCount = 200;
constexpr const char* WorkspaceDockspaceName = PanelNames::WorkspaceDockspace;
constexpr const char* ToolsPanelWindowName = PanelNames::Tools;
constexpr const char* ToolOptionsPanelWindowName = PanelNames::ToolOptions;
constexpr const char* StylePanelWindowName = PanelNames::Style;
constexpr const char* ViewportPanelWindowName = PanelNames::Viewport;
constexpr const char* AssetsPanelWindowName = PanelNames::Assets;
constexpr const char* ForgeLibraryPanelWindowName = PanelNames::ForgeLibrary;
constexpr const char* ScenePanelWindowName = PanelNames::Scene;
constexpr const char* InspectorPanelWindowName = PanelNames::Inspector;
constexpr const char* TransformPanelWindowName = PanelNames::Transform;
constexpr const char* AboutPopupName = "About VoxelForge Studio";
constexpr const char* DirtyConfirmationPopupName = "Unsaved Voxel Model";
constexpr const char* ImportConfirmationPopupName = "Import Models";
constexpr const char* ImportCollisionPopupName = "Model Already Exists";
constexpr const char* OpenImportedModelPopupName = "Open Imported Model";
constexpr const char* DeleteProjectPopupName = "Delete VoxelForge Project";

bool HasAllCreateWorkspaceSettings() noexcept
{
    constexpr std::array<const char*, 10U> officialWindowNames = {
        ToolsPanelWindowName, ToolOptionsPanelWindowName, StylePanelWindowName,
        ViewportPanelWindowName, AssetsPanelWindowName,
        ForgeLibraryPanelWindowName, ScenePanelWindowName,
        InspectorPanelWindowName, TransformPanelWindowName, "Console"};
    for (const char* const windowName : officialWindowNames)
    {
        if (ImGui::FindWindowSettingsByID(ImHashStr(windowName)) == nullptr)
            return false;
    }
    return true;
}

void LoadLegacyWorkspaceSettingsForSmoke()
{
    constexpr const char legacyIni[] =
        "[Window][Explorer]\n"
        "Pos=0,0\nSize=260,620\n"
        "[Window][Scene]\n"
        "Pos=260,0\nSize=760,620\n"
        "[Window][Palette]\n"
        "Pos=1020,0\nSize=260,300\n"
        "[Window][Asset Browser]\n"
        "Pos=0,620\nSize=760,180\n"
        "[Window][Inspector]\n"
        "Pos=1020,300\nSize=260,240\n"
        "[Window][Transform]\n"
        "Pos=1020,540\nSize=260,260\n"
        "[Window][Console]\n"
        "Pos=760,620\nSize=520,180\n";
    ImGui::ClearIniSettings();
    ImGui::LoadIniSettingsFromMemory(legacyIni, sizeof(legacyIni) - 1U);
}

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

// DrawErrorMessage/DrawTooltip/DrawSelectionHandles/SetSelectionHandleCursor
// vivent desormais dans EditorWorkspaceUiHelpers.h (source partagee).

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

[[maybe_unused]] void DrawInspectorDiagnostics(const InspectorLayoutModel& model)
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
    const bool simulatedFileDialogs,
    Core::UserDataPaths userDataPaths)
    : projectManager_(projectManager),
      windowTitleCallback_(std::move(windowTitleCallback)),
      fileDialogService_(simulatedFileDialogs
          ? CreateSimulatedFileDialogService()
          : CreateSDLFileDialogService()),
      projectFolderOpener_(CreateSDLProjectFolderOpener()),
      stampUserLibraryRepository_(userDataPaths),
      transformGizmoManager_(
          transformGizmoModel_, transformGizmoInteraction_,
          transformPivotManager_),
      projectDialogPreferences_(std::move(preferencesFilePath)),
      stampPreview_(
          stampPlacementSession_,
          voxelDocumentSession_,
          voxelEditHistory_,
          static_cast<VoxelEditSession&>(*this),
          console_,
          voxelEditInProgress_,
          [this] { UpdateVoxelHighlights(); })
{
    console_.AddMessage("Console ready");
    console_.AddMessage("VoxelForge Studio initialized");
    toolContext_.Constraints = &constraintSettings_;
    toolContext_.PivotManager = &transformPivotManager_;
    toolContext_.BrushProfiles = &brushProfileService_;
    toolContext_.ActivePaletteIndex = [this]() { return paletteService_.ActiveIndex(); };
    toolContext_.SelectPaletteIndex = [this](const std::size_t index) {
        return paletteService_.SelectColor(index);
    };
    toolContext_.IsGeometryCylinderHeightPhase = [this]() {
        return smartToolStroke_.IsActive() &&
            smartGeometryLockedMode_ == SmartToolMode::CylinderBrush &&
            smartGeometryPhase_ == SmartGeometryInteractionPhase::Height;
    };
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
            if (!paletteService_.ActiveColor())
            {
                static_cast<void>(paletteService_.SelectColor(
                    PaletteService::FirstSelectableIndex));
            }
            voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
            voxelToolInput_.Reset();
            UpdateVoxelHighlights();
            const bool ready = voxelToolState_.IsPencilActive() &&
                paletteService_.ActiveColor().has_value();
            return DirectCreationStepResult{ready,
                ready ? std::string{}
                      : "Pencil or its active color could not be prepared."};
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
    ++drawFrameIndex_;
    // PERF-01: close the previous frame (event handling included) and open
    // the next one; slow frames are summarized once in the console.
    const double probeNowMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (const auto slowFrameSummary =
            frameProbe_.FrameBoundary(probeNowMilliseconds))
    {
        AddConsoleMessage(*slowFrameSummary);
        // PERF-02d: mirror slow-frame summaries into a plain-text log next to
        // the executable so measurement sessions need no UI interaction.
        std::ofstream perfLog("voxelforge-perf.log", std::ios::app);
        if (perfLog) perfLog << *slowFrameSummary << '\n';
    }

    if (closeRequest_.IsClosing())
    {
        if (closeRequest_.ConsumeCloseRequest()) exitRequest_.RequestExit();
        return;
    }

    ProcessDeferredDirtyActionAtFrameStart();
    ConsumeFileDialogResult();
    if (!SynchronizeVoxelDocumentRendering())
        voxelViewportRenderFailed_ = true;
    HandleCommandShortcuts();
    DrawMainMenuBar();

    if (!WelcomeScreenModel::ShowEditorPanels(
            projectManager_.HasActiveProject()))
    {
        viewportDropRect_ = {};
        assetBrowserDropRect_ = {};
        DrawWelcomeScreen();
        DrawStatusBar();
        DrawAboutPopup();
        DrawProjectDialogs();
        DrawProjectDeletionDialog();
        CompleteDeferredCloseAfterFrame();
        return;
    }

    const ImGuiID dockspaceId = ImGui::GetID(WorkspaceDockspaceName);
    DrawEditorDockSpace(dockspaceId);

    bool layoutRebuilt = false;
    if (createWorkspaceSmokeSeedLegacy_)
    {
        LoadLegacyWorkspaceSettingsForSmoke();
        createWorkspaceSettingsChecked_ = false;
        createWorkspaceMigrationApplied_ = false;
        createWorkspaceSmokeSeedLegacy_ = false;
    }
    const bool migrateOldWorkspace = !createWorkspaceSettingsChecked_ &&
        !HasAllCreateWorkspaceSettings();
    createWorkspaceSettingsChecked_ = true;
    if (migrateOldWorkspace || resetLayoutRequested_)
    {
        if (thumbnailVisualLayoutRequested_)
        {
            BuildThumbnailVisualDockLayout(dockspaceId);
            ApplyThumbnailVisualLayoutPanelVisibility();
        }
        else
        {
            BuildDefaultDockLayout(dockspaceId);
            ApplyDefaultLayoutPanelVisibility();
        }
        resetLayoutRequested_ = false;
        thumbnailVisualLayoutRequested_ = false;
        createWorkspaceMigrationApplied_ |= migrateOldWorkspace;
        layoutRebuilt = true;
    }

    if (!showScene_) viewportDropRect_ = {};
    if (!showAssetBrowser_) assetBrowserDropRect_ = {};

    if (showTools_) DrawToolsPanel();
    if (showToolOptions_) DrawToolOptionsPanel();
    if (showExplorer_) DrawExplorerPanel();
    if (showScene_) DrawScenePanel();
    if (showInspector_ && !thumbnailVisualMode_) DrawInspectorPanel();
    if (showTransformPanel_ && !thumbnailVisualMode_) DrawTransformPanel();
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
    if (showForgeLibrary_ && !thumbnailVisualMode_)
        DrawForgeLibraryPanel();
    if (showInspector_ && thumbnailVisualMode_)
    {
        ImGui::SetNextWindowDockID(0U, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(400.0F, 72.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(325.0F, 330.0F), ImGuiCond_Always);
        DrawInspectorPanel();
    }
    if (console_.IsVisible()) DrawConsolePanel();
    if (showProfiler_) DrawProfilerPanel();

    if (showImGuiDemo_)
    {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }

    if (stampPlacementRequested_)
    {
        stampPlacementRequested_ = false;
        PlaceLatestStampPreview();
    }

    DrawStatusBar();
    DrawAboutPopup();
    DrawProjectDialogs();
    DrawProjectDeletionDialog();
    DrawVoxelModelCreationDialogs();
    DrawModelImportDialogs();
    DrawDirtyConfirmationDialog();
    DrawFirstCreationOverlay();
    if (layoutRebuilt && ImGui::GetIO().IniFilename != nullptr)
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    CompleteDeferredCloseAfterFrame();
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
    const auto shortcut = [this](const EditorInputCommand command)
    {
        return editorInputService_.ShortcutLabel(command).data();
    };

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
                "New Model...", "Ctrl+Shift+N", false,
                hasActiveProject))
        {
            RequestNewVoxelModelDialog();
        }
        DrawTooltip(
            "Create a voxel model: choose its name and editable volume");

        if (ImGui::MenuItem(
                "Import Model...", "Ctrl+I", false, hasActiveProject))
        {
            RequestImportModelDialog();
        }
        DrawTooltip("Import one or more .vox models into Assets/Models");

        const bool canSaveSelectionAsStamp = hasActiveProject &&
            voxelDocumentSession_.HasActiveDocument() && !selectionService_.Empty();
        if (ImGui::MenuItem(
                "Save Selection As...", nullptr, false, canSaveSelectionAsStamp))
        {
            BeginSaveSelectionAsStamp();
        }
        DrawTooltip("Capture the current voxel selection as a reusable Project Library Stamp");

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
                shortcut(EditorInputCommand::FileSave),
                false,
                canSaveVoxel))
        {
            ExecuteInputCommand(EditorInputCommand::FileSave);
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
                undoLabel.c_str(), shortcut(EditorInputCommand::EditUndo), false,
                canUndo && !voxelEditInProgress_))
        {
            ExecuteInputCommand(EditorInputCommand::EditUndo);
        }
        const std::string undoTooltip = "Undo the last edit (" +
            std::string(editorInputService_.ShortcutLabel(
                EditorInputCommand::EditUndo)) + ")";
        DrawTooltip(undoTooltip.c_str());

        const std::string redoLabel = canRedo
            ? "Redo " + std::string(redoName)
            : "Redo";
        if (ImGui::MenuItem(
                redoLabel.c_str(), shortcut(EditorInputCommand::EditRedo), false,
                canRedo && !voxelEditInProgress_))
        {
            ExecuteInputCommand(EditorInputCommand::EditRedo);
        }
        const std::string redoTooltip = "Redo the last edit (" +
            std::string(editorInputService_.ShortcutLabel(
                EditorInputCommand::EditRedo)) + ")";
        DrawTooltip(redoTooltip.c_str());

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
                "Forge Library", nullptr, false, hasActiveProject))
        {
            showForgeLibrary_ = true;
            stampCatalogService_.InvalidateCache();
            static_cast<void>(forgeLibraryViewModel_.Refresh());
            ImGui::SetWindowFocus(ForgeLibraryPanelWindowName);
        }

        const bool liveStampPreviewActive =
            stampPlacementSession_.CurrentPreview() != nullptr;
        if (ImGui::MenuItem(
                "Move Stamp Preview +X", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(1, 0, 0);
        }

        if (ImGui::MenuItem(
                "Move Stamp Preview -X", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(-1, 0, 0);
        }

        if (ImGui::MenuItem(
                "Move Stamp Preview +Y", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(0, 1, 0);
        }

        if (ImGui::MenuItem(
                "Move Stamp Preview -Y", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(0, -1, 0);
        }

        if (ImGui::MenuItem(
                "Move Stamp Preview +Z", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(0, 0, 1);
        }

        if (ImGui::MenuItem(
                "Move Stamp Preview -Z", nullptr, false, liveStampPreviewActive))
        {
            MoveLatestStampPreview(0, 0, -1);
        }

        if (ImGui::MenuItem(
                "Rotate Stamp Preview Clockwise",
                shortcut(EditorInputCommand::RotateRight), false,
                liveStampPreviewActive))
        {
            RotateLatestStampPreview(true);
        }

        if (ImGui::MenuItem(
                "Rotate Stamp Preview Counter-Clockwise",
                shortcut(EditorInputCommand::RotateLeft), false,
                liveStampPreviewActive))
        {
            RotateLatestStampPreview(false);
        }

        if (ImGui::MenuItem(
                "Stamp Rotation Step 45 Degrees",
                shortcut(EditorInputCommand::StampToggleRotationStep),
                stampPlacementSession_.RotationStep() ==
                    Stamps::StampRotationStep::Eighth45,
                liveStampPreviewActive))
        {
            ToggleLatestStampRotationStep();
        }

        if (ImGui::MenuItem(
                "Toggle Stamp Mirror X",
                shortcut(EditorInputCommand::MirrorX), false,
                liveStampPreviewActive))
        {
            ToggleLatestStampPreview(Stamps::StampPlacementMirrorMode::X);
        }

        if (ImGui::MenuItem(
                "Toggle Stamp Mirror Z",
                shortcut(EditorInputCommand::MirrorZ), false,
                liveStampPreviewActive))
        {
            ToggleLatestStampPreview(Stamps::StampPlacementMirrorMode::Z);
        }

        if (ImGui::MenuItem(
                "Cycle Stamp Mirror",
                shortcut(EditorInputCommand::StampCycleMirror), false,
                liveStampPreviewActive))
        {
            CycleLatestStampPreview();
        }

        if (ImGui::MenuItem(
                "Reset Stamp Transform",
                shortcut(EditorInputCommand::StampResetTransform), false,
                liveStampPreviewActive))
        {
            ResetLatestStampTransform();
        }

        if (ImGui::MenuItem(
                "Place Full Stamp",
                shortcut(EditorInputCommand::TransformApply), false,
                CanPlaceLatestStampPreview()))
        {
            RequestLatestStampPlacement();
        }

        if (ImGui::MenuItem(
                "Cancel Stamp Placement", "Esc", false,
                liveStampPreviewActive))
        {
            ClearLatestStampPreview();
        }
        ImGui::Separator();
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
        ImGui::MenuItem("Tools", nullptr, &showTools_);
        ImGui::MenuItem("Tool Options", nullptr, &showToolOptions_);
        ImGui::MenuItem("Style", nullptr, &showPalette_);
        ImGui::MenuItem("Viewport", nullptr, &showScene_);
        ImGui::MenuItem("Assets", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Forge Library", nullptr, &showForgeLibrary_);
        ImGui::MenuItem("Scene", nullptr, &showExplorer_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Transform", nullptr, &showTransformPanel_);
        ImGui::MenuItem("Console", nullptr, console_.VisibilityFlag());
        ImGui::MenuItem("Profiler", nullptr, &showProfiler_);
        ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
        if (ImGui::BeginMenu("Viewport Interaction"))
        {
            ImGui::TextDisabled("Selection / Move");
            const bool legacy = !useViewportInteractionV2_;
            if (ImGui::MenuItem("Legacy", nullptr, legacy))
            {
                useViewportInteractionV2_ = false;
                viewportInteractionV2_.Reset();
                viewportRenderer_.ConfigureInteractionV2(
                    {}, std::nullopt, nullptr, voxelModelCenter_, 0U, false);
                UpdateVoxelHighlights();
            }
            if (ImGui::MenuItem(
                    "V2 Selection + Move", nullptr,
                    useViewportInteractionV2_))
            {
                CancelSelectionInteraction();
                CancelTransformGizmoInteraction();
                useViewportInteractionV2_ = true;
                viewportInteractionV2_.Reset();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Pencil");
            if (ImGui::MenuItem("Pencil Legacy", nullptr,
                    !usePencilViewportInteractionV2_))
            {
                usePencilViewportInteractionV2_ = false;
                pencilViewportInteractionV2_.Reset();
                pencilV2PreviewPositions_.clear();
                pencilV2RenderedPresentationRevision_ = 0U;
                UpdateVoxelHighlights();
            }
            if (ImGui::MenuItem("Pencil V2", nullptr,
                    usePencilViewportInteractionV2_))
            {
                CancelSmartToolStroke();
                usePencilViewportInteractionV2_ = true;
                pencilViewportInteractionV2_.Reset();
                pencilV2PreviewPositions_.clear();
                pencilV2RenderedPresentationRevision_ = 0U;
                UpdateVoxelHighlights();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (ImGui::MenuItem("Reset Layout"))
        {
            resetLayoutRequested_ = true;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        const bool hasDocument = voxelDocumentSession_.HasActiveDocument();
        if (ImGui::MenuItem(
                "Pencil", shortcut(EditorInputCommand::ToolPencil),
                voxelToolState_.IsPencilActive() &&
                    toolContext_.Smart.Action() == SmartAction::Add,
                hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolPencil);
        if (ImGui::MenuItem(
                "Pencil Action: Erase",
                shortcut(EditorInputCommand::ToolEraser),
                voxelToolState_.IsPencilActive() &&
                    toolContext_.Smart.Action() == SmartAction::Erase,
                hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolEraser);
        if (ImGui::MenuItem(
                "Pencil Action: Paint", shortcut(EditorInputCommand::ToolFill),
                voxelToolState_.IsPencilActive() &&
                    toolContext_.Smart.Action() == SmartAction::Paint,
                hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolFill);
        if (ImGui::MenuItem(
                "Box", shortcut(EditorInputCommand::ToolBox),
                voxelToolState_.IsBoxActive(), hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolBox);
        if (ImGui::MenuItem(
                "Line", shortcut(EditorInputCommand::ToolLine),
                voxelToolState_.IsLineActive(), hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolLine);
        if (ImGui::MenuItem(
                "Sphere", shortcut(EditorInputCommand::ToolSphere),
                voxelToolState_.IsSphereActive(), hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolSphere);
        ImGui::Separator();
        if (ImGui::MenuItem(
                "Selection", shortcut(EditorInputCommand::ToolSelection),
                voxelToolState_.IsSelectionActive(), hasDocument))
            ExecuteInputCommand(EditorInputCommand::ToolSelection);
        if (ImGui::MenuItem(
                "Move", shortcut(EditorInputCommand::ToolMove),
                voxelToolState_.IsMoveActive(), CanMoveSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolMove);
        if (ImGui::MenuItem(
                "Duplicate", shortcut(EditorInputCommand::ToolDuplicate),
                voxelToolState_.IsDuplicateActive(),
                CanDuplicateSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolDuplicate);
        if (ImGui::MenuItem(
                "Rotate", shortcut(EditorInputCommand::ToolRotate),
                voxelToolState_.IsRotateActive(), CanRotateSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolRotate);
        if (voxelToolState_.IsRotateActive())
        {
            ImGui::Separator();
            if (ImGui::MenuItem(
                    "Rotate Left 90", shortcut(EditorInputCommand::RotateLeft),
                    false, transformPreviewModel_.IsActive()))
                ExecuteInputCommand(EditorInputCommand::RotateLeft);
            if (ImGui::MenuItem(
                    "Rotate Right 90", shortcut(EditorInputCommand::RotateRight),
                    false, transformPreviewModel_.IsActive()))
                ExecuteInputCommand(EditorInputCommand::RotateRight);
            if (ImGui::MenuItem(
                    "Apply Rotation", shortcut(EditorInputCommand::TransformApply),
                    false, transformPreviewModel_.IsActive() &&
                        !transformPreviewModel_.HasCollisions() &&
                        !transformPreviewModel_.HasOutOfBounds()))
                ExecuteInputCommand(EditorInputCommand::TransformApply);
            if (ImGui::MenuItem("Cancel Rotation", "Esc"))
                ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        }
        if (ImGui::MenuItem(
                "Mirror", shortcut(EditorInputCommand::ToolMirror),
                voxelToolState_.IsMirrorActive(), CanMirrorSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolMirror);
        if (voxelToolState_.IsMirrorActive())
        {
            ImGui::Separator();
            if (ImGui::MenuItem(
                    "Mirror X", shortcut(EditorInputCommand::MirrorX)))
                ExecuteInputCommand(EditorInputCommand::MirrorX);
            if (ImGui::MenuItem(
                    "Mirror Z", shortcut(EditorInputCommand::MirrorZ)))
                ExecuteInputCommand(EditorInputCommand::MirrorZ);
            if (ImGui::MenuItem(
                    "Apply Mirror", shortcut(EditorInputCommand::TransformApply),
                    false, transformPreviewModel_.IsActive() &&
                        !transformPreviewModel_.HasCollisions() &&
                        !transformPreviewModel_.HasOutOfBounds()))
                ExecuteInputCommand(EditorInputCommand::TransformApply);
            if (ImGui::MenuItem("Cancel Mirror", "Esc"))
                ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        }
        if (ImGui::MenuItem(
                "Scale", shortcut(EditorInputCommand::ToolScale),
                voxelToolState_.IsScaleActive(), CanScaleSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolScale);
        if (voxelToolState_.IsScaleActive())
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Scale X x2",
                    shortcut(EditorInputCommand::ScaleX)))
                ExecuteInputCommand(EditorInputCommand::ScaleX);
            if (ImGui::MenuItem("Scale Y x2",
                    shortcut(EditorInputCommand::ScaleY)))
                ExecuteInputCommand(EditorInputCommand::ScaleY);
            if (ImGui::MenuItem("Scale Z x2",
                    shortcut(EditorInputCommand::ScaleZ)))
                ExecuteInputCommand(EditorInputCommand::ScaleZ);
            if (ImGui::MenuItem("Scale Uniform x2",
                    shortcut(EditorInputCommand::ScaleUniform)))
                ExecuteInputCommand(EditorInputCommand::ScaleUniform);
            if (ImGui::MenuItem("Apply Scale",
                    shortcut(EditorInputCommand::TransformApply), false,
                    transformPreviewModel_.IsActive() &&
                        !transformPreviewModel_.HasCollisions() &&
                        !transformPreviewModel_.HasOutOfBounds()))
                ExecuteInputCommand(EditorInputCommand::TransformApply);
            if (ImGui::MenuItem("Cancel Scale", "Esc"))
                ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        }
        if (ImGui::MenuItem(
                "Align", shortcut(EditorInputCommand::ToolAlign),
                voxelToolState_.IsAlignActive(), CanAlignSelection()))
            ExecuteInputCommand(EditorInputCommand::ToolAlign);
        if (voxelToolState_.IsAlignActive())
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Align Left"))
                ExecuteInputCommand(EditorInputCommand::AlignLeft);
            if (ImGui::MenuItem("Align Right"))
                ExecuteInputCommand(EditorInputCommand::AlignRight);
            if (ImGui::MenuItem("Align Bottom"))
                ExecuteInputCommand(EditorInputCommand::AlignBottom);
            if (ImGui::MenuItem("Align Top"))
                ExecuteInputCommand(EditorInputCommand::AlignTop);
            if (ImGui::MenuItem("Align Front"))
                ExecuteInputCommand(EditorInputCommand::AlignFront);
            if (ImGui::MenuItem("Align Back"))
                ExecuteInputCommand(EditorInputCommand::AlignBack);
            if (ImGui::MenuItem("Apply Align",
                    shortcut(EditorInputCommand::TransformApply), false,
                    transformPreviewModel_.IsActive() &&
                        !transformPreviewModel_.HasCollisions() &&
                        !transformPreviewModel_.HasOutOfBounds()))
                ExecuteInputCommand(EditorInputCommand::TransformApply);
            if (ImGui::MenuItem("Cancel Align", "Esc"))
                ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        }
        ImGui::Separator();
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

    EditorInputFrame inputFrame;
    inputFrame.Control = io.KeyCtrl;
    inputFrame.Shift = io.KeyShift;
    inputFrame.Alt = io.KeyAlt;
    inputFrame.TextInputActive = io.WantTextInput;
    inputFrame.DialogTextInputActive = incompatiblePopupOpen && io.WantTextInput;
    inputFrame.RenameActive = incompatiblePopupOpen && io.WantTextInput;
    inputFrame.NumericInputActive = context.ActiveItem && !io.WantTextInput;
    inputFrame.PopupOpen = incompatiblePopupOpen;
    inputFrame.SetPressed(
        EditorInputKey::P, ImGui::IsKeyPressed(ImGuiKey_P, false));
    inputFrame.SetPressed(
        EditorInputKey::E, ImGui::IsKeyPressed(ImGuiKey_E, false));
    inputFrame.SetPressed(
        EditorInputKey::F, ImGui::IsKeyPressed(ImGuiKey_F, false));
    inputFrame.SetPressed(
        EditorInputKey::B, ImGui::IsKeyPressed(ImGuiKey_B, false));
    inputFrame.SetPressed(
        EditorInputKey::L, ImGui::IsKeyPressed(ImGuiKey_L, false));
    inputFrame.SetPressed(
        EditorInputKey::S, ImGui::IsKeyPressed(ImGuiKey_S, false));
    inputFrame.SetPressed(
        EditorInputKey::V, ImGui::IsKeyPressed(ImGuiKey_V, false));
    inputFrame.SetPressed(
        EditorInputKey::M, ImGui::IsKeyPressed(ImGuiKey_M, false));
    inputFrame.SetPressed(
        EditorInputKey::D, ImGui::IsKeyPressed(ImGuiKey_D, false));
    inputFrame.SetPressed(
        EditorInputKey::H, ImGui::IsKeyPressed(ImGuiKey_H, false));
    inputFrame.SetPressed(
        EditorInputKey::K, ImGui::IsKeyPressed(ImGuiKey_K, false));
    inputFrame.SetPressed(
        EditorInputKey::U, ImGui::IsKeyPressed(ImGuiKey_U, false));
    inputFrame.SetPressed(
        EditorInputKey::X, ImGui::IsKeyPressed(ImGuiKey_X, false));
    inputFrame.SetPressed(
        EditorInputKey::Q, ImGui::IsKeyPressed(ImGuiKey_Q, false));
    inputFrame.SetPressed(
        EditorInputKey::R, ImGui::IsKeyPressed(ImGuiKey_R, false));
    inputFrame.SetPressed(
        EditorInputKey::A, ImGui::IsKeyPressed(ImGuiKey_A, false));
    inputFrame.SetPressed(
        EditorInputKey::Enter, ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    inputFrame.SetPressed(
        EditorInputKey::Z, ImGui::IsKeyPressed(ImGuiKey_Z, false));
    inputFrame.SetPressed(
        EditorInputKey::Y, ImGui::IsKeyPressed(ImGuiKey_Y, false));
    inputFrame.SetPressed(
        EditorInputKey::Escape,
        ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    ExecuteInputCommand(editorInputService_.Resolve(
        inputFrame, CurrentCommandAvailability()));

    if (inputFrame.IsBlocked()) return;

    constexpr ImGuiInputFlags shortcutFlags = ImGuiInputFlags_RouteGlobal;
    const bool newModelShortcut = io.KeyCtrl && io.KeyShift &&
        !io.KeyAlt && !io.KeySuper &&
        ImGui::IsKeyPressed(ImGuiKey_N, false);
    // ImGui's generic Ctrl+N route may also match while Shift is held. Guard
    // the modifiers explicitly so New Model can never become New Project.
    if (newModelShortcut && context.HasProject)
    {
        RequestNewVoxelModelDialog();
    }
    else if (!io.KeyShift &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, shortcutFlags) &&
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
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, shortcutFlags) &&
             CanRunProjectShortcut(ProjectShortcut::CloseProject, context))
    {
        RequestCloseProject();
    }
}

EditorCommandAvailability EditorWorkspace::CurrentCommandAvailability() const
{
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const bool documentHistory = document != nullptr;
    const bool stampTransform = documentHistory &&
        stampPlacementSession_.IsActive() && !voxelEditInProgress_ &&
        !voxelEditHistory_.IsBusy();
    const bool stampPlacement = CanPlaceLatestStampPreview();
    return {
        documentHistory,
        documentHistory
            ? document->IsDirty() && !voxelDocumentSaveService_.IsBusy()
            : projectManager_.HasActiveProject(),
        !voxelEditInProgress_ && !voxelEditHistory_.IsBusy() &&
            (documentHistory ? voxelEditHistory_.CanUndo()
                             : commandHistory_.CanUndo()),
        !voxelEditInProgress_ && !voxelEditHistory_.IsBusy() &&
            (documentHistory ? voxelEditHistory_.CanRedo()
                             : commandHistory_.CanRedo()),
        voxelBoxInteraction_.IsActive() || voxelLineInteraction_.IsActive() ||
            voxelSphereInteraction_.IsActive() ||
            stampPlacementSession_.IsActive() ||
            transformGizmoManager_.IsDragging() ||
            selectionInteraction_.IsActive() ||
            selectionService_.EditableBounds().Valid ||
            voxelSelection_.Selected().has_value(),
        CanMoveSelection(),
        CanDuplicateSelection(),
        CanRotateSelection(),
        stampTransform ||
            (voxelToolState_.IsRotateActive() && CanRotateSelection()),
        CanMirrorSelection(),
        stampTransform ||
            (voxelToolState_.IsMirrorActive() && CanMirrorSelection()),
        CanScaleSelection(),
        voxelToolState_.IsScaleActive() && CanScaleSelection(),
        CanAlignSelection(),
        voxelToolState_.IsAlignActive() && CanAlignSelection(),
        stampPlacement ||
            ((voxelToolState_.IsRotateActive() ||
              voxelToolState_.IsMirrorActive() ||
              voxelToolState_.IsScaleActive() ||
              voxelToolState_.IsAlignActive()) &&
             transformPreviewModel_.IsActive() &&
             !transformPreviewModel_.HasCollisions() &&
             !transformPreviewModel_.HasOutOfBounds()),
        stampTransform};
}

bool EditorWorkspace::CanMoveSelection() const noexcept
{
    return voxelDocumentSession_.HasActiveDocument() &&
        !voxelEditInProgress_ && !voxelEditHistory_.IsBusy() &&
        !stampPlacementSession_.IsActive() &&
        !selectionService_.Empty() &&
        selectionService_.EditableBounds().Valid &&
        selectionService_.DocumentGeneration() ==
            voxelDocumentSession_.Generation();
}

bool EditorWorkspace::CanDuplicateSelection() const noexcept
{
    return CanMoveSelection() && !selectionInteraction_.IsActive();
}

bool EditorWorkspace::CanRotateSelection() const noexcept
{
    return CanMoveSelection() && !selectionInteraction_.IsActive();
}

bool EditorWorkspace::CanMirrorSelection() const noexcept
{
    return CanMoveSelection() && !selectionInteraction_.IsActive();
}

bool EditorWorkspace::CanScaleSelection() const noexcept
{
    return CanMoveSelection() && !selectionInteraction_.IsActive();
}

bool EditorWorkspace::CanAlignSelection() const noexcept
{
    return CanMoveSelection() && !selectionInteraction_.IsActive();
}

void EditorWorkspace::ExecuteInputCommand(const EditorInputCommand command)
{
    switch (command)
    {
    case EditorInputCommand::ToolPencil:
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Add);
        SelectVoxelTool(ActiveVoxelTool::Pencil); break;
    case EditorInputCommand::ToolEraser:
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Erase);
        SelectVoxelTool(ActiveVoxelTool::Pencil); break;
    case EditorInputCommand::ToolFill:
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Paint);
        SelectVoxelTool(ActiveVoxelTool::Pencil); break;
    case EditorInputCommand::ToolBox:
        SelectVoxelTool(ActiveVoxelTool::Box); break;
    case EditorInputCommand::ToolLine:
        SelectVoxelTool(ActiveVoxelTool::Line); break;
    case EditorInputCommand::ToolSphere:
        SelectVoxelTool(ActiveVoxelTool::Sphere); break;
    case EditorInputCommand::ToolSelection:
        SelectVoxelTool(ActiveVoxelTool::Selection); break;
    case EditorInputCommand::ToolMove:
        SelectVoxelTool(ActiveVoxelTool::Move); break;
    case EditorInputCommand::ToolDuplicate:
        SelectVoxelTool(ActiveVoxelTool::Duplicate); break;
    case EditorInputCommand::ToolRotate:
        SelectVoxelTool(ActiveVoxelTool::Rotate); break;
    case EditorInputCommand::ToolMirror:
        SelectVoxelTool(ActiveVoxelTool::Mirror); break;
    case EditorInputCommand::ToolScale:
        SelectVoxelTool(ActiveVoxelTool::Scale); break;
    case EditorInputCommand::ToolAlign:
        SelectVoxelTool(ActiveVoxelTool::Align); break;
    case EditorInputCommand::RotateLeft:
        if (stampPlacementSession_.IsActive())
            RotateLatestStampPreview(false);
        else
            static_cast<void>(BeginVoxelRotatePreview(
                VoxelRotationDirection::CounterClockwise));
        break;
    case EditorInputCommand::RotateRight:
        if (stampPlacementSession_.IsActive())
            RotateLatestStampPreview(true);
        else
            static_cast<void>(BeginVoxelRotatePreview(
                VoxelRotationDirection::Clockwise));
        break;
    case EditorInputCommand::MirrorX:
        if (stampPlacementSession_.IsActive())
            ToggleLatestStampPreview(Stamps::StampPlacementMirrorMode::X);
        else
            static_cast<void>(BeginVoxelMirrorPreview(VoxelMirrorAxis::X));
        break;
    case EditorInputCommand::MirrorZ:
        if (stampPlacementSession_.IsActive())
            ToggleLatestStampPreview(Stamps::StampPlacementMirrorMode::Z);
        else
            static_cast<void>(BeginVoxelMirrorPreview(VoxelMirrorAxis::Z));
        break;
    case EditorInputCommand::ScaleX:
        static_cast<void>(BeginVoxelScalePreview(VoxelScaleMode::X)); break;
    case EditorInputCommand::ScaleY:
        static_cast<void>(BeginVoxelScalePreview(VoxelScaleMode::Y)); break;
    case EditorInputCommand::ScaleZ:
        static_cast<void>(BeginVoxelScalePreview(VoxelScaleMode::Z)); break;
    case EditorInputCommand::ScaleUniform:
        static_cast<void>(BeginVoxelScalePreview(VoxelScaleMode::Uniform));
        break;
    case EditorInputCommand::AlignLeft:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Left));
        break;
    case EditorInputCommand::AlignRight:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Right));
        break;
    case EditorInputCommand::AlignBottom:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Bottom));
        break;
    case EditorInputCommand::AlignTop:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Top));
        break;
    case EditorInputCommand::AlignFront:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Front));
        break;
    case EditorInputCommand::AlignBack:
        static_cast<void>(BeginVoxelAlignPreview(VoxelAlignDirection::Back));
        break;
    case EditorInputCommand::TransformApply:
        if (stampPlacementSession_.IsActive())
            RequestLatestStampPlacement();
        else if (voxelToolState_.IsRotateActive())
            static_cast<void>(ApplyVoxelRotate());
        else if (voxelToolState_.IsMirrorActive())
            static_cast<void>(ApplyVoxelMirror());
        else if (voxelToolState_.IsScaleActive())
            static_cast<void>(ApplyVoxelScale());
        else if (voxelToolState_.IsAlignActive())
            static_cast<void>(ApplyVoxelAlign());
        break;
    case EditorInputCommand::StampCycleMirror:
        CycleLatestStampPreview();
        break;
    case EditorInputCommand::StampResetTransform:
        ResetLatestStampTransform();
        break;
    case EditorInputCommand::StampToggleRotationStep:
        ToggleLatestStampRotationStep();
        break;
    case EditorInputCommand::FileSave:
        if (voxelDocumentSession_.HasActiveDocument())
            static_cast<void>(SaveVoxelModel());
        else
            SaveProject();
        break;
    case EditorInputCommand::EditUndo: UndoCommand(); break;
    case EditorInputCommand::EditRedo: RedoCommand(); break;
    case EditorInputCommand::InteractionCancel: CancelActiveInteraction(); break;
    case EditorInputCommand::None:
    case EditorInputCommand::Count:
    default: break;
    }
}

void EditorWorkspace::SelectVoxelTool(const ActiveVoxelTool tool)
{
    if (smartToolStroke_.IsActive() &&
        (tool != ActiveVoxelTool::Pencil ||
         toolContext_.Smart.Action() != smartToolStroke_.Action()))
        CancelSmartToolStroke();
    if (tool == ActiveVoxelTool::Eraser)
    {
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Erase);
        SelectVoxelTool(ActiveVoxelTool::Pencil);
        return;
    }
    if (tool == ActiveVoxelTool::Move && !CanMoveSelection()) return;
    if (tool == ActiveVoxelTool::Duplicate && !CanDuplicateSelection()) return;
    if (tool == ActiveVoxelTool::Rotate && !CanRotateSelection()) return;
    if (tool == ActiveVoxelTool::Mirror && !CanMirrorSelection()) return;
    if (tool == ActiveVoxelTool::Scale && !CanScaleSelection()) return;
    if (tool == ActiveVoxelTool::Align && !CanAlignSelection()) return;
    if (tool != ActiveVoxelTool::Box) CancelVoxelBox();
    if (tool != ActiveVoxelTool::Line) CancelVoxelLine();
    if (tool != ActiveVoxelTool::Sphere) CancelVoxelSphere();
    const TransformGizmoCancellation gizmoToolCancellation =
        transformGizmoManager_.OnToolChanged(tool);
    if (gizmoToolCancellation ||
        (tool != ActiveVoxelTool::Move && tool != ActiveVoxelTool::Rotate &&
         tool != ActiveVoxelTool::Scale))
        CancelTransformGizmoInteraction();
    if (selectionInteraction_.IsActive())
    {
        const SelectionInteractionMode interactionMode =
            selectionInteraction_.Mode();
        const bool movingContent = interactionMode ==
            SelectionInteractionMode::MovingContent;
        const bool duplicatingContent = interactionMode ==
            SelectionInteractionMode::DuplicatingContent;
        if ((movingContent && tool != ActiveVoxelTool::Move) ||
            (duplicatingContent && tool != ActiveVoxelTool::Duplicate) ||
            (!movingContent && !duplicatingContent &&
             tool != ActiveVoxelTool::Selection))
            CancelSelectionInteraction();
    }
    if (tool != ActiveVoxelTool::Selection && tool != ActiveVoxelTool::Move &&
        tool != ActiveVoxelTool::Duplicate && tool != ActiveVoxelTool::Rotate &&
        tool != ActiveVoxelTool::Mirror && tool != ActiveVoxelTool::Scale &&
        tool != ActiveVoxelTool::Align)
        selectionBoxInteriorHovered_ = false;
    if (tool == ActiveVoxelTool::Selection)
        static_cast<void>(voxelSelection_.ClearSelection());
    if (tool != ActiveVoxelTool::Move && tool != ActiveVoxelTool::Duplicate &&
        tool != ActiveVoxelTool::Rotate && tool != ActiveVoxelTool::Mirror &&
        tool != ActiveVoxelTool::Scale && tool != ActiveVoxelTool::Align)
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelMoveStatusMessage_.clear();
        voxelDuplicateStatusMessage_.clear();
        voxelRotateStatusMessage_.clear();
        voxelMirrorStatusMessage_.clear();
        voxelScaleStatusMessage_.clear();
        voxelAlignStatusMessage_.clear();
    }
    if (tool != ActiveVoxelTool::Rotate) CancelVoxelRotate();
    if (tool != ActiveVoxelTool::Mirror) CancelVoxelMirror();
    if (tool != ActiveVoxelTool::Scale) CancelVoxelScale();
    if (tool != ActiveVoxelTool::Align) CancelVoxelAlign();
    toolManager_.SetActiveTool(tool);
    voxelToolInput_.Reset();
    UpdateVoxelHighlights();
}

void EditorWorkspace::CancelActiveInteraction()
{
    if (smartToolStroke_.IsActive())
    {
        CancelSmartToolStroke();
        UpdateVoxelHighlights();
        return;
    }
    if (stampPlacementSession_.IsActive())
    {
        ClearLatestStampPreview();
        return;
    }
    if (voxelBoxInteraction_.IsActive()) CancelVoxelBox();
    if (voxelLineInteraction_.IsActive()) CancelVoxelLine();
    if (voxelSphereInteraction_.IsActive()) CancelVoxelSphere();
    if (transformGizmoManager_.IsDragging())
    {
        CancelTransformGizmoInteraction();
        return;
    }
    if (voxelToolState_.IsRotateActive())
    {
        CancelVoxelRotate();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Selection);
        UpdateVoxelHighlights();
        return;
    }
    if (voxelToolState_.IsMirrorActive())
    {
        CancelVoxelMirror();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Selection);
        UpdateVoxelHighlights();
        return;
    }
    if (voxelToolState_.IsScaleActive())
    {
        CancelVoxelScale();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Selection);
        UpdateVoxelHighlights();
        return;
    }
    if (voxelToolState_.IsAlignActive())
    {
        CancelVoxelAlign();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Selection);
        UpdateVoxelHighlights();
        return;
    }
    if (selectionInteraction_.IsActive())
    {
        CancelSelectionInteraction();
        return;
    }
    const bool selectionChanged = selectionService_.Clear();
    const bool legacyChanged = voxelSelection_.ClearSelection();
    if (selectionChanged || legacyChanged) UpdateVoxelHighlights();
}

void EditorWorkspace::UndoCommand()
{
    if (smartToolStroke_.IsActive())
    {
        CancelSmartToolStroke();
        UpdateVoxelHighlights();
        return;
    }
    if (transformGizmoManager_.IsDragging())
        CancelTransformGizmoInteraction();
    if (voxelDocumentSession_.HasActiveDocument())
    {
        if (voxelEditInProgress_ || voxelEditHistory_.IsBusy()) return;
        const bool resumeRotatePreview = voxelToolState_.IsRotateActive() &&
            transformPreviewModel_.IsActive();
        if (resumeRotatePreview) CancelVoxelRotate();
        if (voxelToolState_.IsScaleActive() &&
            transformPreviewModel_.IsActive())
            CancelVoxelScale();
        if (voxelToolState_.IsAlignActive() &&
            transformPreviewModel_.IsActive())
            CancelVoxelAlign();
        voxelEditInProgress_ = true;
        const VoxelEditHistoryResult result = voxelEditHistory_.Undo(*this);
        voxelEditInProgress_ = false;
        if (result)
        {
            ApplyVoxelHistorySelection(result);
            if (stampPlacementSession_.IsActive())
            {
                static_cast<void>(RefreshLatestStampPreview());
            }
            AddConsoleMessage("[Edit] Undo: " + result.Label);
            firstCreationExperience_.OnUndo();
            if (resumeRotatePreview)
                static_cast<void>(BeginVoxelRotatePreview(
                    voxelRotateDirection_));
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
    if (smartToolStroke_.IsActive())
    {
        CancelSmartToolStroke();
        UpdateVoxelHighlights();
        return;
    }
    if (transformGizmoManager_.IsDragging())
        CancelTransformGizmoInteraction();
    if (voxelDocumentSession_.HasActiveDocument())
    {
        if (voxelEditInProgress_ || voxelEditHistory_.IsBusy()) return;
        const bool resumeRotatePreview = voxelToolState_.IsRotateActive() &&
            transformPreviewModel_.IsActive();
        if (resumeRotatePreview) CancelVoxelRotate();
        if (voxelToolState_.IsScaleActive() &&
            transformPreviewModel_.IsActive())
            CancelVoxelScale();
        if (voxelToolState_.IsAlignActive() &&
            transformPreviewModel_.IsActive())
            CancelVoxelAlign();
        voxelEditInProgress_ = true;
        const VoxelEditHistoryResult result = voxelEditHistory_.Redo(*this);
        voxelEditInProgress_ = false;
        if (result)
        {
            ApplyVoxelHistorySelection(result);
            if (stampPlacementSession_.IsActive())
            {
                static_cast<void>(RefreshLatestStampPreview());
            }
            AddConsoleMessage("[Edit] Redo: " + result.Label);
            firstCreationExperience_.OnRedo();
            if (resumeRotatePreview)
                static_cast<void>(BeginVoxelRotatePreview(
                    voxelRotateDirection_));
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

void EditorWorkspace::ApplyVoxelHistorySelection(
    const VoxelEditHistoryResult& result)
{
    if (!result.SelectionTransition ||
        result.SelectionState == VoxelEditSelectionState::None)
        return;
    const VoxelEditSelectionSnapshot& snapshot =
        result.SelectionState == VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    if (snapshot.DocumentGeneration != voxelDocumentSession_.Generation())
        return;
    selectionService_.SetDocumentGeneration(snapshot.DocumentGeneration);
    static_cast<void>(selectionService_.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, SelectionMode::Replace));
    static_cast<void>(voxelSelection_.ClearSelection());
    UpdateVoxelHighlights();
}



void EditorWorkspace::DrawExplorerPanel()
{
    ImGui::Begin(ScenePanelWindowName, &showExplorer_);
    ImGui::TextUnformatted("Scene");
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

void EditorWorkspace::DrawToolsPanel()
{
    if (!ImGui::Begin(ToolsPanelWindowName, &showTools_))
    {
        ImGui::End();
        return;
    }

    const Asset::Voxel::VoxelDocument* activeDocument =
        voxelDocumentSession_.ActiveDocument();
    const bool canSave = activeDocument != nullptr &&
        activeDocument->IsDirty() && !voxelDocumentSaveService_.IsBusy();
    const ToolDescriptor& activeTool = toolManager_.ActiveDescriptor();
    ImGui::TextDisabled("CREATE");
    ImGui::SameLine();
    ImGui::TextUnformatted("TOOLS");
    ImGui::TextDisabled("Active");
    ImGui::SameLine();
    if (stampPlacementSession_.IsActive())
        ImGui::TextUnformatted("Stamp Placement");
    else
        ImGui::TextUnformatted(
            activeTool.Name.data(),
            activeTool.Name.data() + activeTool.Name.size());
    ImGui::Separator();

    constexpr float MinimumToolbarHeight = 96.0F;
    const float toolbarHeight = std::max(
        MinimumToolbarHeight, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild(
            "##CreateTools", ImVec2(0.0F, toolbarHeight), true))
    {
        if (stampPlacementSession_.IsActive())
        {
            ImGui::TextDisabled("ACTIVE TOOL");
            ImGui::TextUnformatted("Stamp Placement");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "The complete Stamp is attached to the viewport gizmo.");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Use Tool Options to place or cancel the Stamp.");
        }
        else
        {
            EditorToolbar::Draw(
                {activeDocument != nullptr, canSave,
                 voxelToolState_.ActiveTool(), CanMoveSelection(),
                 CanDuplicateSelection(), CanRotateSelection(),
                 CanMirrorSelection(), CanScaleSelection(),
                 CanAlignSelection()},
                editorInputService_,
                {[this](const EditorInputCommand command)
                 {
                     ExecuteInputCommand(command);
                 }});
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorWorkspace::DrawToolOptionsPanel()
{
    if (!ImGui::Begin(ToolOptionsPanelWindowName, &showToolOptions_))
    {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("TOOL OPTIONS");
    ImGui::Separator();
    toolContext_.HasDocument = voxelDocumentSession_.HasActiveDocument();
    constexpr float MinimumToolOptionsHeight = 112.0F;
    const float optionsHeight = std::max(
        MinimumToolOptionsHeight, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild(
            "##ActiveToolOptions", ImVec2(0.0F, optionsHeight), true))
    {
        if (stampPlacementSession_.IsActive())
            DrawStampPlacementToolOptions();
        else
            smartBrushPreviewRefreshRequested_ =
                ToolPanel::Draw(toolManager_, toolContext_) ||
                smartBrushPreviewRefreshRequested_;
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorWorkspace::DrawStampPlacementToolOptions()
{
    const bool gizmoBusy = transformGizmoManager_.IsDragging();

    ImGui::TextDisabled("ACTIVE TOOL");
    ImGui::TextUnformatted("Stamp Placement");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Move or rotate the complete preview, then confirm its placement.");
    ImGui::SeparatorText("MANIPULATOR");

    ImGui::BeginDisabled(gizmoBusy);
    if (ImGui::RadioButton(
            "Move", stampGizmoTool_ == ActiveVoxelTool::Move))
    {
        stampGizmoTool_ = ActiveVoxelTool::Move;
        static_cast<void>(transformGizmoManager_.OnToolChanged(
            stampGizmoTool_));
    }
    if (ImGui::RadioButton(
            "Rotation gizmo",
            stampGizmoTool_ == ActiveVoxelTool::Rotate))
    {
        stampGizmoTool_ = ActiveVoxelTool::Rotate;
        static_cast<void>(transformGizmoManager_.OnToolChanged(
            stampGizmoTool_));
    }

    // STAMP-24 : axe de rotation actif. Un seul axe a la fois ; changer d'axe
    // remet l'angle a zero (cf. StampPlacementSession::Rotate90).
    ImGui::TextDisabled("Rotation axis");
    const auto axisRadio = [this](
        const char* const label,
        const Stamps::StampPlacementRotationAxis axis)
    {
        if (ImGui::RadioButton(label, stampRotationAxis_ == axis))
            stampRotationAxis_ = axis;
    };
    axisRadio("X", Stamps::StampPlacementRotationAxis::LateralX);
    ImGui::SameLine();
    axisRadio("Y", Stamps::StampPlacementRotationAxis::VerticalY);
    ImGui::SameLine();
    axisRadio("Z", Stamps::StampPlacementRotationAxis::DepthZ);
    // STAMP-25 : le pas n'est qu'un increment. Dans les deux cas la rotation
    // fait le tour complet : quatre crans a 90 degres, huit a 45. Les
    // positions impaires (45, 135, 225, 315) ne sont pas des symetries de la
    // grille : le Stamp y est reechantillonne, ce que la section PLACEMENT
    // annonce en permanence (pas de boite de confirmation).
    ImGui::TextDisabled("Rotation step");
    const auto stepRadio = [this](
        const char* const label, const Stamps::StampRotationStep step)
    {
        if (ImGui::RadioButton(
                label, stampPlacementSession_.RotationStep() == step) &&
            stampPlacementSession_.RotationStep() != step)
        {
            ToggleLatestStampRotationStep();
        }
    };
    stepRadio("90 deg", Stamps::StampRotationStep::Quarter90);
    ImGui::SameLine();
    stepRadio("45 deg  [Shift+E]", Stamps::StampRotationStep::Eighth45);

    ImGui::TextDisabled(
        "Rotation %s: %u degrees",
        Stamps::StampRotationAxisLabel(
            stampPlacementSession_.RotationAxis()).data(),
        stampPlacementSession_.RotationDegrees());
    if (stampPlacementSession_.HalfQuarterStep())
    {
        ImGui::TextColored(
            ImVec4(1.0F, 0.78F, 0.32F, 1.0F),
            "Approximate: the Stamp is resampled.");
    }
    if (ImGui::Button("Rotate left  [Q]", ImVec2(-1.0F, 0.0F)))
    {
        stampGizmoTool_ = ActiveVoxelTool::Rotate;
        static_cast<void>(transformGizmoManager_.OnToolChanged(
            stampGizmoTool_));
        RotateLatestStampPreview(false);
    }
    if (ImGui::Button(
            "Rotate right  [Shift+Q]", ImVec2(-1.0F, 0.0F)))
    {
        stampGizmoTool_ = ActiveVoxelTool::Rotate;
        static_cast<void>(transformGizmoManager_.OnToolChanged(
            stampGizmoTool_));
        RotateLatestStampPreview(true);
    }
    if (ImGui::Button("Reset transform", ImVec2(-1.0F, 0.0F)))
        ResetLatestStampTransform();
    ImGui::EndDisabled();

    ImGui::SeparatorText("PLACEMENT");
    const Stamps::StampPlacementPlan* const plan =
        stampPlacementSession_.CurrentPlan();
    if (plan != nullptr)
    {
        ImGui::Text("%zu voxels", plan->Statistics.TotalVoxelCount);
        // STAMP-25 : affichage permanent de l'ecart source -> cellules. Il est
        // neutre en rotation exacte (les deux nombres sont egaux) et devient la
        // seule alerte necessaire quand le cran de 45 degres est actif.
        if (plan->Statistics.ApproximateRotation)
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.78F, 0.32F, 1.0F),
                "%zu source voxels -> %zu cells",
                plan->Statistics.TotalVoxelCount,
                plan->Statistics.PlannedVoxelCount);
        }
        else
        {
            ImGui::TextDisabled(
                "%zu source voxels -> %zu cells",
                plan->Statistics.TotalVoxelCount,
                plan->Statistics.PlannedVoxelCount);
        }
        ImGui::TextDisabled(
            "%zu cells changed", plan->Statistics.ChangedVoxelCount);
        ImGui::TextDisabled(
            "%zu overlaps", plan->Statistics.OverlapCount);
        if (plan->Statistics.OutOfBoundsCount != 0U)
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.42F, 0.35F, 1.0F),
                "%zu out of bounds",
                plan->Statistics.OutOfBoundsCount);
        }
    }

    const bool canPlace = CanPlaceLatestStampPreview();
    ImGui::BeginDisabled(!canPlace);
    ImGui::PushStyleColor(
        ImGuiCol_Button, ImVec4(0.08F, 0.48F, 0.28F, 1.0F));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered, ImVec4(0.10F, 0.62F, 0.35F, 1.0F));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive, ImVec4(0.07F, 0.38F, 0.22F, 1.0F));
    if (ImGui::Button(
            "PLACE FULL STAMP", ImVec2(-1.0F, 38.0F)))
    {
        RequestLatestStampPlacement();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Enter confirms the complete Stamp");
    if (!stampPlacementUiMessage_.empty())
    {
        ImGui::TextWrapped("%s", stampPlacementUiMessage_.c_str());
    }
    if (!canPlace && plan != nullptr)
    {
        ImGui::TextWrapped(
            plan->Statistics.OutOfBoundsCount != 0U
                ? "Move the Stamp completely inside the model."
                : "This preview cannot change the document here.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel placement  [Esc]", ImVec2(-1.0F, 0.0F)))
        ClearLatestStampPreview();
}


void EditorWorkspace::DrawWelcomeScreen()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 hostSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - StatusBarHeight));
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(hostSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055F, 0.060F, 0.068F, 1.0F));
    const bool hostVisible = ImGui::Begin(
        "##VoxelForgeWelcomeHost", nullptr, hostFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!hostVisible)
    {
        ImGui::End();
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const WelcomeScreenLayout layout =
        WelcomeScreenModel::CalculateLayout(available.x, available.y);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + layout.TopPadding);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
        std::max(0.0F, (available.x - layout.ContentWidth) * 0.5F));

    ImGui::BeginChild(
        "##VoxelForgeWelcomeContent",
        ImVec2(layout.ContentWidth,
            std::max(1.0F, available.y - layout.TopPadding)),
        false);

    ImGui::SetWindowFontScale(1.45F);
    const char* title = "VoxelForge Studio";
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (layout.ContentWidth - ImGui::CalcTextSize(title).x) * 0.5F));
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0F);

    const char* motto = "Cr\303\251er plus vite. Rester l'artisan.";
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (layout.ContentWidth - ImGui::CalcTextSize(motto).x) * 0.5F));
    ImGui::TextDisabled("%s", motto);
    ImGui::Dummy(ImVec2(1.0F, 18.0F));

    constexpr float ActionHeight = 44.0F;
    constexpr float ActionSpacing = 12.0F;
    const bool stackActions = layout.ContentWidth < 500.0F;
    const float actionWidth = stackActions
        ? std::max(1.0F, layout.ContentWidth - 16.0F)
        : std::min(210.0F, (layout.ContentWidth - ActionSpacing) * 0.5F);
    const float actionsWidth = stackActions
        ? actionWidth : actionWidth * 2.0F + ActionSpacing;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        (layout.ContentWidth - actionsWidth) * 0.5F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.46F, 0.76F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24F, 0.55F, 0.88F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14F, 0.38F, 0.66F, 1.0F));
    if (ImGui::Button("New Project", ImVec2(actionWidth, ActionHeight)))
        RequestNewProjectDialog();
    DrawTooltip("Create a project (Ctrl+N)");
    if (stackActions)
    {
        ImGui::SetCursorPosX(std::max(
            ImGui::GetCursorPosX(),
            (layout.ContentWidth - actionWidth) * 0.5F));
    }
    else
    {
        ImGui::SameLine(0.0F, ActionSpacing);
    }
    if (ImGui::Button("Open Project", ImVec2(actionWidth, ActionHeight)))
        RequestOpenProjectDialog();
    DrawTooltip("Open a project (Ctrl+O)");
    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(1.0F, 20.0F));
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.12F);
    ImGui::TextUnformatted("Recent Projects");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::Spacing();

    std::optional<std::filesystem::path> recentProjectToOpen;
    std::optional<std::filesystem::path> recentProjectToRemove;
    std::optional<std::filesystem::path> recentProjectToDelete;
    const std::vector<WelcomeProjectEntry> entries =
        WelcomeScreenModel::BuildEntries(
            projectManager_.RecentProjectPaths());

    if (entries.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            ImVec4(0.075F, 0.080F, 0.090F, 1.0F));
        ImGui::BeginChild("##EmptyRecentProjects", ImVec2(0.0F, 92.0F), true);
        ImGui::TextDisabled("No recent projects.");
        ImGui::TextWrapped(
            "Create your first project or open an existing project.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    for (const WelcomeProjectEntry& entry : entries)
    {
        const std::string pathText = entry.ProjectFilePath.string();
        ImGui::PushID(pathText.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            ImVec4(0.075F, 0.080F, 0.090F, 1.0F));
        ImGui::BeginChild(
            "##RecentProjectCard",
            ImVec2(0.0F, layout.CardHeight),
            true);
        ImGui::TextUnformatted(entry.Name.c_str());
        ImGui::SameLine();
        if (entry.Availability == WelcomeProjectAvailability::Available)
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(0.40F, 0.78F, 0.52F, 1.0F));
            ImGui::TextUnformatted("Available");
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(0.90F, 0.62F, 0.30F, 1.0F));
            ImGui::TextUnformatted("Missing");
            ImGui::PopStyleColor();
        }
        ImGui::TextDisabled("%s", pathText.c_str());
        ImGui::Spacing();

        ImGui::BeginDisabled(!entry.CanOpen());
        if (ImGui::Button("Open", ImVec2(104.0F, 30.0F)))
            recentProjectToOpen = entry.ProjectFilePath;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Remove from recent list", ImVec2(190.0F, 30.0F)))
            recentProjectToRemove = entry.ProjectFilePath;
        if (entry.CanDelete())
        {
            if (layout.ContentWidth < 560.0F)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0F);
            else
                ImGui::SameLine();
            if (ImGui::Button("Delete Project...", ImVec2(146.0F, 30.0F)))
                recentProjectToDelete = entry.ProjectFilePath;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
    }

    if (!welcomeNotification_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(0.40F, 0.78F, 0.52F, 1.0F));
        ImGui::TextWrapped("%s", welcomeNotification_.c_str());
        ImGui::PopStyleColor();
    }
    if (!welcomeError_.empty()) DrawErrorMessage(welcomeError_);

    if (recentProjectToRemove)
        RemoveRecentProject(*recentProjectToRemove);
    else if (recentProjectToDelete)
        RequestDeleteProject(*recentProjectToDelete);
    else if (recentProjectToOpen)
        RequestOpenProject(*recentProjectToOpen, true);

    ImGui::EndChild();
    ImGui::End();
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

    if (document == nullptr && !activeVoxelModel_)
    {
        ImGui::TextDisabled("No asset selected.");
        ImGui::End();
        return;
    }

    const auto colorToImGui = [](const Asset::Voxel::VoxelColor& color)
    {
        constexpr float ByteScale = 1.0F / 255.0F;
        return ImVec4(
            static_cast<float>(color.Red) * ByteScale,
            static_cast<float>(color.Green) * ByteScale,
            static_cast<float>(color.Blue) * ByteScale,
            1.0F);
    };
    const auto paletteColorAt = [document, this](const std::size_t index)
        -> std::optional<Asset::Voxel::VoxelColor>
    {
        if (document != nullptr) return document->GetPaletteColor(index);
        if (!activeVoxelModel_) return std::nullopt;
        const Voxel::VoxelColor* color =
            activeVoxelModel_->Palette().Get(index);
        if (color == nullptr) return std::nullopt;
        return Asset::Voxel::VoxelColor{
            color->Red, color->Green, color->Blue, color->Alpha};
    };

    const std::optional<VoxelRaycastHit>& selected = voxelSelection_.Selected();
    std::optional<std::uint8_t> selectedPaletteIndex;
    if (selected && document != nullptr &&
        selected->DocumentRevision == document->GetRevision())
    {
        const auto voxel = document->GetVoxel({
            static_cast<std::int32_t>(selected->Coordinates.X),
            static_cast<std::int32_t>(selected->Coordinates.Y),
            static_cast<std::int32_t>(selected->Coordinates.Z)},
            selected->SubModelIndex);
        if (voxel) selectedPaletteIndex = voxel->PaletteIndex;
    }
    else if (selected && document == nullptr && activeVoxelModel_)
    {
        const Voxel::VoxelGrid* grid =
            activeVoxelModel_->GetGrid(selected->SubModelIndex);
        const Voxel::Voxel* voxel = grid
            ? grid->Get(
                selected->Coordinates.X,
                selected->Coordinates.Y,
                selected->Coordinates.Z)
            : nullptr;
        if (voxel != nullptr && voxel->IsOccupied())
            selectedPaletteIndex = voxel->ColorIndex;
    }
    const bool hasOccupiedSelection = selectedPaletteIndex.has_value();

    ImGui::TextUnformatted("Selected voxel");
    if (hasOccupiedSelection)
    {
        ImGui::Text(
            "Coordinates: %u, %u, %u",
            selected->Coordinates.X,
            selected->Coordinates.Y,
            selected->Coordinates.Z);
        ImGui::Text("Current Color: %u", *selectedPaletteIndex);
        const auto currentColor = paletteColorAt(*selectedPaletteIndex);
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
    const auto paintPreview = paletteColorAt(paintPaletteSelection_.Index());
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
        const auto color = paletteColorAt(index);
        if (!color) continue;

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
        *selectedPaletteIndex != paintPaletteSelection_.Index();
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

void EditorWorkspace::DrawTransformPanel()
{
    if (!ImGui::Begin(TransformPanelWindowName, &showTransformPanel_))
    {
        ImGui::End();
        return;
    }

    const TransformPanelSource source = CurrentTransformPanelSource();
    const TransformPanelState state = transformPanelViewModel_.Read(source);
    const bool editable = state.Available && !voxelEditInProgress_ &&
        !voxelEditHistory_.IsBusy() && !transformGizmoManager_.IsDragging();

    ImGui::TextUnformatted("Selection Transform");
    ImGui::TextDisabled(
        "Exact voxel-space values. Press Enter to apply a field.");
    ImGui::Separator();

    const auto drawVector = [](const char* section, const char* identifier,
        float* values, const char* format) -> bool
    {
        ImGui::TextDisabled("%s", section);
        constexpr const char* axes[] = {"X", "Y", "Z"};
        bool committed = false;
        for (std::size_t index = 0U; index < 3U; ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(axes[index]);
            ImGui::SameLine(0.0F, 8.0F);
            ImGui::SetNextItemWidth(-1.0F);
            committed |= ImGui::InputFloat(
                identifier, &values[index], 0.0F, 0.0F, format,
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopID();
        }
        return committed;
    };

    std::array<float, 3U> position{
        state.Position.X, state.Position.Y, state.Position.Z};
    std::array<float, 3U> rotation{
        state.RotationDegrees.X,
        state.RotationDegrees.Y,
        state.RotationDegrees.Z};
    std::array<float, 3U> scale{
        state.Scale.X, state.Scale.Y, state.Scale.Z};

    ImGui::BeginDisabled(!editable);
    if (drawVector("Position", "##TransformPosition", position.data(), "%.3f"))
        static_cast<void>(ApplyTransformPanelPosition(
            {position[0], position[1], position[2]}));
    ImGui::Spacing();
    if (drawVector("Rotation", "##TransformRotation", rotation.data(), "%.1f"))
        static_cast<void>(ApplyTransformPanelRotation(
            {rotation[0], rotation[1], rotation[2]}));
    ImGui::TextDisabled("90-degree voxel increments");
    ImGui::Spacing();
    if (drawVector("Scale", "##TransformScale", scale.data(), "%.3f"))
        static_cast<void>(ApplyTransformPanelScale(
            {scale[0], scale[1], scale[2]}));
    ImGui::Spacing();

    ImGui::TextDisabled("Pivot");
    const char* currentMode =
        TransformPanelViewModel::PivotModeName(state.PivotMode);
    if (ImGui::BeginCombo("##TransformPivot", currentMode))
    {
        constexpr std::array modes{
            TransformPivotMode::Center,
            TransformPivotMode::Bottom,
            TransformPivotMode::Top};
        for (const TransformPivotMode mode : modes)
        {
            const bool selected = mode == state.PivotMode;
            if (ImGui::Selectable(
                    TransformPanelViewModel::PivotModeName(mode), selected))
            {
                static_cast<void>(transformPivotManager_.SetMode(mode));
                transformPanelStatusMessage_.clear();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (!state.Available)
        ImGui::TextDisabled("Select one or more voxels to edit transforms.");
    else if (transformGizmoManager_.IsDragging())
        ImGui::TextDisabled("Release the gizmo before entering exact values.");
    if (!transformPanelStatusMessage_.empty())
    {
        ImGui::Spacing();
        DrawErrorMessage(transformPanelStatusMessage_);
    }
    ImGui::End();
}

void EditorWorkspace::DrawPalettePanel()
{
    if (!ImGui::Begin(StylePanelWindowName, &showPalette_))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("COLOR STYLE");
    ImGui::Separator();

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
    if (openColorEditor) ImGui::OpenPopup("Color Editor Preview");
    if (EditorDialogStyle::BeginPopup(
            "Color Editor Preview",
            EditorDialogIntent::Information,
            "Color editor",
            "Color editing is prepared for a future release."))
    {
        ImGui::Text("Palette Index: %zu",
            paletteColorEditorIndex_.value_or(active->Index));
        ImGui::TextUnformatted(
            "Color editing is prepared but intentionally disabled in v1.");
        if (EditorDialogStyle::ActionButton("Close", true) ||
            EditorDialogStyle::Shortcuts(false) ==
                EditorDialogShortcut::Cancel)
        {
            paletteColorEditorIndex_.reset();
            ImGui::CloseCurrentPopup();
        }
        EditorDialogStyle::EndPopup();
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
    if (const ImGuiWindow* window = ImGui::FindWindowByName(AssetsPanelWindowName))
    {
        assetBrowserDropRect_ = {
            window->Pos.x, window->Pos.y,
            window->Pos.x + window->Size.x,
            window->Pos.y + window->Size.y};
        DrawFileDropOverlay(
            assetBrowserDropRect_, DragDropImportTarget::AssetBrowser);
    }
}

void EditorWorkspace::DrawForgeLibraryPanel()
{
    // STAMP-23 : la generation qui identifie le document pour les Stamps est
    // celle de VoxelEditSession (VoxelModelGeneration), car c'est elle que
    // ExecutePlaceVoxelStampOperation revalide au moment du commit. Utiliser
    // voxelDocumentSession_.Generation() ici faisait diverger les deux
    // compteurs (ClearVoxelViewport n'incremente que le premier) et le
    // placement etait refuse in extremis.
    const Stamps::ForgeLibraryPanelResult result = forgeLibraryPanel_.Draw(
        &showForgeLibrary_, voxelDocumentSession_.ActiveDocument(),
        VoxelModelGeneration());
    if (result.SaveSelectionRequested)
        BeginSaveSelectionAsStamp();
    if (result.ShowAssetsRequested)
    {
        showAssetBrowser_ = true;
        ImGui::SetWindowFocus(AssetsPanelWindowName);
    }
    if (result.SessionActivated)
    {
        stampGizmoTool_ = ActiveVoxelTool::Move;
        stampPlacementRequested_ = false;
        stampPlacementUiMessage_ =
            "Ready: move the preview, then place the complete Stamp.";
        stampGizmoDragActive_ = false;
        stampGizmoDragStartBounds_ = {};
        transformGizmoManager_.Reset();
        UpdateVoxelHighlights();
    }
    if (!result.Message.empty())
        AddConsoleMessage("Forge Library: " + result.Message);
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
    ImGui::Begin("Console", console_.VisibilityFlag());

    for (const std::string& message : console_.Messages())
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

    ImGui::Separator();
    const auto drawReport = [](const char* title,
        const EditorFrameProbe::FrameReport& report)
    {
        ImGui::Text("%s: %.2f ms (frame %llu)", title,
            report.FrameMilliseconds,
            static_cast<unsigned long long>(report.Index));
        for (std::size_t index = 0U;
             index < EditorFrameProbeSlotCount; ++index)
        {
            const EditorFrameProbe::SlotStats& stats = report.Slots[index];
            if (stats.Calls == 0U) continue;
            ImGui::Text("  %s: %.2f ms x%u",
                EditorFrameProbeSlotName(
                    static_cast<EditorFrameProbeSlot>(index)),
                stats.Milliseconds, stats.Calls);
        }
    };
    drawReport("Last frame", frameProbe_.LastFrame());
    drawReport("Worst frame", frameProbe_.WorstFrame());
    ImGui::Text("Slow frames (> %.0f ms): %llu",
        frameProbe_.SlowFrameThreshold(),
        static_cast<unsigned long long>(frameProbe_.SlowFrameCount()));
    if (ImGui::Button("Reset worst frame")) frameProbe_.ResetWorstFrame();

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
        std::string selectionStatus;
        if (voxelToolState_.IsSelectionActive())
        {
            const SelectionBounds& statusBounds =
                selectionInteraction_.IsActive() &&
                selectionInteraction_.IsDragRecognized()
                ? selectionInteraction_.CurrentBounds()
                : selectionService_.EditableBounds();
            if (statusBounds.Valid)
            {
                const auto dimensions = statusBounds.Dimensions();
                selectionStatus = " | Selection Box: " +
                    std::to_string(dimensions.X) + " x " +
                    std::to_string(dimensions.Y) + " x " +
                    std::to_string(dimensions.Z) + " - " +
                    std::to_string(selectionService_.Count()) + " voxel" +
                    (selectionService_.Count() == 1U ? "" : "s");
                if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::Creating)
                    selectionStatus += " Create";
                else if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::ResizingFace)
                    selectionStatus += " Resize";
                else if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingBox)
                    selectionStatus += " Move";
            }
            else
                selectionStatus = " | Selection Box: 0 voxels";
        }

        ImGui::Text(
            "Ready | %s%s%s | FPS %.1f | %.2f ms | Backend: %s | ImGui %s",
            projectStatus.c_str(),
            modelStatus.c_str(),
            selectionStatus.c_str(),
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

    if (EditorDialogStyle::BeginPopup(
            AboutPopupName,
            EditorDialogIntent::Information,
            "VoxelForge Studio",
            "Create faster. Stay the craftsperson."))
    {
        ImGui::TextUnformatted("VoxelForge Studio");
        ImGui::Separator();
        ImGui::TextUnformatted("Créer plus vite. Rester l'artisan.");
        ImGui::Spacing();
        ImGui::TextDisabled("Early development build");
        ImGui::Spacing();

        if (EditorDialogStyle::ActionButton("Close", true) ||
            EditorDialogStyle::Shortcuts(false) ==
                EditorDialogShortcut::Cancel)
        {
            ImGui::CloseCurrentPopup();
        }

        EditorDialogStyle::EndPopup();
    }
}

void EditorWorkspace::DrawProjectDialogs()
{
    DrawNewProjectDialog();
    DrawOpenProjectDialog();
    DrawSaveSelectionAsStampDialog();
}

void EditorWorkspace::BeginSaveSelectionAsStamp()
{
    const auto& project = projectManager_.ActiveProject();
    const Asset::Voxel::VoxelDocument* document = voxelDocumentSession_.ActiveDocument();
    if (!project || document == nullptr || selectionService_.Empty()) return;
    const Stamps::SaveSelectionAsStampResult result = saveSelectionAsStampWorkflow_.Begin({
        .ProjectRoot = project->RootPath(),
        .Document = document,
        .Selection = &selectionService_,
        .DocumentGeneration = voxelDocumentSession_.Generation(),
        .DocumentRevision = document->GetRevision()});
    saveSelectionAsStampMessage_ = result.Message;
    if (result.Status == Stamps::SaveSelectionAsStampStatus::Ready)
    {
        saveSelectionAsStampName_.fill('\0');
        showSaveSelectionAsStampPopup_ = true;
    }
    else AddConsoleMessage("Save Selection As: " + result.Message);
}

void EditorWorkspace::MoveLatestStampPreview(
    const std::int32_t x, const std::int32_t y, const std::int32_t z)
{
    stampPreview_.Move(x, y, z);
}

void EditorWorkspace::RotateLatestStampPreview(const bool clockwise)
{
    stampPreview_.Rotate(clockwise, stampRotationAxis_);
}

void EditorWorkspace::ToggleLatestStampRotationStep()
{
    stampPreview_.ToggleRotationStep();
}

void EditorWorkspace::MirrorLatestStampPreview(
    const Stamps::StampPlacementMirrorMode mirror)
{
    stampPreview_.Mirror(mirror);
}

void EditorWorkspace::ToggleLatestStampPreview(
    const Stamps::StampPlacementMirrorMode axis)
{
    stampPreview_.ToggleMirror(axis);
}

void EditorWorkspace::CycleLatestStampPreview()
{
    stampPreview_.CycleMirror();
}

void EditorWorkspace::ResetLatestStampTransform()
{
    stampPreview_.ResetTransform();
    stampPlacementUiMessage_.clear();
}

void EditorWorkspace::RequestLatestStampPlacement() noexcept
{
    if (!CanPlaceLatestStampPreview()) return;
    stampPlacementRequested_ = true;
    stampPlacementUiMessage_ = "Placement requested...";
}

void EditorWorkspace::PlaceLatestStampPreview()
{
    const Stamps::StampPlacementPlan* const plan =
        stampPlacementSession_.CurrentPlan();
    const std::size_t changed = plan != nullptr
        ? plan->Statistics.ChangedVoxelCount : 0U;
    if (stampPreview_.Place())
    {
        stampPlacementUiMessage_ =
            "STAMP PLACED: " + std::to_string(changed) +
            " cells. Move the preview before placing another copy.";
    }
    else
    {
        stampPlacementUiMessage_ =
            "Placement failed. The detailed reason is shown in Console.";
    }
}

bool EditorWorkspace::CanPlaceLatestStampPreview() const noexcept
{
    const Stamps::StampPlacementPlan* const plan =
        stampPlacementSession_.CurrentPlan();
    return stampPlacementSession_.IsActive() &&
        voxelDocumentSession_.HasActiveDocument() &&
        !voxelEditInProgress_ && !voxelEditHistory_.IsBusy() &&
        !transformGizmoManager_.IsDragging() && plan != nullptr &&
        plan->CanCommit && plan->Statistics.ChangedVoxelCount != 0U;
}

bool EditorWorkspace::RefreshLatestStampPreview()
{
    return stampPreview_.Refresh();
}

std::optional<SelectionBounds>
EditorWorkspace::CurrentStampPreviewBounds() const noexcept
{
    const Stamps::StampPlacementPlan* const plan =
        stampPlacementSession_.CurrentPlan();
    if (plan == nullptr || !plan->WorldBounds.Valid)
        return std::nullopt;
    return SelectionBounds{
        plan->WorldBounds.Minimum,
        plan->WorldBounds.Maximum,
        true};
}

void EditorWorkspace::ClearLatestStampPreview() noexcept
{
    stampPlacementRequested_ = false;
    stampPlacementUiMessage_.clear();
    static_cast<void>(transformGizmoManager_.CancelInteraction());
    stampGizmoDragActive_ = false;
    stampGizmoDragStartBounds_ = {};
    stampGizmoTool_ = ActiveVoxelTool::Move;
    stampPreview_.Clear();
}

void EditorWorkspace::DrawSaveSelectionAsStampDialog()
{
    constexpr const char* popupName = "Save Selection As...";
    if (showSaveSelectionAsStampPopup_)
    {
        ImGui::OpenPopup(popupName);
        showSaveSelectionAsStampPopup_ = false;
    }
    if (!EditorDialogStyle::BeginPopup(
            popupName,
            EditorDialogIntent::Save,
            "Save selection as Stamp",
            "Capture the current selection and add it to this project's Forge Library.",
            true))
        return;

    ImGui::TextUnformatted("Stamp Name");
    EditorDialogStyle::FullWidthField();
    ImGui::InputText(
        "##StampName", saveSelectionAsStampName_.data(), saveSelectionAsStampName_.size());
    ImGui::TextDisabled("Destination: Project Library");

    const bool saveAnyway = saveSelectionAsStampWorkflow_.RequiresSoftLimitConfirmation();
    const Stamps::SaveSelectionAsStampResult validation =
        saveSelectionAsStampWorkflow_.ValidateDraft({
            .Name = saveSelectionAsStampName_.data(),
            .ConfirmSoftLimit = saveAnyway});
    if (saveAnyway)
        EditorDialogStyle::DrawMessage(
            "This selection exceeds a soft Stamp limit. Save Anyway confirms the capture.",
            EditorDialogIntent::Warning);
    else if (validation.Status != Stamps::SaveSelectionAsStampStatus::Ready)
        EditorDialogStyle::DrawMessage(validation.Message, EditorDialogIntent::Warning);
    else if (!saveSelectionAsStampMessage_.empty() &&
             saveSelectionAsStampMessage_ != "Selection snapshot is ready to save.")
        EditorDialogStyle::DrawMessage(saveSelectionAsStampMessage_, EditorDialogIntent::Information);

    const bool canSave = validation.Status == Stamps::SaveSelectionAsStampStatus::Ready;
    const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts(canSave);
    EditorDialogStyle::BeginActions();
    if (EditorDialogStyle::ActionButton("Cancel", false) ||
        shortcut == EditorDialogShortcut::Cancel)
    {
        static_cast<void>(saveSelectionAsStampWorkflow_.Cancel());
        saveSelectionAsStampMessage_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (EditorDialogStyle::ActionButton(
            saveAnyway ? "Save Anyway" : "Save", true, canSave) ||
        shortcut == EditorDialogShortcut::Confirm)
    {
        const auto& project = projectManager_.ActiveProject();
        const Asset::Voxel::VoxelDocument* document = voxelDocumentSession_.ActiveDocument();
        Stamps::SaveSelectionAsStampCurrentContext current{};
        if (project) current.ProjectRoot = project->RootPath();
        current.Document = document;
        current.Selection = &selectionService_;
        current.DocumentGeneration = voxelDocumentSession_.Generation();
        current.DocumentRevision = document ? document->GetRevision() : 0U;
        const Stamps::SaveSelectionAsStampResult result = saveSelectionAsStampWorkflow_.Save(
            {.Name = saveSelectionAsStampName_.data(), .ConfirmSoftLimit = saveAnyway}, current);
        saveSelectionAsStampMessage_ = result.Message;
        if (result.IsSuccess())
        {
            stampCatalogService_.InvalidateCache();
            static_cast<void>(forgeLibraryViewModel_.Refresh());
            AddConsoleMessage("Save Selection As: " + result.Message);
            ImGui::CloseCurrentPopup();
        }
    }
    EditorDialogStyle::EndPopup();
}

void EditorWorkspace::DrawProjectDeletionDialog()
{
    if (showProjectDeletionPopup_)
    {
        ImGui::OpenPopup(DeleteProjectPopupName);
        showProjectDeletionPopup_ = false;
    }

    if (pendingProjectDeletionPath_.empty()) return;
    if (!EditorDialogStyle::BeginPopup(
            DeleteProjectPopupName,
            EditorDialogIntent::Destructive,
            "Delete this project?",
            "The complete project will be moved to the Windows Recycle Bin."))
        return;

    ImGui::TextUnformatted("Name");
    ImGui::TextWrapped("%s",
        pendingProjectDeletionPath_.stem().string().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("Location");
    ImGui::TextWrapped("%s",
        pendingProjectDeletionPath_.parent_path().string().c_str());
    ImGui::Spacing();
    EditorDialogStyle::DrawMessage(
        "This removes its models, scenes, materials, and local settings.",
        EditorDialogIntent::Warning);
    if (!projectDeletionError_.empty())
        EditorDialogStyle::DrawMessage(
            projectDeletionError_, EditorDialogIntent::Destructive);

    EditorDialogStyle::BeginActions();
    const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts(false);
    const bool cancelRequested =
        EditorDialogStyle::ActionButton("Cancel", true) ||
        shortcut == EditorDialogShortcut::Cancel;
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    const bool deleteRequested = EditorDialogStyle::ActionButton(
        "Move to Recycle Bin", false, true, true);

    if (cancelRequested)
    {
        pendingProjectDeletionPath_.clear();
        projectDeletionError_.clear();
        ImGui::CloseCurrentPopup();
    }
    else if (deleteRequested)
    {
        DeletePendingProject();
        if (pendingProjectDeletionPath_.empty())
            ImGui::CloseCurrentPopup();
    }

    EditorDialogStyle::EndPopup();
}

void EditorWorkspace::DrawModelImportDialogs()
{
    if (showImportConfirmationPopup_)
    {
        ImGui::OpenPopup(ImportConfirmationPopupName);
        showImportConfirmationPopup_ = false;
    }
    if (EditorDialogStyle::BeginPopup(
            ImportConfirmationPopupName,
            EditorDialogIntent::Import,
            "Import models",
            "Copy the selected VOX models into Assets/Models."))
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
        const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts();
        EditorDialogStyle::BeginActions();
        if (EditorDialogStyle::ActionButton("Cancel", false) ||
            shortcut == EditorDialogShortcut::Cancel)
        {
            selectedImportPaths_.clear();
            if (importStartedFromDrop_) dragDropImport_.Cancel();
            importStartedFromDrop_ = false;
            pendingDropImportTarget_ = DragDropImportTarget::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton(importLabel, true) ||
            shortcut == EditorDialogShortcut::Confirm)
        {
            std::vector<std::filesystem::path> paths =
                std::move(selectedImportPaths_);
            if (importStartedFromDrop_) dragDropImport_.MarkImporting();
            ImGui::CloseCurrentPopup();
            BeginModelImport(std::move(paths));
        }
        EditorDialogStyle::EndPopup();
    }

    if (showImportCollisionPopup_)
    {
        ImGui::OpenPopup(ImportCollisionPopupName);
        showImportCollisionPopup_ = false;
    }
    if (EditorDialogStyle::BeginPopup(
            ImportCollisionPopupName,
            EditorDialogIntent::Warning,
            "Model already exists",
            "Choose how VoxelForge should resolve this file-name collision."))
    {
        ImGui::TextUnformatted("Le fichier existe déjà.");
        if (const auto& collision = importBatch_.PendingCollision())
        {
            ImGui::TextWrapped("%s",
                collision->DestinationPath.string().c_str());
        }
        const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts();
        EditorDialogStyle::BeginActions();
        if (EditorDialogStyle::ActionButton("Annuler", false) ||
            shortcut == EditorDialogShortcut::Cancel)
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Cancel);
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton("Ignorer", false))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Skip);
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton(
                "Remplacer", false, true, true))
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Replace);
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton("Renommer", true) ||
            shortcut == EditorDialogShortcut::Confirm)
        {
            ImGui::CloseCurrentPopup();
            ContinueModelImport(ModelImportCollisionAction::Rename);
        }
        EditorDialogStyle::EndPopup();
    }

    if (showOpenImportedModelPopup_)
    {
        ImGui::OpenPopup(OpenImportedModelPopupName);
        showOpenImportedModelPopup_ = false;
    }
    if (EditorDialogStyle::BeginPopup(
            OpenImportedModelPopupName,
            EditorDialogIntent::Open,
            "Open imported model",
            "The import is complete. Open this model in the viewport now?"))
    {
        ImGui::TextUnformatted("Open in Viewport?");
        if (importedModelToOpen_)
            ImGui::TextDisabled("%s",
                importedModelToOpen_->filename().string().c_str());
        const bool canOpen = importedModelToOpen_.has_value();
        const EditorDialogShortcut shortcut =
            EditorDialogStyle::Shortcuts(canOpen);
        EditorDialogStyle::BeginActions();
        if (EditorDialogStyle::ActionButton("Non", false) ||
            shortcut == EditorDialogShortcut::Cancel)
        {
            importedModelToOpen_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton("Oui", true, canOpen) ||
            shortcut == EditorDialogShortcut::Confirm)
        {
            const std::filesystem::path path =
                importedModelToOpen_.value_or(std::filesystem::path{});
            importedModelToOpen_.reset();
            ImGui::CloseCurrentPopup();
            if (!path.empty()) static_cast<void>(OpenVoxInViewport(path));
        }
        EditorDialogStyle::EndPopup();
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
    if (!EditorDialogStyle::BeginPopup(
            DirtyConfirmationPopupName,
            EditorDialogIntent::Save,
            "Unsaved changes",
            "Save your voxel model before continuing?"))
        return;

    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::string fileName = document
        ? document->SourcePath().filename().string() : "active model";
    ImGui::TextWrapped("Save changes to %s?", fileName.c_str());
    const bool canSave = !voxelDocumentSaveService_.IsBusy();
    const EditorDialogShortcut shortcut =
        EditorDialogStyle::Shortcuts(canSave);
    EditorDialogStyle::BeginActions();
    if (EditorDialogStyle::ActionButton("Cancel", false) ||
        shortcut == EditorDialogShortcut::Cancel)
    {
        if (dirtyActionConfirmation_.PendingAction() ==
            DestructiveAction::ExitApplication)
            static_cast<void>(closeRequest_.Cancel());
        dirtyActionConfirmation_.Cancel();
        pendingProjectPath_.clear();
        pendingVoxelPath_.clear();
        pendingVoxelModelCreation_ = {};
        pendingVoxelModelCollisionAction_ =
            VoxelModelCreationCollisionAction::Ask;
        pendingInstantVoxelModelCreation_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (EditorDialogStyle::ActionButton(
            "Don't Save", false, true, true))
    {
        const auto action = dirtyActionConfirmation_.Discard();
        ImGui::CloseCurrentPopup();
        if (action == DestructiveAction::ExitApplication)
            static_cast<void>(closeRequest_.Discard());
        else if (action)
            deferredDirtyAction_ = *action;
    }
    ImGui::SameLine();
    if (EditorDialogStyle::ActionButton("Save", true, canSave) ||
        shortcut == EditorDialogShortcut::Confirm)
    {
        if (dirtyActionConfirmation_.PendingAction() ==
            DestructiveAction::ExitApplication)
        {
            static_cast<void>(closeRequest_.ScheduleSave());
        }
        deferredDirtySaveRequested_ = true;
        ImGui::CloseCurrentPopup();
    }
    EditorDialogStyle::EndPopup();
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
    if (EditorDialogStyle::BeginPopup(
            CreatePopup,
            EditorDialogIntent::Create,
            "Create voxel model",
            "Name the model and choose the editable voxel volume.",
            true))
    {
        EditorDialogStyle::FullWidthField();
        ImGui::InputText(
            "##VoxelModelName", newVoxelModelName_.data(), newVoxelModelName_.size());
        ImGui::TextDisabled("Model name");
        EditorDialogStyle::FullWidthField();
        ImGui::InputInt3("Dimensions", newVoxelModelDimensions_.data());
        ImGui::TextDisabled(
            "Editable volume in voxels, 1..256 per axis. The last volume you "
            "entered is kept for the next model.");
        EditorDialogStyle::DrawMessage(
            voxelModelCreationError_, EditorDialogIntent::Destructive);
        const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts();
        EditorDialogStyle::BeginActions();
        if (EditorDialogStyle::ActionButton("Cancel", false) ||
            shortcut == EditorDialogShortcut::Cancel)
        {
            voxelModelCreationError_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton("Create", true) ||
            shortcut == EditorDialogShortcut::Confirm)
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
        EditorDialogStyle::EndPopup();
    }

    if (showVoxelModelCollisionPopup_)
    {
        ImGui::OpenPopup(CollisionPopup);
        showVoxelModelCollisionPopup_ = false;
    }
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
        ImVec2(0.5F, 0.5F));
    if (EditorDialogStyle::BeginPopup(
            CollisionPopup,
            EditorDialogIntent::Warning,
            "Model already exists",
            "Rename the new model, replace the existing file, or cancel."))
    {
        ImGui::TextWrapped("The model already exists.");
        const EditorDialogShortcut shortcut = EditorDialogStyle::Shortcuts();
        EditorDialogStyle::BeginActions();
        if (EditorDialogStyle::ActionButton("Cancel", false) ||
            shortcut == EditorDialogShortcut::Cancel)
        {
            pendingVoxelModelCreation_ = {};
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Ask;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton(
                "Replace", false, true, true))
        {
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Replace;
            CreateVoxelModelNow();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorDialogStyle::ActionButton("Rename", true) ||
            shortcut == EditorDialogShortcut::Confirm)
        {
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Rename;
            CreateVoxelModelNow();
            ImGui::CloseCurrentPopup();
        }
        EditorDialogStyle::EndPopup();
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

    if (!EditorDialogStyle::BeginPopup(
            PopupName,
            EditorDialogIntent::Create,
            "Create project",
            "Choose a name and location. VoxelForge creates the complete project structure.",
            true))
    {
        return;
    }

    EditorDialogStyle::FullWidthField();
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

    const EditorDialogShortcut shortcut =
        EditorDialogStyle::Shortcuts(validation.IsValid());
    EditorDialogStyle::BeginActions();
    if (EditorDialogStyle::ActionButton("Cancel", false) ||
        shortcut == EditorDialogShortcut::Cancel)
    {
        projectDialogError_.clear();
        newProjectName_.fill('\0');
        newProjectParentPath_.fill('\0');
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (EditorDialogStyle::ActionButton(
            "Create", true, validation.IsValid()) ||
        shortcut == EditorDialogShortcut::Confirm)
    {
        CreateProject();
        if (projectDialogError_.empty() &&
            !dirtyActionConfirmation_.IsPending())
            ImGui::CloseCurrentPopup();
    }

    EditorDialogStyle::EndPopup();
}

void EditorWorkspace::DrawOpenProjectDialog()
{
    constexpr const char* PopupName = "Open VoxelForge Project";

    if (showOpenProjectPopup_)
    {
        ImGui::OpenPopup(PopupName);
        showOpenProjectPopup_ = false;
    }

    if (!EditorDialogStyle::BeginPopup(
            PopupName,
            EditorDialogIntent::Open,
            "Open project",
            "Select a .vfproject file to continue your previous session.",
            true))
    {
        return;
    }

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

    const EditorDialogShortcut shortcut =
        EditorDialogStyle::Shortcuts(inputError.empty());
    EditorDialogStyle::BeginActions();
    if (EditorDialogStyle::ActionButton("Cancel", false) ||
        shortcut == EditorDialogShortcut::Cancel)
    {
        projectDialogError_.clear();
        openProjectFilePath_.fill('\0');
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (EditorDialogStyle::ActionButton(
            "Open", true, inputError.empty()) ||
        shortcut == EditorDialogShortcut::Confirm)
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

    EditorDialogStyle::EndPopup();
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
    // MODEL-01 : les dimensions ne sont PAS remises a 64 cube. Un artiste qui
    // travaille en 128 x 32 x 128 enchaine plusieurs modeles a la meme taille ;
    // le champ conserve donc le dernier volume saisi. On le ramene simplement
    // dans les bornes valides, pour ne jamais rouvrir sur une saisie absurde.
    for (int& axis : newVoxelModelDimensions_)
        axis = std::clamp(axis, 1, 256);
    voxelModelCreationError_.clear();
    showNewVoxelModelPopup_ = true;
}

void EditorWorkspace::RequestInstantNewVoxelModel()
{
    if (!projectManager_.HasActiveProject())
    {
        AddConsoleMessage("Voxel model creation failed: no project is loaded.");
        return;
    }
    if (dirtyActionConfirmation_.IsPending()) return;

    pendingVoxelModelCreation_ = NewVoxelModelWorkflow::DefaultRequest();
    pendingVoxelModelCollisionAction_ =
        VoxelModelCreationCollisionAction::Rename;
    pendingInstantVoxelModelCreation_ = true;
    if (!dirtyActionConfirmation_.Request(
            DestructiveAction::CreateVoxelModel,
            HasUnsavedVoxelChanges()))
    {
        showDirtyConfirmationPopup_ = true;
        return;
    }
    CreateVoxelModelNow();
}

void EditorWorkspace::RequestCreateVoxelModel(
    VoxelModelCreationRequest request,
    const VoxelModelCreationCollisionAction collisionAction)
{
    if (dirtyActionConfirmation_.IsPending()) return;
    pendingInstantVoxelModelCreation_ = false;
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
    // A new project is an immediately usable workshop: create and open its
    // first empty model without requiring a second user command.
    RequestInstantNewVoxelModel();
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
    welcomeNotification_ =
        "Removed from recent projects. Files on disk were not changed.";

    if (failedRecentProjectPath_ &&
        *failedRecentProjectPath_ == projectFilePath)
    {
        failedRecentProjectPath_.reset();
    }

    AddConsoleMessage(
        "Removed from recent projects: " + projectFilePath.string());
}

void EditorWorkspace::RequestDeleteProject(
    const std::filesystem::path& projectFilePath)
{
    pendingProjectDeletionPath_ = projectFilePath;
    projectDeletionError_.clear();
    welcomeError_.clear();
    welcomeNotification_.clear();
    showProjectDeletionPopup_ = true;
}

void EditorWorkspace::DeletePendingProject()
{
    if (pendingProjectDeletionPath_.empty()) return;

    ProjectDeletionRequest request;
    request.ProjectFilePath = pendingProjectDeletionPath_;
    if (const auto& activeProject = projectManager_.ActiveProject())
        request.ActiveProjectRoot = activeProject->RootPath();
    request.ProtectedRoots = ProjectDeletionService::DefaultProtectedRoots();
    request.Confirmed = true;

    const ProjectDeletionResult result =
        projectDeletionService_.DeleteProject(request);
    if (!result.Succeeded())
    {
        projectDeletionError_ = result.Message;
        welcomeError_ = result.Message;
        AddConsoleMessage("Project deletion refused: " + result.Message);
        return;
    }

    const std::filesystem::path deletedProject = pendingProjectDeletionPath_;
    if (!projectManager_.RemoveRecentProject(deletedProject))
    {
        welcomeError_ =
            "Project moved to the Recycle Bin, but its recent entry could not "
            "be removed: " + projectManager_.LastError();
        AddConsoleMessage(welcomeError_);
    }
    else
    {
        welcomeError_.clear();
        welcomeNotification_ = "Project moved to the Windows Recycle Bin.";
        AddConsoleMessage(
            "Project moved to Recycle Bin: " + result.ProjectRoot.string());
    }
    if (failedRecentProjectPath_ &&
        *failedRecentProjectPath_ == deletedProject)
        failedRecentProjectPath_.reset();
    projectDeletionError_.clear();
    pendingProjectDeletionPath_.clear();
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
    const bool instant = pendingInstantVoxelModelCreation_;
    const DirectCreationFlowResult flow = instant
        ? newVoxelModelWorkflow_.Create(
            voxelModelCreationService_, directCreationFlowService_)
        : directCreationFlowService_.Create(
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
        if (instant)
        {
            pendingVoxelModelCreation_ = {};
            pendingVoxelModelCollisionAction_ =
                VoxelModelCreationCollisionAction::Ask;
            pendingInstantVoxelModelCreation_ = false;
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
    pendingInstantVoxelModelCreation_ = false;
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
    if (closeRequest_.State() != EditorCloseRequestState::None ||
        dirtyActionConfirmation_.IsPending())
        return;
    const bool hasUnsavedChanges = HasUnsavedVoxelChanges();
    if (!closeRequest_.Request(hasUnsavedChanges)) return;
    if (!hasUnsavedChanges)
    {
        return;
    }
    static_cast<void>(dirtyActionConfirmation_.Request(
        DestructiveAction::ExitApplication, true));
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

void EditorWorkspace::ProcessDeferredDirtyActionAtFrameStart()
{
    if (deferredDirtyAction_)
    {
        const DestructiveAction action = *deferredDirtyAction_;
        deferredDirtyAction_.reset();
        ExecutePendingDirtyAction(action);
    }

    if (!deferredDirtySaveRequested_) return;
    deferredDirtySaveRequested_ = false;
    const bool closingAfterSave =
        dirtyActionConfirmation_.PendingAction() ==
        DestructiveAction::ExitApplication;
    if (!SaveVoxelModel())
    {
        if (closingAfterSave)
            static_cast<void>(closeRequest_.CompleteSave(false));
        showDirtyConfirmationPopup_ = true;
        return;
    }

    const auto action =
        dirtyActionConfirmation_.ContinueAfterSuccessfulSave();
    if (!action)
    {
        if (closingAfterSave)
            static_cast<void>(closeRequest_.CompleteSave(false));
        AddConsoleMessage(
            "[Close] Saved the model, but no pending action was available.");
        return;
    }
    if (*action == DestructiveAction::ExitApplication)
    {
        static_cast<void>(closeRequest_.CompleteSave(true));
        return;
    }
    ExecutePendingDirtyAction(*action);
}

void EditorWorkspace::CompleteDeferredCloseAfterFrame()
{
    if (closeRequest_.State() != EditorCloseRequestState::ReadyToClose) return;
    static_cast<void>(SaveActiveProjectSession());
    PrepareForApplicationClose();
    static_cast<void>(closeRequest_.CompleteRenderedFrame());
}

void EditorWorkspace::PrepareForApplicationClose()
{
    ClearLatestStampPreview();
    voxelBoxInteraction_.Cancel();
    voxelLineInteraction_.Cancel();
    voxelSphereInteraction_.Cancel();
    static_cast<void>(selectionInteraction_.Cancel());
    static_cast<void>(transformPreviewModel_.CancelPreview());
    transformGizmoManager_.Reset();
    viewportRenderer_.ConfigureTransformGizmo(nullptr);
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    voxelScaleStatusMessage_.clear();
    voxelSelectionClickCandidate_ = false;
    selectionPointerAnchor_.reset();
    dragDropImport_.Reset();
    pendingDropImportTarget_ = DragDropImportTarget::None;
    selectedImportPaths_.clear();
    importBatch_.Reset();
    showImportConfirmationPopup_ = false;
    showImportCollisionPopup_ = false;
    showOpenImportedModelPopup_ = false;
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
    // Every project transition invalidates the immutable preview source,
    // including active-project to active-project switches.
    ClearLatestStampPreview();
    forgeLibraryViewModel_.ResetForProjectChange();
    const auto& project = projectManager_.ActiveProject();
    if (!project)
    {
        dragDropImport_.Reset();
        pendingDropImportTarget_ = DragDropImportTarget::None;
        importStartedFromDrop_ = false;
        selectedImportPaths_.clear();
        importBatch_.Reset();
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
        stampProjectLibraryRepository_.ClearProjectRoot();
        stampJsonCatalogStore_.ClearProjectRoot();
        static_cast<void>(saveSelectionAsStampWorkflow_.Cancel());
        ClearLatestStampPreview();
        brushProfileService_.ClearProject();
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
    if (!stampProjectLibraryRepository_.SetProjectRoot(project->RootPath()) ||
        !stampJsonCatalogStore_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("Project Stamp Library setup failed.");
    if (!brushProfileService_.SetProjectRoot(project->RootPath()))
        AddConsoleMessage("Brush profile setup failed.");
    else
    {
        const BrushProfileResult profiles = brushProfileService_.Load();
        if (profiles.Status != BrushProfileStatus::Success &&
            profiles.Status != BrushProfileStatus::NotFound)
            AddConsoleMessage("Brush profile load failed: " + profiles.Message);
        else if (!profiles.Message.empty())
            AddConsoleMessage("Brush profiles: " + profiles.Message);
        if (const BrushProfile* active = brushProfileService_.ActiveProfile(); active != nullptr)
        {
            static_cast<void>(BrushProfileService::Apply(*active, toolContext_.Smart));
            if (paletteService_.HasActivePalette() &&
                !paletteService_.SelectColor(active->PaletteIndex))
                AddConsoleMessage("Brush profile palette is unavailable in this project.");
        }
    }
}

bool EditorWorkspace::SaveActiveProjectSession()
{
    const auto& project = projectManager_.ActiveProject();
    if (!project || projectSessionService_.ProjectRoot().empty()) return true;

    std::filesystem::path lastModel;
    if (voxelDocumentSession_.HasActiveDocument())
    {
        std::error_code relativeError;
        lastModel = std::filesystem::relative(
            voxelDocumentSession_.SourcePath(), project->RootPath(),
            relativeError);
        if (relativeError ||
            !ProjectSessionService::IsValidModelPath(lastModel))
        {
            AddConsoleMessage("Project session save warning: active model path "
                "is not a valid project model.");
            return false;
        }
    }

    const ProjectSessionData session = BuildSessionData(
        viewportCamera_.CaptureState(),
        voxelToolState_,
        toolContext_.Smart,
        paletteService_.ActiveIndex().value_or(
            PaletteService::FirstSelectableIndex),
        std::move(lastModel));

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

    ApplySessionToTools(loaded.Session, toolContext_.Smart, voxelToolState_);
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

    if (!loaded.CameraValid || !viewportCamera_.RestoreState(
            FromSessionCamera(loaded.Session.Camera)))
    {
        AddConsoleMessage("Project session camera invalid; framed model used.");
    }
}

void EditorWorkspace::BeginModelImport(
    std::vector<std::filesystem::path> sourcePaths)
{
    modelImportService_.SetRefreshCallback({});
    importBatch_.Begin(std::move(sourcePaths));
    ContinueModelImport(ModelImportCollisionAction::Ask);
}

void EditorWorkspace::ContinueModelImport(
    ModelImportCollisionAction collisionAction)
{
    while (importBatch_.HasPending())
    {
        const std::filesystem::path source = importBatch_.CurrentSource();
        const ModelImportResult result =
            modelImportService_.ImportModel(source, collisionAction);
        collisionAction = ModelImportCollisionAction::Ask;

        switch (importBatch_.Classify(result))
        {
        case ModelImportBatchStep::Collision:
            showImportCollisionPopup_ = true;
            return;
        case ModelImportBatchStep::Cancelled:
            AddConsoleMessage("Model import cancelled.");
            FinishModelImport(true);
            return;
        case ModelImportBatchStep::Imported:
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
            break;
        case ModelImportBatchStep::Skipped:
            AddConsoleMessage(
                "Import skipped: " + source.filename().string());
            break;
        case ModelImportBatchStep::Failed:
            AddConsoleMessage(
                "Import failed: " + source.filename().string() +
                " - " + result.Message);
            break;
        }
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
    const std::vector<std::filesystem::path>& successes =
        importBatch_.SuccessfulPaths();
    if (!successes.empty())
    {
        const std::filesystem::path relativeToAssets =
            successes.back().lexically_relative(
                modelImportService_.ProjectRoot() / "Assets");
        static_cast<void>(assetBrowser_.RevealEntry(relativeToAssets));
        static_cast<void>(assetInspector_.UpdateSelection(
            assetBrowser_.SelectedEntry()));
    }

    if (importStartedFromDrop_)
    {
        AddConsoleMessage("[Import] Completed:\n" +
            std::to_string(importBatch_.CompletedCount()) + " imported\n" +
            std::to_string(importBatch_.SkippedCount()) + " skipped\n" +
            std::to_string(importBatch_.FailedCount()) + " failed");
        if (!cancelled &&
            pendingDropImportTarget_ == DragDropImportTarget::Viewport &&
            importBatch_.HasSingleSuccess())
            static_cast<void>(OpenVoxInViewportNow(successes.front()));
        if (cancelled)
            dragDropImport_.Cancel();
        else
            dragDropImport_.MarkCompleted();
    }
    else if (importBatch_.RequestedCount() > 1U)
    {
        AddConsoleMessage(
            std::to_string(successes.size()) + " models imported.");
    }
    else if (importBatch_.HasSingleSuccess())
    {
        importedModelToOpen_ = successes.front();
        showOpenImportedModelPopup_ = true;
    }
    importBatch_.Reset();
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
        if (const BrushProfile* active = brushProfileService_.ActiveProfile(); active != nullptr &&
            !paletteService_.SelectColor(active->PaletteIndex))
            AddConsoleMessage("Brush profile palette is unavailable in this document.");
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
    selectionService_.SetDocumentGeneration(
        voxelDocumentSession_.Generation());
    static_cast<void>(selectionInteraction_.ValidateDocumentGeneration(
        voxelDocumentSession_.Generation()));
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
    const AddVoxelTarget target = ResolveAddVoxelTarget();
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
        const std::size_t subModelIndex =
            voxelSelection_.Selected()->SubModelIndex;
        const VoxelEditHistoryResult historyResult =
            voxelEditHistory_.Execute(*this, {
                "Add Voxel",
                {VoxelChange{
                    subModelIndex,
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

AddVoxelTarget EditorWorkspace::ResolveAddVoxelTarget() const noexcept
{
    const std::optional<VoxelRaycastHit>& selection =
        voxelSelection_.Selected();
    if (const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument())
    {
        return FindAddVoxelTarget(*document, selection);
    }
    const Voxel::VoxelGrid* grid = activeVoxelModel_
        ? activeVoxelModel_->GetGrid(0U) : nullptr;
    return FindAddVoxelTarget(grid, selection);
}

bool EditorWorkspace::RefreshSmartToolHover() noexcept
{
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || currentViewportRectangle_.Width <= 0.0F ||
        currentViewportRectangle_.Height <= 0.0F)
        return false;

    const ViewportRayBuildResult ray = BuildViewportRay(
        {ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y},
        currentViewportRectangle_, viewportCamera_.GetViewProjection(),
        viewportCamera_.GetPosition());
    if (!ray.Succeeded()) return false;

    VoxelRaycastOptions options;
    options.Transform = CenteredVoxelModelTransform(voxelModelCenter_);
    try
    {
        std::optional<VoxelRaycastHit> hit =
            RaycastVoxelDocument(*document, *ray.Ray, options);
        return voxelSelection_.SetHovered(
            hit ? VoxelPickingInteractionState::Hit
                : VoxelPickingInteractionState::NoHit,
            std::move(hit));
    }
    catch (...)
    {
        // A transient source-picker failure must not leave an old hit active;
        // the next frame can safely reconstruct it.
        return voxelSelection_.SetHovered(VoxelPickingInteractionState::NoHit);
    }
}

std::optional<SmartToolRequest> EditorWorkspace::BuildSmartPencilRequest(
    const SmartToolStroke* const stroke)
{
    Asset::Voxel::VoxelDocument* document = voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || !toolContext_.Smart.IsOperational()) return std::nullopt;

    const SmartAction action = toolContext_.Smart.Action();
    if (action != SmartAction::Add && action != SmartAction::Erase &&
        action != SmartAction::Paint)
        return std::nullopt;
    const auto dimensions = document->GetDimensions(0U);
    if (!dimensions) return std::nullopt;

    SmartBrushState state = toolContext_.Smart.Brush();
    if (action == SmartAction::Add || action == SmartAction::Paint)
    {
        const std::optional<PaletteColorSelection> activeColor =
            paletteService_.ActiveColor();
        toolContext_.Smart.Brush().PaletteIndex = activeColor ? activeColor->Index : 0U;
        state.PaletteIndex = toolContext_.Smart.Brush().PaletteIndex;
    }

    // Picking remains source-document based for the whole gesture. Pending
    // cells still participate below through stroke->ReadVoxel, but never
    // become a new hover face that could stack a held Pencil stroke.
    if (stroke != nullptr && stroke->IsActive())
        static_cast<void>(RefreshSmartToolHover());
    const std::optional<VoxelRaycastHit>& hit = voxelSelection_.Hovered();
    const bool faceGeometry = toolContext_.Smart.Geometry() == SmartGeometry::Face;
    const bool lineGeometry = toolContext_.Smart.Geometry() == SmartGeometry::Line;
    const SmartGeometry geometry = toolContext_.Smart.Geometry();
    const bool geometryTool = geometry == SmartGeometry::Geometry;
    const bool surfaceGeometry = geometry == SmartGeometry::Surface;
    const bool fillGeometry = geometry == SmartGeometry::Fill;
    // Face and Surface are anchored exclusively to an exposed voxel face. A
    // construction plane has no supporting voxel and therefore cannot seed it.
    const bool usesWorkplane = !faceGeometry && !surfaceGeometry &&
        !fillGeometry && !hit &&
        workplaneHit_.has_value() &&
        (action == SmartAction::Add || geometryTool);
    std::optional<Asset::Voxel::VoxelPosition> target = usesWorkplane
        ? std::optional<Asset::Voxel::VoxelPosition>{workplaneHit_->Position}
        : hit ? action != SmartAction::Add
            ? std::optional<Asset::Voxel::VoxelPosition>{{
                static_cast<std::int32_t>(hit->Coordinates.X),
                static_cast<std::int32_t>(hit->Coordinates.Y),
                static_cast<std::int32_t>(hit->Coordinates.Z)}}
            : std::optional<Asset::Voxel::VoxelPosition>{hit->AdjacentPosition}
        : std::nullopt;
    if (fillGeometry)
    {
        if (!hit) return std::nullopt;
        target = Asset::Voxel::VoxelPosition{
            static_cast<std::int32_t>(hit->Coordinates.X),
            static_cast<std::int32_t>(hit->Coordinates.Y),
            static_cast<std::int32_t>(hit->Coordinates.Z)};
    }
    if (surfaceGeometry)
    {
        // MouseDown must start from an exposed voxel face. Once that seed and
        // its plane are locked, dragging may continue through empty viewport
        // space: the locked ray/plane intersection below supplies B.
        if (!hit && (!smartSurfaceLockedSeed_ || !smartSurfacePlane_))
        {
            return std::nullopt;
        }
        if (hit)
        {
            target = Asset::Voxel::VoxelPosition{
                static_cast<std::int32_t>(hit->Coordinates.X),
                static_cast<std::int32_t>(hit->Coordinates.Y),
                static_cast<std::int32_t>(hit->Coordinates.Z)};
        }
    }
    const Asset::Voxel::VoxelPosition workplaneNormal =
        workplaneService_.Definition().Axis == WorkplaneAxis::X
            ? Asset::Voxel::VoxelPosition{1, 0, 0}
            : workplaneService_.Definition().Axis == WorkplaneAxis::Z
            ? Asset::Voxel::VoxelPosition{0, 0, 1}
            : Asset::Voxel::VoxelPosition{0, 1, 0};
    Asset::Voxel::VoxelPosition normal = usesWorkplane
        ? workplaneNormal
        : hit ? VoxelHitFaceIntegerNormal(hit->Face)
              : Asset::Voxel::VoxelPosition{0, 1, 0};
    float geometrySurfaceCoordinate = normal.X != 0
        ? static_cast<float>(target ? target->X : 0)
        : normal.Y != 0 ? static_cast<float>(target ? target->Y : 0)
        : static_cast<float>(target ? target->Z : 0);
    if (geometryTool && hit)
    {
        const std::int32_t hitCoordinate = normal.X != 0
            ? static_cast<std::int32_t>(hit->Coordinates.X)
            : normal.Y != 0 ? static_cast<std::int32_t>(hit->Coordinates.Y)
            : static_cast<std::int32_t>(hit->Coordinates.Z);
        geometrySurfaceCoordinate = static_cast<float>(hitCoordinate +
            ((normal.X > 0 || normal.Y > 0 || normal.Z > 0) ? 1 : 0));
    }
    else if (geometryTool && usesWorkplane)
    {
        geometrySurfaceCoordinate = static_cast<float>(
            workplaneService_.Definition().Coordinate);
    }
    const SmartToolGeometryPlane* const lockedPlanarPlane = geometryTool &&
            smartGeometryPlane_
        ? &*smartGeometryPlane_
        : surfaceGeometry && smartSurfacePlane_
        ? &*smartSurfacePlane_
        : nullptr;
    const bool geometryHeightPhase = geometryTool &&
        smartGeometryPhase_ == SmartGeometryInteractionPhase::Height &&
        smartGeometryPlannedEnd_.has_value();
    if (geometryHeightPhase)
        target = smartGeometryPlannedEnd_;
    if (lockedPlanarPlane != nullptr && !geometryHeightPhase)
    {
        // Once A has locked the plane, B comes from the current pointer ray
        // against that plane—not from a new face hit or the default
        // workplane. This keeps a wall/floor rectangle stable through empty
        // space and across other voxel faces.
        const ViewportRayBuildResult ray = BuildViewportRay(
            {ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y},
            currentViewportRectangle_, viewportCamera_.GetViewProjection(),
            viewportCamera_.GetPosition());
        if (ray.Succeeded())
        {
            const VoxelModelTransform transform =
                CenteredVoxelModelTransform(voxelModelCenter_);
            const Vec3 origin = TransformPoint(transform.InverseModelMatrix,
                ray.Ray->Origin);
            const Vec3 direction = TransformVector(transform.InverseModelMatrix,
                ray.Ray->Direction);
            const Asset::Voxel::VoxelPosition& planeNormal =
                lockedPlanarPlane->Normal;
            const float denominator = direction.X * planeNormal.X +
                direction.Y * planeNormal.Y + direction.Z * planeNormal.Z;
            const float planeCoordinate = lockedPlanarPlane->SurfaceCoordinate;
            const float rayCoordinate = planeNormal.X != 0 ? origin.X :
                planeNormal.Y != 0 ? origin.Y : origin.Z;
            const float distance = std::abs(denominator) > 1.0e-6F
                ? (planeCoordinate - rayCoordinate) / denominator : -1.0F;
            if (std::isfinite(distance) && distance >= 0.0F)
            {
                const Vec3 point = origin + direction * distance;
                const auto inIntRange = [](const float value) noexcept
                {
                    return value >= static_cast<float>(std::numeric_limits<std::int32_t>::min()) &&
                        value <= static_cast<float>(std::numeric_limits<std::int32_t>::max());
                };
                if (IsFinite(point) && inIntRange(point.X) && inIntRange(point.Y) &&
                    inIntRange(point.Z))
                    target = Asset::Voxel::VoxelPosition{
                        static_cast<std::int32_t>(std::floor(point.X)),
                        static_cast<std::int32_t>(std::floor(point.Y)),
                        static_cast<std::int32_t>(std::floor(point.Z))};
            }
            else target.reset();
        }
        else target.reset();
    }
    if (faceGeometry && faceDepthLockedSeed_)
    {
        normal = faceDepthLockedSeed_->Normal;
        const Asset::Voxel::VoxelPosition seed = faceDepthLockedSeed_->Position;
        target = action == SmartAction::Add
            ? Asset::Voxel::VoxelPosition{seed.X + normal.X, seed.Y + normal.Y,
                seed.Z + normal.Z}
            : seed;
    }
    if (!target) return std::nullopt;
    if (lineGeometry && smartLineLockedStart_)
    {
        const SmartToolLineConstraintResult constrained =
            smartToolLineConstraintResolver_.Resolve(*target, ImGui::GetIO().KeyShift);
        target = constrained.Endpoint;
        toolContext_.Smart.SetLineConstraintAxis(constrained.Axis);
    }
    else if (lineGeometry)
    {
        toolContext_.Smart.SetLineConstraintAxis(std::nullopt);
    }
    if (lockedPlanarPlane != nullptr && target)
    {
        normal = lockedPlanarPlane->Normal;
        target = SmartToolPlanner::ProjectGeometryEndpoint(
            *lockedPlanarPlane, *target);
        if (surfaceGeometry && smartSurfaceLockedSeed_)
        {
            // Surface samples the existing voxel layer, while ray picking
            // must use the visible face plane. Keep those two coordinates
            // explicit so +X/+Y/+Z cannot accidentally extrude one layer.
            const Asset::Voxel::VoxelPosition& seed =
                smartSurfaceLockedSeed_->Position;
            if (normal.X != 0)
            {
                target->X = seed.X;
            }
            else if (normal.Y != 0)
            {
                target->Y = seed.Y;
            }
            else
            {
                target->Z = seed.Z;
            }
        }
    }
    state.Mode = action == SmartAction::Erase
        ? SmartBrushMode::Erase
        : action == SmartAction::Paint
        ? SmartBrushMode::Paint : SmartBrushMode::Add;
    SmartToolRequest request;
    request.Geometry = toolContext_.Smart.Geometry();
    request.FillMode = toolContext_.Smart.FillMode();
    request.GeometryHeight = smartGeometryHeight_;
    request.Mode = faceGeometry || fillGeometry ? std::nullopt
        : std::optional<SmartToolMode>{toolContext_.Smart.Mode()};
    request.Action = action;
    request.BrushRequest = {*dimensions, state, {*target, normal}, {}};
    request.ReadVoxel =
        [document, stroke, faceGeometry, lineGeometry, geometryTool,
            fillGeometry](const Asset::Voxel::VoxelPosition position)
        {
            // A Face depth plan is always source-relative. Its current depth
            // replaces the prior drag depth, so virtual stroke cells must not
            // turn former layers into permanent input geometry.
            if (!faceGeometry && !lineGeometry && !geometryTool &&
                !fillGeometry &&
                stroke != nullptr && stroke->IsActive())
                return stroke->ReadVoxel(position);
            const auto voxel = document->GetVoxel(position, 0U);
            return SmartToolVoxelState{
                voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
        };
    request.ReadFaceSupportVoxel =
        [document](const Asset::Voxel::VoxelPosition position)
        {
            const auto voxel = document->GetVoxel(position, 0U);
            return SmartToolVoxelState{
                voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
        };
    request.ReadSurfaceExtensionVoxel =
        [document, stroke](const Asset::Voxel::VoxelPosition position)
        {
            if (stroke == nullptr || !stroke->IsActive())
            {
                return SmartToolVoxelState{};
            }
            const auto sourceVoxel = document->GetVoxel(position, 0U);
            const SmartToolVoxelState strokeVoxel = stroke->ReadVoxel(position);
            return SmartToolVoxelState{!sourceVoxel.has_value() &&
                strokeVoxel.Exists, strokeVoxel.PaletteIndex};
        };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(document);
    request.SourceRevision = document->GetRevision();
    // Face depth plans use the source snapshot rather than the stroke's
    // virtual overlay, so a replacement depth must not invalidate the cache
    // merely because the pending transaction revision changed.
    request.VirtualRevision = !faceGeometry && !lineGeometry && !geometryTool &&
        !fillGeometry &&
        stroke != nullptr && stroke->IsActive()
        ? stroke->Revision() : 0U;
    request.SourceGeneration = voxelDocumentSession_.Generation();
    request.SourceSubModelIndex = 0U;
    request.ActiveProfileUuid = brushProfileService_.ActiveUuid();
    constexpr float colorScale = 1.0F / 255.0F;
    const auto& palette = document->GetPalette();
    for (std::size_t index = 0U; index < palette.size(); ++index)
    {
        const Asset::Voxel::VoxelColor& color = palette[index];
        request.PaletteColors[index] = {color.Red * colorScale,
            color.Green * colorScale, color.Blue * colorScale,
            color.Alpha * colorScale};
    }
    request.HasPaletteColors = true;
    request.PreviewAlpha = toolContext_.Smart.PreviewAlpha();
    request.Workplane = usesWorkplane
        ? std::optional<SmartBrushPlacement>{
            SmartBrushPlacement{*target, normal}}
        : std::nullopt;
    if (faceGeometry || surfaceGeometry)
    {
        const std::optional<SmartToolFaceSeed>& lockedSeed = faceGeometry
            ? faceDepthLockedSeed_
            : smartSurfaceLockedSeed_;
        if (lockedSeed)
            request.FaceSeed = *lockedSeed;
        else
        {
            if (!hit) return std::nullopt;
            request.FaceSeed = {{static_cast<std::int32_t>(hit->Coordinates.X),
                    static_cast<std::int32_t>(hit->Coordinates.Y),
                    static_cast<std::int32_t>(hit->Coordinates.Z)}, normal};
        }
        request.FaceDepth = faceGeometry && action == SmartAction::Add
            ? faceDepthLayers_
            : 1;
    }
    if (fillGeometry && request.FillMode == SmartFillMode::Plane)
    {
        request.FaceSeed = SmartToolFaceSeed{*target, normal};
    }
    if (lineGeometry && smartLineLockedStart_)
        request.LineStart = *smartLineLockedStart_;
    if (geometryTool && smartGeometryPlane_)
        request.GeometryPlane = *smartGeometryPlane_;
    if (geometryTool && !smartGeometryPlane_)
        request.GeometrySurfaceCoordinate = geometrySurfaceCoordinate;
    return request;
}

std::optional<PencilCompactRequest>
EditorWorkspace::BuildPencilCompactRequest()
{
    // Reuse the established Workspace adapter for the live document, palette,
    // source-only picking and workplane resolution.  Pencil V2 only changes
    // planning/commit ownership; it never writes or mutates Brush Profiles.
    const std::optional<SmartToolRequest> source = BuildSmartPencilRequest();
    if (!source || source->Geometry != SmartGeometry::Pencil) return std::nullopt;

    PencilCompactRequest request;
    request.Dimensions = source->BrushRequest.Dimensions;
    request.Brush = source->BrushRequest.State;
    request.Placement = source->BrushRequest.Placement;
    request.Action = source->Action;
    request.PaletteIndex = request.Brush.PaletteIndex;
    request.DocumentGeneration = source->SourceGeneration;
    request.DocumentRevision = source->SourceRevision;
    if (const BrushProfile* const profile = brushProfileService_.ActiveProfile())
    {
        request.ProfileIdentity = std::hash<std::string_view>{}(profile->Uuid);
        request.ProfileRevision = profile->Timestamp;
    }
    return request;
}

bool EditorWorkspace::BeginSmartToolStroke()
{
    const std::optional<SmartToolRequest> initialRequest = BuildSmartPencilRequest();
    Asset::Voxel::VoxelDocument* const document = voxelDocumentSession_.ActiveDocument();
    if (!initialRequest || document == nullptr) return false;
    const SmartAction action = initialRequest->Action;
    if (action != SmartAction::Add && action != SmartAction::Paint &&
        action != SmartAction::Erase)
        return false;
    if (initialRequest->Geometry == SmartGeometry::Face && initialRequest->FaceSeed)
    {
        faceDepthLockedSeed_ = *initialRequest->FaceSeed;
        faceDepthLayers_ = 1;
        faceDepthPlannedLayers_ = 0;
        const Asset::Voxel::VoxelPosition seed = faceDepthLockedSeed_->Position;
        const Asset::Voxel::VoxelPosition normal = faceDepthLockedSeed_->Normal;
        const Vec3 surfaceWorld = VoxelGridToViewport({
            static_cast<float>(seed.X) + 0.5F,
            static_cast<float>(seed.Y) + 0.5F,
            static_cast<float>(seed.Z) + 0.5F}, voxelModelCenter_);
        faceDepthDragAxis_ = MakeSmartToolFaceDepthDragAxis(surfaceWorld,
            {static_cast<float>(normal.X), static_cast<float>(normal.Y),
                static_cast<float>(normal.Z)}, currentViewportRectangle_,
            viewportCamera_.GetViewProjection());
    }
    if (initialRequest->Geometry == SmartGeometry::Line)
    {
        smartLineLockedStart_ = initialRequest->BrushRequest.Placement.Target;
        smartLinePlannedEnd_.reset();
        smartLineEndpointValid_ = false;
        smartToolLineConstraintResolver_.Begin(*smartLineLockedStart_);
        toolContext_.Smart.SetLineConstraintAxis(std::nullopt);
    }
    if (initialRequest->Geometry == SmartGeometry::Geometry)
    {
        if (!initialRequest->GeometrySurfaceCoordinate) return false;
        const auto plane = SmartToolPlanner::MakeGeometryPlane(
            initialRequest->BrushRequest.Placement.Target,
            initialRequest->BrushRequest.Placement.Normal,
            *initialRequest->GeometrySurfaceCoordinate);
        if (!plane) return false;
        smartGeometryPlane_ = *plane;
        smartGeometryPlannedEnd_.reset();
        smartGeometryEndpointValid_ = false;
        smartGeometryPhase_ = SmartGeometryInteractionPhase::Base;
        smartGeometryLockedMode_ =
            initialRequest->Mode.value_or(SmartToolMode::SingleVoxel);
        smartGeometryLockedAction_ = initialRequest->Action;
        smartGeometryHeight_ = 1;
        smartGeometryPlannedHeight_ = 0;
        const Asset::Voxel::VoxelPosition origin = plane->Origin;
        const Asset::Voxel::VoxelPosition normal = plane->Normal;
        const Vec3 surfaceWorld = VoxelGridToViewport({
            static_cast<float>(origin.X) + 0.5F,
            static_cast<float>(origin.Y) + 0.5F,
            static_cast<float>(origin.Z) + 0.5F}, voxelModelCenter_);
        smartGeometryHeightDragAxis_ = MakeSmartToolFaceDepthDragAxis(
            surfaceWorld, {static_cast<float>(normal.X),
                static_cast<float>(normal.Y), static_cast<float>(normal.Z)},
            currentViewportRectangle_, viewportCamera_.GetViewProjection());
    }
    if (initialRequest->Geometry == SmartGeometry::Surface)
    {
        if (!initialRequest->FaceSeed)
        {
            return false;
        }
        smartSurfaceLockedSeed_ = *initialRequest->FaceSeed;
        const Asset::Voxel::VoxelPosition surface =
            smartSurfaceLockedSeed_->Position;
        const Asset::Voxel::VoxelPosition normal = smartSurfaceLockedSeed_->Normal;
        const std::int32_t layerCoordinate = normal.X != 0
            ? surface.X
            : normal.Y != 0
            ? surface.Y
            : surface.Z;
        const float coordinate = static_cast<float>(layerCoordinate +
            ((normal.X > 0 || normal.Y > 0 || normal.Z > 0) ? 1 : 0));
        const auto plane = SmartToolPlanner::MakeGeometryPlane(
            surface, normal, coordinate);
        if (!plane)
        {
            return false;
        }
        smartSurfacePlane_ = *plane;
        smartSurfaceEndpointValid_ = false;
    }
    if (!smartToolStroke_.Begin({reinterpret_cast<std::uintptr_t>(document),
            document->GetRevision(), voxelDocumentSession_.Generation(), 0U,
            [document](const Asset::Voxel::VoxelPosition position)
            {
                const auto voxel = document->GetVoxel(position, 0U);
                return SmartToolVoxelState{
                    voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
            }}, action, initialRequest->BrushRequest.Placement.Target,
            initialRequest->BrushRequest.Placement.Normal,
            initialRequest->Geometry == SmartGeometry::Pencil
                ? SmartToolStrokeSurfacePolicy::LockPencilSurface
                : SmartToolStrokeSurfacePolicy::Unlocked))
        return false;
    const std::optional<SmartToolRequest> request =
        BuildSmartPencilRequest(&smartToolStroke_);
    if (!request)
    {
        CancelSmartToolStroke();
        return false;
    }
    const SmartToolResult result = smartToolController_.ResolvePreview(
        smartToolSession_, *request);
    if (!result.HasPlan() || result.Code == SmartBrushResultCode::OutOfBounds ||
        !((request->Geometry == SmartGeometry::Face ||
            request->Geometry == SmartGeometry::Line ||
            request->Geometry == SmartGeometry::Geometry)
            ? smartToolStroke_.ReplaceWithPlan(*result.Plan)
            : smartToolStroke_.Accumulate(*result.Plan)))
    {
        CancelSmartToolStroke();
        return false;
    }
    smartToolStrokePreviewPlan_ = result.Plan;
    if (request->Geometry == SmartGeometry::Face)
        faceDepthPlannedLayers_ = request->FaceDepth;
    if (request->Geometry == SmartGeometry::Line)
    {
        smartLinePlannedEnd_ = request->BrushRequest.Placement.Target;
        smartLineEndpointValid_ = true;
    }
    if (request->Geometry == SmartGeometry::Geometry)
    {
        smartGeometryPlannedEnd_ = request->BrushRequest.Placement.Target;
        smartGeometryEndpointValid_ = true;
        smartGeometryPlannedHeight_ = request->GeometryHeight;
    }
    if (request->Geometry == SmartGeometry::Surface)
    {
        smartSurfaceEndpointValid_ = true;
    }
    static_cast<void>(RefreshSmartToolHover());
    return true;
}

bool EditorWorkspace::ContinueSmartToolStroke()
{
    if (!smartToolStroke_.IsActive()) return false;
    const EditorFrameProbeScope strokeProbe(
        frameProbe_, EditorFrameProbeSlot::Stroke);
    Asset::Voxel::VoxelDocument* const document = voxelDocumentSession_.ActiveDocument();
    const SmartToolStrokeContext& context = smartToolStroke_.Context();
    if (document == nullptr || reinterpret_cast<std::uintptr_t>(document) !=
            context.DocumentIdentity || document->GetRevision() !=
            context.DocumentRevision || voxelDocumentSession_.Generation() !=
            context.DocumentGeneration)
    {
        CancelSmartToolStroke();
        return false;
    }
    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
    if (!smartToolStroke_.IsSuspended() && mouseDelta.x == 0.0F &&
        mouseDelta.y == 0.0F)
    {
        // A held-but-motionless click must not create another stroke sample.
        // A real (even one-pixel) pointer movement expresses the next sample.
        return false;
    }
    std::optional<SmartToolRequest> current =
        BuildSmartPencilRequest(&smartToolStroke_);
    if (!current)
    {
        if (smartLineLockedStart_ || smartGeometryPlane_ || smartSurfacePlane_)
        {
            // Preserve A so the user can return to a valid B, but make the
            // last line plan inapplicable. MouseUp must not commit stale
            // changes while the endpoint is invalid.
            smartLineEndpointValid_ = false;
            smartLinePlannedEnd_.reset();
            smartToolStrokePreviewPlan_.reset();
            smartToolStrokePreviewMesh_ = {};
            smartToolStrokePreviewPlanId_ = 0U;
            smartToolStrokePreviewPlanRevision_ = 0U;
            smartToolStrokePreviewStrokeRevision_ = 0U;
            smartGeometryEndpointValid_ = false;
            smartGeometryPlannedEnd_.reset();
            smartSurfaceEndpointValid_ = false;
        }
        smartToolStroke_.Suspend();
        return false;
    }
    if (current->Action != smartToolStroke_.Action())
    {
        CancelSmartToolStroke();
        return false;
    }
    if (current->Geometry == SmartGeometry::Face)
    {
        if (current->FaceDepth == faceDepthPlannedLayers_)
            return false;
        SmartToolRequest request = *current;
        request.VirtualRevision = 0U;
        const SmartToolResult result = smartToolController_.ResolvePreview(
            smartToolSession_, request);
        if (!result.HasPlan() || result.Code == SmartBrushResultCode::OutOfBounds)
        {
            smartToolStroke_.Suspend();
            return false;
        }
        if (!smartToolStroke_.ReplaceWithPlan(*result.Plan))
        {
            CancelSmartToolStroke();
            return false;
        }
        smartToolStrokePreviewPlan_ = result.Plan;
        faceDepthPlannedLayers_ = request.FaceDepth;
        return true;
    }
    if (current->Geometry == SmartGeometry::Line)
    {
        if (smartLinePlannedEnd_ &&
            *smartLinePlannedEnd_ == current->BrushRequest.Placement.Target)
            return false;
        SmartToolRequest request = *current;
        request.VirtualRevision = 0U;
        const SmartToolResult result = smartToolController_.ResolvePreview(
            smartToolSession_, request);
        if (!result.HasPlan() || result.Code == SmartBrushResultCode::OutOfBounds)
        {
            // The endpoint is not applicable even though it was resolved
            // from a request (for example, the complete line is outside the
            // document). Do not leave a prior valid B commit-ready.
            smartLineEndpointValid_ = false;
            smartLinePlannedEnd_.reset();
            smartToolStrokePreviewPlan_.reset();
            smartToolStrokePreviewMesh_ = {};
            smartToolStrokePreviewPlanId_ = 0U;
            smartToolStrokePreviewPlanRevision_ = 0U;
            smartToolStrokePreviewStrokeRevision_ = 0U;
            smartToolStroke_.Suspend();
            return false;
        }
        if (!smartToolStroke_.ReplaceWithPlan(*result.Plan))
        {
            CancelSmartToolStroke();
            return false;
        }
        smartToolStrokePreviewPlan_ = result.Plan;
        smartLinePlannedEnd_ = request.BrushRequest.Placement.Target;
        smartLineEndpointValid_ = true;
        return true;
    }
    if (current->Geometry == SmartGeometry::Geometry)
    {
        if (smartGeometryPhase_ == SmartGeometryInteractionPhase::Height)
        {
            current->BrushRequest.Placement.Target =
                smartGeometryPlannedEnd_.value_or(
                    current->BrushRequest.Placement.Target);
        }
        if (smartGeometryPlannedEnd_ &&
            *smartGeometryPlannedEnd_ == current->BrushRequest.Placement.Target &&
            smartGeometryPlannedHeight_ == current->GeometryHeight)
            return false;
        SmartToolRequest request = *current;
        request.VirtualRevision = 0U;
        const SmartToolResult result = smartToolController_.ResolvePreview(
            smartToolSession_, request);
        if (!result.HasPlan() || result.Code == SmartBrushResultCode::OutOfBounds)
        {
            smartGeometryEndpointValid_ = false;
            if (smartGeometryPhase_ == SmartGeometryInteractionPhase::Base)
                smartGeometryPlannedEnd_.reset();
            smartToolStrokePreviewPlan_.reset();
            smartToolStrokePreviewMesh_ = {};
            smartToolStrokePreviewPlanId_ = 0U;
            smartToolStrokePreviewPlanRevision_ = 0U;
            smartToolStrokePreviewStrokeRevision_ = 0U;
            smartToolStroke_.Suspend();
            return false;
        }
        if (!smartToolStroke_.ReplaceWithPlan(*result.Plan))
        {
            CancelSmartToolStroke();
            return false;
        }
        smartToolStrokePreviewPlan_ = result.Plan;
        smartGeometryPlannedEnd_ = request.BrushRequest.Placement.Target;
        smartGeometryEndpointValid_ = true;
        smartGeometryPlannedHeight_ = request.GeometryHeight;
        return true;
    }
    const std::vector<Asset::Voxel::VoxelPosition> samples = smartToolStroke_.Advance(
        current->BrushRequest.Placement.Target, current->BrushRequest.Placement.Normal);
    if (samples.empty()) return false;
    bool changed = false;
    for (const Asset::Voxel::VoxelPosition sample : samples)
    {
        SmartToolRequest request = *current;
        request.BrushRequest.Placement.Target = sample;
        if (request.Workplane) request.Workplane->Target = sample;
        request.VirtualRevision = smartToolStroke_.Revision();
        const SmartToolResult result = smartToolController_.ResolvePreview(
            smartToolSession_, request);
        if (!result.HasPlan() || result.Code == SmartBrushResultCode::OutOfBounds)
        {
            smartToolStroke_.Suspend();
            return false;
        }
        if (!smartToolStroke_.Accumulate(*result.Plan))
        {
            CancelSmartToolStroke();
            return false;
        }
        smartToolStrokePreviewPlan_ = result.Plan;
        changed = true;
    }
    if (changed && current->Geometry == SmartGeometry::Surface)
    {
        smartSurfaceEndpointValid_ = true;
    }
    if (changed) static_cast<void>(RefreshSmartToolHover());
    return changed;
}

bool EditorWorkspace::CommitSmartToolStroke()
{
    if (!smartToolStroke_.IsActive()) return false;
    if ((smartLineLockedStart_ && !smartLineEndpointValid_) ||
        (smartGeometryPlane_ && !smartGeometryEndpointValid_) ||
        (smartSurfacePlane_ && !smartSurfaceEndpointValid_))
    {
        CancelSmartToolStroke();
        return false;
    }
    Asset::Voxel::VoxelDocument* const document = voxelDocumentSession_.ActiveDocument();
    const SmartToolStrokeContext& context = smartToolStroke_.Context();
    if (document == nullptr || reinterpret_cast<std::uintptr_t>(document) !=
            context.DocumentIdentity || document->GetRevision() !=
            context.DocumentRevision || voxelDocumentSession_.Generation() !=
            context.DocumentGeneration)
    {
        CancelSmartToolStroke();
        return false;
    }
    const std::vector<VoxelChange> changes = smartToolStroke_.Changes();
    const SmartAction action = smartToolStroke_.Action();
    const std::uint64_t voxelCountBefore = document->GetVoxelCount();
    const Asset::Voxel::VoxelPosition target = smartToolStrokePreviewPlan_
        ? smartToolStrokePreviewPlan_->Placement().Target
        : Asset::Voxel::VoxelPosition{};
    voxelEditInProgress_ = true;
    VoxelToolResult result = VoxelPencilTool::ApplyChanges({
        static_cast<VoxelEditSession*>(this), document, 0U,
        voxelDocumentSession_.Generation(), &voxelEditHistory_, std::nullopt,
        nullptr, nullptr}, action, target, changes);
    voxelEditInProgress_ = false;
    viewportFocusRequested_ = false;
    viewportFocusApplied_ = false;
    lastVoxelToolResult_ = result;
    const bool committed = result.Code == VoxelToolResultCode::Applied;
    if (committed)
    {
        toolContext_.Smart.SetStatistics(changes.size(), changes.size(), 0U, 0U);
        workplaneHit_.reset();
        if (action == SmartAction::Add && voxelCountBefore == 0U)
            firstCreationExperience_.OnFirstVoxelCreated();
        if (action == SmartAction::Add || action == SmartAction::Paint)
            static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage(std::string("[Edit] ") +
            (action == SmartAction::Add ? "Added" :
             action == SmartAction::Paint ? "Painted" : "Erased") +
            " Smart Tool stroke (" + std::to_string(changes.size()) +
            " voxel change(s)).");
    }
    else if (result.Code == VoxelToolResultCode::Failed && !result.Error.empty())
    {
        AddConsoleMessage("[Edit] Smart Tool stroke failed: " + result.Error);
    }
    CancelSmartToolStroke();
    if (committed)
    {
        static_cast<void>(RefreshSmartToolHover());
        UpdateVoxelHighlights();
    }
    return committed;
}

void EditorWorkspace::CancelSmartToolStroke() noexcept
{
    smartToolStroke_.Cancel();
    smartToolStrokePreviewPlan_.reset();
    smartToolStrokePreviewMesh_ = {};
    smartToolStrokePreviewPlanId_ = 0U;
    smartToolStrokePreviewPlanRevision_ = 0U;
    smartToolStrokePreviewStrokeRevision_ = 0U;
    faceDepthLockedSeed_.reset();
    faceDepthDragAxis_.reset();
    faceDepthLayers_ = 1;
    faceDepthPlannedLayers_ = 0;
    smartLineLockedStart_.reset();
    smartLinePlannedEnd_.reset();
    smartLineEndpointValid_ = false;
    smartGeometryPlane_.reset();
    smartGeometryPlannedEnd_.reset();
    smartGeometryEndpointValid_ = false;
    smartGeometryPhase_ = SmartGeometryInteractionPhase::Base;
    smartGeometryLockedMode_ = SmartToolMode::SingleVoxel;
    smartGeometryHeightDragAxis_.reset();
    smartGeometryHeightStartMouse_ = {};
    smartGeometryHeight_ = 1;
    smartGeometryPlannedHeight_ = 0;
    smartSurfaceLockedSeed_.reset();
    smartSurfacePlane_.reset();
    smartSurfaceEndpointValid_ = false;
    smartToolLineConstraintResolver_.Reset();
    toolContext_.Smart.SetLineConstraintAxis(std::nullopt);
}

bool EditorWorkspace::ApplySmartFill()
{
    if (voxelEditInProgress_ || smartToolStroke_.IsActive()) return false;
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr ||
        toolContext_.Smart.Geometry() != SmartGeometry::Fill)
        return false;
    const SmartToolPlanPtr plan =
        smartToolController_.ResolveCommit(smartToolSession_).Plan;
    if (plan == nullptr || plan->Geometry() != SmartGeometry::Fill ||
        plan->FillMode() != toolContext_.Smart.FillMode() ||
        plan->Action() != toolContext_.Smart.Action())
        return false;
    const SmartToolRequestKey& key = plan->CacheKey();
    if (key.SourceIdentity != reinterpret_cast<std::uintptr_t>(document) ||
        key.SourceRevision != document->GetRevision() ||
        key.SourceGeneration != voxelDocumentSession_.Generation())
        return false;
    if (!smartToolStroke_.Begin({
            reinterpret_cast<std::uintptr_t>(document),
            document->GetRevision(), voxelDocumentSession_.Generation(), 0U,
            [document](const Asset::Voxel::VoxelPosition position)
            {
                const auto voxel = document->GetVoxel(position, 0U);
                return SmartToolVoxelState{
                    voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
            }}, plan->Action(), plan->Placement().Target,
            plan->Placement().Normal,
            SmartToolStrokeSurfacePolicy::Unlocked))
        return false;
    if (!smartToolStroke_.ReplaceWithPlan(*plan))
    {
        CancelSmartToolStroke();
        return false;
    }
    smartToolStrokePreviewPlan_ = plan;
    return CommitSmartToolStroke();
}

bool EditorWorkspace::ApplyVoxelPencil()
{
    if (voxelEditInProgress_) return false;
    const std::uint64_t voxelCountBefore =
        voxelDocumentSession_.ActiveDocument()
        ? voxelDocumentSession_.ActiveDocument()->GetVoxelCount() : 0U;
    voxelEditInProgress_ = true;
    VoxelToolResult result;
    SmartAction committedAction = toolContext_.Smart.Action();
    SmartToolPlanStatistics planStatistics{};
    const std::optional<PaletteColorSelection> activeColor =
        paletteService_.ActiveColor();
    try
    {
        // Input/highlight processing produced this exact plan before the click.
        // Commit never builds a second request or calls the planner.
        SmartToolPlanPtr plan = smartToolController_.ResolveCommit(
            smartToolSession_).Plan;
        if (plan != nullptr)
        {
            committedAction = plan->Action();
            planStatistics = plan->Statistics();
        }
        VoxelPencilContext pencilContext;
        pencilContext.Plan = std::move(plan);
        pencilContext.Execution = {
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(), 0U,
            voxelDocumentSession_.Generation(), &voxelEditHistory_,
            activeColor ? std::optional<std::size_t>{activeColor->Index}
                        : std::nullopt,
            nullptr, nullptr};
        result = VoxelPencilTool::Apply(pencilContext);
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
    toolContext_.Smart.SetStatistics(planStatistics.Total,
        planStatistics.Changed, planStatistics.Unchanged,
        planStatistics.Clipped);

    if (result.Code == VoxelToolResultCode::Applied)
    {
        if (committedAction == SmartAction::Add ||
            committedAction == SmartAction::Paint)
            static_cast<void>(paletteService_.RecordActiveColorUsage());
        workplaneHit_.reset();
        if (committedAction == SmartAction::Add && voxelCountBefore == 0U)
            firstCreationExperience_.OnFirstVoxelCreated();
        if (committedAction == SmartAction::Paint)
        {
            AddConsoleMessage("[Edit] Painted " +
                std::to_string(planStatistics.Changed) + " voxel(s).");
        }
        else
        {
            AddConsoleMessage(
                std::string(committedAction == SmartAction::Erase
                    ? "[Edit] Erased voxel at (" : "[Edit] Added voxel at (") +
                std::to_string(result.Position.X) + ", " +
                std::to_string(result.Position.Y) + ", " +
                std::to_string(result.Position.Z) +
                (committedAction == SmartAction::Erase ? ")." :
                    ") using palette index " +
                    std::to_string(activeColor ? activeColor->Index : 0U) + "."));
        }
        static_cast<void>(RefreshSmartToolHover());
        UpdateVoxelHighlights();
        return true;
    }
    if (result.Code == VoxelToolResultCode::Failed)
    {
        const char* actionName = committedAction == SmartAction::Erase
            ? "erase" : committedAction == SmartAction::Paint
            ? "paint" : "add";
        AddConsoleMessage(
            std::string("[Edit] Failed to ") + actionName + " voxel at (" +
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

bool EditorWorkspace::ApplyVoxelPaintBrush()
{
    if (voxelEditInProgress_) return false;
    voxelEditInProgress_ = true;
    VoxelPaintBrushResult result;
    const std::optional<PaletteColorSelection> activeColor =
        paletteService_.ActiveColor();
    try
    {
        toolContext_.Smart.Brush().PaletteIndex =
            activeColor ? activeColor->Index : 0U;
        SmartBrushState brushState = toolContext_.Smart.Brush();
        brushState.Shape = ResolveSmartBrushShape(
            toolContext_.Smart.Geometry(), brushState.Shape);
        brushState.Mode = SmartBrushMode::Paint;
        result = VoxelPaintBrushTool::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            brushState,
            !(voxelToolState_.IsFillActive() ||
              (voxelToolState_.IsPencilActive() &&
               toolContext_.Smart.IsOperational() &&
               toolContext_.Smart.Action() == SmartAction::Paint)),
            &voxelEditHistory_,
            paintPreviewEvaluation_.Plan});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelPaintBrushResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelPaintBrushResultCode::Failed;
        result.Error = "Unknown Paint Brush failure.";
    }
    voxelEditInProgress_ = false;
    lastVoxelPaintBrushResult_ = result;
    toolContext_.Smart.SetStatistics(result.Statistics.Total,
        result.Statistics.Painted, result.Statistics.Ignored,
        result.Statistics.Clipped);

    if (result.Code == VoxelPaintBrushResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage("[Edit] Painted " +
            std::to_string(result.Statistics.Painted) + " voxel(s).");
        return true;
    }
    if (result.Code == VoxelPaintBrushResultCode::Failed)
        AddConsoleMessage("[Edit] Paint Brush failed: " + result.Error);
    return false;
}

bool EditorWorkspace::ApplyVoxelFill()
{
    if (voxelEditInProgress_) return false;
    voxelEditInProgress_ = true;
    VoxelFillResult result;
    const std::optional<PaletteColorSelection> activeColor =
        paletteService_.ActiveColor();
    try
    {
        result = VoxelFillService::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            activeColor ? activeColor->Index : 0U,
            !voxelToolState_.IsFillActive(),
            &voxelEditHistory_});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelFillResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelFillResultCode::Failed;
        result.Error = "Unknown Fill failure.";
    }
    voxelEditInProgress_ = false;
    lastVoxelFillResult_ = result;

    if (result.Code == VoxelFillResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage(
            "[Edit] Filled " + std::to_string(result.ChangedVoxelCount) +
            " voxel(s) from palette index " +
            std::to_string(result.PreviousPaletteIndex) + " to " +
            std::to_string(result.NewPaletteIndex) + ".");
        return true;
    }
    if (result.Code == VoxelFillResultCode::Failed)
        AddConsoleMessage("[Edit] Fill failed: " + result.Error);
    return false;
}

std::optional<Asset::Voxel::VoxelPosition>
EditorWorkspace::CurrentTwoPointToolTarget() const noexcept
{
    if (const auto& hit = voxelSelection_.Hovered();
        hit && hit->AdjacentWithinBounds)
    {
        return hit->AdjacentPosition;
    }
    if (workplaneHit_ && workplaneHit_->IsValid())
        return workplaneHit_->Position;
    return std::nullopt;
}

std::optional<Asset::Voxel::VoxelPosition>
EditorWorkspace::CurrentSelectionTarget() const noexcept
{
    if (const auto& hit = voxelSelection_.Hovered())
        return Asset::Voxel::VoxelPosition{
            static_cast<std::int32_t>(hit->Coordinates.X),
            static_cast<std::int32_t>(hit->Coordinates.Y),
            static_cast<std::int32_t>(hit->Coordinates.Z)};
    if (workplaneHit_ && workplaneHit_->IsValid())
        return workplaneHit_->Position;
    return std::nullopt;
}

bool EditorWorkspace::ApplySelectionBounds(
    const SelectionBounds bounds, const SelectionMode mode)
{
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const Asset::Voxel::VoxelSubModel* model =
        document ? document->GetModel(0U) : nullptr;
    if (!model || !bounds.Valid) return false;
    const SelectionBounds clamped = bounds.ClampedTo(model->Dimensions());
    if (!clamped.Valid) return false;
    const std::uint64_t generation = voxelDocumentSession_.Generation();
    const std::uint64_t revision = document->GetRevision();
    if (!selectionVolumeCache_.SourceCurrent(generation, revision))
    {
        std::vector<Asset::Voxel::VoxelPosition> existing;
        existing.reserve(model->VoxelCount());
        model->ForEachVoxel(
            [&existing](const Asset::Voxel::VoxelPosition position,
                        const Asset::Voxel::Voxel&)
            {
                existing.push_back(position);
            });
        selectionVolumeCache_.UpdateSource(
            std::move(existing), generation, revision);
    }
    const SelectionVolumeEvaluation evaluation =
        selectionVolumeCache_.Evaluate(clamped);
    if (!evaluation.Recalculated) return false;
    const bool changed = selectionService_.ApplySortedVolume(
        evaluation.Voxels, clamped, mode);
    UpdateVoxelHighlights();
    return changed;
}

void EditorWorkspace::CancelSelectionInteraction()
{
    if (!selectionInteraction_.IsActive()) return;
    const SelectionInteractionMode mode = selectionInteraction_.Mode();
    const std::optional<SelectionBounds> restore =
        selectionInteraction_.Cancel();
    if (mode == SelectionInteractionMode::MovingContent ||
        mode == SelectionInteractionMode::DuplicatingContent)
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelMoveStatusMessage_.clear();
        voxelDuplicateStatusMessage_.clear();
    }
    if ((mode == SelectionInteractionMode::ResizingFace ||
         mode == SelectionInteractionMode::MovingBox) && restore)
        static_cast<void>(ApplySelectionBounds(
            *restore, SelectionMode::Replace));
    else
        UpdateVoxelHighlights();
    voxelSelectionClickCandidate_ = false;
    selectionPointerAnchor_.reset();
}

void EditorWorkspace::CancelVoxelBox() noexcept
{
    if (!voxelBoxInteraction_.IsActive()) return;
    voxelBoxInteraction_.Cancel();
    UpdateVoxelHighlights();
}

void EditorWorkspace::CancelVoxelLine() noexcept
{
    if (!voxelLineInteraction_.IsActive()) return;
    voxelLineInteraction_.Cancel();
    UpdateVoxelHighlights();
}

void EditorWorkspace::CancelVoxelSphere() noexcept
{
    if (!voxelSphereInteraction_.IsActive()) return;
    voxelSphereInteraction_.Cancel();
    UpdateVoxelHighlights();
}

bool EditorWorkspace::ApplyVoxelBox()
{
    if (voxelEditInProgress_ || !voxelBoxInteraction_.CornerA() ||
        !voxelBoxInteraction_.CornerB() ||
        voxelBoxInteraction_.DocumentGeneration() !=
            voxelDocumentSession_.Generation())
    {
        CancelVoxelBox();
        return false;
    }
    voxelEditInProgress_ = true;
    VoxelBoxResult result;
    const auto activeColor = paletteService_.ActiveColor();
    try
    {
        result = VoxelBoxService::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(), 0U,
            *voxelBoxInteraction_.CornerA(), *voxelBoxInteraction_.CornerB(),
            activeColor ? activeColor->Index : 0U,
            !voxelToolState_.IsBoxActive(), &voxelEditHistory_});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelBoxResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelBoxResultCode::Failed;
        result.Error = "Unknown Box failure.";
    }
    voxelEditInProgress_ = false;
    voxelBoxInteraction_.Cancel();
    lastVoxelBoxResult_ = result;
    UpdateVoxelHighlights();
    if (result.Code == VoxelBoxResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage(
            "[Edit] Created solid box with " +
            std::to_string(result.ChangedVoxelCount) + " voxel(s)." );
        return true;
    }
    if (result.Code == VoxelBoxResultCode::Failed)
        AddConsoleMessage("[Edit] Box failed: " + result.Error);
    return false;
}

bool EditorWorkspace::ApplyVoxelLine()
{
    if (voxelEditInProgress_ || !voxelLineInteraction_.PointA() ||
        !voxelLineInteraction_.PointB() ||
        voxelLineInteraction_.DocumentGeneration() !=
            voxelDocumentSession_.Generation())
    {
        CancelVoxelLine();
        return false;
    }
    voxelEditInProgress_ = true;
    VoxelLineResult result;
    const auto activeColor = paletteService_.ActiveColor();
    try
    {
        result = VoxelLineService::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(), 0U,
            *voxelLineInteraction_.PointA(), *voxelLineInteraction_.PointB(),
            activeColor ? activeColor->Index : 0U,
            !voxelToolState_.IsLineActive(), &voxelEditHistory_});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelLineResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelLineResultCode::Failed;
        result.Error = "Unknown Line failure.";
    }
    voxelEditInProgress_ = false;
    voxelLineInteraction_.Cancel();
    lastVoxelLineResult_ = result;
    UpdateVoxelHighlights();
    if (result.Code == VoxelLineResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage(
            "[Edit] Created line with " +
            std::to_string(result.ChangedVoxelCount) + " voxel(s)." );
        return true;
    }
    if (result.Code == VoxelLineResultCode::Failed)
        AddConsoleMessage("[Edit] Line failed: " + result.Error);
    return false;
}

bool EditorWorkspace::ApplyVoxelSphere()
{
    if (voxelEditInProgress_ || !voxelSphereInteraction_.Center() ||
        !voxelSphereInteraction_.RadiusPoint() ||
        voxelSphereInteraction_.DocumentGeneration() !=
            voxelDocumentSession_.Generation())
    {
        CancelVoxelSphere();
        return false;
    }
    voxelEditInProgress_ = true;
    VoxelSphereResult result;
    const auto activeColor = paletteService_.ActiveColor();
    try
    {
        result = VoxelSphereService::Apply({
            static_cast<VoxelEditSession*>(this),
            voxelDocumentSession_.ActiveDocument(), 0U,
            *voxelSphereInteraction_.Center(),
            *voxelSphereInteraction_.RadiusPoint(),
            activeColor ? activeColor->Index : 0U,
            !voxelToolState_.IsSphereActive(), &voxelEditHistory_});
    }
    catch (const std::exception& exception)
    {
        result.Code = VoxelSphereResultCode::Failed;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = VoxelSphereResultCode::Failed;
        result.Error = "Unknown Sphere failure.";
    }
    voxelEditInProgress_ = false;
    voxelSphereInteraction_.Cancel();
    lastVoxelSphereResult_ = result;
    UpdateVoxelHighlights();
    if (result.Code == VoxelSphereResultCode::Applied)
    {
        static_cast<void>(paletteService_.RecordActiveColorUsage());
        AddConsoleMessage(
            "[Edit] Created solid sphere with " +
            std::to_string(result.ChangedVoxelCount) + " voxel(s)." );
        return true;
    }
    if (result.Code == VoxelSphereResultCode::Failed)
        AddConsoleMessage("[Edit] Sphere failed: " + result.Error);
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
        [this, document, identity]
        {
            const EditorFrameProbeScope meshProbe(
                frameProbe_, EditorFrameProbeSlot::MeshSynchronize);
            return voxelDocumentMeshCache_.Synchronize(*document, identity);
        }();
    if (!synchronized.Succeeded || !voxelDocumentMeshCache_.HasMesh())
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
    // VF-0262 (lot 262-4): patch only the chunks the cache rebuilt when the
    // inputs baked into the vertices (palette colours, model center) and the
    // document identity are unchanged; anything else refreshes the whole
    // chunk set. Palette-only revisions arrive as Unchanged with no touched
    // chunks and land in the full-refresh branch, re-baking the colours.
    const bool canPatch =
        synchronized.Rebuilt() &&
        !voxelDocumentMeshCache_.LastSyncWasFullRebuild() &&
        uploadedDocumentIdentity_ == identity &&
        uploadedDocumentModelCenter_.X == modelCenter.X &&
        uploadedDocumentModelCenter_.Y == modelCenter.Y &&
        uploadedDocumentModelCenter_.Z == modelCenter.Z &&
        uploadedDocumentPalette_.Data() == palette.Data();
    const bool uploaded =
        [this, &palette, modelCenter, canPatch]
        {
            const EditorFrameProbeScope uploadProbe(
                frameProbe_, EditorFrameProbeSlot::GpuUpload);
            const auto& chunks = voxelDocumentMeshCache_.Chunks();
            std::vector<ViewportRenderer::ModelChunkUpdate> updates;
            if (canPatch)
            {
                const auto& touched =
                    voxelDocumentMeshCache_.LastSyncTouchedChunks();
                updates.reserve(touched.size());
                for (const auto& key : touched)
                {
                    const auto found = chunks.find(key);
                    updates.push_back({
                        {key.X, key.Y, key.Z},
                        found != chunks.end() ? &found->second : nullptr});
                }
            }
            else
            {
                updates.reserve(chunks.size());
                for (const auto& entry : chunks)
                {
                    updates.push_back({
                        {entry.first.X, entry.first.Y, entry.first.Z},
                        &entry.second});
                }
            }
            return viewportRenderer_.UploadModelChunks(
                updates, palette, modelCenter, !canPatch);
        }();
    if (!uploaded)
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
    // VF-0262 (lot 262-4c): statistics come from the chunk totals — the
    // per-edit path never assembles the single mesh anymore.
    const std::size_t meshVertexCount =
        voxelDocumentMeshCache_.TotalVertexCount();
    const std::size_t meshTriangleCount =
        voxelDocumentMeshCache_.TotalTriangleCount();
    if (viewportState_.HasModel())
    {
        if (!viewportState_.UpdateDocumentStatistics(
                *document, meshVertexCount, meshTriangleCount))
            return false;
    }
    else if (!viewportState_.ReplaceDocument(
                 document->SourcePath().filename().string(),
                 *document,
                 meshVertexCount,
                 meshTriangleCount))
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
    uploadedDocumentModelCenter_ = modelCenter;
    uploadedDocumentPalette_ = palette;
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
        // PERF-02c: while a stroke or pointer gesture streams edits, the
        // rebuild nested in every transaction is redundant with the
        // once-per-frame synchronization in Draw(), which rebuilds on any
        // revision change. Deferring it caps mesh rebuilds at one per frame
        // during drawing (measured 90 ms x2 per frame on large models).
        // Trade-off, accepted in VF-0261: mid-stroke steps give up the
        // rollback-on-rebuild-failure guard; a rebuild failure surfaces at
        // the frame synchronization instead. Single-click edits, undo and
        // redo keep the synchronous rebuild and its transactional guard.
        if (smartToolStroke_.IsActive() ||
            viewportInteractionV2_.OwnsPointer() ||
            pencilViewportInteractionV2_.OwnsPointer())
            return CommandResult::Success();
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
    static_cast<void>(selectionService_.Clear());
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


void EditorWorkspace::DrawUniversalPreviewCursor2D() const noexcept
{
    if (!universalCursor2DTarget_) return;
    const std::optional<PaletteColorSelection> activeColor =
        paletteService_.ActiveColor();
    if (!activeColor) return;
    const UniversalCursor2DGeometry cursor = ProjectUniversalCursor2D(
        *universalCursor2DTarget_, currentViewportRectangle_,
        viewportCamera_.GetViewProjection());
    if (!cursor.Visible) return;

    std::array<ImVec2, 4U> points{};
    for (std::size_t index = 0U; index < points.size(); ++index)
        points[index] = {cursor.Corners[index].X, cursor.Corners[index].Y};
    const Asset::Voxel::VoxelColor& color = activeColor->Color;
    const ImU32 fillColor =
        IM_COL32(color.Red, color.Green, color.Blue, 72);
    const ImU32 darkKeyline = IM_COL32(8, 12, 18, 220);
    const ImU32 lightKeyline = IM_COL32(245, 248, 255, 235);
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    // Keep the overlay inside the rendered image, away from Scene chrome.
    drawList->PushClipRect(
        {currentViewportRectangle_.X, currentViewportRectangle_.Y},
        {currentViewportRectangle_.X + currentViewportRectangle_.Width,
         currentViewportRectangle_.Y + currentViewportRectangle_.Height},
        true);
    drawList->AddConvexPolyFilled(
        points.data(), static_cast<int>(points.size()), fillColor);
    // Dual contrast keeps the face boundary readable over both light and
    // dark voxels while the translucent fill retains the active palette.
    drawList->AddPolyline(
        points.data(), static_cast<int>(points.size()), darkKeyline,
        ImDrawFlags_Closed, 3.0F);
    drawList->AddPolyline(
        points.data(), static_cast<int>(points.size()), lightKeyline,
        ImDrawFlags_Closed, 1.0F);
    drawList->PopClipRect();
}

void EditorWorkspace::DrawViewportInteractionV2Overlay() const noexcept
{
    if (!useViewportInteractionV2_) return;
    const InteractionV2::ViewportPresentation& presentation =
        viewportInteractionV2_.Presentation();
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(
        {currentViewportRectangle_.X, currentViewportRectangle_.Y},
        {currentViewportRectangle_.X + currentViewportRectangle_.Width,
         currentViewportRectangle_.Y + currentViewportRectangle_.Height},
        true);
    if (presentation.GestureOverlay2D)
    {
        const auto& rectangle = *presentation.GestureOverlay2D;
        const ImVec2 minimum{rectangle.MinimumX, rectangle.MinimumY};
        const ImVec2 maximum{rectangle.MaximumX, rectangle.MaximumY};
        drawList->AddRectFilled(
            minimum, maximum, IM_COL32(58, 143, 230, 38));
        drawList->AddRect(
            minimum, maximum, IM_COL32(104, 190, 255, 255), 0.0F, 0, 1.5F);
    }
    if (presentation.SelectionScreenBounds &&
        presentation.Phase != InteractionV2::InteractionPhase::Selecting)
    {
        const auto& rectangle = *presentation.SelectionScreenBounds;
        const ImVec2 minimum{rectangle.MinimumX, rectangle.MinimumY};
        const ImVec2 maximum{rectangle.MaximumX, rectangle.MaximumY};
        drawList->AddRect(
            minimum, maximum, IM_COL32(116, 205, 255, 235), 2.0F, 0, 1.5F);
        if (presentation.DrawCompactHandles)
        {
            const ImVec2 center{
                (minimum.x + maximum.x) * 0.5F,
                (minimum.y + maximum.y) * 0.5F};
            constexpr float halfSize = 4.0F;
            drawList->AddRectFilled(
                {center.x - halfSize, center.y - halfSize},
                {center.x + halfSize, center.y + halfSize},
                IM_COL32(205, 237, 255, 235), 1.0F);
            drawList->AddRect(
                {center.x - halfSize, center.y - halfSize},
                {center.x + halfSize, center.y + halfSize},
                IM_COL32(12, 25, 38, 255), 1.0F);
        }
    }
    const InteractionV2::ViewportInteractionMetrics& metrics =
        viewportInteractionV2_.Metrics();
    const char* const phase =
        presentation.Phase == InteractionV2::InteractionPhase::Selecting
            ? "Selecting"
        : presentation.Phase == InteractionV2::InteractionPhase::SelectionReady
            ? "SelectionReady"
        : presentation.Phase == InteractionV2::InteractionPhase::Moving
            ? "Moving" : "Idle";
    const std::string diagnostic =
        "V2 " + std::string(phase) +
        "  session " + std::to_string(presentation.SessionId) +
        "  plan " + std::to_string(presentation.PlanId) +
        "  selected " + std::to_string(presentation.ExactSelectionCount) +
        "  resolves " + std::to_string(metrics.BusinessResolves) +
        "  move plans " + std::to_string(metrics.MovePlanBuilds) +
        "/" + std::to_string(metrics.MovePlanReuses) +
        "  projection " + std::to_string(metrics.ProjectionBuilds) +
        "  uploads " + std::to_string(metrics.PresentationUploads) +
        "  buffers " +
        std::to_string(metrics.PresentationBufferRecreations) +
        "  source uploads " + std::to_string(metrics.MoveSourceUploads) +
        "  source bytes " +
        std::to_string(metrics.MoveSourceUploadedBytes) +
        "  delta updates " +
        std::to_string(metrics.MoveDeltaGpuUpdates);
    drawList->AddText(
        {currentViewportRectangle_.X + 10.0F,
         currentViewportRectangle_.Y + 10.0F},
        IM_COL32(174, 219, 250, 225), diagnostic.c_str());
    drawList->PopClipRect();
}

void EditorWorkspace::CommitViewportInteractionV2Move()
{
    std::optional<VoxelEditOperation> operation =
        viewportInteractionV2_.TakeCommit();
    if (!operation) return;
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        viewportInteractionV2_.NotifyCommitApplied(
            selectionService_, voxelDocumentSession_.Generation(),
            document ? document->GetRevision() : 0U);
        return;
    }
    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        static_cast<VoxelEditSession&>(*this), std::move(*operation));
    voxelEditInProgress_ = false;
    if (result)
    {
        ApplyVoxelHistorySelection(result);
        AddConsoleMessage("[Edit] V2 moved " +
            std::to_string(selectionService_.Count()) + " voxel(s).");
    }
    else
    {
        AddConsoleMessage("[Edit] V2 Move failed: " + result.Message);
    }
    viewportInteractionV2_.NotifyCommitApplied(
        selectionService_, voxelDocumentSession_.Generation(),
        document->GetRevision());
}

void EditorWorkspace::CommitPencilViewportInteractionV2()
{
    std::optional<VoxelEditOperation> operation =
        pencilViewportInteractionV2_.TakeCommit();
    if (!operation) return;

    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        pencilViewportInteractionV2_.NotifyCommitApplied();
        return;
    }

    // The V2 gateway has already converted the immutable gesture plans into
    // one operation.  Workspace remains only the document/history façade:
    // no brush geometry, picking, or plan resolution happens at commit time.
    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        static_cast<VoxelEditSession&>(*this), std::move(*operation));
    voxelEditInProgress_ = false;
    if (result)
    {
        ApplyVoxelHistorySelection(result);
        AddConsoleMessage("[Edit] V2 Pencil stroke committed.");
    }
    else
    {
        AddConsoleMessage("[Edit] V2 Pencil failed: " + result.Message);
    }
    pencilViewportInteractionV2_.NotifyCommitApplied();
}

void EditorWorkspace::UpdateTransformGizmo(
    const float viewportHeightPixels) noexcept
{
    const std::optional<SelectionBounds> stampBounds =
        CurrentStampPreviewBounds();
    const bool stampGizmo = stampPlacementSession_.IsActive() &&
        stampBounds.has_value();
    if (!stampGizmo && useViewportInteractionV2_ &&
        (voxelToolState_.IsSelectionActive() ||
         voxelToolState_.IsMoveActive()))
    {
        transformPivotManager_.Invalidate();
        viewportRenderer_.ConfigureTransformGizmo(nullptr);
        return;
    }
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionBounds gizmoBounds = stampGizmo
        ? *stampBounds
        : transformGizmoManager_.IsDragging() &&
                transformPreviewModel_.IsActive()
            ? transformPreviewModel_.PreviewBounds()
            : selectionService_.EditableBounds();
    const bool gizmoSourceValid = stampGizmo
        ? document != nullptr && voxelDocumentSession_.Generation() != 0U
        : document != nullptr && !selectionService_.Empty() &&
            selectionService_.DocumentGeneration() ==
                voxelDocumentSession_.Generation();
    if (gizmoSourceValid && gizmoBounds.Valid)
    {
        static_cast<void>(transformPivotManager_.UpdateFromBounds(
            gizmoBounds, voxelModelCenter_));
    }
    else
    {
        transformPivotManager_.Invalidate();
    }
    TransformGizmoUpdateContext context;
    context.DocumentActive = document != nullptr;
    context.SelectionEmpty = stampGizmo
        ? false : selectionService_.Empty();
    context.Closing =
        closeRequest_.State() != EditorCloseRequestState::None;
    context.ActiveDocumentGeneration = voxelDocumentSession_.Generation();
    context.SelectionDocumentGeneration =
        stampGizmo ? voxelDocumentSession_.Generation()
                   : selectionService_.DocumentGeneration();
    context.Bounds = gizmoBounds;
    context.ActiveTool = stampGizmo
        ? stampGizmoTool_ : voxelToolState_.ActiveTool();
    // STAMP-24 : les trois anneaux sont utilisables ; l'anneau saisi decide de
    // l'axe (cf. la branche stamp du drag dans EditorWorkspaceViewportPanel).
    if (stampGizmo && stampGizmoTool_ == ActiveVoxelTool::Rotate)
        context.AxisEnabled = {{true, true, true}};
    context.CameraPosition = viewportCamera_.GetPosition();
    context.CameraForward = viewportCamera_.GetForward();
    context.VerticalFieldOfViewDegrees =
        viewportCamera_.GetFieldOfViewDegrees();
    context.ViewportHeightPixels = viewportHeightPixels;
    context.Projection = TransformGizmoProjection::Perspective;
    context.InteractionState = transformGizmoManager_.State();
    context.ActiveAxis = transformGizmoManager_.ActiveAxis();
    context.ViewProjection = viewportCamera_.GetViewProjection();
    context.Viewport = currentViewportRectangle_;
    static_cast<void>(transformGizmoManager_.UpdateView(context));
    const TransformGizmoView& view = transformGizmoManager_.View();
    viewportRenderer_.ConfigureTransformGizmo(view.Visible ? &view : nullptr);
}

void EditorWorkspace::DrawTransformGizmoVisibilityAnchor() const noexcept
{
    if (!TransformGizmoRenderPolicy::CenterScreenOverlayEnabled)
        return;
    const TransformGizmoView& view = transformGizmoManager_.View();
    if (!view.Visible || currentViewportRectangle_.Width <= 0.0F ||
        currentViewportRectangle_.Height <= 0.0F)
        return;
    const Matrix4 viewProjection = viewportCamera_.GetViewProjection();
    const auto project = [this, &viewProjection](const Vec3 world)
        -> std::optional<ImVec2>
    {
        const auto projected = TransformGizmoModel::ProjectWorldToScreen(
            world, currentViewportRectangle_, viewProjection);
        return projected
            ? std::optional<ImVec2>(ImVec2{projected->X, projected->Y})
            : std::nullopt;
    };
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(
        {currentViewportRectangle_.X, currentViewportRectangle_.Y},
        {currentViewportRectangle_.X + currentViewportRectangle_.Width,
         currentViewportRectangle_.Y + currentViewportRectangle_.Height},
        true);
    const ImDrawListFlags previousFlags = drawList->Flags;
    drawList->Flags |= ImDrawListFlags_AntiAliasedLines;
    if (ImGui::GetStyle().AntiAliasedLinesUseTex &&
        !(ImGui::GetIO().Fonts->Flags & ImFontAtlasFlags_NoBakedLines))
        drawList->Flags |= ImDrawListFlags_AntiAliasedLinesUseTex;

    if (view.Mode == TransformGizmoMode::Rotate)
    {
        const Vec3 cameraPosition = viewportCamera_.GetPosition();
        const Vec3 cameraForward = viewportCamera_.GetForward();
        for (const TransformGizmoAxisView& axis : view.Axes)
        {
            if (!axis.HasRotationRing) continue;
            constexpr std::size_t pointCount =
                TransformGizmoAxisView::RotationRingSegmentCount;
            std::array<ImVec2, pointCount> points{};
            std::array<bool, pointCount> valid{};
            std::array<bool, pointCount> occluded{};
            for (std::size_t index = 0U; index < pointCount; ++index)
            {
                const Vec3 world = axis.RotationRingPoints[index];
                const float cameraDepth =
                    Dot(world - cameraPosition, cameraForward);
                const auto screen = cameraDepth >
                        GizmoStyle::RotateNearPlaneDistance
                    ? project(world) : std::nullopt;
                valid[index] = screen.has_value() &&
                    std::isfinite(screen->x) && std::isfinite(screen->y);
                if (screen) points[index] = *screen;
                const std::size_t next = (index + 1U) % pointCount;
                const Vec3 midpoint =
                    (axis.RotationRingPoints[index] +
                     axis.RotationRingPoints[next]) * 0.5F;
                occluded[index] =
                    Dot(midpoint - view.Center, cameraForward) > 0.0F;
            }
            const float screenThickness =
                axis.Thickness > 0.0F && axis.RotationRingRadius > 0.001F
                ? axis.Thickness * axis.ProjectedLengthPixels /
                    axis.RotationRingRadius
                : GizmoStyle::RotateIdleThicknessPixels;
            const auto colorFor = [&axis](const bool behind)
            {
                const float intensity = behind
                    ? GizmoStyle::RotateOccludedIntensity : 1.0F;
                return ImGui::ColorConvertFloat4ToU32({
                    axis.Color[0] * intensity,
                    axis.Color[1] * intensity,
                    axis.Color[2] * intensity,
                    1.0F});
            };
            std::array<bool, pointCount> validSegment{};
            bool closed = true;
            bool uniformVisibility = true;
            for (std::size_t index = 0U; index < pointCount; ++index)
            {
                const std::size_t next = (index + 1U) % pointCount;
                if (!valid[index] || !valid[next])
                {
                    closed = false;
                    continue;
                }
                const float dx = points[next].x - points[index].x;
                const float dy = points[next].y - points[index].y;
                validSegment[index] = dx * dx + dy * dy <=
                    GizmoStyle::RotateMaximumChordPixels *
                    GizmoStyle::RotateMaximumChordPixels;
                closed = closed && validSegment[index];
                if (index > 0U && occluded[index] != occluded[0])
                    uniformVisibility = false;
            }
            if (closed && uniformVisibility)
            {
                drawList->AddPolyline(points.data(),
                    static_cast<int>(points.size()), colorFor(occluded[0]),
                    ImDrawFlags_Closed, screenThickness);
                continue;
            }
            std::size_t start = 0U;
            for (; start < pointCount; ++start)
            {
                const std::size_t previous =
                    (start + pointCount - 1U) % pointCount;
                if (!validSegment[previous] || !validSegment[start] ||
                    occluded[start] != occluded[previous])
                    break;
            }
            std::array<ImVec2, pointCount + 1U> run{};
            std::size_t runCount = 0U;
            bool runOccluded = false;
            const auto flush = [&]()
            {
                if (runCount >= 2U)
                    drawList->AddPolyline(run.data(),
                        static_cast<int>(runCount), colorFor(runOccluded),
                        ImDrawFlags_None, screenThickness);
                runCount = 0U;
            };
            for (std::size_t step = 0U; step < pointCount; ++step)
            {
                const std::size_t index = (start + step) % pointCount;
                const std::size_t next = (index + 1U) % pointCount;
                if (!validSegment[index])
                {
                    flush();
                    continue;
                }
                if (runCount == 0U)
                {
                    runOccluded = occluded[index];
                    run[runCount++] = points[index];
                }
                else if (runOccluded != occluded[index])
                {
                    flush();
                    runOccluded = occluded[index];
                    run[runCount++] = points[index];
                }
                run[runCount++] = points[next];
            }
            flush();
        }
    }
    if (view.Mode == TransformGizmoMode::Move)
    {
        const Vec3 cameraForward = viewportCamera_.GetForward();
        for (const TransformGizmoAxisView& axis : view.Axes)
        {
            if (!axis.HasArrowHead) continue;
            const auto start = project(axis.Start);
            const auto end = project(axis.End);
            if (!start || !end) continue;
            const Vec3 midpoint = (axis.Start + axis.End) * 0.5F;
            const float visibility =
                Dot(midpoint - view.Center, cameraForward) > 0.0F
                ? GizmoStyle::OccludedIntensity : 1.0F;
            const ImU32 color = ImGui::ColorConvertFloat4ToU32({
                axis.Color[0] * visibility,
                axis.Color[1] * visibility,
                axis.Color[2] * visibility,
                1.0F});
            float dx = end->x - start->x;
            float dy = end->y - start->y;
            const float projectedLength = std::sqrt(dx * dx + dy * dy);
            if (projectedLength >= 0.5F)
            {
                const float inverseLength = 1.0F / projectedLength;
                dx *= inverseLength;
                dy *= inverseLength;
            }
            else if (axis.Axis == TransformGizmoAxis::X)
            {
                dx = 1.0F;
                dy = 0.0F;
            }
            else if (axis.Axis == TransformGizmoAxis::Y)
            {
                dx = 0.0F;
                dy = -1.0F;
            }
            else
            {
                constexpr float inverseRootTwo = 0.70710678F;
                dx = -inverseRootTwo;
                dy = -inverseRootTwo;
            }
            const ImVec2 direction{dx, dy};
            const ImVec2 perpendicular{-direction.y, direction.x};
            const float headLength = std::clamp(
                projectedLength * GizmoStyle::MoveArrowLengthRatio,
                TransformGizmoModel::MinimumArrowLengthPixels,
                TransformGizmoModel::MaximumArrowLengthPixels);
            const float headWidth = std::clamp(
                headLength * GizmoStyle::MoveArrowWidthRatio,
                TransformGizmoModel::MinimumArrowWidthPixels,
                TransformGizmoModel::MaximumArrowWidthPixels);
            const ImVec2 base{
                end->x - direction.x * headLength,
                end->y - direction.y * headLength};
            const ImVec2 left{
                base.x + perpendicular.x * headWidth * 0.5F,
                base.y + perpendicular.y * headWidth * 0.5F};
            const ImVec2 right{
                base.x - perpendicular.x * headWidth * 0.5F,
                base.y - perpendicular.y * headWidth * 0.5F};
            const float worldLength = Length(axis.End - axis.Start);
            const float screenThickness = axis.Thickness > 0.0F &&
                    worldLength > 0.0001F
                ? axis.Thickness * axis.ProjectedLengthPixels / worldLength
                : GizmoStyle::MoveAxisIdleThicknessPixels;
            if (projectedLength > headLength * 0.75F)
                drawList->AddLine(*start, base, color, screenThickness);
            drawList->AddTriangleFilled(*end, left, right, color);
        }
    }
    if (view.Mode == TransformGizmoMode::Scale)
    {
        const Vec3 cameraForward = viewportCamera_.GetForward();
        for (const TransformGizmoAxisView& axis : view.Axes)
        {
            if (!axis.HasScaleHandle) continue;
            const auto start = project(axis.Start);
            const auto end = project(axis.End);
            if (!start || !end) continue;
            const Vec3 midpoint = (axis.Start + axis.End) * 0.5F;
            const float visibility =
                Dot(midpoint - view.Center, cameraForward) > 0.0F
                ? GizmoStyle::OccludedIntensity : 1.0F;
            const ImU32 color = ImGui::ColorConvertFloat4ToU32({
                axis.Color[0] * visibility,
                axis.Color[1] * visibility,
                axis.Color[2] * visibility,
                1.0F});
            const float worldLength = Length(axis.End - axis.Start);
            const float screenThickness = axis.Thickness > 0.0F &&
                    worldLength > 0.0001F
                ? axis.Thickness * axis.ProjectedLengthPixels / worldLength
                : GizmoStyle::MoveAxisIdleThicknessPixels;
            drawList->AddLine(*start, *end, color, screenThickness);
            const float halfSize = axis.ScaleHandleSizePixels * 0.5F;
            drawList->AddRectFilled(
                {end->x - halfSize, end->y - halfSize},
                {end->x + halfSize, end->y + halfSize}, color, 1.0F);
        }
    }
    if (const auto center = project(view.Center))
    {
        if (view.Mode == TransformGizmoMode::Rotate)
        {
            const float intensity = view.State ==
                    TransformGizmoInteractionState::Dragging
                ? GizmoStyle::RotateCenterDraggingIntensity
                : GizmoStyle::RotateCenterIdleIntensity;
            drawList->AddCircleFilled(*center,
                GizmoStyle::RotateCenterDiameterPixels * 0.5F,
                ImGui::ColorConvertFloat4ToU32({
                    0.72F * intensity,
                    0.74F * intensity,
                    0.78F * intensity,
                    0.72F}), 12);
        }
        else if (view.Mode == TransformGizmoMode::Move ||
                 view.Mode == TransformGizmoMode::Scale)
        {
            const float intensity = view.State ==
                    TransformGizmoInteractionState::Dragging
                ? GizmoStyle::MoveCenterDraggingIntensity
                : GizmoStyle::MoveCenterIdleIntensity;
            drawList->AddCircleFilled(*center,
                GizmoStyle::MoveCenterDiameterPixels * 0.5F,
                ImGui::ColorConvertFloat4ToU32({
                    0.72F * intensity,
                    0.74F * intensity,
                    0.78F * intensity,
                    0.72F}), 12);
        }
        else
        {
            drawList->AddCircleFilled(*center, 3.0F,
                IM_COL32(170, 178, 194, 255), 16);
            drawList->AddCircle(*center, 4.25F,
                IM_COL32(24, 28, 36, 255), 16, 1.25F);
        }
    }
    drawList->Flags = previousFlags;
    drawList->PopClipRect();
}

void EditorWorkspace::ClearVoxelViewport() noexcept
{
    CancelSmartToolStroke();
    commandHistory_.Clear();
    voxelEditHistory_.Clear();
    ++voxelModelGeneration_;
    transformPreviewModel_.Reset();
    transformGizmoManager_.Reset();
    transformPivotManager_.Invalidate();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    voxelScaleStatusMessage_.clear();
    viewportRenderer_.ClearModel();
    smartToolExactPreviewCache_.Clear();
    viewportRenderer_.ConfigureGuides(0.0F, 0.0F, 0.0F);
    viewportState_.Clear();
    voxelDocumentSession_.Close();
    voxelDocumentMeshCache_.Clear();
    uploadedDocumentIdentity_.reset();
    uploadedDocumentRevision_.reset();
    failedDocumentIdentity_.reset();
    failedDocumentRevision_.reset();
    activeVoxelModel_.reset();
    selectionService_.ClearDocument();
    selectionVolumeCache_.Clear();
    selectionBoxInteriorHovered_ = false;
    static_cast<void>(selectionInteraction_.Cancel());
    voxelSelectionClickCandidate_ = false;
    selectionPointerAnchor_.reset();
    static_cast<void>(voxelSelection_.Clear());
    voxelToolState_.Reset();
    paletteService_.Clear();
    paletteColorEditorIndex_.reset();
    voxelToolInput_.Reset();
    voxelToolSmokeInput_.Reset();
    voxelBoxInteraction_.Cancel();
    voxelLineInteraction_.Cancel();
    voxelSphereInteraction_.Cancel();
    voxelPlacementPreview_ = {};
    workplaneHit_.reset();
    currentViewportRectangle_ = {};
    layoutStabilitySmokePickingOverride_.reset();
    layoutStabilitySmokeHitOverride_.reset();
    layoutStabilitySmokeWorkplaneOverride_.reset();
    firstCreationExperience_.Hide();
    lastVoxelToolResult_.reset();
    lastVoxelEraserResult_.reset();
    lastVoxelFillResult_.reset();
    lastVoxelBoxResult_.reset();
    lastVoxelLineResult_.reset();
    lastVoxelSphereResult_.reset();
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
    static_cast<void>(viewportNavigation_.FrameAll(SceneNavigationBounds()));
}

void EditorWorkspace::FocusSelectionOrFrameAll() noexcept
{
    if (!selectionService_.Empty() &&
        viewportNavigation_.FocusSelection(SelectionNavigationBounds()))
        return;
    FrameVoxelViewport();
}

ViewportNavigationBounds EditorWorkspace::SelectionNavigationBounds() const noexcept
{
    const SelectionBounds& bounds = selectionService_.EditableBounds().Valid
        ? selectionService_.EditableBounds() : selectionService_.Bounds();
    if (!bounds.Valid) return {};
    return {
        {static_cast<float>(bounds.Minimum.X) - voxelModelCenter_.X,
         static_cast<float>(bounds.Minimum.Y) - voxelModelCenter_.Y,
         static_cast<float>(bounds.Minimum.Z) - voxelModelCenter_.Z},
        {static_cast<float>(bounds.Maximum.X + 1) - voxelModelCenter_.X,
         static_cast<float>(bounds.Maximum.Y + 1) - voxelModelCenter_.Y,
         static_cast<float>(bounds.Maximum.Z + 1) - voxelModelCenter_.Z},
        true};
}

ViewportNavigationBounds EditorWorkspace::SceneNavigationBounds() const noexcept
{
    if (!viewportState_.HasModel())
        return {{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}, true};
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    return {
        {-voxelModelCenter_.X, -voxelModelCenter_.Y, -voxelModelCenter_.Z},
        {static_cast<float>(statistics.Width) - voxelModelCenter_.X,
         static_cast<float>(statistics.Height) - voxelModelCenter_.Y,
         static_cast<float>(statistics.Depth) - voxelModelCenter_.Z},
        statistics.Width > 0U && statistics.Height > 0U &&
            statistics.Depth > 0U};
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
    console_.AddMessage(std::move(message));
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

void EditorWorkspace::ApplyDefaultLayoutPanelVisibility()
{
    showTools_ = true;
    showToolOptions_ = true;
    showExplorer_ = true;
    showScene_ = true;
    showInspector_ = true;
    showTransformPanel_ = true;
    showPalette_ = true;
    showAssetBrowser_ = true;
    showForgeLibrary_ = true;
    console_.SetVisible(true);
}

void EditorWorkspace::ApplyThumbnailVisualLayoutPanelVisibility()
{
    showExplorer_ = false;
    showScene_ = false;
    showInspector_ = true;
    showAssetBrowser_ = true;
    console_.SetVisible(false);
}

} // namespace VoxelForge::Editor
