#include "EditorWorkspace.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelSelection/VoxelRayTransform.h"
#include "EditorWindowTitle.h"
#include "Layout/PalettePanelLayout.h"
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
constexpr float StatusBarHeight = 26.0F;
constexpr std::size_t MaximumConsoleMessageCount = 200;
constexpr const char* WorkspaceDockspaceName = "VoxelForgeStudioDockSpace";
constexpr const char* ToolsPanelWindowName = "Tools";
constexpr const char* ToolOptionsPanelWindowName = "Tool Options";
constexpr const char* StylePanelWindowName = "Style";
constexpr const char* ViewportPanelWindowName = "Viewport";
constexpr const char* AssetsPanelWindowName = "Assets";
constexpr const char* ForgeLibraryPanelWindowName = "Forge Library";
constexpr const char* ScenePanelWindowName = "Scene";
constexpr const char* InspectorPanelWindowName = "Inspector";
constexpr const char* TransformPanelWindowName = "Transform";
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

void DrawSelectionHandles(
    const SelectionHandles& handles,
    const std::optional<SelectionFace> hoveredFace,
    const SelectionFace activeFace)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float NormalHalfSize = 4.5F;
    constexpr float ActiveHalfSize = 6.0F;
    for (const SelectionHandle& handle : handles)
    {
        if (!handle.Visible) continue;
        const bool active = handle.Face == activeFace;
        const bool hovered = hoveredFace && *hoveredFace == handle.Face;
        const float halfSize = active ? ActiveHalfSize : NormalHalfSize;
        const ImVec2 minimum{
            handle.ScreenPosition.X - halfSize,
            handle.ScreenPosition.Y - halfSize};
        const ImVec2 maximum{
            handle.ScreenPosition.X + halfSize,
            handle.ScreenPosition.Y + halfSize};
        const ImU32 fill = active
            ? IM_COL32(255, 194, 92, 255)
            : hovered ? IM_COL32(104, 255, 220, 255)
                      : IM_COL32(18, 45, 50, 245);
        const ImU32 outline = active || hovered
            ? IM_COL32(248, 255, 253, 255)
            : IM_COL32(78, 232, 202, 255);
        drawList->AddRectFilled(minimum, maximum, fill, 1.5F);
        drawList->AddRect(minimum, maximum, IM_COL32(4, 8, 12, 255), 1.5F,
            0, 3.0F);
        drawList->AddRect(minimum, maximum, outline, 1.5F, 0, 1.0F);
    }
}

void SetSelectionHandleCursor(const SelectionHandle& handle)
{
    const float horizontal = std::abs(handle.ScreenAxisPerVoxel.X);
    const float vertical = std::abs(handle.ScreenAxisPerVoxel.Y);
    if (horizontal > vertical * 1.5F)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (vertical > horizontal * 1.5F)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    else
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
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
    const bool simulatedFileDialogs)
    : projectManager_(projectManager),
      windowTitleCallback_(std::move(windowTitleCallback)),
      fileDialogService_(simulatedFileDialogs
          ? CreateSimulatedFileDialogService()
          : CreateSDLFileDialogService()),
      projectFolderOpener_(CreateSDLProjectFolderOpener()),
      transformGizmoManager_(
          transformGizmoModel_, transformGizmoInteraction_,
          transformPivotManager_),
      consoleMessages_{
          "Console ready",
          "VoxelForge Studio initialized"},
      projectDialogPreferences_(std::move(preferencesFilePath))
{
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
    DrawDockSpace(dockspaceId);

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
            BuildThumbnailVisualLayout(dockspaceId);
        else
            BuildDefaultLayout(dockspaceId);
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
    if (showConsole_) DrawConsolePanel();
    if (showProfiler_) DrawProfilerPanel();

    if (showImGuiDemo_)
    {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
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
                "New Model", "Ctrl+Shift+N", false,
                hasActiveProject))
        {
            RequestInstantNewVoxelModel();
        }
        DrawTooltip("Create and open a new voxel model instantly");

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
                "Rotate Stamp Preview Clockwise", nullptr, false,
                liveStampPreviewActive))
        {
            RotateLatestStampPreview(true);
        }

        if (ImGui::MenuItem(
                "Rotate Stamp Preview Counter-Clockwise", nullptr, false,
                liveStampPreviewActive))
        {
            RotateLatestStampPreview(false);
        }

        if (ImGui::MenuItem(
                "Place Stamp Preview (Developer)", nullptr, false,
                liveStampPreviewActive && voxelDocumentSession_.HasActiveDocument() &&
                    !voxelEditInProgress_))
        {
            PlaceLatestStampPreview();
        }

        if (ImGui::MenuItem("Clear Stamp Preview", "Esc", false, liveStampPreviewActive))
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
        ImGui::MenuItem("Console", nullptr, &showConsole_);
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
        RequestInstantNewVoxelModel();
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
            transformGizmoManager_.IsDragging() ||
            selectionInteraction_.IsActive() ||
            selectionService_.EditableBounds().Valid ||
            voxelSelection_.Selected().has_value(),
        CanMoveSelection(),
        CanDuplicateSelection(),
        CanRotateSelection(),
        voxelToolState_.IsRotateActive() && CanRotateSelection(),
        CanMirrorSelection(),
        voxelToolState_.IsMirrorActive() && CanMirrorSelection(),
        CanScaleSelection(),
        voxelToolState_.IsScaleActive() && CanScaleSelection(),
        CanAlignSelection(),
        voxelToolState_.IsAlignActive() && CanAlignSelection(),
        (voxelToolState_.IsRotateActive() ||
         voxelToolState_.IsMirrorActive() ||
         voxelToolState_.IsScaleActive() ||
         voxelToolState_.IsAlignActive()) &&
            transformPreviewModel_.IsActive() &&
            !transformPreviewModel_.HasCollisions() &&
            !transformPreviewModel_.HasOutOfBounds()};
}

bool EditorWorkspace::CanMoveSelection() const noexcept
{
    return voxelDocumentSession_.HasActiveDocument() &&
        !voxelEditInProgress_ && !voxelEditHistory_.IsBusy() &&
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
        static_cast<void>(BeginVoxelRotatePreview(
            VoxelRotationDirection::CounterClockwise)); break;
    case EditorInputCommand::RotateRight:
        static_cast<void>(BeginVoxelRotatePreview(
            VoxelRotationDirection::Clockwise)); break;
    case EditorInputCommand::MirrorX:
        static_cast<void>(BeginVoxelMirrorPreview(VoxelMirrorAxis::X)); break;
    case EditorInputCommand::MirrorZ:
        static_cast<void>(BeginVoxelMirrorPreview(VoxelMirrorAxis::Z)); break;
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
        if (voxelToolState_.IsRotateActive())
            static_cast<void>(ApplyVoxelRotate());
        else if (voxelToolState_.IsMirrorActive())
            static_cast<void>(ApplyVoxelMirror());
        else if (voxelToolState_.IsScaleActive())
            static_cast<void>(ApplyVoxelScale());
        else if (voxelToolState_.IsAlignActive())
            static_cast<void>(ApplyVoxelAlign());
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

    constexpr float PreferredLeftPanelFraction = 0.070F;
    constexpr float PreferredRightPanelFraction = 0.073F;
    constexpr float MinimumSidePanelWidth = 140.0F;
    constexpr float MaximumSidePanelWidth = 220.0F;
    constexpr float MinimumViewportWidthFraction = 0.75F;
    constexpr float PreferredConsoleHeightFraction = 0.10F;
    constexpr float MinimumConsoleHeight = 96.0F;
    constexpr float MaximumConsoleHeight = 140.0F;

    float leftPanelWidth = std::clamp(
        workspaceSize.x * PreferredLeftPanelFraction,
        MinimumSidePanelWidth,
        MaximumSidePanelWidth);
    float rightPanelWidth = std::clamp(
        workspaceSize.x * PreferredRightPanelFraction,
        MinimumSidePanelWidth,
        MaximumSidePanelWidth);
    const float maximumSidePanelTotal = workspaceSize.x *
        (1.0F - MinimumViewportWidthFraction);
    const float requestedSidePanelTotal = leftPanelWidth + rightPanelWidth;
    if (requestedSidePanelTotal > maximumSidePanelTotal &&
        requestedSidePanelTotal > 0.0F)
    {
        const float scale = maximumSidePanelTotal / requestedSidePanelTotal;
        leftPanelWidth *= scale;
        rightPanelWidth *= scale;
    }
    const float consoleHeight = std::clamp(
        workspaceSize.y * PreferredConsoleHeightFraction,
        MinimumConsoleHeight,
        MaximumConsoleHeight);

    ImGuiID topId = dockspaceId;
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Down,
        consoleHeight / workspaceSize.y,
        nullptr,
        &topId);

    ImGuiID rightId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Right,
        rightPanelWidth / workspaceSize.x,
        nullptr,
        &topId);

    const ImGuiID leftId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Left,
        leftPanelWidth / (workspaceSize.x - rightPanelWidth),
        nullptr,
        &topId);

    ImGuiID toolsId = leftId;
    const ImGuiID styleId = ImGui::DockBuilderSplitNode(
        toolsId, ImGuiDir_Down, 0.34F, nullptr, &toolsId);
    const ImGuiID toolOptionsId = ImGui::DockBuilderSplitNode(
        toolsId, ImGuiDir_Down, 0.50F, nullptr, &toolsId);

    ImGui::DockBuilderDockWindow(ToolsPanelWindowName, toolsId);
    ImGui::DockBuilderDockWindow(ToolOptionsPanelWindowName, toolOptionsId);
    ImGui::DockBuilderDockWindow(StylePanelWindowName, styleId);
    ImGui::DockBuilderDockWindow(ViewportPanelWindowName, topId);
    ImGui::DockBuilderDockWindow(AssetsPanelWindowName, rightId);
    ImGui::DockBuilderDockWindow(ForgeLibraryPanelWindowName, rightId);
    ImGui::DockBuilderDockWindow(ScenePanelWindowName, rightId);
    ImGui::DockBuilderDockWindow(InspectorPanelWindowName, rightId);
    ImGui::DockBuilderDockWindow(TransformPanelWindowName, rightId);
    ImGui::DockBuilderDockWindow("Console", bottomId);
    if (ImGuiDockNode* const rightNode = ImGui::DockBuilderGetNode(rightId))
        rightNode->SelectedTabId = ImHashStr(AssetsPanelWindowName);
    ImGui::DockBuilderFinish(dockspaceId);

    showTools_ = true;
    showToolOptions_ = true;
    showExplorer_ = true;
    showScene_ = true;
    showInspector_ = true;
    showTransformPanel_ = true;
    showPalette_ = true;
    showAssetBrowser_ = true;
    showForgeLibrary_ = true;
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
    ImGui::DockBuilderDockWindow(AssetsPanelWindowName, browserId);
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
        EditorToolbar::Draw(
            {activeDocument != nullptr, canSave, voxelToolState_.ActiveTool(),
             CanMoveSelection(), CanDuplicateSelection(), CanRotateSelection(),
             CanMirrorSelection(), CanScaleSelection(), CanAlignSelection()},
            editorInputService_,
            {[this](const EditorInputCommand command)
             {
                 ExecuteInputCommand(command);
             }});
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
        smartBrushPreviewRefreshRequested_ =
            ToolPanel::Draw(toolManager_, toolContext_) ||
            smartBrushPreviewRefreshRequested_;
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorWorkspace::DrawScenePanel()
{
    const bool focusRequested = std::exchange(
        viewportFocusRequested_, false);
    if (focusRequested) ImGui::SetNextWindowFocus();
    const bool visible = ImGui::Begin(ViewportPanelWindowName, &showScene_);
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
    DrawTooltip("Frame the active voxel model");
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

    if (voxelToolState_.IsRotateActive())
    {
        if (ImGui::Button("Left 90"))
            ExecuteInputCommand(EditorInputCommand::RotateLeft);
        DrawTooltip("Rotate the preview left by 90 degrees (Q)");
        ImGui::SameLine();
        if (ImGui::Button("Right 90"))
            ExecuteInputCommand(EditorInputCommand::RotateRight);
        DrawTooltip("Rotate the preview right by 90 degrees (Shift+Q)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current 90-degree rotation (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Rotate without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsScaleActive())
    {
        if (ImGui::Button("X x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleX);
        DrawTooltip("Preview Scale X by 2 (X)");
        ImGui::SameLine();
        if (ImGui::Button("Y x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleY);
        DrawTooltip("Preview Scale Y by 2 (Y)");
        ImGui::SameLine();
        if (ImGui::Button("Z x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleZ);
        DrawTooltip("Preview Scale Z by 2 (Z)");
        ImGui::SameLine();
        if (ImGui::Button("Uniform x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleUniform);
        DrawTooltip("Preview uniform Scale by 2 (U)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current Scale (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Scale without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsMirrorActive())
    {
        if (ImGui::Button("Mirror X"))
            ExecuteInputCommand(EditorInputCommand::MirrorX);
        DrawTooltip("Preview Mirror X (X)");
        ImGui::SameLine();
        if (ImGui::Button("Mirror Z"))
            ExecuteInputCommand(EditorInputCommand::MirrorZ);
        DrawTooltip("Preview Mirror Z (Z)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current mirror (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Mirror without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsAlignActive())
    {
        const auto directionButton = [this](
            const char* label, const VoxelAlignDirection direction)
        {
            if (ImGui::Button(label))
                static_cast<void>(BeginVoxelAlignPreview(direction));
            ImGui::SameLine();
        };
        directionButton("Left", VoxelAlignDirection::Left);
        directionButton("Right", VoxelAlignDirection::Right);
        directionButton("Bottom", VoxelAlignDirection::Bottom);
        directionButton("Top", VoxelAlignDirection::Top);
        directionButton("Front", VoxelAlignDirection::Front);
        directionButton("Back", VoxelAlignDirection::Back);
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current alignment (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Align without changing the document (Esc)");
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanUndo() || voxelEditInProgress_);
    if (ImGui::Button("Undo"))
        ExecuteInputCommand(EditorInputCommand::EditUndo);
    ImGui::EndDisabled();
    const std::string undoTooltip = "Undo the last edit (" +
        std::string(editorInputService_.ShortcutLabel(
            EditorInputCommand::EditUndo)) + ")";
    DrawTooltip(undoTooltip.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanRedo() || voxelEditInProgress_);
    if (ImGui::Button("Redo"))
        ExecuteInputCommand(EditorInputCommand::EditRedo);
    ImGui::EndDisabled();
    const std::string redoTooltip = "Redo the last undone edit (" +
        std::string(editorInputService_.ShortcutLabel(
            EditorInputCommand::EditRedo)) + ")";
    DrawTooltip(redoTooltip.c_str());

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
        "F: focus | Right: orbit | Middle: pan | Wheel: zoom | Home: reset");

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0F);
    available.y = std::max(available.y, 1.0F);
    const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    currentViewportRectangle_ = {
        imageOrigin.x, imageOrigin.y, available.x, available.y};
    viewportCamera_.SetAspectRatio(available.x / available.y);
    viewportNavigation_.Tick(ImGui::GetIO().DeltaTime);
    UpdateTransformGizmo(available.y);
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
        DrawUniversalPreviewCursor2D();
        DrawTransformGizmoVisibilityAnchor();
        const bool imageHovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        SelectionHandles selectionHandles{};
        std::optional<SelectionHandle> hoveredSelectionHandle;
        if (!useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive() ||
             voxelToolState_.IsDuplicateActive() ||
             voxelToolState_.IsRotateActive() ||
             voxelToolState_.IsMirrorActive() ||
             voxelToolState_.IsScaleActive() ||
             voxelToolState_.IsAlignActive()) &&
            selectionService_.EditableBounds().Valid &&
            selectionInteraction_.Mode() != SelectionInteractionMode::Creating)
        {
            const SelectionBounds& handleBounds =
                (voxelToolState_.IsRotateActive() ||
                 voxelToolState_.IsMirrorActive() ||
                 voxelToolState_.IsScaleActive() ||
                 voxelToolState_.IsAlignActive()) &&
                    transformPreviewModel_.IsActive()
                ? transformPreviewModel_.PreviewBounds()
                :
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::ResizingFace ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingBox ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingContent ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::DuplicatingContent
                ? selectionInteraction_.CurrentBounds()
                : selectionService_.EditableBounds();
            selectionHandles = ProjectSelectionHandles(
                handleBounds, voxelModelCenter_, currentViewportRectangle_,
                viewportCamera_.GetViewProjection());
            if (voxelToolState_.IsSelectionActive() &&
                selectionInteraction_.Mode() == SelectionInteractionMode::Idle &&
                imageHovered)
            {
                hoveredSelectionHandle = PickSelectionHandle(
                    selectionHandles, {io.MousePos.x, io.MousePos.y});
            }
            if (hoveredSelectionHandle)
                SetSelectionHandleCursor(*hoveredSelectionHandle);
            DrawSelectionHandles(
                selectionHandles,
                hoveredSelectionHandle
                    ? std::optional<SelectionFace>(hoveredSelectionHandle->Face)
                    : std::nullopt,
                selectionInteraction_.ActiveFace());
        }
        ImGui::GetWindowDrawList()->AddRect(
            imageOrigin,
            ImVec2(imageOrigin.x + available.x, imageOrigin.y + available.y),
            IM_COL32(55, 64, 78, 255));

        if (voxelToolState_.IsBoxActive() && voxelBoxInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelBox();
        }
        if (voxelToolState_.IsLineActive() && voxelLineInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelLine();
        }
        if (voxelToolState_.IsSphereActive() &&
            voxelSphereInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelSphere();
        }
        const bool sceneFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const VoxelCameraInteraction cameraInteraction =
            ImGui::IsMouseDown(ImGuiMouseButton_Right)
                ? VoxelCameraInteraction::Orbit
                : ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                ? VoxelCameraInteraction::Pan
                : (imageHovered && io.MouseWheel != 0.0F && !io.KeyCtrl)
                ? VoxelCameraInteraction::Zoom
                : VoxelCameraInteraction::None;
        const bool cameraControl =
            cameraInteraction != VoxelCameraInteraction::None;
        const bool incompatiblePopupOpen = ImGui::IsPopupOpen(
            nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const DragDropImportState dropState = dragDropImport_.State();
        const bool dragDropActive =
            dropState != DragDropImportState::Idle &&
            dropState != DragDropImportState::Completed &&
            dropState != DragDropImportState::Cancelled;
        const bool selectionPointerTracking =
            selectionInteraction_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool pencilV2Active = usePencilViewportInteractionV2_ &&
            voxelToolState_.IsPencilActive() &&
            toolContext_.Smart.IsOperational() &&
            toolContext_.Smart.Geometry() == SmartGeometry::Pencil;
        const bool interactionV2PointerTracking =
            ((useViewportInteractionV2_ && viewportInteractionV2_.OwnsPointer()) ||
             (pencilV2Active && pencilViewportInteractionV2_.OwnsPointer())) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool gizmoPointerTracking =
            transformGizmoManager_.IsDragging() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        // ImGui keeps the viewport item active after MouseDown. A Smart Tool
        // stroke owns that same pointer until MouseUp, so it must be treated
        // like selection/gizmo tracking rather than as an unrelated UI edit.
        const bool smartStrokePointerTracking = !pencilV2Active &&
            smartToolStroke_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool inputBlocked =
            (ImGui::IsAnyItemActive() && !selectionPointerTracking &&
             !interactionV2PointerTracking &&
             !gizmoPointerTracking && !smartStrokePointerTracking) ||
            io.WantTextInput || incompatiblePopupOpen;
        const bool smartBrushOptionsChanged = std::exchange(
            smartBrushPreviewRefreshRequested_, false);
        const SmartBrushSizeInputResult brushSizeInput =
            EditorInputService::ResolveSmartBrushSize({
                io.MouseWheel,
                ImGui::IsKeyDown(ImGuiKey_LeftCtrl),
                ImGui::IsKeyDown(ImGuiKey_RightCtrl),
                voxelDocumentSession_.HasActiveDocument(),
                voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational(),
                imageHovered,
                sceneFocused,
                inputBlocked || (io.WantCaptureMouse && !imageHovered),
                incompatiblePopupOpen,
                dragDropActive,
                selectionPointerTracking || gizmoPointerTracking ||
                    smartStrokePointerTracking || cameraControl,
                toolContext_.Smart.Brush().Size});
        if (brushSizeInput.Changed)
        {
            toolContext_.Smart.Brush().Size = brushSizeInput.Size;
            smartBrushSizeFeedback_.Rearm(brushSizeInput.Size,
                toolContext_.Smart.Brush().Shape, toolContext_.Smart.Action(),
                static_cast<std::uint64_t>(ImGui::GetTime() * 1000.0));
        }
        bool selectionInputAvailable = imageHovered && sceneFocused &&
            !inputBlocked && !cameraControl;
        const bool stampPreviewConsumesPointer = selectionInputAvailable &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (stampPreviewConsumesPointer)
        {
            PlaceLatestStampPreview();
            selectionInputAvailable = false;
        }
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const auto previousWorkplaneHit = workplaneHit_;
        workplaneHit_.reset();
        std::optional<VoxelRay> viewportRay;
        std::optional<VoxelRaycastHit> hoveredHit;
        VoxelPickingInteractionState pickingState =
            VoxelPickingInteractionState::Unavailable;
        if (!imageHovered)
            pickingState = VoxelPickingInteractionState::OutsideViewport;
        else if (inputBlocked || dragDropActive || !sceneFocused)
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
                viewportRay = ray.Ray;
                VoxelRaycastOptions options;
                options.Transform =
                    CenteredVoxelModelTransform(voxelModelCenter_);
                hoveredHit = RaycastVoxelDocument(
                    *document, *viewportRay, options);
                if (!hoveredHit &&
                    (voxelToolState_.IsPencilActive() ||
                     voxelToolState_.IsBoxActive() ||
                     voxelToolState_.IsLineActive() ||
                     voxelToolState_.IsSphereActive() ||
                     voxelToolState_.IsSelectionActive()))
                {
                    workplaneHit_ = workplaneService_.Intersect(
                        *document, 0U, *viewportRay, voxelModelCenter_);
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
            previousWorkplaneHit != workplaneHit_ || brushSizeInput.Changed ||
            smartBrushOptionsChanged)
        {
            UpdateVoxelHighlights();
        }
        TransformGizmoPointerInput gizmoPointerInput;
        gizmoPointerInput.ScreenPosition = {io.MousePos.x, io.MousePos.y};
        gizmoPointerInput.Viewport = currentViewportRectangle_;
        gizmoPointerInput.ViewProjection = viewportCamera_.GetViewProjection();
        gizmoPointerInput.Ray = viewportRay;
        const bool gizmoToolAvailable = !useViewportInteractionV2_ &&
            (voxelToolState_.IsMoveActive() && CanMoveSelection()) ||
            (voxelToolState_.IsRotateActive() && CanRotateSelection()) ||
            (voxelToolState_.IsScaleActive() && CanScaleSelection());
        const TransformGizmoRuntimeContext gizmoContext{
            document != nullptr,
            document != nullptr && voxelDocumentSession_.Generation() != 0U,
            !selectionService_.Empty() &&
                selectionService_.EditableBounds().Valid,
            gizmoToolAvailable,
            sceneFocused && currentViewportRectangle_.Width > 0.0F &&
                currentViewportRectangle_.Height > 0.0F,
            imageHovered,
            inputBlocked,
            cameraControl,
            dragDropActive,
            closeRequest_.State() != EditorCloseRequestState::None,
            document == nullptr || !transformPreviewModel_.IsActive() ||
                transformPreviewModel_.IsValidFor(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation()),
            voxelToolState_.ActiveTool(),
            voxelDocumentSession_.Generation(),
            selectionService_.EditableBounds()};
        const TransformGizmoCancellation gizmoContextCancellation =
            transformGizmoManager_.UpdateContext(gizmoContext);
        if (gizmoContextCancellation)
            CancelTransformGizmoInteraction();
        const bool gizmoInputAvailable =
            transformGizmoManager_.CanBeginInteraction();
        if (!transformGizmoManager_.IsDragging())
            static_cast<void>(
                transformGizmoManager_.UpdateHover(gizmoPointerInput));
        const bool gizmoAxisHovered =
            transformGizmoManager_.HoveredAxis() !=
            TransformGizmoAxis::None;
        if (transformGizmoManager_.CursorRecommendation() ==
            TransformGizmoCursorRecommendation::ResizeAll)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        bool gizmoCaptured = false;
        if (!stampPreviewConsumesPointer && gizmoInputAvailable && gizmoAxisHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            gizmoCaptured =
                transformGizmoManager_.BeginInteraction(gizmoPointerInput);
            if (gizmoCaptured &&
                (voxelToolState_.IsMoveActive() ||
                 voxelToolState_.IsScaleActive()))
                gizmoCaptured = transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation());
            if (!gizmoCaptured)
                CancelTransformGizmoInteraction();
            else
            {
                voxelMoveStatusMessage_.clear();
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        if (transformGizmoManager_.IsDragging() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && document &&
            transformGizmoManager_.UpdateInteraction(gizmoPointerInput))
        {
            if (transformGizmoManager_.Mode() == TransformGizmoMode::Move)
                static_cast<void>(transformPreviewModel_.SetDelta(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation(),
                    ConstrainMoveDelta(transformGizmoManager_.Delta())));
            else if (transformGizmoManager_.Mode() ==
                     TransformGizmoMode::Rotate)
            {
                const TransformGizmoAxis axis =
                    transformGizmoManager_.ActiveAxis();
                const VoxelRotationAxis rotationAxis =
                    axis == TransformGizmoAxis::X ? VoxelRotationAxis::X :
                    axis == TransformGizmoAxis::Y ? VoxelRotationAxis::Y :
                    VoxelRotationAxis::Z;
                const std::int32_t turns =
                    transformGizmoManager_.QuarterTurns();
                if ((turns % 4) == 0)
                    static_cast<void>(transformPreviewModel_.CancelPreview());
                else
                    static_cast<void>(BeginVoxelRotatePreview(
                        rotationAxis, turns));
            }
            else
            {
                const TransformGizmoAxis axis =
                    transformGizmoManager_.ActiveAxis();
                const VoxelScaleMode scaleMode =
                    axis == TransformGizmoAxis::X ? VoxelScaleMode::X :
                    axis == TransformGizmoAxis::Y ? VoxelScaleMode::Y :
                    VoxelScaleMode::Z;
                static_cast<void>(UpdateVoxelScalePreview(
                    scaleMode,
                    transformGizmoManager_.TargetDimensions()));
            }
            UpdateVoxelHighlights();
        }
        if (transformGizmoManager_.IsDragging() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            CancelTransformGizmoInteraction();
        const bool gizmoConsumesPointer = gizmoCaptured || gizmoAxisHovered ||
            transformGizmoManager_.IsDragging();
        const bool interactionV2ToolActive = useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive());
        if (interactionV2ToolActive)
        {
            InteractionV2::ViewportInputFrame interactionInput;
            interactionInput.Frame =
                static_cast<std::uint64_t>(ImGui::GetFrameCount());
            interactionInput.MouseScreen = {io.MousePos.x, io.MousePos.y};
            interactionInput.PrimaryPressed =
                ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            interactionInput.PrimaryHeld =
                ImGui::IsMouseDown(ImGuiMouseButton_Left);
            interactionInput.PrimaryReleased =
                ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            interactionInput.EscapePressed =
                ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            interactionInput.Control = io.KeyCtrl;
            interactionInput.Shift = io.KeyShift;
            interactionInput.ViewportHovered = imageHovered ||
                viewportInteractionV2_.OwnsPointer();
            interactionInput.ViewportFocused = sceneFocused;
            interactionInput.UiCapturesPointer = inputBlocked &&
                !viewportInteractionV2_.OwnsPointer();
            interactionInput.CameraActive = cameraControl;
            interactionInput.SelectionToolActive =
                voxelToolState_.IsSelectionActive();
            interactionInput.MoveToolActive =
                voxelToolState_.IsMoveActive();
            interactionInput.Viewport = currentViewportRectangle_;
            interactionInput.ViewProjection =
                viewportCamera_.GetViewProjection();
            interactionInput.CameraWorldPosition =
                viewportCamera_.GetPosition();
            interactionInput.ModelCenter = voxelModelCenter_;
            interactionInput.FramebufferScale =
                std::max(1.0F, io.DisplayFramebufferScale.x);
            interactionInput.DocumentGeneration =
                voxelDocumentSession_.Generation();
            interactionInput.DocumentRevision =
                document != nullptr ? document->GetRevision() : 0U;
            interactionInput.PointerRay = viewportRay;
            viewportInteractionV2_.SubmitInput(std::move(interactionInput));
            viewportInteractionV2_.Tick(
                document,
                selectionService_);
            CommitViewportInteractionV2Move();

            const InteractionV2::ViewportPresentation& presentation =
                viewportInteractionV2_.Presentation();
            const std::optional<SelectionBounds> renderedBounds =
                presentation.Phase == InteractionV2::InteractionPhase::Selecting
                ? std::nullopt : presentation.SelectionBox;
            viewportRenderer_.ConfigureInteractionV2(
                presentation.SelectionDetail, renderedBounds,
                presentation.MovePreview, voxelModelCenter_,
                presentation.Revision, true);
            viewportInteractionV2_.SetPresentationGpuMetrics(
                viewportRenderer_.InteractionV2UploadCount(),
                viewportRenderer_.InteractionV2BufferRecreationCount(),
                viewportRenderer_.InteractionV2UploadedBytes(),
                viewportRenderer_.InteractionV2MoveSourceUploadCount(),
                viewportRenderer_.InteractionV2MoveSourceUploadedBytes(),
                viewportRenderer_.InteractionV2MoveDeltaUpdateCount());
            DrawViewportInteractionV2Overlay();
        }
        else if (useViewportInteractionV2_)
        {
            viewportInteractionV2_.Reset();
            viewportRenderer_.ConfigureInteractionV2(
                {}, std::nullopt, nullptr, voxelModelCenter_, 0U, false);
        }
        if (pencilV2Active)
        {
            InteractionV2::PencilViewportInputFrame pencilInput;
            pencilInput.Frame = static_cast<std::uint64_t>(ImGui::GetFrameCount());
            pencilInput.PrimaryPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            pencilInput.PrimaryHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            pencilInput.PrimaryReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            pencilInput.EscapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            pencilInput.PencilToolActive = true;
            pencilInput.Interaction = {imageHovered ||
                    pencilViewportInteractionV2_.OwnsPointer(),
                sceneFocused, inputBlocked && !pencilViewportInteractionV2_.OwnsPointer(),
                cameraControl, !sceneFocused};
            if (const std::optional<PencilCompactRequest> request =
                    BuildPencilCompactRequest())
            {
                pencilInput.Request = *request;
                pencilInput.Target = request->Placement.Target;
            }
            pencilViewportInteractionV2_.SubmitInput(std::move(pencilInput));
            pencilViewportInteractionV2_.Tick(document);
            CommitPencilViewportInteractionV2();
            const InteractionV2::PencilCompactPresentation& presentation =
                pencilViewportInteractionV2_.Presentation();
            if (pencilV2RenderedPresentationRevision_ != presentation.Revision)
                UpdateVoxelHighlights();
            // Pencil V2 deliberately reuses the existing highlight renderer.
            // These are observed renderer counters only: the controller never
            // asks the renderer to calculate a footprint or materialize a
            // compact preview.  There is currently no dedicated Pencil V2
            // GPU buffer, so unavailable source/delta counters stay zero.
            pencilViewportInteractionV2_.SetRendererMetrics(
                viewportRenderer_.HighlightUploadCount(), 0U,
                viewportRenderer_.HighlightRenderCount(), 0U, 0U);
        }
        else if (usePencilViewportInteractionV2_)
        {
            pencilViewportInteractionV2_.Reset();
            pencilV2PreviewPositions_.clear();
            pencilV2RenderedPresentationRevision_ = 0U;
        }
        const bool doubleClickFocus = !useViewportInteractionV2_ &&
            !pencilV2Active &&
            !gizmoConsumesPointer &&
            selectionInputAvailable &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            voxelSelection_.Hovered().has_value();
        if (doubleClickFocus)
        {
            const auto& hit = *voxelSelection_.Hovered();
            const Asset::Voxel::VoxelPosition position{
                static_cast<std::int32_t>(hit.Coordinates.X),
                static_cast<std::int32_t>(hit.Coordinates.Y),
                static_cast<std::int32_t>(hit.Coordinates.Z)};
            static_cast<void>(selectionService_.Select(position));
            UpdateVoxelHighlights();
            static_cast<void>(viewportNavigation_.FocusSelection(
                SelectionNavigationBounds()));
        }
        std::optional<SelectionBoxRayHit> hoveredSelectionInterior;
        if (!useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive() ||
             voxelToolState_.IsDuplicateActive()) &&
            selectionInteraction_.Mode() == SelectionInteractionMode::Idle &&
            selectionService_.EditableBounds().Valid &&
            selectionInputAvailable && viewportRay && !gizmoConsumesPointer &&
            !hoveredSelectionHandle)
        {
            std::optional<float> occluderDistance;
            if (const auto& voxelHit = voxelSelection_.Hovered())
            {
                const Asset::Voxel::VoxelPosition hitPosition{
                    static_cast<std::int32_t>(voxelHit->Coordinates.X),
                    static_cast<std::int32_t>(voxelHit->Coordinates.Y),
                    static_cast<std::int32_t>(voxelHit->Coordinates.Z)};
                if (!selectionService_.EditableBounds().Contains(hitPosition))
                    occluderDistance = voxelHit->Distance;
            }
            hoveredSelectionInterior = PickSelectionBoxInterior(
                selectionService_.EditableBounds(), voxelModelCenter_,
                *viewportRay, occluderDistance);
        }
        const bool interiorHovered = hoveredSelectionInterior.has_value();
        if (selectionBoxInteriorHovered_ != interiorHovered)
        {
            selectionBoxInteriorHovered_ = interiorHovered;
            UpdateVoxelHighlights();
        }
        if ((interiorHovered || selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingBox ||
             selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingContent ||
             selectionInteraction_.Mode() ==
                SelectionInteractionMode::DuplicatingContent) &&
            !hoveredSelectionHandle)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        const char* viewportHelp = "Click a voxel to select it";
        if (voxelToolState_.IsSelectionActive())
        {
            if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::Creating)
                viewportHelp = "Drag to size selection box — Release to validate";
            else if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::ResizingFace)
                viewportHelp = "Resize selection box — Release to validate — Esc to cancel";
            else if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingBox)
                viewportHelp = "Move selection box — Release to validate — Esc to cancel";
            else if (hoveredSelectionHandle)
                viewportHelp = "Drag to resize this face";
            else if (interiorHovered)
                viewportHelp = "Drag to move the selection box";
            else if (selectionService_.EditableBounds().Valid)
                viewportHelp = "Drag a face handle to resize — Drag inside to move the selection box";
            else
                viewportHelp = "Click-drag to draw a selection box — Click to select one voxel";
        }
        else if (voxelToolState_.IsMoveActive())
        {
            if (transformGizmoManager_.IsDragging())
            {
                const std::string_view gizmoHelp =
                    transformGizmoManager_.HelpText();
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Move blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Move blocked: destination is outside the model"
                    : !gizmoHelp.empty()
                    ? gizmoHelp.data()
                    : "Moving — Release to apply — Esc to cancel";
            }
            else if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingContent)
            {
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Move blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Move blocked: destination is outside the model"
                    : "Move voxels — Release to validate — Esc to cancel";
            }
            else
            {
                const std::string_view gizmoHelp =
                    transformGizmoManager_.HelpText();
                viewportHelp = !gizmoHelp.empty()
                    ? gizmoHelp.data()
                    : !voxelMoveStatusMessage_.empty()
                    ? voxelMoveStatusMessage_.c_str()
                    : CanMoveSelection()
                    ? "Drag an axis or drag inside the selection to move its voxels"
                    : "Select voxels before using Move";
            }
        }
        else if (voxelToolState_.IsDuplicateActive())
        {
            if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::DuplicatingContent)
            {
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Duplicate blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Duplicate blocked: destination is outside the model"
                    : "Duplicate voxels — Release to validate — Esc to cancel";
            }
            else
            {
                viewportHelp = !voxelDuplicateStatusMessage_.empty()
                    ? voxelDuplicateStatusMessage_.c_str()
                    : CanDuplicateSelection()
                    ? "Drag inside the selection to duplicate its voxels"
                    : "Select voxels before using Duplicate";
            }
        }
        else if (voxelToolState_.IsRotateActive())
        {
            const std::string_view gizmoHelp =
                transformGizmoManager_.HelpText();
            viewportHelp = transformPreviewModel_.HasCollisions()
                ? "Rotate blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Rotate blocked: destination is outside the model"
                : !gizmoHelp.empty()
                ? gizmoHelp.data()
                : !voxelRotateStatusMessage_.empty()
                ? voxelRotateStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Drag a rotation ring — Q left — Shift+Q right — Esc to exit"
                : voxelRotateDirection_ == VoxelRotationDirection::Clockwise
                ? "Rotate Y: +90° — Enter to apply — Esc to cancel"
                : "Rotate Y: -90° — Enter to apply — Esc to cancel";
        }
        else if (voxelToolState_.IsMirrorActive())
        {
            viewportHelp = !voxelMirrorStatusMessage_.empty()
                ? voxelMirrorStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Choose X or Z to preview a mirror"
                : transformPreviewModel_.HasCollisions()
                ? "Mirror blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Mirror blocked: destination is outside the model"
                : voxelMirrorAxis_ == VoxelMirrorAxis::X
                ? "Mirror X — Enter to apply — Esc to cancel"
                : "Mirror Z — Enter to apply — Esc to cancel";
        }
        else if (voxelToolState_.IsScaleActive())
        {
            const std::string_view gizmoHelp =
                transformGizmoManager_.HelpText();
            viewportHelp = !voxelScaleStatusMessage_.empty()
                ? voxelScaleStatusMessage_.c_str()
                : transformPreviewModel_.HasCollisions()
                ? "Scale blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Scale blocked: destination is outside the model"
                : !gizmoHelp.empty()
                ? gizmoHelp.data()
                : !transformPreviewModel_.IsActive()
                ? "Choose X, Y, Z or U to preview Scale x2"
                : voxelScaleMode_ == VoxelScaleMode::Uniform
                ? "Scale Uniform x2 - Enter to apply - Esc to cancel"
                : voxelScaleMode_ == VoxelScaleMode::X
                ? "Scale X x2 - Enter to apply - Esc to cancel"
                : voxelScaleMode_ == VoxelScaleMode::Y
                ? "Scale Y x2 - Enter to apply - Esc to cancel"
                : "Scale Z x2 - Enter to apply - Esc to cancel";
        }
        else if (voxelToolState_.IsAlignActive())
        {
            viewportHelp = !voxelAlignStatusMessage_.empty()
                ? voxelAlignStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Choose Left, Right, Bottom, Top, Front or Back"
                : transformPreviewModel_.HasCollisions()
                ? "Align blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Align blocked: destination is outside the model"
                : "Align preview - Enter to apply - Esc to cancel";
        }
        DrawTooltip(viewportHelp);
        if (voxelToolState_.IsSelectionActive() ||
            voxelToolState_.IsMoveActive() ||
            voxelToolState_.IsDuplicateActive() ||
            voxelToolState_.IsRotateActive() ||
            voxelToolState_.IsMirrorActive() ||
            voxelToolState_.IsScaleActive() ||
            voxelToolState_.IsAlignActive())
        {
            const ImVec2 textSize = ImGui::CalcTextSize(viewportHelp);
            const ImVec2 helpMinimum{imageOrigin.x + 10.0F, imageOrigin.y + 10.0F};
            const ImVec2 helpMaximum{
                helpMinimum.x + textSize.x + 16.0F,
                helpMinimum.y + textSize.y + 10.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                helpMinimum, helpMaximum, IM_COL32(8, 13, 20, 210), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                helpMinimum, helpMaximum, IM_COL32(55, 220, 190, 230), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {helpMinimum.x + 8.0F, helpMinimum.y + 5.0F},
                IM_COL32(225, 245, 242, 255), viewportHelp);
        }
        if (voxelToolState_.IsBoxActive() && voxelBoxInteraction_.IsActive() &&
            voxelBoxInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        if (voxelToolState_.IsLineActive() && voxelLineInteraction_.IsActive() &&
            voxelLineInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        if (voxelToolState_.IsSphereActive() &&
            voxelSphereInteraction_.IsActive() &&
            voxelSphereInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        const VoxelToolInputDecision toolDecision =
            voxelToolInput_.Update({
                ImGui::IsMouseDown(ImGuiMouseButton_Left),
                voxelToolState_.IsEditingToolActive(),
                document != nullptr,
                imageHovered,
                sceneFocused,
                (io.WantCaptureMouse && (!imageHovered || inputBlocked)) ||
                    gizmoConsumesPointer,
                incompatiblePopupOpen,
                dragDropActive,
                cameraInteraction,
                voxelEditInProgress_,
                voxelDocumentSession_.Generation()});
        const bool smartContinuousTool = !pencilV2Active &&
            voxelToolState_.IsPencilActive() &&
            toolContext_.Smart.IsOperational() &&
            toolContext_.Smart.Geometry() != SmartGeometry::Fill &&
            (toolContext_.Smart.Action() == SmartAction::Add ||
             toolContext_.Smart.Action() == SmartAction::Paint ||
             toolContext_.Smart.Action() == SmartAction::Erase);
        const bool smartStrokeMayContinue = document != nullptr && imageHovered &&
            sceneFocused && !((io.WantCaptureMouse && (!imageHovered || inputBlocked)) ||
                gizmoConsumesPointer) && !incompatiblePopupOpen && !dragDropActive &&
            cameraInteraction == VoxelCameraInteraction::None && !voxelEditInProgress_;
        // A continuous stroke is valid only while its original interaction
        // context remains intact. Leaving the viewport, losing focus, a
        // popup, drag/drop, camera interaction, tool or action change must
        // cancel (not commit) the pending atomic edit. An invalid voxel
        // target is handled inside ContinueSmartToolStroke() and merely
        // suspends the segment so a later valid target starts a new one.
        if (smartToolStroke_.IsActive() &&
            (!smartContinuousTool || !smartStrokeMayContinue ||
             toolContext_.Smart.Action() != smartToolStroke_.Action() ||
             (smartLineLockedStart_ &&
                 toolContext_.Smart.Geometry() != SmartGeometry::Line) ||
             (smartGeometryPlane_ &&
                 (toolContext_.Smart.Geometry() != SmartGeometry::Geometry ||
                  toolContext_.Smart.Mode() != smartGeometryLockedMode_ ||
                  toolContext_.Smart.Action() != smartGeometryLockedAction_)) ||
             (smartSurfacePlane_ &&
                 toolContext_.Smart.Geometry() != SmartGeometry::Surface)))
            CancelSmartToolStroke();
        if (!doubleClickFocus && smartContinuousTool)
        {
            const bool cylinderHeightPhase = smartToolStroke_.IsActive() &&
                toolContext_.Smart.Geometry() == SmartGeometry::Geometry &&
                smartGeometryLockedMode_ == SmartToolMode::CylinderBrush &&
                smartGeometryPhase_ == SmartGeometryInteractionPhase::Height;
            if (cylinderHeightPhase &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                static_cast<void>(CommitSmartToolStroke());
                UpdateVoxelHighlights();
            }
            else if (cylinderHeightPhase)
            {
                if (smartStrokeMayContinue)
                {
                    constexpr float pixelsPerGeometryLayer = 24.0F;
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    smartGeometryHeight_ = ResolveSmartToolGeometryHeight(
                        smartGeometryHeightDragAxis_.value_or(
                            SmartToolFaceDepthDragAxis{}),
                        {mouse.x - smartGeometryHeightStartMouse_.X,
                         mouse.y - smartGeometryHeightStartMouse_.Y},
                        MaximumSmartGeometryHeight, pixelsPerGeometryLayer);
                    if (ContinueSmartToolStroke())
                        UpdateVoxelHighlights();
                }
                else CancelSmartToolStroke();
            }
            else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (smartToolStroke_.IsActive())
                {
                    if (toolContext_.Smart.Geometry() ==
                            SmartGeometry::Geometry &&
                        smartGeometryLockedMode_ ==
                            SmartToolMode::CylinderBrush &&
                        smartGeometryEndpointValid_)
                    {
                        smartGeometryPhase_ =
                            SmartGeometryInteractionPhase::Height;
                        const ImVec2 mouse = ImGui::GetIO().MousePos;
                        smartGeometryHeightStartMouse_ = {mouse.x, mouse.y};
                        smartGeometryHeight_ = 1;
                    }
                    else
                        static_cast<void>(CommitSmartToolStroke());
                    UpdateVoxelHighlights();
                }
            }
            else if (!smartToolStroke_.IsActive())
            {
                if (toolDecision == VoxelToolInputDecision::Apply &&
                    BeginSmartToolStroke())
                    UpdateVoxelHighlights();
            }
            else if (smartStrokeMayContinue)
            {
                if (smartToolStroke_.IsActive() &&
                    toolContext_.Smart.Geometry() == SmartGeometry::Face &&
                    smartToolStroke_.Action() == SmartAction::Add)
                {
                    // The axis was projected once when the face was locked.
                    // Thus X/Z faces follow their visible screen direction,
                    // while camera motion cannot perturb a live extrusion.
                    constexpr float pixelsPerFaceLayer = 24.0F;
                    const ImVec2 drag = ImGui::GetMouseDragDelta(
                        ImGuiMouseButton_Left, 0.0F);
                    faceDepthLayers_ = ResolveSmartToolFaceDepthLayers(
                        faceDepthDragAxis_.value_or(SmartToolFaceDepthDragAxis{}),
                        {drag.x, drag.y}, 1, MaximumSmartToolBrushSize,
                        pixelsPerFaceLayer);
                }
                const bool previewPlanWasAvailable =
                    smartToolStrokePreviewPlan_ != nullptr;
                const bool strokeChanged = ContinueSmartToolStroke();
                if (strokeChanged ||
                    (previewPlanWasAvailable &&
                     smartToolStrokePreviewPlan_ == nullptr))
                    UpdateVoxelHighlights();
            }
            else CancelSmartToolStroke();
        }
        else if (!pencilV2Active && !doubleClickFocus &&
                 toolDecision == VoxelToolInputDecision::Apply)
        {
            if (voxelToolState_.IsPencilActive())
            {
                if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Geometry() == SmartGeometry::Fill)
                    static_cast<void>(ApplySmartFill());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Add)
                    static_cast<void>(ApplyVoxelPencil());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Erase)
                    static_cast<void>(ApplyVoxelPencil());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Paint)
                    static_cast<void>(ApplyVoxelPencil());
            }
            else if (voxelToolState_.IsEraserActive())
            {
                toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
                toolContext_.Smart.SetAction(SmartAction::Erase);
                SelectVoxelTool(ActiveVoxelTool::Pencil);
                static_cast<void>(ApplyVoxelPencil());
            }
            else if (voxelToolState_.IsFillActive())
                static_cast<void>(ApplyVoxelPaintBrush());
            else if (voxelToolState_.IsBoxActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelBoxInteraction_.IsActive())
                    {
                        static_cast<void>(voxelBoxInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelBoxInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelBox());
                    }
                }
            }
            else if (voxelToolState_.IsLineActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelLineInteraction_.IsActive())
                    {
                        static_cast<void>(voxelLineInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelLineInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelLine());
                    }
                }
            }
            else if (voxelToolState_.IsSphereActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelSphereInteraction_.IsActive())
                    {
                        static_cast<void>(voxelSphereInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelSphereInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelSphere());
                    }
                }
            }
        }

        if (!useViewportInteractionV2_)
        {
        const SelectionPointerTarget selectionPointerTarget =
            ResolveSelectionPointerTarget(
                hoveredSelectionHandle.has_value(), interiorHovered);
        bool selectionHandleCaptured = false;
        if (voxelToolState_.IsSelectionActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Handle &&
            hoveredSelectionHandle &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            selectionHandleCaptured = selectionInteraction_.BeginResizingFace(
                hoveredSelectionHandle->Face,
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(),
                io.MousePos.x, io.MousePos.y,
                hoveredSelectionHandle->ScreenAxisPerVoxel);
            if (selectionHandleCaptured)
            {
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        bool selectionBoxCaptured = false;
        if (!selectionHandleCaptured && voxelToolState_.IsSelectionActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            selectionBoxCaptured = selectionInteraction_.BeginMovingBox(
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(), movePlane,
                hoveredSelectionInterior->WorldPosition);
            if (selectionBoxCaptured)
            {
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        bool voxelMoveCaptured = false;
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            voxelToolState_.IsMoveActive() && selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay && document &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            voxelMoveCaptured = selectionInteraction_.BeginMovingContent(
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(), movePlane,
                hoveredSelectionInterior->WorldPosition) &&
                transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation());
            if (!voxelMoveCaptured)
            {
                static_cast<void>(selectionInteraction_.Cancel());
                static_cast<void>(transformPreviewModel_.CancelPreview());
            }
            else
            {
                voxelMoveStatusMessage_.clear();
                UpdateVoxelHighlights();
            }
        }
        bool voxelDuplicateCaptured = false;
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            !voxelMoveCaptured && voxelToolState_.IsDuplicateActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay && document &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            voxelDuplicateCaptured =
                selectionInteraction_.BeginDuplicatingContent(
                    selectionService_.EditableBounds(),
                    voxelDocumentSession_.Generation(), movePlane,
                    hoveredSelectionInterior->WorldPosition) &&
                transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation(), 0U,
                    TransformPreviewCollisionPolicy::IncludeSource);
            if (!voxelDuplicateCaptured)
            {
                static_cast<void>(selectionInteraction_.Cancel());
                static_cast<void>(transformPreviewModel_.CancelPreview());
            }
            else
            {
                voxelDuplicateStatusMessage_.clear();
                UpdateVoxelHighlights();
            }
        }
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            !voxelMoveCaptured && !voxelDuplicateCaptured &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.ActiveTool() == ActiveVoxelTool::None) &&
            selectionInputAvailable &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            voxelSelectionClickCandidate_ = !cameraControl &&
                !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            selectionPointerAnchor_ = CurrentSelectionTarget();
            selectionPointerMode_ = io.KeyCtrl && io.KeyShift
                ? SelectionMode::Intersect
                : io.KeyCtrl ? SelectionMode::Subtract
                : io.KeyShift ? SelectionMode::Add
                              : SelectionMode::Replace;
            if (voxelSelectionClickCandidate_ &&
                voxelToolState_.IsSelectionActive())
            {
                if (selectionInteraction_.PointerDown(
                        selectionPointerAnchor_,
                        voxelDocumentSession_.Generation(),
                        selectionPointerMode_, io.MousePos.x, io.MousePos.y))
                    UpdateVoxelHighlights();
            }
        }
        if (selectionInteraction_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            std::optional<Asset::Voxel::VoxelDimensions> dimensions;
            if (document) dimensions = document->GetDimensions(0U);
            bool selectionChanged = false;
            if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingBox &&
                viewportRay && dimensions)
            {
                const auto pointerWorld = IntersectSelectionMovePlane(
                    *viewportRay, selectionInteraction_.MovePlane());
                selectionChanged = pointerWorld &&
                    selectionInteraction_.MoveBox(
                        *pointerWorld, *dimensions);
            }
            else if ((selectionInteraction_.Mode() ==
                          SelectionInteractionMode::MovingContent ||
                      selectionInteraction_.Mode() ==
                          SelectionInteractionMode::DuplicatingContent) &&
                     viewportRay && document)
            {
                const auto pointerWorld = IntersectSelectionMovePlane(
                    *viewportRay, selectionInteraction_.MovePlane());
                selectionChanged = pointerWorld &&
                    selectionInteraction_.MoveContent(*pointerWorld);
                if (selectionChanged)
                {
                    Asset::Voxel::VoxelPosition delta =
                        selectionInteraction_.MoveDelta();
                    if (selectionInteraction_.Mode() ==
                        SelectionInteractionMode::MovingContent)
                        delta = ConstrainMoveDelta(delta);
                    static_cast<void>(transformPreviewModel_.SetDelta(
                        *document, selectionService_,
                        voxelDocumentSession_.Generation(),
                        delta));
                }
            }
            else
            {
                selectionChanged = selectionInteraction_.PointerMove(
                    io.MousePos.x, io.MousePos.y,
                    CurrentSelectionTarget(), dimensions);
            }
            if (selectionChanged)
            {
                if (selectionInteraction_.Mode() ==
                        SelectionInteractionMode::ResizingFace ||
                    selectionInteraction_.Mode() ==
                        SelectionInteractionMode::MovingBox)
                    static_cast<void>(ApplySelectionBounds(
                        selectionInteraction_.CurrentBounds(),
                        SelectionMode::Replace));
                else
                    UpdateVoxelHighlights();
            }
        }
        if (voxelSelectionClickCandidate_ &&
            (!sceneFocused || io.WantTextInput))
            voxelSelectionClickCandidate_ = false;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const TransformGizmoDragRelease gizmoRelease =
                transformGizmoManager_.EndInteraction();
            if (gizmoRelease.WasDragging)
            {
                if (gizmoRelease.Mode == TransformGizmoMode::Rotate)
                {
                    if ((gizmoRelease.QuarterTurns % 4) == 0)
                        static_cast<void>(
                            transformPreviewModel_.CancelPreview());
                    else
                        static_cast<void>(ApplyVoxelRotate());
                }
                else if (gizmoRelease.Mode == TransformGizmoMode::Scale)
                {
                    if (gizmoRelease.Delta ==
                            Asset::Voxel::VoxelPosition{})
                        static_cast<void>(
                            transformPreviewModel_.CancelPreview());
                    else
                        static_cast<void>(ApplyVoxelScale());
                }
                else if (gizmoRelease.Delta ==
                             Asset::Voxel::VoxelPosition{})
                    static_cast<void>(transformPreviewModel_.CancelPreview());
                else
                    static_cast<void>(ApplyVoxelMove());
                UpdateVoxelHighlights();
            }
            else
            {
                const SelectionPointerRelease release =
                    selectionInteraction_.PointerUp();
                if (release.WasDrag && release.Bounds)
                {
                    if (release.Mode == SelectionInteractionMode::Creating)
                        static_cast<void>(ApplySelectionBounds(
                            *release.Bounds, release.Operation));
                    else if (release.Mode ==
                             SelectionInteractionMode::MovingContent)
                        static_cast<void>(ApplyVoxelMove());
                    else if (release.Mode ==
                             SelectionInteractionMode::DuplicatingContent)
                        static_cast<void>(ApplyVoxelDuplicate());
                    else
                        UpdateVoxelHighlights();
                }
                else
                {
                    bool changed = false;
                    if (voxelSelectionClickCandidate_ &&
                        voxelToolState_.IsSelectionActive())
                    {
                        if (const auto& hit = voxelSelection_.Hovered())
                        {
                            const Asset::Voxel::VoxelPosition position{
                                static_cast<std::int32_t>(hit->Coordinates.X),
                                static_cast<std::int32_t>(hit->Coordinates.Y),
                                static_cast<std::int32_t>(hit->Coordinates.Z)};
                            changed = selectionService_.Select(
                                position, selectionPointerMode_);
                        }
                        else if (!io.KeyCtrl && !io.KeyShift)
                        {
                            changed = selectionService_.Clear();
                        }
                    }
                    else if (voxelSelectionClickCandidate_)
                    {
                        changed = voxelSelection_.SelectHovered();
                    }
                    if (changed) UpdateVoxelHighlights();
                }
            }
            voxelSelectionClickCandidate_ = false;
            selectionPointerAnchor_.reset();
        }
        }
        if (imageHovered && !transformGizmoManager_.IsDragging())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                viewportNavigation_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                viewportNavigation_.Pan(
                    io.MouseDelta.x, io.MouseDelta.y, available.y);
            if (io.MouseWheel != 0.0F && !brushSizeInput.ConsumeWheel)
                viewportNavigation_.Zoom(io.MouseWheel);
        }
        const std::uint64_t feedbackNow = static_cast<std::uint64_t>(
            ImGui::GetTime() * 1000.0);
        if (smartBrushGhostPreview_ != nullptr &&
            !smartBrushGhostPreview_->GhostVoxels.empty())
        {
            const SmartBrushState& state = toolContext_.Smart.Brush();
            const char* const shape = state.Shape == SmartBrushShape::Sphere
                ? "Sphere" : "Cube";
            const char* const action = toolContext_.Smart.Action() ==
                    SmartAction::Erase ? "Erase"
                : toolContext_.Smart.Action() == SmartAction::Paint
                    ? "Paint" : "Add";
            const SmartToolPlanStatistics& ghostStatistics =
                smartBrushGhostPreview_->Statistics;
            const std::string label = std::string(shape) + " / " + action +
                " / Size " + std::to_string(state.Size) + "\n" +
                "Total " + std::to_string(ghostStatistics.Total) +
                "  Affected " + std::to_string(ghostStatistics.Changed) +
                "  Ignored " + std::to_string(ghostStatistics.Unchanged) +
                "  Clipped " + std::to_string(ghostStatistics.Clipped);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 minimum{imageOrigin.x + 12.0F,
                imageOrigin.y + available.y - textSize.y - 18.0F};
            const ImVec2 maximum{minimum.x + textSize.x + 16.0F,
                minimum.y + textSize.y + 12.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                minimum, maximum, IM_COL32(8, 13, 20, 220), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                minimum, maximum, IM_COL32(105, 188, 238, 235), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {minimum.x + 8.0F, minimum.y + 6.0F},
                IM_COL32(232, 244, 255, 255), label.c_str());
        }
        if (smartBrushSizeFeedback_.IsVisible(feedbackNow))
        {
            const char* const shape = smartBrushSizeFeedback_.Shape() ==
                    SmartBrushShape::Sphere ? "Sphere" : "Cube";
            const char* const action = smartBrushSizeFeedback_.Action() ==
                    SmartAction::Erase ? "Erase"
                : smartBrushSizeFeedback_.Action() == SmartAction::Paint
                    ? "Paint" : "Add";
            const std::string label = "Brush Size: " + std::to_string(
                smartBrushSizeFeedback_.Size()) + "\n" + shape +
                " - " + action;
            const ImVec2 minimum{imageOrigin.x + 12.0F, imageOrigin.y + 42.0F};
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 maximum{minimum.x + textSize.x + 16.0F,
                minimum.y + textSize.y + 12.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                minimum, maximum, IM_COL32(8, 13, 20, 220), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                minimum, maximum, IM_COL32(70, 190, 235, 235), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {minimum.x + 8.0F, minimum.y + 6.0F},
                IM_COL32(232, 244, 255, 255), label.c_str());
        }
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
        if (cameraActions.FocusRequested)
            FocusSelectionOrFrameAll();
        if (cameraActions.ResetRequested)
            viewportCamera_.Reset();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            eraseRequested = true;
        if (shortcutsEnabled && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_A, false))
            addRequested = true;
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
    const Stamps::ForgeLibraryPanelResult result = forgeLibraryPanel_.Draw(
        &showForgeLibrary_, voxelDocumentSession_.ActiveDocument(),
        voxelDocumentSession_.Generation());
    if (result.SaveSelectionRequested)
        BeginSaveSelectionAsStamp();
    if (result.SessionActivated)
        UpdateVoxelHighlights();
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
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (!stampPlacementSession_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        stampPlacementSession_.TranslateTarget(
            x, y, z, *document, voxelDocumentSession_.Generation());
    if (!result.Succeeded)
    {
        AddConsoleMessage(
            "Live Stamp Preview: " +
            std::string(Stamps::StampPlacementDiagnosticMessage(
                result.Diagnostic)));
        return;
    }
    if (result.PreviewChanged)
    {
        UpdateVoxelHighlights();
    }

    const Stamps::StampPlacementPlan* const plan =
        stampPlacementSession_.CurrentPlan();
    AddConsoleMessage(plan != nullptr && plan->Statistics.OverlapCount != 0U
        ? "Live Stamp Preview: overlap is allowed."
        : "Live Stamp Preview: valid preview active.");
}

void EditorWorkspace::RotateLatestStampPreview(const bool clockwise)
{
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (!stampPlacementSession_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result = clockwise
        ? stampPlacementSession_.RotateClockwise(
              *document, voxelDocumentSession_.Generation())
        : stampPlacementSession_.RotateCounterClockwise(
              *document, voxelDocumentSession_.Generation());
    if (result.PreviewChanged)
    {
        UpdateVoxelHighlights();
    }
    if (!result.Succeeded)
    {
        AddConsoleMessage(
            "Live Stamp Preview: " +
            std::string(Stamps::StampPlacementDiagnosticMessage(
                result.Diagnostic)));
        return;
    }

    AddConsoleMessage(
        "Live Stamp Preview: rotation " +
        std::to_string(
            static_cast<unsigned int>(
                stampPlacementSession_.QuarterRotation()) *
            90U) +
        " degrees.");
}

void EditorWorkspace::MirrorLatestStampPreview(
    const Stamps::StampPlacementMirrorMode mirror)
{
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (!stampPlacementSession_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        stampPlacementSession_.SetMirror(
            mirror, *document, voxelDocumentSession_.Generation());
    if (result.PreviewChanged)
    {
        UpdateVoxelHighlights();
    }
    if (!result.Succeeded)
    {
        AddConsoleMessage(
            "Live Stamp Preview: " +
            std::string(Stamps::StampPlacementDiagnosticMessage(
                result.Diagnostic)));
        return;
    }

    const char* label = "None";
    switch (stampPlacementSession_.Mirror())
    {
    case Stamps::StampPlacementMirrorMode::X:
        label = "X";
        break;
    case Stamps::StampPlacementMirrorMode::Z:
        label = "Z";
        break;
    case Stamps::StampPlacementMirrorMode::XZ:
        label = "XZ";
        break;
    case Stamps::StampPlacementMirrorMode::None:
    default:
        break;
    }
    AddConsoleMessage(
        "Live Stamp Preview: mirror " + std::string(label) + ".");
}

void EditorWorkspace::PlaceLatestStampPreview()
{
    Asset::Voxel::VoxelDocument* const document = voxelDocumentSession_.ActiveDocument();
    const Stamps::StampPlacementPlan* plan =
        stampPlacementSession_.CurrentPlan();
    if (plan == nullptr || document == nullptr ||
        voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        return;
    }

    if (!stampPlacementSession_.IsCurrent(
            *document, voxelDocumentSession_.Generation()))
    {
        const Stamps::StampPlacementSessionResult refreshed =
            stampPlacementSession_.Rebuild(
                *document, voxelDocumentSession_.Generation());
        if (refreshed.PreviewChanged)
        {
            UpdateVoxelHighlights();
        }
        AddConsoleMessage(
            "Place Stamp: the document changed; preview refreshed. "
            "Click again to place.");
        return;
    }

    Stamps::PlaceVoxelStampPreparation prepared =
        Stamps::PreparePlaceVoxelStampOperation(*plan);
    if (!prepared.IsReady())
    {
        if (prepared.IsNoChange())
        {
            AddConsoleMessage("Place Stamp: preview already matches the document.");
            return;
        }
        AddConsoleMessage(
            "Place Stamp: " +
            std::string(Stamps::PlaceVoxelStampPreparationStatusMessage(prepared.Status)) +
            (prepared.Status == Stamps::PlaceVoxelStampPreparationStatus::PaletteMappingFailed
                ? " Palette: " +
                    std::string(Stamps::PaletteMappingStatusMessage(prepared.PaletteStatus))
                : ""));
        return;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        static_cast<VoxelEditSession&>(*this), std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        AddConsoleMessage("Place Stamp failed: " + result.Message);
        return;
    }

    stampPlacementSession_.MarkPlacementCommitted();
    static_cast<void>(RefreshLatestStampPreview());
    AddConsoleMessage("Placed Stamp: " + result.Label);
}

bool EditorWorkspace::RefreshLatestStampPreview()
{
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (!stampPlacementSession_.IsActive() || document == nullptr)
    {
        return false;
    }

    const Stamps::StampPlacementSessionResult result =
        stampPlacementSession_.Rebuild(
            *document, voxelDocumentSession_.Generation());
    if (result.PreviewChanged)
    {
        UpdateVoxelHighlights();
    }
    return result.Succeeded;
}

void EditorWorkspace::ClearLatestStampPreview() noexcept
{
    const bool changed = stampPlacementSession_.Cancel();
    if (changed)
    {
        UpdateVoxelHighlights();
    }
}

bool EditorWorkspace::RunStampLivePreviewVisualStep(const std::size_t)
{
    const Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr)
    {
        return false;
    }

    if (!stampLivePreviewVisualStartedAt_)
    {
        const Stamps::StampBounds bounds{{0, 0, 0}, {2, 0, 2}, {3U, 1U, 3U}};
        const Stamps::StampPivot pivot{
            .RequestedMode = Stamps::StampPivotMode::Center,
            .ResolvedMode = Stamps::StampPivotMode::Center,
            .LocalPosition = {0, 0, 0}};
        const std::vector<Stamps::StampPaletteEntry> palette{
            {0U, {255U, 82U, 82U, 255U}},
            {1U, {255U, 210U, 64U, 255U}},
            {2U, {88U, 188U, 255U, 255U}}};
        const std::vector<Stamps::StampVoxel> voxels{
            {{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}, {{2, 0, 0}, 2U},
            {{0, 0, 1}, 1U}, {{1, 0, 1}, 2U}, {{2, 0, 1}, 0U},
            {{0, 0, 2}, 2U}, {{1, 0, 2}, 0U}, {{2, 0, 2}, 1U}};
        Stamps::StampValidationResult validation{};
        const std::optional<Stamps::VoxelStamp> fixture =
            Stamps::VoxelStamp::TryCreate(
                {.Id = Core::UUID{0x5354414d503039ULL},
                 .ContentHash = "stamp-live-preview-visual-v1"},
                bounds,
                pivot,
                {},
                palette,
                voxels,
                Stamps::DefaultStampResourceLimits(),
                &validation);
        if (!fixture || !validation.IsValid())
        {
            AddConsoleMessage("Stamp live preview visual test: fixture creation failed.");
            return false;
        }

        // The viewport can still be settling its final docked extent on the
        // first visual-test frame. Frame generously so the fixture remains
        // wholly visible even in a maximized, high-DPI workspace.
        viewportCamera_.Frame(20.0F, 20.0F, 20.0F);
        viewportCamera_.SetView(EditorCameraView::Perspective);
        const Stamps::StampPlacementSessionResult previewResult =
            stampPlacementSession_.Begin(
                *fixture, *document, voxelDocumentSession_.Generation(),
                0U, {0, 0, 0});
        const VoxelPreviewData* const preview =
            stampPlacementSession_.CurrentPreview();
        stampLivePreviewVisualDocumentRevision_ = document->GetRevision();
        stampLivePreviewVisualValid_ = previewResult.Succeeded &&
            preview != nullptr && preview->IsActive() &&
            preview->State == VoxelPreviewState::Valid;
        if (stampLivePreviewVisualValid_)
        {
            UpdateVoxelHighlights();
        }

        stampLivePreviewVisualStartedAt_ = std::chrono::steady_clock::now();
        AddConsoleMessage(stampLivePreviewVisualValid_
            ? "Stamp live preview visual test: valid state."
            : "Stamp live preview visual test: valid state failed.");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - *stampLivePreviewVisualStartedAt_);
    if (elapsed >= std::chrono::seconds{12} && !stampLivePreviewVisualClear_)
    {
        ClearLatestStampPreview();
        stampLivePreviewVisualClear_ = stampLivePreviewVisualOverlap_ &&
            stampPlacementSession_.CurrentPreview() == nullptr &&
            document->GetRevision() == stampLivePreviewVisualDocumentRevision_ &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo();
        AddConsoleMessage(stampLivePreviewVisualClear_
            ? "Stamp live preview visual test: clear state."
            : "Stamp live preview visual test: clear state failed.");
    }
    else if (elapsed >= std::chrono::seconds{6} &&
             !stampLivePreviewVisualOverlap_ &&
             stampPlacementSession_.IsActive())
    {
        const Stamps::StampPlacementSessionResult previewResult =
            stampPlacementSession_.SetTarget(
                {0, Stamps::StampFixedPoint::UnitsPerVoxel, 0},
                *document, voxelDocumentSession_.Generation());
        const VoxelPreviewData* const preview =
            stampPlacementSession_.CurrentPreview();
        stampLivePreviewVisualOverlap_ = stampLivePreviewVisualValid_ &&
            previewResult.Succeeded && preview != nullptr &&
            preview->IsActive() && preview->State == VoxelPreviewState::Overlap &&
            document->GetRevision() == stampLivePreviewVisualDocumentRevision_ &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo();
        if (stampLivePreviewVisualOverlap_)
        {
            UpdateVoxelHighlights();
        }

        AddConsoleMessage(stampLivePreviewVisualOverlap_
            ? "Stamp live preview visual test: overlap state."
            : "Stamp live preview visual test: overlap state failed.");
    }
    return stampLivePreviewVisualValid_ ||
        stampLivePreviewVisualOverlap_ || stampLivePreviewVisualClear_;
}

bool EditorWorkspace::RunStampPlacementVisualStep(const std::size_t)
{
    Asset::Voxel::VoxelDocument* const document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr)
    {
        return false;
    }

    if (!stampPlacementVisualStartedAt_)
    {
        constexpr std::int32_t unit =
            Stamps::StampFixedPoint::UnitsPerVoxel;
        const Stamps::StampBounds bounds{{0, 0, 0}, {2, 0, 1}, {3U, 1U, 2U}};
        const Stamps::StampPivot pivot{
            .RequestedMode = Stamps::StampPivotMode::Center,
            .ResolvedMode = Stamps::StampPivotMode::Center,
            .LocalPosition = {unit, 0, 0}};
        const std::vector<Stamps::StampPaletteEntry> palette{
            {0U, {255U, 82U, 82U, 255U}},
            {1U, {255U, 210U, 64U, 255U}},
            {2U, {148U, 104U, 255U, 255U}}};
        const std::vector<Stamps::StampVoxel> voxels{
            {{0, 0, 0}, 0U}, {{2, 0, 0}, 1U},
            {{0, 0, 1}, 2U}};
        Stamps::StampValidationResult validation{};
        const std::optional<Stamps::VoxelStamp> fixture =
            Stamps::VoxelStamp::TryCreate(
                {.Id = Core::UUID{0x5354414d503133ULL},
                 .ContentHash = "stamp-placement-visual-v1"},
                bounds,
                pivot,
                {},
                palette,
                voxels,
                Stamps::DefaultStampResourceLimits(),
                &validation);
        if (!fixture || !validation.IsValid())
        {
            AddConsoleMessage("Stamp placement visual test: fixture creation failed.");
            return false;
        }

        viewportCamera_.Frame(20.0F, 20.0F, 20.0F);
        viewportCamera_.SetView(EditorCameraView::Perspective);
        stampPlacementVisualDocumentRevision_ = document->GetRevision();
        const Stamps::StampPlacementSessionResult previewResult =
            stampPlacementSession_.Begin(
                *fixture, *document, voxelDocumentSession_.Generation(),
                0U, {unit, unit, unit});
        const VoxelPreviewData* const preview =
            stampPlacementSession_.CurrentPreview();
        stampPlacementVisualPreviewed_ = previewResult.Succeeded &&
            preview != nullptr &&
            preview->State == VoxelPreviewState::Valid;
        stampPlacementVisualStartedAt_ = std::chrono::steady_clock::now();
        AddConsoleMessage(stampPlacementVisualPreviewed_
            ? "Stamp mirror visual test [0-2s]: neutral preview."
            : "Stamp mirror visual test [0-2s]: preview failed.");
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - *stampPlacementVisualStartedAt_);
    if (elapsed >= std::chrono::seconds{2} &&
        !stampPlacementVisualMirroredX_)
    {
        MirrorLatestStampPreview(Stamps::StampPlacementMirrorMode::X);
        stampPlacementVisualMirroredX_ =
            stampPlacementSession_.Mirror() ==
                Stamps::StampPlacementMirrorMode::X &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->Transform.MirrorMode ==
                static_cast<std::uint8_t>(
                    Stamps::StampPlacementMirrorMode::X);
        AddConsoleMessage(stampPlacementVisualMirroredX_
            ? "Stamp mirror visual test [2-4s]: Mirror X."
            : "Stamp mirror visual test [2-4s]: Mirror X failed.");
    }
    if (elapsed >= std::chrono::seconds{4} &&
        stampPlacementVisualMirroredX_ &&
        !stampPlacementVisualMirroredZ_)
    {
        MirrorLatestStampPreview(Stamps::StampPlacementMirrorMode::Z);
        stampPlacementVisualMirroredZ_ =
            stampPlacementSession_.Mirror() ==
                Stamps::StampPlacementMirrorMode::Z &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->Transform.MirrorMode ==
                static_cast<std::uint8_t>(
                    Stamps::StampPlacementMirrorMode::Z);
        AddConsoleMessage(stampPlacementVisualMirroredZ_
            ? "Stamp mirror visual test [4-6s]: Mirror Z."
            : "Stamp mirror visual test [4-6s]: Mirror Z failed.");
    }
    if (elapsed >= std::chrono::seconds{6} &&
        stampPlacementVisualMirroredZ_ &&
        !stampPlacementVisualMirroredXZ_)
    {
        MirrorLatestStampPreview(Stamps::StampPlacementMirrorMode::XZ);
        stampPlacementVisualMirroredXZ_ =
            stampPlacementSession_.Mirror() ==
                Stamps::StampPlacementMirrorMode::XZ &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->Transform.MirrorMode ==
                static_cast<std::uint8_t>(
                    Stamps::StampPlacementMirrorMode::XZ);
        AddConsoleMessage(stampPlacementVisualMirroredXZ_
            ? "Stamp mirror visual test [6-8s]: Mirror XZ."
            : "Stamp mirror visual test [6-8s]: Mirror XZ failed.");
    }
    if (elapsed >= std::chrono::seconds{8} &&
        stampPlacementVisualMirroredXZ_ &&
        !stampPlacementVisualMirrorRotated_)
    {
        RotateLatestStampPreview(true);
        stampPlacementVisualMirrorRotated_ =
            stampPlacementSession_.Mirror() ==
                Stamps::StampPlacementMirrorMode::XZ &&
            stampPlacementSession_.QuarterRotation() == 1U &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->Transform.MirrorMode ==
                static_cast<std::uint8_t>(
                    Stamps::StampPlacementMirrorMode::XZ) &&
            stampPlacementSession_.CurrentPreview()->Transform.QuarterTurns ==
                1U;
        AddConsoleMessage(stampPlacementVisualMirrorRotated_
            ? "Stamp mirror visual test [8-10s]: Mirror XZ + rotation 90."
            : "Stamp mirror visual test [8-10s]: combined transform failed.");
    }
    if (elapsed >= std::chrono::seconds{10} &&
        stampPlacementVisualMirrorRotated_ &&
        !stampPlacementVisualFirstPlaced_)
    {
        PlaceLatestStampPreview();
        stampPlacementVisualFirstPlaced_ = stampPlacementVisualPreviewed_ &&
            document->GetRevision() == stampPlacementVisualDocumentRevision_ + 1U &&
            voxelEditHistory_.UndoCount() == 1U &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->State == VoxelPreviewState::Overlap;
        AddConsoleMessage(stampPlacementVisualFirstPlaced_
            ? "Stamp mirror visual test [10-12s]: mirrored placement."
            : "Stamp mirror visual test [10-12s]: placement failed.");
    }
    if (elapsed >= std::chrono::seconds{12} &&
        stampPlacementVisualFirstPlaced_ && !stampPlacementVisualUndone_)
    {
        UndoCommand();
        stampPlacementVisualUndone_ =
            voxelEditHistory_.UndoCount() == 0U &&
            voxelEditHistory_.RedoCount() == 1U &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->State == VoxelPreviewState::Valid;
        AddConsoleMessage(stampPlacementVisualUndone_
            ? "Stamp mirror visual test [12-14s]: Undo."
            : "Stamp mirror visual test [12-14s]: Undo failed.");
    }
    if (elapsed >= std::chrono::seconds{14} &&
        stampPlacementVisualUndone_ && !stampPlacementVisualRedone_)
    {
        RedoCommand();
        stampPlacementVisualRedone_ =
            voxelEditHistory_.UndoCount() == 1U &&
            voxelEditHistory_.RedoCount() == 0U &&
            stampPlacementSession_.CurrentPreview() != nullptr &&
            stampPlacementSession_.CurrentPreview()->State == VoxelPreviewState::Overlap;
        AddConsoleMessage(stampPlacementVisualRedone_
            ? "Stamp mirror visual test [14-16s]: Redo."
            : "Stamp mirror visual test [14-16s]: Redo failed.");
    }
    if (elapsed >= std::chrono::seconds{16} &&
        stampPlacementVisualRedone_ && !stampPlacementVisualCleared_)
    {
        const std::uint64_t revision = document->GetRevision();
        ClearLatestStampPreview();
        stampPlacementVisualCleared_ =
            stampPlacementSession_.CurrentPreview() == nullptr &&
            !stampPlacementSession_.IsActive() &&
            document->GetRevision() == revision;
        AddConsoleMessage(stampPlacementVisualCleared_
            ? "Stamp mirror visual test [16-18s]: Clear/Esc."
            : "Stamp mirror visual test [16-18s]: Clear/Esc failed.");
    }
    if (elapsed >= std::chrono::seconds{18} &&
        !stampPlacementVisualFinished_)
    {
        stampPlacementVisualFinished_ = true;
        stampPlacementVisualSucceeded_ = stampPlacementVisualPreviewed_ &&
            stampPlacementVisualMirroredX_ &&
            stampPlacementVisualMirroredZ_ &&
            stampPlacementVisualMirroredXZ_ &&
            stampPlacementVisualMirrorRotated_ &&
            stampPlacementVisualFirstPlaced_ && stampPlacementVisualUndone_ &&
            stampPlacementVisualRedone_ && stampPlacementVisualCleared_;
        AddConsoleMessage(stampPlacementVisualSucceeded_
            ? "Stamp mirror visual test: completed successfully; closing."
            : "Stamp mirror visual test: failed; keeping workspace open.");
    }
    return stampPlacementVisualSucceeded_;
}

bool EditorWorkspace::RunForgeLibraryVisualStep(const std::size_t frame)
{
    showForgeLibrary_ = true;
    if (frame == 0U)
    {
        resetLayoutRequested_ = true;
        stampCatalogService_.InvalidateCache();
        const Stamps::ForgeLibraryOperationResult refreshed =
            forgeLibraryViewModel_.Refresh();
        if (!refreshed.Succeeded || forgeLibraryViewModel_.Items().empty())
            return false;
        forgeLibraryViewModel_.SetDisplayMode(
            Stamps::ForgeLibraryDisplayMode::Grid);
        static_cast<void>(forgeLibraryViewModel_.Select(
            forgeLibraryViewModel_.Items().front().CatalogEntry.Reference.Id));
    }
    if (frame == 1U)
        ImGui::SetWindowFocus(ForgeLibraryPanelWindowName);
    if (frame == 240U)
    {
        forgeLibraryViewModel_.SetDisplayMode(
            Stamps::ForgeLibraryDisplayMode::List);
    }
    if (frame == 480U)
    {
        forgeLibraryViewModel_.SetSearchText("No matching Stamp");
        static_cast<void>(forgeLibraryViewModel_.Refresh());
    }
    if (frame == 720U)
    {
        forgeLibraryViewModel_.SetSearchText({});
        forgeLibraryViewModel_.SetDisplayMode(
            Stamps::ForgeLibraryDisplayMode::Grid);
        static_cast<void>(forgeLibraryViewModel_.Refresh());
        if (!forgeLibraryViewModel_.Items().empty())
        {
            static_cast<void>(forgeLibraryViewModel_.Select(
                forgeLibraryViewModel_.Items().back()
                    .CatalogEntry.Reference.Id));
            Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            if (document != nullptr)
            {
                const Stamps::ForgeLibraryOperationResult activated =
                    forgeLibraryViewModel_.ActivateSelected(
                        *document, voxelDocumentSession_.Generation());
                if (activated.SessionActivated) UpdateVoxelHighlights();
            }
        }
    }
    if (frame == 960U && stampPlacementSession_.CurrentPlan() != nullptr)
        PlaceLatestStampPreview();
    if (frame == 1200U)
    {
        forgeLibraryViewModel_.SetFilter(
            Stamps::ForgeLibraryFilter::Favorites);
        static_cast<void>(forgeLibraryViewModel_.Refresh());
    }
    return true;
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
        if (pendingImportCollision_)
        {
            ImGui::TextWrapped("%s",
                pendingImportCollision_->DestinationPath.string().c_str());
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
        ImGui::TextDisabled("Valid range: 1..256. Default: 64 x 64 x 64.");
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
    newVoxelModelDimensions_ = {64, 64, 64};
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

std::vector<std::filesystem::path>
EditorWorkspace::ProtectedProjectDeletionRoots() const
{
    std::vector<std::filesystem::path> roots;
    std::error_code error;
    std::filesystem::path candidate = std::filesystem::current_path(error);
    if (error) return roots;

    while (!candidate.empty())
    {
        const bool repositoryRoot =
            std::filesystem::is_directory(candidate / "editor", error) &&
            !error &&
            std::filesystem::is_directory(candidate / "engine", error) &&
            !error &&
            std::filesystem::is_regular_file(candidate / "CMakeLists.txt", error) &&
            !error;
        if (repositoryRoot)
        {
            roots.push_back(candidate);
            roots.push_back(candidate / "assets");
            roots.push_back(candidate / "Assets");
            break;
        }
        const std::filesystem::path parent = candidate.parent_path();
        if (parent == candidate) break;
        candidate = parent;
        error.clear();
    }
    return roots;
}

void EditorWorkspace::DeletePendingProject()
{
    if (pendingProjectDeletionPath_.empty()) return;

    ProjectDeletionRequest request;
    request.ProjectFilePath = pendingProjectDeletionPath_;
    if (const auto& activeProject = projectManager_.ActiveProject())
        request.ActiveProjectRoot = activeProject->RootPath();
    request.ProtectedRoots = ProtectedProjectDeletionRoots();
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
    pendingImportPaths_.clear();
    pendingImportCollision_.reset();
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
    session.ActiveTool =
        (voxelToolState_.IsPencilActive() &&
         toolContext_.Smart.Action() == SmartAction::Erase)
        ? ProjectSessionTool::Eraser
        : (voxelToolState_.IsPencilActive() &&
           toolContext_.Smart.Action() == SmartAction::Paint)
        ? ProjectSessionTool::Fill
        : voxelToolState_.IsEraserActive()
        ? ProjectSessionTool::Eraser
        : voxelToolState_.IsFillActive()
        ? ProjectSessionTool::Fill
        : voxelToolState_.IsBoxActive()
        ? ProjectSessionTool::Box
        : voxelToolState_.IsLineActive()
        ? ProjectSessionTool::Line
        : voxelToolState_.IsSphereActive()
        ? ProjectSessionTool::Sphere
        : ProjectSessionTool::Pencil;
    session.ActivePaletteIndex =
        paletteService_.ActiveIndex().value_or(
            PaletteService::FirstSelectableIndex);
    const SmartBrushShape sessionShape = ResolveSmartBrushShape(
        toolContext_.Smart.Geometry(), toolContext_.Smart.Brush().Shape);
    session.SmartGeometry = sessionShape == SmartBrushShape::Cube
        ? ProjectSessionSmartGeometry::Cube
        : sessionShape == SmartBrushShape::Sphere
        ? ProjectSessionSmartGeometry::Sphere
        : ProjectSessionSmartGeometry::Pencil;
    session.SmartAction = toolContext_.Smart.Action() == SmartAction::Erase
        ? ProjectSessionSmartAction::Erase
        : toolContext_.Smart.Action() == SmartAction::Paint
        ? ProjectSessionSmartAction::Paint
        : ProjectSessionSmartAction::Add;
    session.SmartBrushSize = toolContext_.Smart.Brush().Size;

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

    // Cube and Sphere were stored as SmartGeometry before Shape became the
    // sole active geometry selector. Preserve the legacy brush volume while
    // normalizing the live tool to Pencil for the current UI.
    toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
    if (loaded.Session.SmartGeometry == ProjectSessionSmartGeometry::Cube)
        toolContext_.Smart.Brush().Shape = SmartBrushShape::Cube;
    else if (loaded.Session.SmartGeometry == ProjectSessionSmartGeometry::Sphere)
        toolContext_.Smart.Brush().Shape = SmartBrushShape::Sphere;
    toolContext_.Smart.SetAction(
        loaded.Session.SmartAction == ProjectSessionSmartAction::Erase
            ? SmartAction::Erase
            : loaded.Session.SmartAction == ProjectSessionSmartAction::Paint
            ? SmartAction::Paint
            : SmartAction::Add);
    toolContext_.Smart.Brush().Size = loaded.Session.SmartBrushSize;
    voxelToolState_.SetActiveTool(
        loaded.Session.ActiveTool == ProjectSessionTool::Eraser ||
        loaded.Session.ActiveTool == ProjectSessionTool::Fill
            ? ActiveVoxelTool::Pencil
            : loaded.Session.ActiveTool == ProjectSessionTool::Box
            ? ActiveVoxelTool::Box
            : loaded.Session.ActiveTool == ProjectSessionTool::Line
            ? ActiveVoxelTool::Line
            : loaded.Session.ActiveTool == ProjectSessionTool::Sphere
            ? ActiveVoxelTool::Sphere
            : ActiveVoxelTool::Pencil);
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
        EditorInputFrame input;
        input.SetPressed(EditorInputKey::V);
        const EditorInputCommand command = editorInputService_.Resolve(
            input, CurrentCommandAvailability());
        const auto toolbarButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Selection;
            });
        if (command != EditorInputCommand::ToolSelection ||
            toolbarButton == EditorToolbarModel::Buttons().end() ||
            !EditorToolbarModel::IsEnabled(
                *toolbarButton,
                {true, false, voxelToolState_.ActiveTool()}))
            return false;
        ExecuteInputCommand(command);
        const Asset::Voxel::VoxelPosition first{
            static_cast<std::int32_t>(hit->Coordinates.X),
            static_cast<std::int32_t>(hit->Coordinates.Y),
            static_cast<std::int32_t>(hit->Coordinates.Z)};
        if (!selectionService_.Select(first, SelectionMode::Replace))
            return false;
        const Asset::Voxel::VoxelPosition second{1, 1, 1};
        static_cast<void>(selectionService_.Select(second, SelectionMode::Add));
        if (selectionService_.Count() < 1U ||
            !selectionService_.Contains(first) ||
            !selectionService_.Bounds().Valid)
            return false;
        selectionSystemSmokeStarted_ =
            voxelToolState_.IsSelectionActive();
        UpdateVoxelHighlights();
    }
    else if (frame == 10U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const Asset::Voxel::VoxelSubModel* model =
            document ? document->GetModel(0U) : nullptr;
        if (!model || model->VoxelCount() == 0U) return false;
        std::vector<Asset::Voxel::VoxelPosition> voxels;
        model->ForEachVoxel(
            [&voxels](const Asset::Voxel::VoxelPosition position,
                      const Asset::Voxel::Voxel&)
            {
                voxels.push_back(position);
            });
        const auto first = voxels.front();
        const auto last = voxels.back();
        const std::uint64_t generation = voxelDocumentSession_.Generation();
        if (!selectionInteraction_.PointerDown(
                first, generation, SelectionMode::Replace, 0.0F, 0.0F))
            return false;
        static_cast<void>(selectionInteraction_.PointerMove(
            8.0F, 0.0F, last, document->GetDimensions(0U)));
        const SelectionPointerRelease volume = selectionInteraction_.PointerUp();
        if (!volume.WasDrag || !volume.Bounds || !volume.Bounds->Valid)
            return false;
        static_cast<void>(ApplySelectionBounds(
            *volume.Bounds, volume.Operation));
        const std::size_t count = selectionService_.Count();
        SelectVoxelTool(ActiveVoxelTool::Pencil);
        SelectVoxelTool(ActiveVoxelTool::Selection);
        selectionSystemSmokeToolChanged_ = selectionSystemSmokeStarted_ &&
            voxelToolState_.IsSelectionActive() && count > 0U &&
            selectionService_.Count() == count &&
            selectionService_.EditableBounds().Valid;
    }
    else if (frame == 12U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!document) return false;
        const SelectionBounds original = selectionService_.EditableBounds();
        const SelectionHandles handles = ProjectSelectionHandles(
            original, voxelModelCenter_, currentViewportRectangle_,
            viewportCamera_.GetViewProjection());
        const auto dimensions = document->GetDimensions(0U);
        if (!dimensions) return false;
        auto visibleHandle = handles.end();
        std::int32_t resizeDelta = 0;
        for (auto handle = handles.begin(); handle != handles.end(); ++handle)
        {
            if (!handle->Visible) continue;
            if (ResizeSelectionBounds(
                    original, handle->Face, 1, *dimensions) != original)
            {
                visibleHandle = handle;
                resizeDelta = 1;
                break;
            }
            if (ResizeSelectionBounds(
                    original, handle->Face, -1, *dimensions) != original)
            {
                visibleHandle = handle;
                resizeDelta = -1;
                break;
            }
        }
        if (visibleHandle == handles.end() || handles.size() != 6U ||
            resizeDelta == 0)
            return false;
        const auto picked = PickSelectionHandle(
            handles, visibleHandle->ScreenPosition);
        if (!picked || picked->Face != visibleHandle->Face)
            return false;
        if (!selectionInteraction_.BeginResizingFace(
                picked->Face, original, voxelDocumentSession_.Generation(),
                picked->ScreenPosition.X, picked->ScreenPosition.Y,
                picked->ScreenAxisPerVoxel))
            return false;
        const bool moved = selectionInteraction_.PointerMove(
            picked->ScreenPosition.X +
                picked->ScreenAxisPerVoxel.X * resizeDelta,
            picked->ScreenPosition.Y +
                picked->ScreenAxisPerVoxel.Y * resizeDelta,
            std::nullopt, *dimensions);
        if (moved)
            static_cast<void>(ApplySelectionBounds(
                selectionInteraction_.CurrentBounds(),
                SelectionMode::Replace));
        const SelectionPointerRelease resized = selectionInteraction_.PointerUp();
        if (!moved || !resized.Bounds || !resized.WasDrag ||
            resized.Mode != SelectionInteractionMode::ResizingFace ||
            *resized.Bounds == original)
            return false;
        static_cast<void>(ApplySelectionBounds(
            *resized.Bounds, SelectionMode::Replace));
        selectionSystemSmokeToolChanged_ =
            selectionSystemSmokeToolChanged_ &&
            selectionService_.EditableBounds() == *resized.Bounds &&
            selectionInteraction_.Mode() == SelectionInteractionMode::Idle;
    }
    else if (frame == 13U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!document) return false;
        const SelectionBounds original = selectionService_.EditableBounds();
        if (!selectionInteraction_.BeginResizingFace(
                SelectionFace::XMinimum, original,
                voxelDocumentSession_.Generation(),
                10.0F, 10.0F, {10.0F, 0.0F}))
            return false;
        if (selectionInteraction_.PointerMove(
                20.0F, 10.0F, std::nullopt,
                document->GetDimensions(0U)))
            static_cast<void>(ApplySelectionBounds(
                selectionInteraction_.CurrentBounds(),
                SelectionMode::Replace));
        const auto restored = selectionInteraction_.Cancel();
        if (!restored) return false;
        static_cast<void>(ApplySelectionBounds(
            *restored, SelectionMode::Replace));
        selectionSystemSmokeToolChanged_ =
            selectionSystemSmokeToolChanged_ && *restored == original &&
            selectionService_.EditableBounds() == original &&
            !selectionInteraction_.IsActive();
    }
    else if (frame == 14U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const Asset::Voxel::VoxelSubModel* model =
            document ? document->GetModel(0U) : nullptr;
        if (!document || !model) return false;
        const auto dimensions = document->GetDimensions(0U);
        const SelectionBounds original = selectionService_.EditableBounds();
        if (!dimensions || !original.Valid) return false;
        Asset::Voxel::VoxelPosition requestedDelta{};
        if (original.Maximum.X + 1 <
            static_cast<std::int32_t>(dimensions->X))
            requestedDelta.X = 1;
        else if (original.Minimum.X > 0)
            requestedDelta.X = -1;
        else if (original.Maximum.Z + 1 <
            static_cast<std::int32_t>(dimensions->Z))
            requestedDelta.Z = 1;
        else if (original.Minimum.Z > 0)
            requestedDelta.Z = -1;
        const SelectionBounds translated = TranslateSelectionBounds(
            original, requestedDelta, *dimensions);
        if (translated == original) return false;

        const Vec3 spatialCenter{
            (static_cast<float>(original.Minimum.X) +
             static_cast<float>(original.Maximum.X) + 1.0F) * 0.5F,
            (static_cast<float>(original.Minimum.Y) +
             static_cast<float>(original.Maximum.Y) + 1.0F) * 0.5F,
            (static_cast<float>(original.Minimum.Z) +
             static_cast<float>(original.Maximum.Z) + 1.0F) * 0.5F};
        const VoxelRay interiorRay{
            {static_cast<float>(original.Minimum.X) - voxelModelCenter_.X - 4.0F,
             spatialCenter.Y - voxelModelCenter_.Y,
             spatialCenter.Z - voxelModelCenter_.Z},
            {1.0F, 0.0F, 0.0F}};
        const auto interior = PickSelectionBoxInterior(
            original, voxelModelCenter_, interiorRay);
        if (!interior || ResolveSelectionPointerTarget(true, true) !=
                SelectionPointerTarget::Handle ||
            ResolveSelectionPointerTarget(false, true) !=
                SelectionPointerTarget::Interior)
            return false;

        std::vector<std::pair<Asset::Voxel::VoxelPosition, std::uint8_t>>
            voxelSnapshot;
        const auto sortVoxels = [](auto& voxels)
        {
            std::sort(voxels.begin(), voxels.end(),
                [](const auto& left, const auto& right)
                {
                    if (left.first.X != right.first.X)
                        return left.first.X < right.first.X;
                    if (left.first.Y != right.first.Y)
                        return left.first.Y < right.first.Y;
                    if (left.first.Z != right.first.Z)
                        return left.first.Z < right.first.Z;
                    return left.second < right.second;
                });
        };
        model->ForEachVoxel(
            [&voxelSnapshot](const Asset::Voxel::VoxelPosition position,
                             const Asset::Voxel::Voxel& voxel)
            {
                voxelSnapshot.emplace_back(position, voxel.PaletteIndex);
            });
        sortVoxels(voxelSnapshot);
        const std::uint64_t revision = document->GetRevision();
        const std::size_t undoCount = voxelEditHistory_.UndoCount();
        const std::size_t redoCount = voxelEditHistory_.RedoCount();
        const SelectionMovePlane plane = MakeSelectionMovePlane(
            interior->WorldPosition, interiorRay.Direction);
        if (!selectionInteraction_.BeginMovingBox(
                original, voxelDocumentSession_.Generation(), plane,
                interior->WorldPosition) ||
            !selectionInteraction_.MoveBox(
                interior->WorldPosition + Vec3{
                    static_cast<float>(requestedDelta.X),
                    static_cast<float>(requestedDelta.Y),
                    static_cast<float>(requestedDelta.Z)},
                *dimensions))
            return false;
        static_cast<void>(ApplySelectionBounds(
            selectionInteraction_.CurrentBounds(), SelectionMode::Replace));
        const SelectionHandles movedHandles = GenerateSelectionHandles(
            selectionInteraction_.CurrentBounds(), voxelModelCenter_);
        const SelectionPointerRelease moved = selectionInteraction_.PointerUp();

        std::vector<std::pair<Asset::Voxel::VoxelPosition, std::uint8_t>>
            voxelsAfter;
        model->ForEachVoxel(
            [&voxelsAfter](const Asset::Voxel::VoxelPosition position,
                           const Asset::Voxel::Voxel& voxel)
            {
                voxelsAfter.emplace_back(position, voxel.PaletteIndex);
            });
        sortVoxels(voxelsAfter);
        std::size_t expectedCount = 0U;
        for (const auto& [position, palette] : voxelsAfter)
        {
            static_cast<void>(palette);
            if (translated.Contains(position)) ++expectedCount;
        }
        const SelectionHandles expectedHandles = GenerateSelectionHandles(
            translated, voxelModelCenter_);
        const bool handlesMatch = std::equal(
            movedHandles.begin(), movedHandles.end(), expectedHandles.begin(),
            [](const SelectionHandle& left, const SelectionHandle& right)
            {
                return left.Face == right.Face &&
                    left.WorldPosition == right.WorldPosition;
            });
        selectionSystemSmokeToolChanged_ =
            selectionSystemSmokeToolChanged_ && moved.WasDrag &&
            moved.Mode == SelectionInteractionMode::MovingBox &&
            moved.Bounds && *moved.Bounds == translated &&
            translated.Dimensions() == original.Dimensions() &&
            handlesMatch &&
            selectionService_.EditableBounds() == translated &&
            selectionService_.Count() == expectedCount &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == undoCount &&
            voxelEditHistory_.RedoCount() == redoCount &&
            voxelSnapshot == voxelsAfter && !selectionInteraction_.IsActive();
    }
    else if (frame == 15U)
    {
        const SelectionBounds persistent = selectionService_.EditableBounds();
        const std::uint64_t generation = voxelDocumentSession_.Generation();
        if (!selectionInteraction_.PointerDown(
                persistent.Minimum, generation, SelectionMode::Replace,
                0.0F, 0.0F))
            return false;
        static_cast<void>(selectionInteraction_.PointerMove(
            2.0F, 2.0F, persistent.Minimum));
        const SelectionPointerRelease click = selectionInteraction_.PointerUp();
        selectionSystemSmokeToolChanged_ = selectionSystemSmokeToolChanged_ &&
            !click.WasDrag && !click.Bounds &&
            !selectionInteraction_.IsActive() &&
            selectionService_.EditableBounds() == persistent;
        UpdateVoxelHighlights();
    }
    else if (frame == 16U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!document) return false;
        const auto dimensions = document->GetDimensions(0U);
        const SelectionBounds original = selectionService_.EditableBounds();
        if (!dimensions || !original.Valid) return false;
        Asset::Voxel::VoxelPosition cancellationDelta{};
        if (original.Maximum.X + 1 <
            static_cast<std::int32_t>(dimensions->X))
            cancellationDelta.X = 1;
        else if (original.Minimum.X > 0)
            cancellationDelta.X = -1;
        else if (original.Maximum.Y + 1 <
            static_cast<std::int32_t>(dimensions->Y))
            cancellationDelta.Y = 1;
        else if (original.Minimum.Y > 0)
            cancellationDelta.Y = -1;
        else if (original.Maximum.Z + 1 <
            static_cast<std::int32_t>(dimensions->Z))
            cancellationDelta.Z = 1;
        else if (original.Minimum.Z > 0)
            cancellationDelta.Z = -1;
        if (cancellationDelta == Asset::Voxel::VoxelPosition{}) return false;
        const auto plane = MakeSelectionMovePlane(
            {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        if (!selectionInteraction_.BeginMovingBox(
                original, voxelDocumentSession_.Generation(), plane, {}) ||
            !selectionInteraction_.MoveBox(
                {static_cast<float>(cancellationDelta.X),
                 static_cast<float>(cancellationDelta.Y),
                 static_cast<float>(cancellationDelta.Z)},
                *dimensions))
            return false;
        static_cast<void>(ApplySelectionBounds(
            selectionInteraction_.CurrentBounds(), SelectionMode::Replace));
        const auto restored = selectionInteraction_.Cancel();
        if (!restored) return false;
        static_cast<void>(ApplySelectionBounds(
            *restored, SelectionMode::Replace));
        selectionSystemSmokeToolChanged_ =
            selectionSystemSmokeToolChanged_ && *restored == original &&
            selectionService_.EditableBounds() == original &&
            !selectionInteraction_.IsActive();
    }
    else if (frame == 18U)
    {
        const SelectionBounds original = selectionService_.EditableBounds();
        const auto plane = MakeSelectionMovePlane(
            {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        if (!selectionInteraction_.BeginMovingBox(
                original, voxelDocumentSession_.Generation(), plane, {}))
            return false;
        selectionSystemSmokeToolChanged_ =
            selectionSystemSmokeToolChanged_ &&
            !selectionInteraction_.ValidateDocumentGeneration(
                voxelDocumentSession_.Generation() + 1U) &&
            !selectionInteraction_.IsActive();
    }
    else if (frame == 20U)
    {
        static_cast<void>(voxelSelection_.Clear());
        selectionService_.SetDocumentGeneration(
            selectionService_.DocumentGeneration() + 1U);
        selectionSystemSmokePassed_ = selectionSystemSmokeToolChanged_ &&
            selectionService_.Empty() &&
            !selectionService_.EditableBounds().Valid;
        UpdateVoxelHighlights();
    }
    else if (frame == 22U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const Asset::Voxel::VoxelSubModel* model =
            document ? document->GetModel(0U) : nullptr;
        if (!document || !model) return false;
        const std::uint64_t generation = voxelDocumentSession_.Generation();
        selectionService_.SetDocumentGeneration(generation);
        if (!selectionService_.Select(
                {0, 1, 1}, SelectionMode::Replace) ||
            !selectionService_.Select({1, 1, 1}, SelectionMode::Add))
            return false;

        transformPreviewSmokeDocumentSnapshot_.clear();
        model->ForEachVoxel(
            [this](const Asset::Voxel::VoxelPosition position,
                   const Asset::Voxel::Voxel& voxel)
            {
                transformPreviewSmokeDocumentSnapshot_.emplace_back(
                    position, voxel);
            });
        std::sort(transformPreviewSmokeDocumentSnapshot_.begin(),
            transformPreviewSmokeDocumentSnapshot_.end(),
            [](const auto& left, const auto& right)
            {
                if (left.first.X != right.first.X)
                    return left.first.X < right.first.X;
                if (left.first.Y != right.first.Y)
                    return left.first.Y < right.first.Y;
                return left.first.Z < right.first.Z;
            });
        transformPreviewSmokeDocumentRevision_ = document->GetRevision();
        transformPreviewSmokeDocumentDirty_ = document->IsDirty();
        transformPreviewSmokeUndoCount_ = voxelEditHistory_.UndoCount();
        transformPreviewSmokeRedoCount_ = voxelEditHistory_.RedoCount();
        transformPreviewSmokeHighlightUploadBaseline_ =
            viewportRenderer_.HighlightUploadCount();
        transformPreviewSmokeRendered_ = false;

        if (!transformPreviewModel_.BeginPreview(
                *document, selectionService_, generation) ||
            !transformPreviewModel_.SetDelta(
                *document, selectionService_, generation, {1, 0, 0}))
            return false;
        const TransformPreviewRenderData renderData =
            transformPreviewModel_.RenderData();
        if (renderData.Voxels.size() != 2U ||
            renderData.Plan.SourceVoxelCount != 2U ||
            renderData.Plan.DestinationVoxelCount != 2U ||
            transformPreviewModel_.CollisionCount() != 1U ||
            transformPreviewModel_.OutOfBoundsCount() != 0U)
            return false;
        UpdateVoxelHighlights();
    }
    else if (frame == 23U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!document || !viewportRenderer_.HasTransformPreview() ||
            viewportRenderer_.TransformPreviewSourcePrimitiveCount() != 2U ||
            viewportRenderer_.TransformPreviewDestinationPrimitiveCount() !=
                2U ||
            viewportRenderer_.TransformPreviewCollisionPrimitiveCount() != 1U ||
            viewportRenderer_.HighlightUploadCount() <=
                transformPreviewSmokeHighlightUploadBaseline_)
            return false;
        transformPreviewSmokeRendered_ = true;
        if (!transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {-1, 0, 0}) ||
            transformPreviewModel_.CollisionCount() != 0U ||
            transformPreviewModel_.OutOfBoundsCount() != 1U)
            return false;
        UpdateVoxelHighlights();
    }
    else if (frame == 24U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!document ||
            viewportRenderer_.TransformPreviewCollisionPrimitiveCount() != 1U)
            return false;
        const std::uint64_t rebuildCount =
            transformPreviewModel_.Metrics().RebuildCount;
        if (transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {-1, 0, 0}) ||
            transformPreviewModel_.Metrics().RebuildCount != rebuildCount ||
            !transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {2, 0, 0}) ||
            transformPreviewModel_.CollisionCount() != 1U ||
            transformPreviewModel_.OutOfBoundsCount() != 1U)
            return false;
        UpdateVoxelHighlights();
    }
    else if (frame == 25U)
    {
        if (!viewportRenderer_.HasTransformPreview() ||
            viewportRenderer_.TransformPreviewCollisionPrimitiveCount() != 2U ||
            !transformPreviewModel_.CancelPreview())
            return false;
        UpdateVoxelHighlights();
    }
    else if (frame == 26U)
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const Asset::Voxel::VoxelSubModel* model =
            document ? document->GetModel(0U) : nullptr;
        if (!document || !model) return false;
        std::vector<std::pair<Asset::Voxel::VoxelPosition,
            Asset::Voxel::Voxel>> current;
        model->ForEachVoxel(
            [&current](const Asset::Voxel::VoxelPosition position,
                       const Asset::Voxel::Voxel& voxel)
            {
                current.emplace_back(position, voxel);
            });
        std::sort(current.begin(), current.end(),
            [](const auto& left, const auto& right)
            {
                if (left.first.X != right.first.X)
                    return left.first.X < right.first.X;
                if (left.first.Y != right.first.Y)
                    return left.first.Y < right.first.Y;
                return left.first.Z < right.first.Z;
            });
        selectionSystemSmokePassed_ = selectionSystemSmokePassed_ &&
            transformPreviewSmokeRendered_ &&
            !transformPreviewModel_.IsActive() &&
            !viewportRenderer_.HasTransformPreview() &&
            document->GetRevision() ==
                transformPreviewSmokeDocumentRevision_ &&
            document->IsDirty() == transformPreviewSmokeDocumentDirty_ &&
            voxelEditHistory_.UndoCount() ==
                transformPreviewSmokeUndoCount_ &&
            voxelEditHistory_.RedoCount() ==
                transformPreviewSmokeRedoCount_ &&
            transformPreviewSmokeDocumentSnapshot_ == current &&
            selectionService_.Count() == 2U &&
            selectionService_.Contains({0, 1, 1}) &&
            selectionService_.Contains({1, 1, 1});
    }
    return true;
}

bool EditorWorkspace::SelectionSystemSmokePassed() const noexcept
{
    return selectionSystemSmokePassed_;
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
        // Add Adjacent is a selection-driven legacy command. Keep this smoke
        // in the Selection context so it validates ray picking/highlighting,
        // rather than the Smart Tool's exact-model preview override.
        SelectVoxelTool(ActiveVoxelTool::Selection);
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
            voxelSaveState_.IsDirty() && !voxelSelection_.Selected();
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
        // This smoke verifies the ordinary picking/highlight route, not the
        // Smart Tool exact preview. Select explicitly so both routes remain
        // independently covered.
        SelectVoxelTool(ActiveVoxelTool::Selection);
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
            viewportRenderer_.HasModelMesh() &&
            !viewportRenderer_.HasExactPreviewMesh() &&
            universalCursor2DTarget_.has_value();
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        const bool planPrepared = request && smartToolController_.ResolvePreview(
            smartToolSession_, *request).HasPlan();
        const bool applied = firstClick && planPrepared && ApplyVoxelPencil();
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        const bool planPrepared = request && smartToolController_.ResolvePreview(
            smartToolSession_, *request).HasPlan();
        const bool applied = click && planPrepared && ApplyVoxelPencil();
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        const bool added = request && smartToolController_.ResolvePreview(
            smartToolSession_, *request).HasPlan() && ApplyVoxelPencil();
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        voxelUndoRedoSmokePencilExecuted_ = request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        voxelUndoRedoSmokeBranchClearedRedo_ = request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        if (!request || !smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan()) return false;
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        if (!request || !smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan()) return false;
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        if (!request || !smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan()) return false;
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
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Erase);
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
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
            voxelToolState_.IsPencilActive() &&
            toolContext_.Smart.Action() == SmartAction::Erase &&
            !document->IsDirty() &&
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
        const DirectCreationFlowResult flow = newVoxelModelWorkflow_.Create(
            voxelModelCreationService_, directCreationFlowService_);
        document = voxelDocumentSession_.ActiveDocument();
        directCreationSmokePath_ = flow.Creation.ModelPath;
        directCreationSmokeTarget_ = {32, 0, 32};
        EditorCamera expectedCamera;
        expectedCamera.Frame(64.0F, 64.0F, 64.0F);
        directCreationSmokeCreated_ = flow.Ready() &&
            flow.Creation.ThumbnailGenerated && document != nullptr &&
            directCreationSmokePath_.filename() == "New Model.vox" &&
            document->SourcePath().lexically_normal() ==
                directCreationSmokePath_.lexically_normal() &&
            document->GetVoxelCount() == 0U && !document->IsDirty() &&
            voxelToolState_.IsPencilActive() && viewportState_.HasModel() &&
            viewportState_.IsGridVisible() &&
            workplaneService_.Grid(*document).has_value() &&
            viewportCamera_.CaptureState() == expectedCamera.CaptureState() &&
            assetBrowser_.SelectedRelativePath() ==
                std::optional<std::filesystem::path>(
                    "Models/New Model.vox") &&
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        directCreationSmokePencilled_ = directCreationSmokeFocused_ && request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() &&
            ApplyVoxelPencil() && document->GetVoxelCount() == 1U &&
            document->HasVoxel(directCreationSmokeTarget_) &&
            document->GetRevision() == revision + 1U &&
            document->IsDirty() && voxelEditHistory_.CanUndo();
    }
    else if (frame == 2U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!directCreationSmokePencilled_ || document == nullptr) return false;
        UndoCommand();
        directCreationSmokeUndone_ = document->GetVoxelCount() == 0U &&
            !document->HasVoxel(directCreationSmokeTarget_) &&
            voxelEditHistory_.CanRedo();
    }
    else if (frame == 3U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!directCreationSmokeUndone_ || document == nullptr) return false;
        RedoCommand();
        directCreationSmokeRedone_ = document->GetVoxelCount() == 1U &&
            document->HasVoxel(directCreationSmokeTarget_) &&
            !voxelEditHistory_.CanRedo();
    }
    else if (frame == 4U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!directCreationSmokeRedone_ || document == nullptr) return false;
        const std::uint64_t revision = document->GetRevision();
        directCreationSmokeSaved_ = SaveVoxelModel() &&
            !document->IsDirty() && document->GetRevision() == revision;
    }
    else if (frame == 5U)
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
        directCreationSmokePencilled_ && directCreationSmokeUndone_ &&
        directCreationSmokeRedone_ && directCreationSmokeSaved_ &&
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
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        paletteSmokePencilled_ = request && smartToolController_.ResolvePreview(
            smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
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
        const ImGuiWindow* paletteWindow = ImGui::FindWindowByName(
            StylePanelWindowName);
        paletteSmokeLayoutValid_ = inspectorWindow != nullptr &&
            paletteWindow != nullptr && inspectorWindow->DockNode != nullptr &&
            paletteWindow->DockNode != nullptr &&
            inspectorWindow->DockNode != paletteWindow->DockNode;
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

bool EditorWorkspace::RunVoxelFillSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"FillSmoke", {16U, 16U, 16U}});
        voxelFillSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || document == nullptr) return false;
        const std::vector<VoxelChange> seed{
            {0U, {1, 0, 1}, false, 0U, true, 2U},
            {0U, {2, 0, 1}, false, 0U, true, 2U},
            {0U, {1, 0, 2}, false, 0U, true, 2U},
            {0U, {2, 0, 2}, false, 0U, true, 2U},
            {0U, {8, 0, 8}, false, 0U, true, 3U},
            {0U, {9, 0, 8}, false, 0U, true, 3U},
            {0U, {8, 0, 9}, false, 0U, true, 3U},
            {0U, {9, 0, 9}, false, 0U, true, 3U}};
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Draw Fill Smoke Zones", seed});
        voxelFillSmokeSeeded_ = seeded && document->GetVoxelCount() == 8U &&
            document->GetRevision() == 1U;
        if (!voxelFillSmokeSeeded_ || !paletteService_.SelectColor(4U))
            return false;
        VoxelRaycastHit hit;
        hit.Coordinates = {1U, 0U, 1U};
        hit.Face = VoxelHitFace::PositiveY;
        hit.SubModelIndex = 0U;
        hit.DocumentRevision = document->GetRevision();
        static_cast<void>(voxelSelection_.SetHovered(hit));
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Fill);
        voxelFillSmokeApplied_ = ApplyVoxelFill() &&
            document->GetRevision() == 2U &&
            document->GetVoxel({1, 0, 1})->PaletteIndex == 4U &&
            document->GetVoxel({2, 0, 2})->PaletteIndex == 4U &&
            document->GetVoxel({8, 0, 8})->PaletteIndex == 3U &&
            voxelEditHistory_.UndoCount() == 2U && lastVoxelFillResult_ &&
            lastVoxelFillResult_->ChangedVoxelCount == 4U;
    }
    else if (frame == 1U)
    {
        if (!document || !voxelFillSmokeApplied_) return false;
        UndoCommand();
        voxelFillSmokeUndone_ = document->GetRevision() == 3U &&
            document->GetVoxel({1, 0, 1})->PaletteIndex == 2U &&
            document->GetVoxel({8, 0, 8})->PaletteIndex == 3U &&
            voxelEditHistory_.RedoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelFillSmokeUndone_) return false;
        RedoCommand();
        voxelFillSmokeRedone_ = document->GetRevision() == 4U &&
            document->GetVoxel({1, 0, 1})->PaletteIndex == 4U &&
            document->GetVoxel({8, 0, 8})->PaletteIndex == 3U &&
            voxelEditHistory_.RedoCount() == 0U;
    }
    else if (frame == 3U)
    {
        voxelFillSmokeSaved_ = voxelFillSmokeRedone_ && SaveVoxelModel() &&
            std::filesystem::is_regular_file(voxelFillSmokePath_) &&
            document && !document->IsDirty();
    }
    else if (frame == 4U)
    {
        if (!voxelFillSmokeSaved_) return false;
        CloseProject();
        voxelFillSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelFillSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelFillSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelFillSmokePassed();
}

bool EditorWorkspace::VoxelFillSmokePassed() const noexcept
{
    return voxelFillSmokeSeeded_ && voxelFillSmokeApplied_ &&
        voxelFillSmokeUndone_ && voxelFillSmokeRedone_ &&
        voxelFillSmokeSaved_ && voxelFillSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelBoxSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"BoxSmoke", {16U, 16U, 16U}});
        voxelBoxSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Box);
        voxelBoxSmokeCreated_ = flow.Ready() && document != nullptr &&
            paletteService_.SelectColor(6U) &&
            voxelBoxInteraction_.Begin(
                {2, 0, 2}, voxelDocumentSession_.Generation()) &&
            voxelBoxInteraction_.Update(
                Asset::Voxel::VoxelPosition{11, 9, 11});
        if (!voxelBoxSmokeCreated_) return false;
        voxelBoxSmokeApplied_ = ApplyVoxelBox() &&
            document->GetVoxelCount() == 1000U &&
            document->GetRevision() == 1U &&
            document->GetVoxel({2, 0, 2})->PaletteIndex == 6U &&
            document->GetVoxel({11, 9, 11})->PaletteIndex == 6U &&
            voxelEditHistory_.UndoCount() == 1U &&
            !voxelBoxInteraction_.IsActive();
    }
    else if (frame == 1U)
    {
        if (!document || !voxelBoxSmokeApplied_) return false;
        UndoCommand();
        voxelBoxSmokeUndone_ = document->GetVoxelCount() == 0U &&
            document->GetRevision() == 2U &&
            voxelEditHistory_.RedoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelBoxSmokeUndone_) return false;
        RedoCommand();
        voxelBoxSmokeRedone_ = document->GetVoxelCount() == 1000U &&
            document->GetRevision() == 3U &&
            voxelEditHistory_.RedoCount() == 0U;
    }
    else if (frame == 3U)
    {
        voxelBoxSmokeSaved_ = voxelBoxSmokeRedone_ && SaveVoxelModel() &&
            std::filesystem::is_regular_file(voxelBoxSmokePath_) &&
            document && !document->IsDirty();
    }
    else if (frame == 4U)
    {
        if (!voxelBoxSmokeSaved_) return false;
        CloseProject();
        voxelBoxSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelBoxInteraction_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelBoxSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelBoxSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelBoxSmokePassed();
}

bool EditorWorkspace::VoxelBoxSmokePassed() const noexcept
{
    return voxelBoxSmokeCreated_ && voxelBoxSmokeApplied_ &&
        voxelBoxSmokeUndone_ && voxelBoxSmokeRedone_ &&
        voxelBoxSmokeSaved_ && voxelBoxSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelLineSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"LineSmoke", {16U, 16U, 16U}});
        voxelLineSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Line);
        voxelLineSmokeCreated_ = flow.Ready() && document != nullptr &&
            paletteService_.SelectColor(12U);
        if (!voxelLineSmokeCreated_) return false;

        const auto drawLine = [this](
            const Asset::Voxel::VoxelPosition a,
            const Asset::Voxel::VoxelPosition b)
        {
            return voxelLineInteraction_.Begin(
                    a, voxelDocumentSession_.Generation()) &&
                voxelLineInteraction_.Update(b) && ApplyVoxelLine();
        };
        voxelLineSmokeApplied_ =
            drawLine({1, 0, 1}, {8, 0, 1}) &&
            drawLine({1, 1, 2}, {8, 8, 9}) &&
            document->GetVoxelCount() == 16U &&
            document->GetRevision() == 2U &&
            document->GetVoxel({1, 0, 1})->PaletteIndex == 12U &&
            document->GetVoxel({8, 8, 9})->PaletteIndex == 12U &&
            voxelEditHistory_.UndoCount() == 2U &&
            !voxelLineInteraction_.IsActive();
    }
    else if (frame == 1U)
    {
        if (!document || !voxelLineSmokeApplied_) return false;
        UndoCommand();
        voxelLineSmokeUndone_ = document->GetVoxelCount() == 8U &&
            document->GetRevision() == 3U &&
            voxelEditHistory_.RedoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelLineSmokeUndone_) return false;
        RedoCommand();
        voxelLineSmokeRedone_ = document->GetVoxelCount() == 16U &&
            document->GetRevision() == 4U &&
            voxelEditHistory_.RedoCount() == 0U;
    }
    else if (frame == 3U)
    {
        voxelLineSmokeSaved_ = voxelLineSmokeRedone_ && SaveVoxelModel() &&
            std::filesystem::is_regular_file(voxelLineSmokePath_) &&
            document && !document->IsDirty();
    }
    else if (frame == 4U)
    {
        if (!voxelLineSmokeSaved_) return false;
        CloseProject();
        voxelLineSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelLineInteraction_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelLineSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelLineSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelLineSmokePassed();
}

bool EditorWorkspace::VoxelLineSmokePassed() const noexcept
{
    return voxelLineSmokeCreated_ && voxelLineSmokeApplied_ &&
        voxelLineSmokeUndone_ && voxelLineSmokeRedone_ &&
        voxelLineSmokeSaved_ && voxelLineSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelSphereSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"SphereSmoke", {24U, 24U, 24U}});
        voxelSphereSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Sphere);
        voxelSphereSmokeCreated_ = flow.Ready() && document != nullptr &&
            paletteService_.SelectColor(31U);
        if (!voxelSphereSmokeCreated_) return false;
        const auto expected = VoxelSphereService::CalculatePositions(
            *document, 0U, {12, 8, 12}, {16, 8, 12});
        voxelSphereSmokeApplied_ = voxelSphereInteraction_.Begin(
                {12, 8, 12}, voxelDocumentSession_.Generation()) &&
            voxelSphereInteraction_.Update(
                Asset::Voxel::VoxelPosition{16, 8, 12}) &&
            ApplyVoxelSphere() && !expected.empty() &&
            document->GetVoxelCount() == expected.size() &&
            document->GetRevision() == 1U &&
            document->GetVoxel({12, 8, 12})->PaletteIndex == 31U &&
            document->GetVoxel({16, 8, 12})->PaletteIndex == 31U &&
            voxelEditHistory_.UndoCount() == 1U &&
            !voxelSphereInteraction_.IsActive();
    }
    else if (frame == 1U)
    {
        if (!document || !voxelSphereSmokeApplied_) return false;
        UndoCommand();
        voxelSphereSmokeUndone_ = document->GetVoxelCount() == 0U &&
            document->GetRevision() == 2U &&
            voxelEditHistory_.RedoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelSphereSmokeUndone_) return false;
        RedoCommand();
        voxelSphereSmokeRedone_ = document->GetVoxelCount() > 0U &&
            document->GetRevision() == 3U &&
            voxelEditHistory_.RedoCount() == 0U;
    }
    else if (frame == 3U)
    {
        voxelSphereSmokeSaved_ = voxelSphereSmokeRedone_ && SaveVoxelModel() &&
            std::filesystem::is_regular_file(voxelSphereSmokePath_) &&
            document && !document->IsDirty();
    }
    else if (frame == 4U)
    {
        if (!voxelSphereSmokeSaved_) return false;
        CloseProject();
        voxelSphereSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelSphereInteraction_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelSphereSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelSphereSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelSphereSmokePassed();
}

bool EditorWorkspace::VoxelSphereSmokePassed() const noexcept
{
    return voxelSphereSmokeCreated_ && voxelSphereSmokeApplied_ &&
        voxelSphereSmokeUndone_ && voxelSphereSmokeRedone_ &&
        voxelSphereSmokeSaved_ && voxelSphereSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelMoveSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"MoveSmoke", {16U, 16U, 16U}});
        voxelMoveSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || document == nullptr) return false;
        const std::vector<VoxelChange> seed{
            {0U, {2, 2, 2}, false, 0U, true, 3U},
            {0U, {3, 2, 2}, false, 0U, true, 4U},
            {0U, {4, 2, 2}, false, 0U, true, 5U},
            {0U, {10, 2, 2}, false, 0U, true, 9U}};
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Move Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const SelectionBounds sourceBounds = SelectionBounds::FromCorners(
            {2, 2, 2}, {4, 2, 2});
        const std::array<Asset::Voxel::VoxelPosition, 3U> selected{
            Asset::Voxel::VoxelPosition{2, 2, 2},
            Asset::Voxel::VoxelPosition{3, 2, 2},
            Asset::Voxel::VoxelPosition{4, 2, 2}};
        static_cast<void>(selectionService_.ApplySortedVolume(
            selected,
            sourceBounds, SelectionMode::Replace));
        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::M);
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolMove;
        const auto moveButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            moveButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*moveButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection()});
        ExecuteInputCommand(EditorInputCommand::ToolMove);
        const SelectionMovePlane plane = MakeSelectionMovePlane(
            {}, {0.0F, 0.0F, 1.0F});
        const std::uint64_t revision = document->GetRevision();
        voxelMoveSmokePrepared_ = seeded && inputReady && toolbarReady &&
            voxelToolState_.IsMoveActive() &&
            selectionInteraction_.BeginMovingContent(
                sourceBounds, voxelDocumentSession_.Generation(), plane, {}) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation()) &&
            selectionInteraction_.MoveContent({1.0F, 0.0F, 0.0F}) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {1, 0, 0}) &&
            transformPreviewModel_.RenderData().Plan.SourceVoxelCount == 3U &&
            document->GetRevision() == revision;
        if (!voxelMoveSmokePrepared_) return false;
        const auto release = selectionInteraction_.PointerUp();
        voxelMoveSmokeApplied_ =
            release.Mode == SelectionInteractionMode::MovingContent &&
            ApplyVoxelMove() && document->GetRevision() == revision + 1U &&
            !document->HasVoxel({2, 2, 2}) &&
            document->GetVoxel({3, 2, 2})->PaletteIndex == 3U &&
            document->GetVoxel({4, 2, 2})->PaletteIndex == 4U &&
            document->GetVoxel({5, 2, 2})->PaletteIndex == 5U &&
            selectionService_.Contains({5, 2, 2}) &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({3, 2, 2}, {5, 2, 2}) &&
            voxelToolState_.IsMoveActive() &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 1U)
    {
        if (!document || !voxelMoveSmokeApplied_) return false;
        UndoCommand();
        voxelMoveSmokeUndone_ = document->HasVoxel({2, 2, 2}) &&
            document->GetVoxel({2, 2, 2})->PaletteIndex == 3U &&
            !document->HasVoxel({5, 2, 2}) &&
            selectionService_.Contains({2, 2, 2}) &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({2, 2, 2}, {4, 2, 2});
    }
    else if (frame == 2U)
    {
        if (!document || !voxelMoveSmokeUndone_) return false;
        RedoCommand();
        voxelMoveSmokeRedone_ = !document->HasVoxel({2, 2, 2}) &&
            document->GetVoxel({5, 2, 2})->PaletteIndex == 5U &&
            selectionService_.Contains({5, 2, 2});
    }
    else if (frame == 3U)
    {
        if (!document || !voxelMoveSmokeRedone_) return false;
        const std::uint64_t revision = document->GetRevision();
        const std::size_t undoCount = voxelEditHistory_.UndoCount();
        const SelectionBounds source = selectionService_.EditableBounds();
        const SelectionMovePlane plane = MakeSelectionMovePlane(
            {}, {0.0F, 0.0F, 1.0F});
        const auto rejected = [this, document, plane](
            const Asset::Voxel::VoxelPosition delta)
        {
            if (!selectionInteraction_.BeginMovingContent(
                    selectionService_.EditableBounds(),
                    voxelDocumentSession_.Generation(), plane, {}) ||
                !transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation()))
                return false;
            const Vec3 pointer{
                static_cast<float>(delta.X), static_cast<float>(delta.Y),
                static_cast<float>(delta.Z)};
            if (!selectionInteraction_.MoveContent(pointer) ||
                !transformPreviewModel_.SetDelta(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation(), delta))
                return false;
            static_cast<void>(selectionInteraction_.PointerUp());
            return !ApplyVoxelMove();
        };
        const bool collision = rejected({5, 0, 0});
        const bool outside = rejected({-4, 0, 0});
        const bool beganCancel = selectionInteraction_.BeginMovingContent(
                source, voxelDocumentSession_.Generation(), plane, {}) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation()) &&
            selectionInteraction_.MoveContent({1.0F, 1.0F, 0.0F}) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {1, 1, 0});
        if (beganCancel) CancelSelectionInteraction();
        voxelMoveSmokeRejected_ = collision && outside && beganCancel &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == undoCount &&
            selectionService_.EditableBounds() == source &&
            !transformPreviewModel_.IsActive() &&
            !selectionInteraction_.IsActive();
    }
    else if (frame == 4U)
    {
        voxelMoveSmokeSaved_ = voxelMoveSmokeRejected_ && SaveVoxelModel() &&
            document && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelMoveSmokePath_);
    }
    else if (frame == 5U)
    {
        if (!voxelMoveSmokeSaved_) return false;
        ClearVoxelViewport();
        voxelMoveSmokeReopened_ = OpenVoxInViewportNow(voxelMoveSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelMoveSmokeReopened_ = voxelMoveSmokeReopened_ && document &&
            !document->HasVoxel({2, 2, 2}) &&
            document->GetVoxel({3, 2, 2})->PaletteIndex == 3U &&
            document->GetVoxel({5, 2, 2})->PaletteIndex == 5U;
    }
    else if (frame == 6U)
    {
        if (!voxelMoveSmokeReopened_) return false;
        CloseProject();
        voxelMoveSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !selectionInteraction_.IsActive() &&
            !transformPreviewModel_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelMoveSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelMoveSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelMoveSmokePassed();
}

bool EditorWorkspace::VoxelMoveSmokePassed() const noexcept
{
    return voxelMoveSmokePrepared_ && voxelMoveSmokeApplied_ &&
        voxelMoveSmokeUndone_ && voxelMoveSmokeRedone_ &&
        voxelMoveSmokeRejected_ && voxelMoveSmokeSaved_ &&
        voxelMoveSmokeReopened_ && voxelMoveSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelDuplicateSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionMovePlane plane = MakeSelectionMovePlane(
        {}, {0.0F, 0.0F, 1.0F});
    const auto beginPreview = [this, &plane, &document](
        const Asset::Voxel::VoxelPosition delta)
    {
        if (document == nullptr ||
            !selectionInteraction_.BeginDuplicatingContent(
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(), plane, {}) ||
            !transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), 0U,
                TransformPreviewCollisionPolicy::IncludeSource))
            return false;
        const Vec3 pointer{
            static_cast<float>(delta.X), static_cast<float>(delta.Y),
            static_cast<float>(delta.Z)};
        return selectionInteraction_.MoveContent(pointer) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), delta);
    };

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"DuplicateSmoke", {64U, 64U, 64U}});
        voxelDuplicateSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || document == nullptr) return false;

        std::vector<VoxelChange> seed;
        std::vector<Asset::Voxel::VoxelPosition> selected;
        seed.reserve(513U);
        selected.reserve(512U);
        for (std::int32_t z = 1; z <= 8; ++z)
        {
            for (std::int32_t y = 1; y <= 8; ++y)
            {
                for (std::int32_t x = 1; x <= 8; ++x)
                {
                    const Asset::Voxel::VoxelPosition position{x, y, z};
                    const std::uint8_t palette = static_cast<std::uint8_t>(
                        1 + ((x + y + z) % 15));
                    seed.push_back({0U, position, false, 0U, true, palette});
                    selected.push_back(position);
                }
            }
        }
        seed.push_back({0U, {50, 1, 1}, false, 0U, true, 31U});
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Duplicate Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const SelectionBounds sourceBounds = SelectionBounds::FromCorners(
            {1, 1, 1}, {8, 8, 8});
        const bool selectedVolume = selectionService_.ApplySortedVolume(
            selected, sourceBounds, SelectionMode::Replace);

        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::D);
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolDuplicate;
        const auto duplicateButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            duplicateButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*duplicateButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection(), CanDuplicateSelection()});
        ExecuteInputCommand(EditorInputCommand::ToolDuplicate);
        const std::uint64_t revision = document->GetRevision();
        voxelDuplicateSmokePrepared_ = seeded && selectedVolume && inputReady &&
            toolbarReady && voxelToolState_.IsDuplicateActive() &&
            beginPreview({16, 0, 0}) &&
            transformPreviewModel_.RenderData().Plan.SourceVoxelCount == 512U &&
            transformPreviewModel_.RenderData().Plan.DestinationVoxelCount ==
                512U &&
            !transformPreviewModel_.RenderData().DrawSourceGhost &&
            !transformPreviewModel_.HasCollisions() &&
            !transformPreviewModel_.HasOutOfBounds() &&
            document->GetVoxelCount() == 513U &&
            document->GetRevision() == revision;
        if (!voxelDuplicateSmokePrepared_) return false;

        const auto release = selectionInteraction_.PointerUp();
        voxelDuplicateSmokeApplied_ =
            release.Mode == SelectionInteractionMode::DuplicatingContent &&
            ApplyVoxelDuplicate() && document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 1025U &&
            document->GetVoxel({1, 1, 1})->PaletteIndex == 4U &&
            document->GetVoxel({17, 1, 1})->PaletteIndex == 4U &&
            document->GetVoxel({24, 8, 8}).has_value() &&
            selectionService_.Count() == 512U &&
            selectionService_.Contains({17, 1, 1}) &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({17, 1, 1}, {24, 8, 8}) &&
            voxelToolState_.IsDuplicateActive() &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 1U)
    {
        if (!document || !voxelDuplicateSmokeApplied_) return false;
        UndoCommand();
        voxelDuplicateSmokeUndone_ = document->GetVoxelCount() == 513U &&
            document->HasVoxel({1, 1, 1}) &&
            !document->HasVoxel({17, 1, 1}) &&
            selectionService_.Contains({1, 1, 1}) &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({1, 1, 1}, {8, 8, 8});
    }
    else if (frame == 2U)
    {
        if (!document || !voxelDuplicateSmokeUndone_) return false;
        RedoCommand();
        voxelDuplicateSmokeRedone_ = document->GetVoxelCount() == 1025U &&
            document->HasVoxel({1, 1, 1}) &&
            document->GetVoxel({17, 1, 1})->PaletteIndex == 4U &&
            selectionService_.Contains({17, 1, 1});
    }
    else if (frame == 3U)
    {
        if (!document || !voxelDuplicateSmokeRedone_) return false;
        const std::uint64_t revision = document->GetRevision();
        voxelDuplicateSmokeRepeated_ = beginPreview({16, 0, 0});
        if (!voxelDuplicateSmokeRepeated_) return false;
        const auto release = selectionInteraction_.PointerUp();
        voxelDuplicateSmokeRepeated_ =
            release.Mode == SelectionInteractionMode::DuplicatingContent &&
            ApplyVoxelDuplicate() && document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 1537U &&
            document->HasVoxel({1, 1, 1}) &&
            document->HasVoxel({17, 1, 1}) &&
            document->GetVoxel({33, 1, 1})->PaletteIndex == 4U &&
            selectionService_.Contains({33, 1, 1}) &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({33, 1, 1}, {40, 8, 8});
    }
    else if (frame == 4U)
    {
        if (!document || !voxelDuplicateSmokeRepeated_) return false;
        const std::uint64_t revision = document->GetRevision();
        const std::size_t undoCount = voxelEditHistory_.UndoCount();
        const SelectionBounds source = selectionService_.EditableBounds();
        const auto rejected = [this, &beginPreview](
            const Asset::Voxel::VoxelPosition delta)
        {
            if (!beginPreview(delta)) return false;
            static_cast<void>(selectionInteraction_.PointerUp());
            return !ApplyVoxelDuplicate();
        };
        const bool sourceOverlap = rejected({1, 0, 0});
        const bool externalCollision = rejected({10, 0, 0});
        const bool outside = rejected({32, 0, 0});
        const bool beganCancel = beginPreview({-8, 0, 0});
        if (beganCancel) CancelSelectionInteraction();
        voxelDuplicateSmokeRejected_ = sourceOverlap && externalCollision &&
            outside && beganCancel && document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == undoCount &&
            document->GetVoxelCount() == 1537U &&
            selectionService_.EditableBounds() == source &&
            !transformPreviewModel_.IsActive() &&
            !selectionInteraction_.IsActive();
    }
    else if (frame == 5U)
    {
        voxelDuplicateSmokeSaved_ = voxelDuplicateSmokeRejected_ &&
            SaveVoxelModel() && document && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelDuplicateSmokePath_);
    }
    else if (frame == 6U)
    {
        if (!voxelDuplicateSmokeSaved_) return false;
        const bool previewBeforeDocumentChange = beginPreview({-8, 0, 0});
        ClearVoxelViewport();
        voxelDuplicateSmokeReopened_ = previewBeforeDocumentChange &&
            !transformPreviewModel_.IsActive() &&
            !selectionInteraction_.IsActive() &&
            OpenVoxInViewportNow(voxelDuplicateSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelDuplicateSmokeReopened_ = voxelDuplicateSmokeReopened_ &&
            document && document->GetVoxelCount() == 1537U &&
            document->HasVoxel({1, 1, 1}) &&
            document->HasVoxel({17, 1, 1}) &&
            document->HasVoxel({33, 1, 1}) &&
            document->GetVoxel({50, 1, 1})->PaletteIndex == 31U;
    }
    else if (frame == 7U)
    {
        if (!voxelDuplicateSmokeReopened_) return false;
        CloseProject();
        voxelDuplicateSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !selectionInteraction_.IsActive() &&
            !transformPreviewModel_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelDuplicateSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelDuplicateSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelDuplicateSmokePassed();
}

bool EditorWorkspace::VoxelDuplicateSmokePassed() const noexcept
{
    return voxelDuplicateSmokePrepared_ && voxelDuplicateSmokeApplied_ &&
        voxelDuplicateSmokeUndone_ && voxelDuplicateSmokeRedone_ &&
        voxelDuplicateSmokeRepeated_ && voxelDuplicateSmokeRejected_ &&
        voxelDuplicateSmokeSaved_ && voxelDuplicateSmokeReopened_ &&
        voxelDuplicateSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelRotateSmokeStep(const std::size_t frame)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<Asset::Voxel::VoxelPosition> source{
        {2, 1, 2}, {2, 1, 3}, {2, 1, 4}, {3, 1, 2}};
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({2, 1, 2}, {3, 1, 4});

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"RotateSmoke", {16U, 16U, 16U}});
        voxelRotateSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;

        const std::vector<VoxelChange> seed{
            {0U, source[0], false, 0U, true, 3U},
            {0U, source[1], false, 0U, true, 5U},
            {0U, source[2], false, 0U, true, 7U},
            {0U, source[3], false, 0U, true, 11U}};
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Rotate Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace);
        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::R);
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolRotate;
        const auto rotateButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            rotateButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*rotateButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection(), CanDuplicateSelection(),
                 CanRotateSelection()});
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::ToolRotate);
        EditorInputFrame leftShortcut;
        leftShortcut.SetPressed(EditorInputKey::Q);
        EditorInputFrame rightShortcut;
        rightShortcut.Shift = true;
        rightShortcut.SetPressed(EditorInputKey::Q);
        EditorInputFrame applyShortcut;
        applyShortcut.SetPressed(EditorInputKey::Enter);
        const EditorCommandAvailability rotateAvailability =
            CurrentCommandAvailability();
        voxelRotateSmokePrepared_ = seeded && selected && inputReady &&
            toolbarReady && voxelToolState_.IsRotateActive() &&
            !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == sourceBounds &&
            editorInputService_.Resolve(leftShortcut, rotateAvailability) ==
                EditorInputCommand::RotateLeft &&
            editorInputService_.Resolve(rightShortcut, rotateAvailability) ==
                EditorInputCommand::RotateRight &&
            editorInputService_.Resolve(applyShortcut, rotateAvailability) ==
                EditorInputCommand::None &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == 1U;
    }
    else if (frame == 1U)
    {
        if (!document || !voxelRotateSmokePrepared_) return false;
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::RotateLeft);
        std::vector<Asset::Voxel::VoxelPosition> leftDestinations;
        leftDestinations.reserve(transformPreviewModel_.VoxelCount());
        for (const TransformPreviewVoxel& voxel :
            transformPreviewModel_.Voxels())
            leftDestinations.push_back(voxel.PreviewPosition);
        const bool leftReady = voxelRotateDirection_ ==
            VoxelRotationDirection::CounterClockwise &&
            transformPreviewModel_.IsActive() &&
            !transformPreviewModel_.HasCollisions();
        ExecuteInputCommand(EditorInputCommand::RotateRight);
        std::vector<Asset::Voxel::VoxelPosition> rightDestinations;
        rightDestinations.reserve(transformPreviewModel_.VoxelCount());
        for (const TransformPreviewVoxel& voxel :
            transformPreviewModel_.Voxels())
            rightDestinations.push_back(voxel.PreviewPosition);
        const bool rightReady = voxelRotateDirection_ ==
            VoxelRotationDirection::Clockwise &&
            transformPreviewModel_.IsActive() &&
            leftDestinations != rightDestinations;
        voxelRotateSmokeApplied_ = leftReady && rightReady &&
            ApplyVoxelRotate() &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 4U &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({2, 1, 2}, {4, 1, 3}) &&
            voxelToolState_.IsRotateActive() &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 2U)
    {
        if (!document || !voxelRotateSmokeApplied_) return false;
        UndoCommand();
        const bool undone = selectionService_.EditableBounds() == sourceBounds &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
        RedoCommand();
        voxelRotateSmokeUndoRedo_ = undone &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({2, 1, 2}, {4, 1, 3}) &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 3U)
    {
        if (!document || !voxelRotateSmokeUndoRedo_) return false;
        bool turns = true;
        for (int turn = 0; turn < 3; ++turn)
            turns = turns && BeginVoxelRotatePreview(
                VoxelRotationDirection::Clockwise) && ApplyVoxelRotate();
        voxelRotateSmokeCycled_ = turns && document->GetVoxelCount() == 4U &&
            selectionService_.EditableBounds() == sourceBounds &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[1])->PaletteIndex == 5U &&
            document->GetVoxel(source[2])->PaletteIndex == 7U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
    }
    else if (frame == 4U)
    {
        voxelRotateSmokeSaved_ = voxelRotateSmokeCycled_ && SaveVoxelModel() &&
            document && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelRotateSmokePath_);
        if (!voxelRotateSmokeSaved_) return false;
        ClearVoxelViewport();
        voxelRotateSmokeReopened_ =
            OpenVoxInViewportNow(voxelRotateSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelRotateSmokeReopened_ = voxelRotateSmokeReopened_ && document &&
            document->GetVoxelCount() == 4U &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
    }
    else if (frame == 5U)
    {
        if (!document || !voxelRotateSmokeReopened_) return false;
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        static_cast<void>(selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace));
        const std::uint64_t revision = document->GetRevision();
        SelectVoxelTool(ActiveVoxelTool::Rotate);
        const bool activationStable = !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == sourceBounds;
        const bool previewReady = BeginVoxelRotatePreview(
            VoxelRotationDirection::CounterClockwise) &&
            transformPreviewModel_.IsActive();
        ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        const bool cancelled = activationStable && previewReady &&
            voxelToolState_.IsSelectionActive() &&
            !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == sourceBounds &&
            document->GetRevision() == revision;
        CloseProject();
        voxelRotateSmokeCleaned_ = cancelled &&
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !transformPreviewModel_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelRotateSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelRotateSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelRotateSmokePassed();
}

bool EditorWorkspace::VoxelRotateSmokePassed() const noexcept
{
    return voxelRotateSmokePrepared_ && voxelRotateSmokeApplied_ &&
        voxelRotateSmokeUndoRedo_ && voxelRotateSmokeCycled_ &&
        voxelRotateSmokeSaved_ && voxelRotateSmokeReopened_ &&
        voxelRotateSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelMirrorSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<VoxelPosition> source{
        {2, 1, 2}, {2, 1, 3}, {2, 1, 4}, {3, 1, 2}};
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({2, 1, 2}, {3, 1, 4});

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"MirrorSmoke", {64U, 64U, 64U}});
        voxelMirrorSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const std::vector<VoxelChange> seed{
            {0U, source[0], false, 0U, true, 3U},
            {0U, source[1], false, 0U, true, 5U},
            {0U, source[2], false, 0U, true, 7U},
            {0U, source[3], false, 0U, true, 11U}};
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Mirror Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace);
        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::H);
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolMirror;
        const auto mirrorButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            mirrorButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*mirrorButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection(), CanDuplicateSelection(),
                 CanRotateSelection(), CanMirrorSelection()});
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::ToolMirror);
        EditorInputFrame x;
        x.SetPressed(EditorInputKey::X);
        EditorInputFrame z;
        z.SetPressed(EditorInputKey::Z);
        EditorInputFrame apply;
        apply.SetPressed(EditorInputKey::Enter);
        const EditorCommandAvailability availability =
            CurrentCommandAvailability();
        voxelMirrorSmokePrepared_ = seeded && selected && inputReady &&
            toolbarReady && voxelToolState_.IsMirrorActive() &&
            !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == sourceBounds &&
            editorInputService_.Resolve(x, availability) ==
                EditorInputCommand::MirrorX &&
            editorInputService_.Resolve(z, availability) ==
                EditorInputCommand::MirrorZ &&
            editorInputService_.Resolve(apply, availability) ==
                EditorInputCommand::None &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == 1U;
    }
    else if (frame == 1U)
    {
        if (!document || !voxelMirrorSmokePrepared_) return false;
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::MirrorX);
        std::vector<VoxelPosition> xDestinations;
        for (const TransformPreviewVoxel& voxel : transformPreviewModel_.Voxels())
            xDestinations.push_back(voxel.PreviewPosition);
        const bool xReady = voxelMirrorAxis_ == VoxelMirrorAxis::X &&
            transformPreviewModel_.IsActive() &&
            !transformPreviewModel_.HasCollisions();
        ExecuteInputCommand(EditorInputCommand::MirrorZ);
        std::vector<VoxelPosition> zDestinations;
        for (const TransformPreviewVoxel& voxel : transformPreviewModel_.Voxels())
            zDestinations.push_back(voxel.PreviewPosition);
        voxelMirrorSmokePreviewed_ = xReady &&
            voxelMirrorAxis_ == VoxelMirrorAxis::Z &&
            transformPreviewModel_.IsActive() &&
            xDestinations != zDestinations &&
            document->GetRevision() == revision &&
            selectionService_.EditableBounds() == sourceBounds;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelMirrorSmokePreviewed_) return false;
        const bool rendered = viewportRenderer_.HasTransformPreview() &&
            viewportRenderer_.TransformPreviewSourcePrimitiveCount() == 4U &&
            viewportRenderer_.TransformPreviewDestinationPrimitiveCount() == 4U;
        const std::uint64_t revision = document->GetRevision();
        voxelMirrorSmokeApplied_ = rendered && ApplyVoxelMirror() &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 4U &&
            voxelToolState_.IsMirrorActive() &&
            !transformPreviewModel_.IsActive() &&
            document->GetVoxel({2, 1, 4})->PaletteIndex == 3U &&
            document->GetVoxel({3, 1, 4})->PaletteIndex == 11U;
    }
    else if (frame == 3U)
    {
        if (!document || !voxelMirrorSmokeApplied_) return false;
        UndoCommand();
        const bool undone = selectionService_.EditableBounds() == sourceBounds &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
        RedoCommand();
        voxelMirrorSmokeUndoRedo_ = undone &&
            selectionService_.Count() == 4U &&
            !transformPreviewModel_.IsActive();
        voxelMirrorSmokeInvolutive_ = voxelMirrorSmokeUndoRedo_ &&
            BeginVoxelMirrorPreview(VoxelMirrorAxis::Z) &&
            ApplyVoxelMirror() &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
        const std::array<VoxelPosition, 2U> symmetric{{
            {6, 1, 6}, {7, 1, 6}}};
        const VoxelMirrorGeometry identity =
            MirrorVoxelSelectionOperation::BuildGeometry(
                symmetric,
                SelectionBounds::FromCorners({6, 1, 6}, {7, 1, 6}),
                VoxelMirrorAxis::X);
        std::vector<VoxelPosition> dense;
        dense.reserve(512U);
        for (std::int32_t z = 0; z < 8; ++z)
            for (std::int32_t y = 0; y < 8; ++y)
                for (std::int32_t x = 0; x < 8; ++x)
                    dense.push_back({x + 16, y + 1, z + 16});
        const VoxelMirrorGeometry large =
            MirrorVoxelSelectionOperation::BuildGeometry(
                dense,
                SelectionBounds::FromCorners({16, 1, 16}, {23, 8, 23}),
                VoxelMirrorAxis::X);
        voxelMirrorSmokeIdentity_ = identity.Valid() && identity.Identity &&
            large.Valid() && large.Destinations.size() == 512U;
    }
    else if (frame == 4U)
    {
        if (!document || !voxelMirrorSmokeInvolutive_ ||
            !voxelMirrorSmokeIdentity_) return false;
        const auto added =
            document->SetVoxel({3, 1, 3}, 19U);
        const bool preview = BeginVoxelMirrorPreview(VoxelMirrorAxis::X);
        voxelMirrorSmokeRejected_ = added.Changed && preview &&
            transformPreviewModel_.HasCollisions() &&
            !ApplyVoxelMirror() && document->HasVoxel({3, 1, 3}) &&
            document->RemoveVoxel({3, 1, 3}).Changed;
        static_cast<void>(selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace));
    }
    else if (frame == 5U)
    {
        voxelMirrorSmokeSaved_ = document && voxelMirrorSmokeRejected_ &&
            SaveVoxelModel() && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelMirrorSmokePath_);
        if (!voxelMirrorSmokeSaved_) return false;
        ClearVoxelViewport();
        voxelMirrorSmokeReopened_ =
            OpenVoxInViewportNow(voxelMirrorSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelMirrorSmokeReopened_ = voxelMirrorSmokeReopened_ && document &&
            document->GetVoxelCount() == 4U &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[3])->PaletteIndex == 11U;
    }
    else if (frame == 6U)
    {
        if (!document || !voxelMirrorSmokeReopened_) return false;
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        static_cast<void>(selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace));
        SelectVoxelTool(ActiveVoxelTool::Mirror);
        const bool preview = BeginVoxelMirrorPreview(VoxelMirrorAxis::X);
        PrepareForApplicationClose();
        const bool closeSafe = preview && !transformPreviewModel_.IsActive();
        CloseProject();
        voxelMirrorSmokeCleaned_ = closeSafe &&
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !transformPreviewModel_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelMirrorSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelMirrorSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelMirrorSmokePassed();
}

bool EditorWorkspace::VoxelMirrorSmokePassed() const noexcept
{
    return voxelMirrorSmokePrepared_ && voxelMirrorSmokePreviewed_ &&
        voxelMirrorSmokeApplied_ && voxelMirrorSmokeUndoRedo_ &&
        voxelMirrorSmokeInvolutive_ && voxelMirrorSmokeIdentity_ &&
        voxelMirrorSmokeRejected_ && voxelMirrorSmokeSaved_ &&
        voxelMirrorSmokeReopened_ && voxelMirrorSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelScaleSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<VoxelPosition> source{{2, 1, 2}, {3, 1, 2}};
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({2, 1, 2}, {3, 1, 2});
    const auto scaledPositions = []()
    {
        std::vector<VoxelPosition> result;
        result.reserve(16U);
        for (std::int32_t z = 2; z <= 3; ++z)
            for (std::int32_t y = 1; y <= 2; ++y)
                for (std::int32_t x = 2; x <= 5; ++x)
                    result.push_back({x, y, z});
        std::sort(result.begin(), result.end(),
            [](const VoxelPosition left, const VoxelPosition right)
            {
                if (left.X != right.X) return left.X < right.X;
                if (left.Y != right.Y) return left.Y < right.Y;
                return left.Z < right.Z;
            });
        return result;
    };

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"ScaleSmoke", {64U, 64U, 64U}});
        voxelScaleSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const std::vector<VoxelChange> seed{
            {0U, source[0], false, 0U, true, 3U},
            {0U, source[1], false, 0U, true, 11U}};
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Scale Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace);
        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::K);
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolScale;
        const auto scaleButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            scaleButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*scaleButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection(), CanDuplicateSelection(),
                 CanRotateSelection(), CanMirrorSelection(),
                 CanScaleSelection(), CanAlignSelection()});
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::ToolScale);
        EditorInputFrame apply;
        apply.SetPressed(EditorInputKey::Enter);
        voxelScaleSmokePrepared_ = seeded && selected && inputReady &&
            toolbarReady && voxelToolState_.IsScaleActive() &&
            !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == sourceBounds &&
            editorInputService_.Resolve(
                apply, CurrentCommandAvailability()) ==
                EditorInputCommand::None &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == 1U;
    }
    else if (frame == 1U)
    {
        if (!document || !voxelScaleSmokePrepared_) return false;
        const std::uint64_t revision = document->GetRevision();
        const auto preview = [this, document](
            const EditorInputCommand command,
            const VoxelScaleMode mode,
            const Asset::Voxel::VoxelDimensions dimensions,
            const std::size_t count)
        {
            ExecuteInputCommand(command);
            return voxelScaleMode_ == mode &&
                transformPreviewModel_.IsActive() &&
                transformPreviewModel_.HasExpandedDestinations() &&
                transformPreviewModel_.SourceVoxels().size() == 2U &&
                transformPreviewModel_.VoxelCount() == count &&
                transformPreviewModel_.PreviewBounds().Dimensions() ==
                    dimensions &&
                !transformPreviewModel_.HasCollisions() &&
                !transformPreviewModel_.HasOutOfBounds() &&
                document->GetVoxelCount() == 2U;
        };
        EditorInputFrame x;
        x.SetPressed(EditorInputKey::X);
        const bool contextualInput = editorInputService_.Resolve(
            x, CurrentCommandAvailability()) == EditorInputCommand::ScaleX;
        voxelScaleSmokePreviewed_ = contextualInput &&
            preview(EditorInputCommand::ScaleX, VoxelScaleMode::X,
                {4U, 1U, 1U}, 4U) &&
            preview(EditorInputCommand::ScaleY, VoxelScaleMode::Y,
                {2U, 2U, 1U}, 4U) &&
            preview(EditorInputCommand::ScaleZ, VoxelScaleMode::Z,
                {2U, 1U, 2U}, 4U) &&
            preview(EditorInputCommand::ScaleUniform,
                VoxelScaleMode::Uniform, {4U, 2U, 2U}, 16U) &&
            document->GetRevision() == revision &&
            selectionService_.EditableBounds() == sourceBounds;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelScaleSmokePreviewed_) return false;
        const bool rendered = viewportRenderer_.HasTransformPreview() &&
            viewportRenderer_.TransformPreviewSourcePrimitiveCount() == 2U &&
            viewportRenderer_.TransformPreviewDestinationPrimitiveCount() ==
                16U;
        const std::uint64_t revision = document->GetRevision();
        voxelScaleSmokeApplied_ = rendered && ApplyVoxelScale() &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 16U &&
            selectionService_.Count() == 16U &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({2, 1, 2}, {5, 2, 3}) &&
            document->GetVoxel({2, 2, 3})->PaletteIndex == 3U &&
            document->GetVoxel({5, 2, 3})->PaletteIndex == 11U &&
            voxelToolState_.IsScaleActive() &&
            !transformPreviewModel_.IsActive() &&
            voxelEditHistory_.UndoCount() == 2U;
    }
    else if (frame == 3U)
    {
        if (!document || !voxelScaleSmokeApplied_) return false;
        UndoCommand();
        const bool undone = document->GetVoxelCount() == 2U &&
            selectionService_.EditableBounds() == sourceBounds &&
            document->GetVoxel(source[0])->PaletteIndex == 3U &&
            document->GetVoxel(source[1])->PaletteIndex == 11U;
        RedoCommand();
        voxelScaleSmokeUndoRedo_ = undone &&
            document->GetVoxelCount() == 16U &&
            selectionService_.Count() == 16U &&
            voxelToolState_.IsScaleActive() &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 4U)
    {
        if (!document || !voxelScaleSmokeUndoRedo_) return false;
        const auto collisionVoxel = document->SetVoxel({9, 1, 2}, 19U);
        const bool collisionPreview =
            BeginVoxelScalePreview(VoxelScaleMode::X);
        const bool collisionRejected = collisionVoxel.Changed &&
            collisionPreview && transformPreviewModel_.HasCollisions() &&
            !ApplyVoxelScale() && document->HasVoxel({9, 1, 2}) &&
            document->RemoveVoxel({9, 1, 2}).Changed;

        const auto edgeA = document->SetVoxel({62, 1, 1}, 5U);
        const auto edgeB = document->SetVoxel({63, 1, 1}, 7U);
        const std::array<VoxelPosition, 2U> edge{{{62, 1, 1}, {63, 1, 1}}};
        static_cast<void>(selectionService_.ApplySortedVolume(
            edge, SelectionBounds::FromCorners(edge[0], edge[1]),
            SelectionMode::Replace));
        const bool outsidePreview =
            BeginVoxelScalePreview(VoxelScaleMode::X);
        const bool outsideRejected = edgeA.Changed && edgeB.Changed &&
            outsidePreview && transformPreviewModel_.HasOutOfBounds() &&
            !ApplyVoxelScale() && document->RemoveVoxel(edge[0]).Changed &&
            document->RemoveVoxel(edge[1]).Changed;
        const std::vector<VoxelPosition> restored = scaledPositions();
        static_cast<void>(selectionService_.ApplySortedVolume(
            restored, SelectionBounds::FromCorners({2, 1, 2}, {5, 2, 3}),
            SelectionMode::Replace));
        voxelScaleSmokeRejected_ = collisionRejected && outsideRejected &&
            document->GetVoxelCount() == 16U;
    }
    else if (frame == 5U)
    {
        voxelScaleSmokeSaved_ = document && voxelScaleSmokeRejected_ &&
            SaveVoxelModel() && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelScaleSmokePath_);
        if (!voxelScaleSmokeSaved_) return false;
        ClearVoxelViewport();
        voxelScaleSmokeReopened_ = OpenVoxInViewportNow(voxelScaleSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelScaleSmokeReopened_ = voxelScaleSmokeReopened_ && document &&
            document->GetVoxelCount() == 16U &&
            document->GetVoxel({2, 2, 3})->PaletteIndex == 3U &&
            document->GetVoxel({5, 2, 3})->PaletteIndex == 11U;
    }
    else if (frame == 6U)
    {
        if (!document || !voxelScaleSmokeReopened_) return false;
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const std::vector<VoxelPosition> selected = scaledPositions();
        static_cast<void>(selectionService_.ApplySortedVolume(
            selected, SelectionBounds::FromCorners({2, 1, 2}, {5, 2, 3}),
            SelectionMode::Replace));
        SelectVoxelTool(ActiveVoxelTool::Scale);
        const bool preview = BeginVoxelScalePreview(VoxelScaleMode::Uniform);
        PrepareForApplicationClose();
        const bool closeSafe = preview && !transformPreviewModel_.IsActive();
        CloseProject();
        voxelScaleSmokeCleaned_ = closeSafe &&
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !transformPreviewModel_.IsActive() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                voxelScaleSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelScaleSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelScaleSmokePassed();
}

bool EditorWorkspace::VoxelScaleSmokePassed() const noexcept
{
    return voxelScaleSmokePrepared_ && voxelScaleSmokePreviewed_ &&
        voxelScaleSmokeApplied_ && voxelScaleSmokeUndoRedo_ &&
        voxelScaleSmokeRejected_ && voxelScaleSmokeSaved_ &&
        voxelScaleSmokeReopened_ && voxelScaleSmokeCleaned_;
}

bool EditorWorkspace::RunVoxelAlignSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionBounds initialBounds =
        SelectionBounds::FromCorners({2, 1, 2}, {3, 1, 2});
    const SelectionBounds leftBounds =
        SelectionBounds::FromCorners({0, 1, 2}, {1, 1, 2});
    const std::array<VoxelPosition, 2U> initial{{{2, 1, 2}, {3, 1, 2}}};
    const std::array<VoxelPosition, 2U> left{{{0, 1, 2}, {1, 1, 2}}};

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"AlignSmoke", {64U, 64U, 64U}});
        voxelAlignSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Align Smoke",
                {{0U, initial[0], false, 0U, true, 3U},
                 {0U, initial[1], false, 0U, true, 11U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            initial, initialBounds, SelectionMode::Replace);
        EditorInputFrame shortcut;
        shortcut.SetPressed(EditorInputKey::A);
        shortcut.Shift = true;
        const bool inputReady = editorInputService_.Resolve(
            shortcut, CurrentCommandAvailability()) ==
            EditorInputCommand::ToolAlign;
        const auto alignButton = std::find_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [](const EditorToolbarButton& button)
            {
                return button.Action == EditorToolbarAction::Transform;
            });
        const bool toolbarReady =
            alignButton != EditorToolbarModel::Buttons().end() &&
            EditorToolbarModel::IsEnabled(*alignButton,
                {true, document->IsDirty(), voxelToolState_.ActiveTool(),
                 CanMoveSelection(), CanDuplicateSelection(),
                 CanRotateSelection(), CanMirrorSelection(),
                 CanScaleSelection(), CanAlignSelection()});
        const std::uint64_t revision = document->GetRevision();
        ExecuteInputCommand(EditorInputCommand::ToolAlign);
        voxelAlignSmokePrepared_ = seeded && selected && inputReady &&
            toolbarReady && voxelToolState_.IsAlignActive() &&
            !transformPreviewModel_.IsActive() &&
            selectionService_.EditableBounds() == initialBounds &&
            document->GetRevision() == revision;
    }
    else if (frame == 1U)
    {
        if (!document || !voxelAlignSmokePrepared_) return false;
        struct DirectionExpectation final
        {
            VoxelAlignDirection Direction;
            VoxelPosition Delta;
        };
        constexpr std::array<DirectionExpectation, 6U> expectations{{
            {VoxelAlignDirection::Left, {-2, 0, 0}},
            {VoxelAlignDirection::Right, {60, 0, 0}},
            {VoxelAlignDirection::Bottom, {0, -1, 0}},
            {VoxelAlignDirection::Top, {0, 62, 0}},
            {VoxelAlignDirection::Front, {0, 0, -2}},
            {VoxelAlignDirection::Back, {0, 0, 61}}}};
        const std::uint64_t revision = document->GetRevision();
        bool directionsValid = true;
        for (const auto& expectation : expectations)
        {
            directionsValid = directionsValid &&
                BeginVoxelAlignPreview(expectation.Direction) &&
                transformPreviewModel_.Delta() == expectation.Delta &&
                !transformPreviewModel_.HasCollisions() &&
                !transformPreviewModel_.HasOutOfBounds();
        }
        voxelAlignSmokeDirections_ = directionsValid &&
            BeginVoxelAlignPreview(VoxelAlignDirection::Left) &&
            document->GetRevision() == revision &&
            document->GetVoxelCount() == 2U;
    }
    else if (frame == 2U)
    {
        if (!document || !voxelAlignSmokeDirections_) return false;
        const bool rendered = viewportRenderer_.HasTransformPreview() &&
            viewportRenderer_.TransformPreviewSourcePrimitiveCount() == 2U &&
            viewportRenderer_.TransformPreviewDestinationPrimitiveCount() ==
                2U;
        const std::uint64_t revision = document->GetRevision();
        voxelAlignSmokeApplied_ = rendered && ApplyVoxelAlign() &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 2U &&
            document->GetVoxel(left[0])->PaletteIndex == 3U &&
            document->GetVoxel(left[1])->PaletteIndex == 11U &&
            selectionService_.EditableBounds() == leftBounds &&
            voxelEditHistory_.UndoCount() == 2U &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 3U)
    {
        if (!document || !voxelAlignSmokeApplied_) return false;
        UndoCommand();
        const bool undone = document->HasVoxel(initial[0]) &&
            document->HasVoxel(initial[1]) &&
            selectionService_.EditableBounds() == initialBounds;
        RedoCommand();
        voxelAlignSmokeUndoRedo_ = undone && document->HasVoxel(left[0]) &&
            document->HasVoxel(left[1]) &&
            selectionService_.EditableBounds() == leftBounds &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 4U)
    {
        if (!document || !voxelAlignSmokeUndoRedo_) return false;
        const std::uint64_t revision = document->GetRevision();
        const auto obstacle = document->SetVoxel({63, 1, 2}, 19U);
        const bool collisionPreview =
            BeginVoxelAlignPreview(VoxelAlignDirection::Right);
        const bool collisionRejected = obstacle.Changed && collisionPreview &&
            transformPreviewModel_.HasCollisions() && !ApplyVoxelAlign() &&
            document->HasVoxel({63, 1, 2}) &&
            document->RemoveVoxel({63, 1, 2}).Changed;
        static_cast<void>(selectionService_.ApplySortedVolume(
            left, leftBounds, SelectionMode::Replace));
        const bool outsidePreview = transformPreviewModel_.BeginPreview(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            0U, TransformPreviewCollisionPolicy::IgnoreSource) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {-1, 0, 0});
        const bool outsideRejected = outsidePreview &&
            transformPreviewModel_.HasOutOfBounds() && !ApplyVoxelAlign();
        voxelAlignSmokeRejected_ = collisionRejected && outsideRejected &&
            document->GetVoxelCount() == 2U &&
            document->GetRevision() == revision + 2U;
    }
    else if (frame == 5U)
    {
        voxelAlignSmokeSaved_ = document && voxelAlignSmokeRejected_ &&
            SaveVoxelModel() && !document->IsDirty() &&
            std::filesystem::is_regular_file(voxelAlignSmokePath_);
        if (!voxelAlignSmokeSaved_) return false;
        ClearVoxelViewport();
        voxelAlignSmokeReopened_ = OpenVoxInViewportNow(voxelAlignSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        voxelAlignSmokeReopened_ = voxelAlignSmokeReopened_ && document &&
            document->GetVoxelCount() == 2U &&
            document->GetVoxel(left[0])->PaletteIndex == 3U &&
            document->GetVoxel(left[1])->PaletteIndex == 11U;
    }
    else if (frame == 6U)
    {
        if (!document || !voxelAlignSmokeReopened_) return false;
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            left, leftBounds, SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Align);
        const bool aligned = BeginVoxelAlignPreview(
                VoxelAlignDirection::Right) && ApplyVoxelAlign();
        const bool pendingPreview = BeginVoxelAlignPreview(
            VoxelAlignDirection::Left);
        RequestExit();
        voxelAlignSmokeSaveOnExit_ = selected && aligned && pendingPreview &&
            document->IsDirty() && transformPreviewModel_.IsActive() &&
            closeRequest_.State() ==
                EditorCloseRequestState::WaitingForUser;
    }
    else if (frame == 7U)
    {
        if (!document || !voxelAlignSmokeSaveOnExit_) return false;
        const bool scheduled = closeRequest_.ScheduleSave();
        deferredDirtySaveRequested_ = scheduled;
        voxelAlignSmokeSaveOnExit_ = scheduled && document->IsDirty() &&
            transformPreviewModel_.IsActive() &&
            closeRequest_.State() ==
                EditorCloseRequestState::SavingBeforeClose;
    }
    else if (frame == 8U)
    {
        const Asset::Voxel::VoxDocumentLoadResult loaded =
            Asset::Voxel::VoxDocumentLoader{}.Load(voxelAlignSmokePath_);
        voxelAlignSmokeSaveOnExit_ = voxelAlignSmokeSaveOnExit_ && document &&
            closeRequest_.State() == EditorCloseRequestState::Closing &&
            !document->IsDirty() && !transformPreviewModel_.IsActive() &&
            loaded.Succeeded() && loaded.Document &&
            loaded.Document->GetVoxelCount() == 2U &&
            loaded.Document->HasVoxel({62, 1, 2}) &&
            loaded.Document->HasVoxel({63, 1, 2}) &&
            !std::filesystem::exists(
                voxelAlignSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                voxelAlignSmokePath_.string() + ".vfsave.bak");
    }
    return VoxelAlignSmokePassed();
}

bool EditorWorkspace::VoxelAlignSmokePassed() const noexcept
{
    return voxelAlignSmokePrepared_ && voxelAlignSmokeDirections_ &&
        voxelAlignSmokeApplied_ && voxelAlignSmokeUndoRedo_ &&
        voxelAlignSmokeRejected_ && voxelAlignSmokeSaved_ &&
        voxelAlignSmokeReopened_ && voxelAlignSmokeSaveOnExit_;
}

bool EditorWorkspace::RunTransformGizmoFoundationSmokeStep(
    const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    const SelectionBounds initialBounds =
        SelectionBounds::FromCorners({2, 2, 2}, {3, 3, 3});
    const SelectionBounds movedBounds =
        SelectionBounds::FromCorners({5, 2, 2}, {6, 3, 3});
    const std::array<VoxelPosition, 2U> initial{{{2, 2, 2}, {3, 3, 3}}};
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const auto apparentPixels = [](const TransformGizmoView& view)
    {
        for (const TransformGizmoAxisView& axis : view.Axes)
            if (!axis.CameraFacing && axis.ProjectedLengthPixels > 0.0F)
                return axis.ProjectedLengthPixels;
        return 0.0F;
    };

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"TransformGizmoSmoke", {64U, 64U, 64U}});
        transformGizmoSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Transform Gizmo Smoke",
                {{0U, initial[0], false, 0U, true, 3U},
                 {0U, initial[1], false, 0U, true, 11U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            initial, initialBounds, SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Move);
        transformGizmoSmokePrepared_ = seeded && selected &&
            !transformGizmoModel_.View().Visible &&
            document->GetVoxelCount() == 2U;
    }
    else if (frame == 2U)
    {
        UpdateTransformGizmo(720.0F);
        const TransformGizmoView& view = transformGizmoModel_.View();
        const auto hybridPixels = [](const float depth,
                                     const float viewportHeight,
                                     const SelectionBounds bounds)
        {
            TransformGizmoUpdateContext context;
            context.CameraPosition = {0.0F, 0.0F, -depth};
            context.CameraForward = {0.0F, 0.0F, 1.0F};
            context.ViewportHeightPixels = viewportHeight;
            context.Bounds = bounds;
            const TransformGizmoSizingResult sizing =
                TransformGizmoModel::CalculateSizing(context, {});
            return sizing.Visible && std::isfinite(sizing.WorldLength)
                ? sizing.ProjectedLengthPixels : -1.0F;
        };
        const auto selectionRelative = []
        {
            TransformGizmoModel model;
            TransformGizmoUpdateContext context;
            context.DocumentActive = true;
            context.SelectionEmpty = false;
            context.ActiveDocumentGeneration = 1U;
            context.SelectionDocumentGeneration = 1U;
            context.ActiveTool = ActiveVoxelTool::Move;
            context.CameraPosition = {0.0F, 0.0F, -20.0F};
            context.CameraForward = {0.0F, 0.0F, 1.0F};
            context.ViewportHeightPixels = 720.0F;
            context.Bounds =
                SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0});
            context.PivotValid = true;
            context.PivotWorldPosition = {};
            if (!model.Update(context)) return false;
            const float smallLength = model.View().AxisLength;
            context.Bounds =
                SelectionBounds::FromCorners({0, 0, 0}, {63, 63, 63});
            static_cast<void>(model.Update(context));
            return model.View().Visible &&
                model.View().AxisLength > smallLength;
        };
        const SelectionBounds unitBounds =
            SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0});
        const float nearPixels = hybridPixels(0.05F, 360.0F, unitBounds);
        const float mediumPixels = hybridPixels(20.0F, 720.0F, unitBounds);
        const float farPixels = hybridPixels(500.0F, 1440.0F, unitBounds);
        transformGizmoSmokeInitialLength_ = view.AxisLength;
        transformGizmoSmokeInitialPixels_ = apparentPixels(view);
        transformGizmoSmokeRendered_ = transformGizmoSmokePrepared_ &&
            view.Visible && view.Mode == TransformGizmoMode::Move &&
            view.State == TransformGizmoInteractionState::Idle &&
            view.ActiveAxis == TransformGizmoAxis::None &&
            view.Center == Vec3{-29.0F, -29.0F, -29.0F} &&
            viewportRenderer_.HasTransformGizmo() &&
            viewportRenderer_.TransformGizmoAxisPrimitiveCount() == 3U &&
            transformGizmoSmokeInitialPixels_ > 0.0F &&
            transformGizmoSmokeInitialPixels_ <=
                TransformGizmoModel::MaximumAxisLengthPixels + 1.0F &&
            nearPixels > 0.0F && nearPixels <=
                TransformGizmoModel::MaximumAxisLengthPixels + 1.0F &&
            mediumPixels > farPixels && farPixels > 0.0F &&
            selectionRelative() &&
            ViewportRenderer::TransformGizmoVisibleDepthTestEnabled &&
            !ViewportRenderer::TransformGizmoVisibleDepthWriteEnabled &&
            ViewportRenderer::TransformGizmoOccludedDepthTestEnabled &&
            !ViewportRenderer::TransformGizmoOccludedDepthWriteEnabled;
        viewportCamera_.Zoom(4.0F);
    }
    else if (frame == 3U)
    {
        UpdateTransformGizmo(720.0F);
        const TransformGizmoView& view = transformGizmoModel_.View();
        const float zoomedPixels = apparentPixels(view);
        const bool zoomInStable = transformGizmoSmokeRendered_ &&
            view.AxisLength <= transformGizmoSmokeInitialLength_ &&
            zoomedPixels >= transformGizmoSmokeInitialPixels_ &&
            zoomedPixels <=
                TransformGizmoModel::MaximumAxisLengthPixels + 1.0F;
        viewportCamera_.Zoom(-8.0F);
        transformGizmoSmokeScaleStable_ = zoomInStable;
    }
    else if (frame == 4U)
    {
        UpdateTransformGizmo(720.0F);
        const TransformGizmoView& view = transformGizmoModel_.View();
        transformGizmoSmokeScaleStable_ = transformGizmoSmokeScaleStable_ &&
            view.AxisLength >= transformGizmoSmokeInitialLength_ &&
            apparentPixels(view) < transformGizmoSmokeInitialPixels_;
        SelectVoxelTool(ActiveVoxelTool::Rotate);
    }
    else if (frame == 5U)
    {
        UpdateTransformGizmo(720.0F);
        transformGizmoSmokeModes_ = transformGizmoSmokeScaleStable_ &&
            transformGizmoModel_.View().Mode == TransformGizmoMode::Rotate;
        SelectVoxelTool(ActiveVoxelTool::Scale);
    }
    else if (frame == 6U)
    {
        UpdateTransformGizmo(720.0F);
        transformGizmoSmokeModes_ = transformGizmoSmokeModes_ &&
            transformGizmoModel_.View().Mode == TransformGizmoMode::Scale;
        SelectVoxelTool(ActiveVoxelTool::Pencil);
    }
    else if (frame == 7U)
    {
        const bool hiddenForPencil = !transformGizmoModel_.View().Visible &&
            !viewportRenderer_.HasTransformGizmo();
        SelectVoxelTool(ActiveVoxelTool::Move);
        const bool cleared = selectionService_.Clear();
        transformGizmoSmokeVisibility_ = transformGizmoSmokeModes_ &&
            hiddenForPencil && cleared;
    }
    else if (frame == 8U)
    {
        const bool hiddenWithoutSelection =
            !transformGizmoModel_.View().Visible &&
            !viewportRenderer_.HasTransformGizmo();
        const bool selected = selectionService_.ApplySortedVolume(
            initial, initialBounds, SelectionMode::Replace);
        transformGizmoSmokeVisibility_ = transformGizmoSmokeVisibility_ &&
            hiddenWithoutSelection && selected;
    }
    else if (frame == 9U)
    {
        if (!document || !transformGizmoSmokeVisibility_) return false;
        const Vec3 before = transformGizmoModel_.View().Center;
        const bool preview = transformPreviewModel_.BeginPreview(
            *document, selectionService_, voxelDocumentSession_.Generation()) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {3, 0, 0});
        const bool moved = preview && ApplyVoxelMove();
        transformGizmoSmokeMoveUndoRedo_ = moved &&
            selectionService_.EditableBounds() == movedBounds &&
            before == Vec3{-29.0F, -29.0F, -29.0F};
    }
    else if (frame == 10U)
    {
        transformGizmoSmokeMoveUndoRedo_ =
            transformGizmoSmokeMoveUndoRedo_ &&
            transformGizmoModel_.View().Center ==
                Vec3{-26.0F, -29.0F, -29.0F};
        UndoCommand();
    }
    else if (frame == 11U)
    {
        transformGizmoSmokeMoveUndoRedo_ =
            transformGizmoSmokeMoveUndoRedo_ &&
            transformGizmoModel_.View().Center ==
                Vec3{-29.0F, -29.0F, -29.0F} &&
            selectionService_.EditableBounds() == initialBounds;
        RedoCommand();
    }
    else if (frame == 12U)
    {
        if (!document || !transformGizmoSmokeMoveUndoRedo_) return false;
        const bool followedRedo = transformGizmoModel_.View().Center ==
            Vec3{-26.0F, -29.0F, -29.0F};
        const bool saved = SaveVoxelModel();
        ClearVoxelViewport();
        const bool purged = !transformGizmoModel_.View().Visible &&
            !viewportRenderer_.HasTransformGizmo();
        const bool reopened = saved &&
            OpenVoxInViewportNow(transformGizmoSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        if (reopened && document)
        {
            selectionService_.SetDocumentGeneration(
                voxelDocumentSession_.Generation());
            static_cast<void>(selectionService_.ApplySortedVolume(
                std::array<VoxelPosition, 2U>{{{5, 2, 2}, {6, 3, 3}}},
                movedBounds, SelectionMode::Replace));
            SelectVoxelTool(ActiveVoxelTool::Move);
        }
        transformGizmoSmokeDocumentReset_ = followedRedo && purged &&
            reopened && document && document->GetVoxelCount() == 2U;
    }
    else if (frame == 13U)
    {
        document = voxelDocumentSession_.ActiveDocument();
        if (!document || !transformGizmoSmokeDocumentReset_) return false;
        const bool rebuiltForNewDocument =
            transformGizmoModel_.View().Visible &&
            transformGizmoModel_.View().Mode == TransformGizmoMode::Move;
        const VoxelEditHistoryResult edited = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Dirty Transform Gizmo Smoke",
                {{0U, {10, 10, 10}, false, 0U, true, 17U}}});
        RequestExit();
        transformGizmoSmokeSaveOnExit_ = rebuiltForNewDocument && edited &&
            document->IsDirty() && closeRequest_.State() ==
                EditorCloseRequestState::WaitingForUser;
    }
    else if (frame == 14U)
    {
        const bool hiddenDuringClose =
            !transformGizmoModel_.View().Visible &&
            !viewportRenderer_.HasTransformGizmo();
        const bool scheduled = closeRequest_.ScheduleSave();
        deferredDirtySaveRequested_ = scheduled;
        transformGizmoSmokeSaveOnExit_ = transformGizmoSmokeSaveOnExit_ &&
            hiddenDuringClose && scheduled;
    }
    else if (frame == 15U)
    {
        const Asset::Voxel::VoxDocumentLoadResult loaded =
            Asset::Voxel::VoxDocumentLoader{}.Load(transformGizmoSmokePath_);
        transformGizmoSmokeSaveOnExit_ = transformGizmoSmokeSaveOnExit_ &&
            closeRequest_.State() == EditorCloseRequestState::Closing &&
            !transformGizmoModel_.View().Visible &&
            !viewportRenderer_.HasTransformGizmo() && loaded.Succeeded() &&
            loaded.Document && loaded.Document->HasVoxel({10, 10, 10}) &&
            !std::filesystem::exists(
                transformGizmoSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                transformGizmoSmokePath_.string() + ".vfsave.bak");
    }
    return TransformGizmoFoundationSmokePassed();
}

bool EditorWorkspace::TransformGizmoFoundationSmokePassed() const noexcept
{
    return transformGizmoSmokePrepared_ && transformGizmoSmokeRendered_ &&
        transformGizmoSmokeScaleStable_ && transformGizmoSmokeModes_ &&
        transformGizmoSmokeVisibility_ &&
        transformGizmoSmokeMoveUndoRedo_ &&
        transformGizmoSmokeDocumentReset_ && transformGizmoSmokeSaveOnExit_;
}

bool EditorWorkspace::RunMoveGizmoSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    const SelectionBounds initialBounds =
        SelectionBounds::FromCorners({2, 2, 2}, {9, 9, 9});
    const SelectionBounds movedBounds =
        SelectionBounds::FromCorners({5, 2, 2}, {12, 9, 9});
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();

    const auto makeView = []
    {
        TransformGizmoView view;
        view.Visible = true;
        view.Mode = TransformGizmoMode::Move;
        view.State = TransformGizmoInteractionState::Idle;
        view.AxisLength = 10.0F;
        view.AxisThickness = 0.25F;
        view.CenterRadius = 0.5F;
        view.Axes = {{
            {TransformGizmoAxis::X, {}, {10, 0, 0}, {1, 0, 0, 1}, 0.25F},
            {TransformGizmoAxis::Y, {}, {0, 10, 0}, {0, 1, 0, 1}, 0.25F},
            {TransformGizmoAxis::Z, {}, {0, 0, 10}, {0, 0, 1, 1}, 0.25F}}};
        return view;
    };
    const auto makeInput = [](const TransformGizmoAxis axis)
    {
        TransformGizmoPointerInput input;
        input.Viewport = {0, 0, 1000, 1000};
        input.ViewProjection = IdentityMatrix();
        input.ViewProjection[0] = 0.02F;
        input.ViewProjection[2] = -0.014F;
        input.ViewProjection[5] = 0.02F;
        input.ViewProjection[6] = 0.014F;
        input.ViewProjection[10] = 0.01F;
        input.ScreenPosition = axis == TransformGizmoAxis::X
            ? Vec2{575, 500}
            : axis == TransformGizmoAxis::Y
            ? Vec2{500, 425}
            : Vec2{447.5F, 447.5F};
        return input;
    };
    const auto screenStablePick = [](const float depth,
                                     const float viewportWidth,
                                     const float viewportHeight)
    {
        TransformGizmoUpdateContext context;
        context.DocumentActive = true;
        context.SelectionEmpty = false;
        context.ActiveDocumentGeneration = 1U;
        context.SelectionDocumentGeneration = 1U;
        context.Bounds =
            SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0});
        context.PivotValid = true;
        context.PivotWorldPosition = {};
        context.ActiveTool = ActiveVoxelTool::Move;
        context.CameraPosition = {0.0F, 0.0F, -depth};
        context.CameraForward = {0.0F, 0.0F, 1.0F};
        context.ViewportHeightPixels = viewportHeight;
        const float yScale = 1.0F / std::tan(
            DegreesToRadians(context.VerticalFieldOfViewDegrees) * 0.5F);
        const float xScale = yScale / (viewportWidth / viewportHeight);
        context.Viewport = {
            0.0F, 0.0F, viewportWidth, viewportHeight};
        context.ViewProjection = {
            xScale, 0.0F, 0.0F, 0.0F,
            0.0F, yScale, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F, depth};
        TransformGizmoModel model;
        if (!model.Update(context) || !model.View().Visible ||
            !model.View().Axes[0].HasArrowHead)
            return false;
        TransformGizmoPointerInput input;
        input.Viewport = context.Viewport;
        input.ViewProjection = context.ViewProjection;
        const TransformGizmoAxisView& xAxis = model.View().Axes[0];
        const auto start = TransformGizmoModel::ProjectWorldToScreen(
            xAxis.Start, input.Viewport, input.ViewProjection);
        const auto end = TransformGizmoModel::ProjectWorldToScreen(
            xAxis.End, input.Viewport, input.ViewProjection);
        if (!start || !end) return false;
        input.ScreenPosition = {
            (start->X + end->X) * 0.5F,
            (start->Y + end->Y) * 0.5F};
        TransformGizmoInteraction interaction;
        const bool segmentHit = interaction.UpdateHover(
            model.View(), input) == TransformGizmoAxis::X;
        input.ScreenPosition = *end;
        const bool arrowHit = interaction.UpdateHover(
            model.View(), input) == TransformGizmoAxis::X;
        return segmentHit && arrowHit;
    };
    const auto professionalStyle = []
    {
        TransformGizmoUpdateContext context;
        context.DocumentActive = true;
        context.SelectionEmpty = false;
        context.ActiveDocumentGeneration = 1U;
        context.SelectionDocumentGeneration = 1U;
        context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {7, 3, 2});
        context.PivotValid = true;
        context.PivotWorldPosition = {};
        context.ActiveTool = ActiveVoxelTool::Move;
        context.CameraPosition = {0.0F, 0.0F, -20.0F};
        context.CameraForward = {0.0F, 0.0F, 1.0F};
        context.ViewportHeightPixels = 720.0F;
        context.Viewport = {0.0F, 0.0F, 1280.0F, 720.0F};
        const float yScale = 1.0F / std::tan(
            DegreesToRadians(context.VerticalFieldOfViewDegrees) * 0.5F);
        const float xScale = yScale /
            (context.Viewport.Width / context.Viewport.Height);
        context.ViewProjection = {
            xScale, 0.0F, 0.0F, 0.0F,
            0.0F, yScale, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 20.0F};
        TransformGizmoModel model;
        if (!model.Update(context)) return false;
        const TransformGizmoView normal = model.View();
        constexpr std::array<std::array<float, 4U>, 3U> colors{
            GizmoStyle::AxisColorX,
            GizmoStyle::AxisColorY,
            GizmoStyle::AxisColorZ};
        for (std::size_t index = 0U; index < normal.Axes.size(); ++index)
        {
            const TransformGizmoAxisView& axis = normal.Axes[index];
            if (!axis.HasArrowHead || axis.ArrowWidth <= 0.0F ||
                axis.ArrowWidth >= axis.ArrowLength ||
                std::abs(axis.Color[index] -
                    colors[index][index] * GizmoStyle::IdleIntensity) >
                    0.001F)
                return false;
        }

        constexpr std::array<TransformGizmoAxis, 3U> axes{
            TransformGizmoAxis::X,
            TransformGizmoAxis::Y,
            TransformGizmoAxis::Z};
        for (const TransformGizmoAxis axis : axes)
        {
            const std::size_t index = static_cast<std::size_t>(axis) - 1U;
            context.InteractionState = TransformGizmoInteractionState::Hover;
            context.ActiveAxis = axis;
            if (!model.Update(context)) return false;
            const TransformGizmoView hovered = model.View();
            const float ratio = hovered.Axes[index].Thickness /
                normal.Axes[index].Thickness;
            if ((!hovered.Axes[index].CameraFacing &&
                    (ratio < 1.10F || ratio > 1.20F)) ||
                TransformGizmoModel::ContextHelpFor(
                    hovered.State, hovered.ActiveAxis).empty())
                return false;
            context.InteractionState = TransformGizmoInteractionState::Dragging;
            if (!model.Update(context)) return false;
            const TransformGizmoView dragged = model.View();
            if (dragged.Axes[index].Color != colors[index] ||
                (!dragged.Axes[index].CameraFacing &&
                    (dragged.Axes[index].Thickness >=
                        hovered.Axes[index].Thickness ||
                     dragged.Axes[index].Thickness <=
                        normal.Axes[index].Thickness)) ||
                TransformGizmoModel::ContextHelpFor(
                    dragged.State, dragged.ActiveAxis).empty())
                return false;
            for (std::size_t other = 0U; other < axes.size(); ++other)
                if (other != index &&
                    dragged.Axes[other].Color[other] >=
                        normal.Axes[other].Color[other])
                    return false;
        }
        context.InteractionState = TransformGizmoInteractionState::Idle;
        context.ActiveAxis = TransformGizmoAxis::None;
        if (!model.Update(context) || model.View().Axes != normal.Axes)
            return false;
        context.ActiveTool = ActiveVoxelTool::Rotate;
        if (!model.Update(context) ||
            !std::ranges::all_of(model.View().Axes,
                [](const TransformGizmoAxisView& axis)
                {
                    return axis.HasRotationRing && !axis.HasArrowHead;
                }))
            return false;
        context.ActiveTool = ActiveVoxelTool::Move;
        return model.Update(context) && model.View().Axes == normal.Axes;
    };

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"MoveGizmoSmoke", {32U, 32U, 32U}});
        moveGizmoSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        std::vector<VoxelChange> seed;
        std::vector<VoxelPosition> selected;
        seed.reserve(512U);
        selected.reserve(512U);
        for (std::int32_t x = 2; x <= 9; ++x)
            for (std::int32_t y = 2; y <= 9; ++y)
                for (std::int32_t z = 2; z <= 9; ++z)
                {
                    const VoxelPosition position{x, y, z};
                    seed.push_back({0U, position, false, 0U, true,
                        static_cast<std::uint8_t>(3 + (x + y + z) % 20)});
                    selected.push_back(position);
                }
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Move Gizmo Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selectionReady = selectionService_.ApplySortedVolume(
            selected, initialBounds, SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Move);
        moveGizmoSmokePrepared_ = seeded && selectionReady &&
            selectionService_.Count() == 512U && CanMoveSelection() &&
            document->GetVoxelCount() == 512U;
    }
    else if (frame == 1U)
    {
        if (!document || !moveGizmoSmokePrepared_) return false;
        const TransformGizmoView view = makeView();
        bool allAxes = true;
        for (const TransformGizmoAxis axis : {
                 TransformGizmoAxis::X,
                 TransformGizmoAxis::Y,
                 TransformGizmoAxis::Z})
        {
            auto input = makeInput(axis);
            allAxes = allAxes &&
                transformGizmoInteraction_.UpdateHover(view, input) == axis;
        }
        auto input = makeInput(TransformGizmoAxis::X);
        const EditorCameraState cameraBefore = viewportCamera_.CaptureState();
        const std::uint64_t revision = document->GetRevision();
        const bool began =
            transformGizmoInteraction_.UpdateHover(view, input) ==
                TransformGizmoAxis::X &&
            transformGizmoInteraction_.BeginDrag(
                view, TransformGizmoAxis::X, input,
                voxelDocumentSession_.Generation(), initialBounds) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation());
        input.ScreenPosition.X += 30.0F;
        const bool changed = began &&
            transformGizmoInteraction_.UpdateDrag(input) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(),
                transformGizmoInteraction_.Delta());
        UpdateTransformGizmo(720.0F);
        moveGizmoSmokeAxes_ = allAxes &&
            screenStablePick(0.05F, 640.0F, 360.0F) &&
            screenStablePick(250.0F, 2560.0F, 1440.0F) &&
            professionalStyle() && changed &&
            transformGizmoInteraction_.LockedAxis() == TransformGizmoAxis::X &&
            transformGizmoInteraction_.Delta() == VoxelPosition{3, 0, 0} &&
            transformPreviewModel_.VoxelCount() == 512U &&
            document->GetRevision() == revision &&
            viewportCamera_.CaptureState() == cameraBefore &&
            transformGizmoModel_.View().State ==
                TransformGizmoInteractionState::Dragging &&
            transformGizmoModel_.View().ActiveAxis == TransformGizmoAxis::X;
        const TransformGizmoDragRelease release =
            transformGizmoInteraction_.EndDrag();
        moveGizmoSmokeMoved_ = moveGizmoSmokeAxes_ && release.WasDragging &&
            release.Delta == VoxelPosition{3, 0, 0} && ApplyVoxelMove() &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 512U &&
            selectionService_.EditableBounds() == movedBounds &&
            !document->HasVoxel({2, 2, 2}) &&
            document->HasVoxel({12, 9, 9}) &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 2U)
    {
        if (!document || !moveGizmoSmokeMoved_) return false;
    }
    else if (frame == 3U)
    {
        if (!document || !moveGizmoSmokeMoved_) return false;
        UndoCommand();
        const bool undone = selectionService_.EditableBounds() == initialBounds &&
            document->HasVoxel({2, 2, 2}) && !document->HasVoxel({12, 9, 9});
        RedoCommand();
        moveGizmoSmokeUndoRedo_ = undone &&
            selectionService_.EditableBounds() == movedBounds &&
            document->HasVoxel({12, 9, 9}) &&
            voxelEditHistory_.UndoCount() == 2U;
    }
    else if (frame == 4U)
    {
        if (!document || !moveGizmoSmokeUndoRedo_) return false;
        const auto obstacle = document->SetVoxel({15, 2, 2}, 27U);
        const bool collision = obstacle.Changed &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation()) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {3, 0, 0}) &&
            transformPreviewModel_.HasCollisions() && !ApplyVoxelMove();
        const bool removed = document->RemoveVoxel({15, 2, 2}).Changed;
        const bool outside = transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation()) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(), {-6, 0, 0}) &&
            transformPreviewModel_.HasOutOfBounds() && !ApplyVoxelMove();
        moveGizmoSmokeRejected_ = collision && removed && outside &&
            selectionService_.EditableBounds() == movedBounds &&
            document->GetVoxelCount() == 512U;
    }
    else if (frame == 5U)
    {
        if (!document || !moveGizmoSmokeRejected_) return false;
        const TransformGizmoView view = makeView();
        auto input = makeInput(TransformGizmoAxis::Y);
        const std::uint64_t revision = document->GetRevision();
        const bool began =
            transformGizmoInteraction_.UpdateHover(view, input) ==
                TransformGizmoAxis::Y &&
            transformGizmoInteraction_.BeginDrag(
                view, TransformGizmoAxis::Y, input,
                voxelDocumentSession_.Generation(), movedBounds) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation());
        input.ScreenPosition.Y -= 20.0F;
        const bool previewed = began &&
            transformGizmoInteraction_.UpdateDrag(input) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(),
                transformGizmoInteraction_.Delta());
        CancelActiveInteraction();
        const bool escaped = previewed &&
            !transformGizmoInteraction_.IsDragging() &&
            !transformPreviewModel_.IsActive() &&
            document->GetRevision() == revision;
        input = makeInput(TransformGizmoAxis::X);
        const bool restarted =
            transformGizmoInteraction_.UpdateHover(view, input) ==
                TransformGizmoAxis::X &&
            transformGizmoInteraction_.BeginDrag(
                view, TransformGizmoAxis::X, input,
                voxelDocumentSession_.Generation(), movedBounds) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation());
        SelectVoxelTool(ActiveVoxelTool::Pencil);
        moveGizmoSmokeCancelled_ = escaped && restarted &&
            !transformGizmoInteraction_.IsDragging() &&
            !transformPreviewModel_.IsActive() &&
            document->GetRevision() == revision;
        SelectVoxelTool(ActiveVoxelTool::Move);
    }
    else if (frame == 6U)
    {
        if (!document || !moveGizmoSmokeCancelled_) return false;
        const TransformGizmoView view = makeView();
        auto input = makeInput(TransformGizmoAxis::Z);
        const bool began =
            transformGizmoInteraction_.UpdateHover(view, input) ==
                TransformGizmoAxis::Z &&
            transformGizmoInteraction_.BeginDrag(
                view, TransformGizmoAxis::Z, input,
                voxelDocumentSession_.Generation(), movedBounds) &&
            transformPreviewModel_.BeginPreview(
                *document, selectionService_,
                voxelDocumentSession_.Generation());
        input.ScreenPosition.X -= 14.2F;
        input.ScreenPosition.Y -= 14.2F;
        const bool pending = began &&
            transformGizmoInteraction_.UpdateDrag(input) &&
            transformPreviewModel_.SetDelta(
                *document, selectionService_,
                voxelDocumentSession_.Generation(),
                transformGizmoInteraction_.Delta());
        RequestExit();
        moveGizmoSmokeCleaned_ = pending && document->IsDirty() &&
            transformGizmoInteraction_.IsDragging() &&
            closeRequest_.State() == EditorCloseRequestState::WaitingForUser;
    }
    else if (frame == 7U)
    {
        if (!document || !moveGizmoSmokeCleaned_) return false;
        const bool scheduled = closeRequest_.ScheduleSave();
        deferredDirtySaveRequested_ = scheduled;
        moveGizmoSmokeCleaned_ = scheduled &&
            closeRequest_.State() == EditorCloseRequestState::SavingBeforeClose;
    }
    else if (frame == 8U)
    {
        const Asset::Voxel::VoxDocumentLoadResult loaded =
            Asset::Voxel::VoxDocumentLoader{}.Load(moveGizmoSmokePath_);
        moveGizmoSmokeCleaned_ = moveGizmoSmokeCleaned_ &&
            closeRequest_.State() == EditorCloseRequestState::Closing &&
            !transformGizmoInteraction_.IsDragging() &&
            !transformPreviewModel_.IsActive() &&
            !transformGizmoModel_.View().Visible && loaded.Succeeded() &&
            loaded.Document && loaded.Document->GetVoxelCount() == 512U &&
            loaded.Document->HasVoxel({5, 2, 2}) &&
            loaded.Document->HasVoxel({12, 9, 9}) &&
            !std::filesystem::exists(
                moveGizmoSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                moveGizmoSmokePath_.string() + ".vfsave.bak");
    }
    return MoveGizmoSmokePassed();
}

bool EditorWorkspace::MoveGizmoSmokePassed() const noexcept
{
    return moveGizmoSmokePrepared_ && moveGizmoSmokeAxes_ &&
        moveGizmoSmokeMoved_ && moveGizmoSmokeUndoRedo_ &&
        moveGizmoSmokeRejected_ && moveGizmoSmokeCancelled_ &&
        moveGizmoSmokeCleaned_;
}

bool EditorWorkspace::RunRotateGizmoSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    const std::vector<VoxelPosition> source{{4, 3, 4}, {4, 4, 4}, {4, 5, 4}};
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"RotateGizmoSmoke", {16U, 16U, 16U}});
        rotateGizmoSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        std::vector<VoxelChange> seed;
        for (std::size_t index = 0U; index < source.size(); ++index)
            seed.push_back({0U, source[index], false, 0U, true,
                static_cast<std::uint8_t>(3U + index)});
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Rotate Gizmo Smoke", seed});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, SelectionBounds::FromCorners({4, 3, 4}, {4, 5, 4}),
            SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Rotate);
        rotateGizmoSmokePrepared_ = seeded && selected &&
            document->GetVoxelCount() == 3U && CanRotateSelection();
    }
    else if (frame == 1U)
    {
        if (!document || !rotateGizmoSmokePrepared_) return false;
        TransformGizmoUpdateContext context;
        context.DocumentActive = true;
        context.SelectionEmpty = false;
        context.ActiveDocumentGeneration = 9U;
        context.SelectionDocumentGeneration = 9U;
        context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {5, 5, 5});
        context.PivotValid = true;
        context.PivotWorldPosition = {};
        context.ActiveTool = ActiveVoxelTool::Rotate;
        context.CameraPosition = {0, 0, -20};
        context.CameraForward = {0, 0, 1};
        context.ViewportHeightPixels = 1000;
        context.Viewport = {0, 0, 1000, 1000};
        context.ViewProjection = IdentityMatrix();
        context.ViewProjection[0] = 0.035F;
        context.ViewProjection[2] = -0.018F;
        context.ViewProjection[4] = 0.008F;
        context.ViewProjection[5] = 0.032F;
        context.ViewProjection[6] = 0.014F;
        TransformGizmoModel model;
        if (!model.Update(context)) return false;
        const TransformGizmoView view = model.View();
        TransformGizmoPointerInput input;
        input.Viewport = context.Viewport;
        input.ViewProjection = context.ViewProjection;
        bool allAxesPickable = true;
        for (const TransformGizmoAxisView& ring : view.Axes)
        {
            bool picked = false;
            for (const Vec3 point : ring.RotationRingPoints)
            {
                const auto screen = TransformGizmoModel::ProjectWorldToScreen(
                    point, input.Viewport, input.ViewProjection);
                if (!screen) continue;
                input.ScreenPosition = *screen;
                TransformGizmoInteraction probe;
                if (probe.UpdateHover(view, input) == ring.Axis)
                {
                    picked = true;
                    break;
                }
            }
            allAxesPickable = allAxesPickable && picked;
        }
        const std::uint64_t revision = document->GetRevision();
        const bool previewed = BeginVoxelRotatePreview(
            VoxelRotationAxis::X, 1);
        rotateGizmoSmokeInteractive_ = allAxesPickable && previewed &&
            view.Mode == TransformGizmoMode::Rotate &&
            std::ranges::all_of(view.Axes,
                [](const TransformGizmoAxisView& ring)
                { return ring.HasRotationRing && !ring.HasArrowHead; }) &&
            transformPreviewModel_.VoxelCount() == 3U &&
            document->GetRevision() == revision;
        rotateGizmoSmokeApplied_ = rotateGizmoSmokeInteractive_ &&
            ApplyVoxelRotate() && document->GetRevision() == revision + 1U &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({4, 4, 3}, {4, 4, 5});
    }
    else if (frame == 2U)
    {
        if (!document || !rotateGizmoSmokeApplied_) return false;
        UndoCommand();
        const bool undone = selectionService_.EditableBounds() ==
            SelectionBounds::FromCorners({4, 3, 4}, {4, 5, 4});
        RedoCommand();
        rotateGizmoSmokeUndoRedo_ = undone &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({4, 4, 3}, {4, 4, 5});
    }
    else if (frame == 3U)
    {
        if (!document || !rotateGizmoSmokeUndoRedo_) return false;
        const std::uint64_t revision = document->GetRevision();
        const bool previewed = BeginVoxelRotatePreview(
            VoxelRotationAxis::Z, -1);
        CancelVoxelRotate();
        rotateGizmoSmokeCancelled_ = previewed &&
            !transformPreviewModel_.IsActive() &&
            document->GetRevision() == revision;
    }
    else if (frame == 4U)
    {
        rotateGizmoSmokeSaved_ = rotateGizmoSmokeCancelled_ &&
            SaveVoxelModel() && document && !document->IsDirty();
    }
    else if (frame == 5U)
    {
        if (!rotateGizmoSmokeSaved_) return false;
        rotateGizmoSmokeReopened_ = OpenVoxInViewportNow(
            rotateGizmoSmokePath_);
        document = voxelDocumentSession_.ActiveDocument();
        rotateGizmoSmokeReopened_ = rotateGizmoSmokeReopened_ && document &&
            document->GetVoxelCount() == 3U &&
            document->HasVoxel({4, 4, 3}) &&
            document->HasVoxel({4, 4, 5});
    }
    else if (frame == 6U)
    {
        if (!rotateGizmoSmokeReopened_) return false;
        CloseProject();
        rotateGizmoSmokeReopened_ =
            voxelDocumentSession_.ActiveDocument() == nullptr &&
            !transformPreviewModel_.IsActive() &&
            !transformGizmoInteraction_.IsDragging();
    }
    return RotateGizmoSmokePassed();
}

bool EditorWorkspace::RotateGizmoSmokePassed() const noexcept
{
    return rotateGizmoSmokePrepared_ && rotateGizmoSmokeInteractive_ &&
        rotateGizmoSmokeApplied_ && rotateGizmoSmokeUndoRedo_ &&
        rotateGizmoSmokeCancelled_ && rotateGizmoSmokeSaved_ &&
        rotateGizmoSmokeReopened_;
}

bool EditorWorkspace::RunScaleGizmoSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<VoxelPosition> source{{2, 2, 2}, {3, 2, 2}, {4, 2, 2}};
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({2, 2, 2}, {4, 2, 2});

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"ScaleGizmoSmoke", {16U, 16U, 16U}});
        scaleGizmoSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Scale Gizmo Smoke",
                {{0U, source[0], false, 0U, true, 3U},
                 {0U, source[1], false, 0U, true, 7U},
                 {0U, source[2], false, 0U, true, 11U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, sourceBounds, SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Scale);
        scaleGizmoSmokePrepared_ = seeded && selected &&
            voxelToolState_.IsScaleActive() && CanScaleSelection() &&
            document->GetVoxelCount() == 3U;
    }
    else if (frame == 1U)
    {
        if (!document || !scaleGizmoSmokePrepared_) return false;
        TransformGizmoUpdateContext context;
        context.DocumentActive = true;
        context.SelectionEmpty = false;
        context.ActiveDocumentGeneration = 9U;
        context.SelectionDocumentGeneration = 9U;
        context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {5, 5, 5});
        context.PivotValid = true;
        context.PivotWorldPosition = {};
        context.ActiveTool = ActiveVoxelTool::Scale;
        context.CameraPosition = {0, 0, -20};
        context.CameraForward = {0, 0, 1};
        context.ViewportHeightPixels = 1000;
        context.Viewport = {0, 0, 1000, 1000};
        context.ViewProjection = IdentityMatrix();
        context.ViewProjection[0] = 0.035F;
        context.ViewProjection[2] = -0.018F;
        context.ViewProjection[4] = 0.008F;
        context.ViewProjection[5] = 0.032F;
        context.ViewProjection[6] = 0.014F;
        TransformGizmoModel model;
        if (!model.Update(context)) return false;
        const TransformGizmoView view = model.View();
        TransformGizmoPointerInput input;
        input.Viewport = context.Viewport;
        input.ViewProjection = context.ViewProjection;
        bool allAxesPickable = true;
        for (const TransformGizmoAxisView& handle : view.Axes)
        {
            const auto screen = TransformGizmoModel::ProjectWorldToScreen(
                handle.End, input.Viewport, input.ViewProjection);
            TransformGizmoInteraction probe;
            if (!screen)
            {
                allAxesPickable = false;
                continue;
            }
            input.ScreenPosition = *screen;
            allAxesPickable = allAxesPickable &&
                probe.UpdateHover(view, input) == handle.Axis;
        }
        const auto start = TransformGizmoModel::ProjectWorldToScreen(
            view.Axes[0].Start, input.Viewport, input.ViewProjection);
        const auto end = TransformGizmoModel::ProjectWorldToScreen(
            view.Axes[0].End, input.Viewport, input.ViewProjection);
        TransformGizmoInteraction drag;
        bool dragged = false;
        if (start && end)
        {
            input.ScreenPosition = *end;
            dragged = drag.UpdateHover(view, input) == TransformGizmoAxis::X &&
                drag.BeginDrag(view, TransformGizmoAxis::X, input, 9U,
                    context.Bounds);
            const float dx = end->X - start->X;
            const float dy = end->Y - start->Y;
            const float pixels = std::sqrt(dx * dx + dy * dy);
            const float worldLength = Length(
                view.Axes[0].End - view.Axes[0].Start);
            if (dragged && pixels > 1.0F && worldLength > 0.0F)
            {
                const float distance = 2.0F * pixels / worldLength;
                input.ScreenPosition = {
                    end->X + dx / pixels * distance,
                    end->Y + dy / pixels * distance};
                dragged = drag.UpdateDrag(input) &&
                    drag.TargetDimensions().X == 8U;
            }
        }
        const std::uint64_t revision = document->GetRevision();
        const bool previewed = transformPreviewModel_.BeginPreview(
                *document, selectionService_, voxelDocumentSession_.Generation()) &&
            UpdateVoxelScalePreview(VoxelScaleMode::X, {5U, 1U, 1U});
        scaleGizmoSmokeInteractive_ = allAxesPickable && dragged &&
            view.Mode == TransformGizmoMode::Scale &&
            std::ranges::all_of(view.Axes,
                [](const TransformGizmoAxisView& axis)
                { return axis.HasScaleHandle && !axis.HasArrowHead; }) &&
            previewed && transformPreviewModel_.VoxelCount() == 5U &&
            document->GetRevision() == revision;
        scaleGizmoSmokeApplied_ = scaleGizmoSmokeInteractive_ &&
            ApplyVoxelScale() && document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() == 5U &&
            selectionService_.EditableBounds() ==
                SelectionBounds::FromCorners({2, 2, 2}, {6, 2, 2});
    }
    else if (frame == 2U)
    {
        if (!document || !scaleGizmoSmokeApplied_) return false;
        UndoCommand();
        const bool expandedUndone = document->GetVoxelCount() == 3U &&
            selectionService_.EditableBounds() == sourceBounds;
        RedoCommand();
        const bool expandedRedone = document->GetVoxelCount() == 5U;
        const bool reduced = transformPreviewModel_.BeginPreview(
                *document, selectionService_, voxelDocumentSession_.Generation()) &&
            UpdateVoxelScalePreview(VoxelScaleMode::X, {3U, 1U, 1U}) &&
            ApplyVoxelScale();
        UndoCommand();
        const bool reductionUndone = document->GetVoxelCount() == 5U;
        RedoCommand();
        scaleGizmoSmokeUndoRedo_ = expandedUndone && expandedRedone &&
            reduced && reductionUndone && document->GetVoxelCount() == 3U &&
            selectionService_.EditableBounds() == sourceBounds;
    }
    else if (frame == 3U)
    {
        if (!document || !scaleGizmoSmokeUndoRedo_) return false;
        const auto obstacle = document->SetVoxel({6, 2, 2}, 19U);
        const bool collision = transformPreviewModel_.BeginPreview(
                *document, selectionService_, voxelDocumentSession_.Generation()) &&
            UpdateVoxelScalePreview(VoxelScaleMode::X, {5U, 1U, 1U}) &&
            transformPreviewModel_.HasCollisions() && !ApplyVoxelScale();
        const bool obstacleRemoved = document->RemoveVoxel({6, 2, 2}).Changed;
        const bool minimum = transformPreviewModel_.BeginPreview(
                *document, selectionService_, voxelDocumentSession_.Generation()) &&
            UpdateVoxelScalePreview(VoxelScaleMode::X, {1U, 1U, 1U}) &&
            transformPreviewModel_.PreviewBounds().Dimensions() ==
                Asset::Voxel::VoxelDimensions{1U, 1U, 1U};
        CancelTransformGizmoInteraction();
        SelectVoxelTool(ActiveVoxelTool::Move);
        scaleGizmoSmokeRejected_ = obstacle.Changed && collision &&
            obstacleRemoved && minimum && !transformPreviewModel_.IsActive() &&
            voxelToolState_.IsMoveActive();
    }
    else if (frame == 4U)
    {
        if (!document || !scaleGizmoSmokeRejected_) return false;
        const bool saved = SaveVoxelModel();
        CloseProject();
        scaleGizmoSmokeCleaned_ = saved &&
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !transformPreviewModel_.IsActive() &&
            !transformGizmoInteraction_.IsDragging() &&
            !std::filesystem::exists(
                scaleGizmoSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                scaleGizmoSmokePath_.string() + ".vfsave.bak");
    }
    return ScaleGizmoSmokePassed();
}

bool EditorWorkspace::ScaleGizmoSmokePassed() const noexcept
{
    return scaleGizmoSmokePrepared_ && scaleGizmoSmokeInteractive_ &&
        scaleGizmoSmokeApplied_ && scaleGizmoSmokeUndoRedo_ &&
        scaleGizmoSmokeRejected_ && scaleGizmoSmokeCleaned_;
}

bool EditorWorkspace::RunTransformGizmoManagerSmokeStep(
    const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<VoxelPosition> source{{2, 2, 2}, {3, 2, 2}, {4, 2, 2}};
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({2, 2, 2}, {4, 2, 2});

    const auto beginInteraction = [this, document, bounds](
        const ActiveVoxelTool tool,
        const std::size_t axisIndex) -> bool
    {
        if (!document) return false;
        SelectVoxelTool(tool);
        TransformGizmoUpdateContext visual;
        visual.DocumentActive = true;
        visual.SelectionEmpty = false;
        visual.ActiveDocumentGeneration = voxelDocumentSession_.Generation();
        visual.SelectionDocumentGeneration =
            selectionService_.DocumentGeneration();
        visual.Bounds = bounds;
        visual.ActiveTool = tool;
        visual.CameraPosition = {0.0F, 0.0F, -20.0F};
        visual.CameraForward = {0.0F, 0.0F, 1.0F};
        visual.ViewportHeightPixels = 1000.0F;
        visual.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
        visual.ViewProjection = IdentityMatrix();
        visual.ViewProjection[0] = 0.035F;
        visual.ViewProjection[2] = -0.018F;
        visual.ViewProjection[4] = 0.008F;
        visual.ViewProjection[5] = 0.032F;
        visual.ViewProjection[6] = 0.014F;
        static_cast<void>(transformPivotManager_.UpdateFromBounds(
            bounds, {3.0F, 2.0F, 2.0F}));
        if (!transformGizmoManager_.UpdateView(visual) ||
            axisIndex >= transformGizmoManager_.View().Axes.size())
            return false;

        TransformGizmoRuntimeContext runtime;
        runtime.DocumentOpen = true;
        runtime.SessionValid = true;
        runtime.SelectionValid = true;
        runtime.OperationAvailable = true;
        runtime.ViewportAvailable = true;
        runtime.PointerOverViewport = true;
        runtime.ActiveTool = tool;
        runtime.DocumentGeneration = voxelDocumentSession_.Generation();
        runtime.Bounds = bounds;
        if (transformGizmoManager_.UpdateContext(runtime)) return false;

        const TransformGizmoAxisView& axis =
            transformGizmoManager_.View().Axes[axisIndex];
        const Vec3 pickWorld = axis.HasRotationRing
            ? axis.RotationRingPoints[0U] : axis.End;
        TransformGizmoPointerInput input;
        input.Viewport = visual.Viewport;
        input.ViewProjection = visual.ViewProjection;
        const auto screen = TransformGizmoModel::ProjectWorldToScreen(
            pickWorld, input.Viewport, input.ViewProjection);
        if (!screen) return false;
        input.ScreenPosition = *screen;
        return transformGizmoManager_.UpdateHover(input) == axis.Axis &&
            transformGizmoManager_.BeginInteraction(input);
    };

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_,
            {"TransformGizmoManagerSmoke", {16U, 16U, 16U}});
        transformGizmoManagerSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Transform Gizmo Manager Smoke",
                {{0U, source[0], false, 0U, true, 3U},
                 {0U, source[1], false, 0U, true, 7U},
                 {0U, source[2], false, 0U, true, 11U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, bounds, SelectionMode::Replace);
        transformGizmoManagerSmokePrepared_ = seeded && selected &&
            document->GetVoxelCount() == 3U;
    }
    else if (frame == 1U)
    {
        if (!document || !transformGizmoManagerSmokePrepared_) return false;
        const std::uint64_t revision = document->GetRevision();
        const bool move = beginInteraction(ActiveVoxelTool::Move, 0U) &&
            transformGizmoManager_.HelpText().find("Moving on X") !=
                std::string_view::npos &&
            transformGizmoManager_.CancelInteraction();
        const bool rotate = beginInteraction(ActiveVoxelTool::Rotate, 1U) &&
            transformGizmoManager_.HelpText().find("Rotating on Y") !=
                std::string_view::npos &&
            transformGizmoManager_.CancelInteraction();
        const bool scale = beginInteraction(ActiveVoxelTool::Scale, 2U) &&
            transformGizmoManager_.HelpText().find("Scaling on Z") !=
                std::string_view::npos &&
            transformGizmoManager_.CancelInteraction();
        transformGizmoManagerSmokeModes_ = move && rotate && scale &&
            document->GetRevision() == revision &&
            voxelEditHistory_.UndoCount() == 1U;
    }
    else if (frame == 2U)
    {
        if (!document || !transformGizmoManagerSmokeModes_) return false;
        const bool moveBegan = beginInteraction(ActiveVoxelTool::Move, 0U);
        const TransformGizmoCancellation managerToolChange =
            transformGizmoManager_.OnToolChanged(ActiveVoxelTool::Rotate);
        const bool rotateBegan =
            beginInteraction(ActiveVoxelTool::Rotate, 1U);
        SelectVoxelTool(ActiveVoxelTool::Scale);
        const bool toolSwitchCancelled = rotateBegan &&
            !transformGizmoManager_.IsDragging();
        const bool scaleBegan = beginInteraction(ActiveVoxelTool::Scale, 2U);
        const bool explicitCancelled = static_cast<bool>(
            transformGizmoManager_.CancelInteraction());
        transformGizmoManagerSmokeTransitions_ = moveBegan &&
            managerToolChange && managerToolChange.Reason ==
                TransformGizmoCancellationReason::ToolChanged &&
            toolSwitchCancelled && scaleBegan && explicitCancelled;
    }
    else if (frame == 3U)
    {
        if (!document || !transformGizmoManagerSmokeTransitions_) return false;
        const bool selectionBegan =
            beginInteraction(ActiveVoxelTool::Move, 0U);
        TransformGizmoRuntimeContext invalid;
        invalid.DocumentOpen = true;
        invalid.SessionValid = true;
        invalid.SelectionValid = false;
        invalid.OperationAvailable = true;
        invalid.ViewportAvailable = true;
        invalid.PointerOverViewport = true;
        invalid.ActiveTool = ActiveVoxelTool::Move;
        invalid.DocumentGeneration = voxelDocumentSession_.Generation();
        invalid.Bounds = bounds;
        const TransformGizmoCancellation selectionLost =
            transformGizmoManager_.UpdateContext(invalid);
        const bool viewportBegan =
            beginInteraction(ActiveVoxelTool::Scale, 2U);
        invalid.SelectionValid = true;
        invalid.ActiveTool = ActiveVoxelTool::Scale;
        invalid.ViewportAvailable = false;
        const TransformGizmoCancellation viewportLost =
            transformGizmoManager_.UpdateContext(invalid);
        const bool pivotBegan =
            beginInteraction(ActiveVoxelTool::Move, 0U);
        transformPivotManager_.Invalidate();
        TransformGizmoRuntimeContext pivotInvalid;
        pivotInvalid.DocumentOpen = true;
        pivotInvalid.SessionValid = true;
        pivotInvalid.SelectionValid = true;
        pivotInvalid.OperationAvailable = true;
        pivotInvalid.ViewportAvailable = true;
        pivotInvalid.PointerOverViewport = true;
        pivotInvalid.ActiveTool = ActiveVoxelTool::Move;
        pivotInvalid.DocumentGeneration = voxelDocumentSession_.Generation();
        pivotInvalid.Bounds = bounds;
        const TransformGizmoCancellation pivotLost =
            transformGizmoManager_.UpdateContext(pivotInvalid);
        transformGizmoManagerSmokeInvalidation_ = selectionBegan &&
            selectionLost.Reason ==
                TransformGizmoCancellationReason::SelectionChanged &&
            viewportBegan && viewportLost.Reason ==
                TransformGizmoCancellationReason::ViewportUnavailable &&
            pivotBegan && pivotLost.Reason ==
                TransformGizmoCancellationReason::ContextInvalid &&
            !transformGizmoManager_.IsDragging() &&
            !transformPreviewModel_.IsActive();
    }
    else if (frame == 4U)
    {
        if (!document || !transformGizmoManagerSmokeInvalidation_) return false;
        CloseProject();
        transformGizmoManagerSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !transformGizmoManager_.IsVisible() &&
            !transformGizmoManager_.IsDragging() &&
            !transformPreviewModel_.IsActive() &&
            !std::filesystem::exists(
                transformGizmoManagerSmokePath_.string() + ".tmp") &&
            !std::filesystem::exists(
                transformGizmoManagerSmokePath_.string() + ".bak");
    }
    return TransformGizmoManagerSmokePassed();
}

bool EditorWorkspace::TransformGizmoManagerSmokePassed() const noexcept
{
    return transformGizmoManagerSmokePrepared_ &&
        transformGizmoManagerSmokeModes_ &&
        transformGizmoManagerSmokeTransitions_ &&
        transformGizmoManagerSmokeInvalidation_ &&
        transformGizmoManagerSmokeCleaned_;
}

bool EditorWorkspace::RunTransformPanelSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::vector<VoxelPosition> source{
        {2, 2, 2}, {3, 2, 2}, {2, 3, 2}};
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({2, 2, 2}, {3, 3, 2});

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"TransformPanelSmoke", {16U, 16U, 16U}});
        transformPanelSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult seeded = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Seed Transform Panel Smoke",
                {{0U, source[0], false, 0U, true, 3U},
                 {0U, source[1], false, 0U, true, 7U},
                 {0U, source[2], false, 0U, true, 11U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const bool selected = selectionService_.ApplySortedVolume(
            source, bounds, SelectionMode::Replace);
        const TransformPanelState state = transformPanelViewModel_.Read(
            CurrentTransformPanelSource());
        transformPanelSmokePrepared_ = seeded && selected &&
            state.Available && state.PivotMode == TransformPivotMode::Center &&
            state.RotationDegrees == Vec3{} &&
            state.Scale == Vec3{1.0F, 1.0F, 1.0F};
    }
    else if (frame == 1U)
    {
        if (!document || !transformPanelSmokePrepared_) return false;
        const TransformPanelState before = transformPanelViewModel_.Read(
            CurrentTransformPanelSource());
        const Vec3 requested = before.Position + Vec3{2.0F, 1.0F, 0.0F};
        const std::uint64_t revision = document->GetRevision();
        transformPanelSmokeMoved_ = ApplyTransformPanelPosition(requested) &&
            document->GetRevision() == revision + 1U &&
            transformPanelViewModel_.Read(CurrentTransformPanelSource()).Position ==
                requested;
    }
    else if (frame == 2U)
    {
        if (!document || !transformPanelSmokeMoved_) return false;
        const std::uint64_t revision = document->GetRevision();
        transformPanelSmokeRotated_ = ApplyTransformPanelRotation(
                {0.0F, 90.0F, 0.0F}) &&
            document->GetRevision() == revision + 1U &&
            transformPanelViewModel_.Read(CurrentTransformPanelSource()).Available;
    }
    else if (frame == 3U)
    {
        if (!document || !transformPanelSmokeRotated_) return false;
        const std::uint64_t revision = document->GetRevision();
        transformPanelSmokeScaled_ = ApplyTransformPanelScale(
                {1.0F, 2.0F, 1.0F}) &&
            document->GetRevision() == revision + 1U &&
            document->GetVoxelCount() > source.size();
    }
    else if (frame == 4U)
    {
        if (!document || !transformPanelSmokeScaled_) return false;
        static_cast<void>(transformPivotManager_.SetMode(
            TransformPivotMode::Bottom));
        const TransformPanelState bottom = transformPanelViewModel_.Read(
            CurrentTransformPanelSource());
        static_cast<void>(transformPivotManager_.SetMode(
            TransformPivotMode::Top));
        const TransformPanelState top = transformPanelViewModel_.Read(
            CurrentTransformPanelSource());
        static_cast<void>(transformPivotManager_.SetMode(
            TransformPivotMode::Center));
        const TransformPanelState center = transformPanelViewModel_.Read(
            CurrentTransformPanelSource());
        transformPanelSmokePivot_ = bottom.Available && top.Available &&
            center.Available && bottom.PivotMode == TransformPivotMode::Bottom &&
            top.PivotMode == TransformPivotMode::Top &&
            center.PivotMode == TransformPivotMode::Center &&
            bottom.Position.Y < center.Position.Y &&
            center.Position.Y < top.Position.Y;
    }
    else if (frame == 5U)
    {
        if (!document || !transformPanelSmokePivot_) return false;
        const std::size_t scaledCount = document->GetVoxelCount();
        UndoCommand();
        const bool undone = document->GetVoxelCount() == source.size() &&
            transformPanelViewModel_.Read(CurrentTransformPanelSource()).Available;
        RedoCommand();
        transformPanelSmokeUndoRedo_ = undone &&
            document->GetVoxelCount() == scaledCount &&
            transformPanelViewModel_.Read(CurrentTransformPanelSource()).Available;
    }
    else if (frame == 6U)
    {
        if (!document || !transformPanelSmokeUndoRedo_) return false;
        const bool saved = SaveVoxelModel();
        CloseProject();
        transformPanelSmokeCleaned_ = saved &&
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !transformPreviewModel_.IsActive() &&
            !transformGizmoManager_.IsDragging() &&
            !transformPivotManager_.HasValidPivot() &&
            !std::filesystem::exists(
                transformPanelSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                transformPanelSmokePath_.string() + ".vfsave.bak");
    }
    return TransformPanelSmokePassed();
}

bool EditorWorkspace::TransformPanelSmokePassed() const noexcept
{
    return transformPanelSmokePrepared_ && transformPanelSmokeMoved_ &&
        transformPanelSmokeRotated_ && transformPanelSmokeScaled_ &&
        transformPanelSmokePivot_ && transformPanelSmokeUndoRedo_ &&
        transformPanelSmokeCleaned_;
}

bool EditorWorkspace::RunSaveOnExitSmokeStep(const std::size_t frame)
{
    using Asset::Voxel::VoxelPosition;
    constexpr VoxelPosition savedVoxel{3, 2, 4};
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();

    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"SaveOnExitSmoke", {16U, 16U, 16U}});
        saveOnExitSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || !document) return false;
        const VoxelEditHistoryResult edited = voxelEditHistory_.Execute(
            *this, VoxelEditOperation{"Save On Exit Smoke",
                {{0U, savedVoxel, false, 0U, true, 9U}}});
        selectionService_.SetDocumentGeneration(
            voxelDocumentSession_.Generation());
        const SelectionBounds bounds =
            SelectionBounds::FromCorners(savedVoxel, savedVoxel);
        const std::array<VoxelPosition, 1U> selectedVoxels{savedVoxel};
        const bool selected = selectionService_.ApplySortedVolume(
            selectedVoxels, bounds, SelectionMode::Replace);
        SelectVoxelTool(ActiveVoxelTool::Scale);
        const bool previewStarted = BeginVoxelScalePreview(
            VoxelScaleMode::Uniform);
        RequestExit();
        saveOnExitSmokeRequested_ = edited && selected && previewStarted &&
            document->IsDirty() &&
            transformPreviewModel_.IsActive() &&
            dirtyActionConfirmation_.PendingAction() ==
                DestructiveAction::ExitApplication &&
            closeRequest_.State() ==
                EditorCloseRequestState::WaitingForUser;
    }
    else if (frame == 1U)
    {
        if (!document || !saveOnExitSmokeRequested_) return false;
        const bool scheduled = closeRequest_.ScheduleSave();
        deferredDirtySaveRequested_ = scheduled;
        saveOnExitSmokeCallbackDeferred_ = scheduled && document->IsDirty() &&
            voxelDocumentSession_.HasActiveDocument() &&
            transformPreviewModel_.IsActive() &&
            closeRequest_.State() ==
                EditorCloseRequestState::SavingBeforeClose;
    }
    else if (frame == 2U)
    {
        if (!document || !saveOnExitSmokeCallbackDeferred_) return false;
        const Asset::Voxel::VoxDocumentLoadResult loaded =
            Asset::Voxel::VoxDocumentLoader{}.Load(
                saveOnExitSmokePath_, document->AssetId());
        saveOnExitSmokePassed_ =
            closeRequest_.State() == EditorCloseRequestState::Closing &&
            voxelDocumentSession_.HasActiveDocument() && !document->IsDirty() &&
            !transformPreviewModel_.IsActive() &&
            !selectionInteraction_.IsActive() &&
            dragDropImport_.State() == DragDropImportState::Idle &&
            loaded.Succeeded() && loaded.Document &&
            loaded.Document->HasVoxel(savedVoxel) &&
            loaded.Document->GetVoxel(savedVoxel)->PaletteIndex == 9U &&
            !std::filesystem::exists(
                saveOnExitSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                saveOnExitSmokePath_.string() + ".vfsave.bak");
    }
    return SaveOnExitSmokePassed();
}

bool EditorWorkspace::SaveOnExitSmokePassed() const noexcept
{
    return saveOnExitSmokeRequested_ && saveOnExitSmokeCallbackDeferred_ &&
        saveOnExitSmokePassed_;
}

bool EditorWorkspace::RunModernToolbarSmokeStep(const std::size_t frame)
{
    const auto toolbarState = [this]()
    {
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        return EditorToolbarState{
            document != nullptr,
            document != nullptr && document->IsDirty() &&
                !voxelDocumentSaveService_.IsBusy(),
            voxelToolState_.ActiveTool(), CanMoveSelection(),
            CanDuplicateSelection(), CanRotateSelection(),
            CanMirrorSelection(), CanScaleSelection(), CanAlignSelection()};
    };
    const auto activeToolCount = [](const EditorToolbarState state)
    {
        return std::count_if(
            EditorToolbarModel::Buttons().begin(),
            EditorToolbarModel::Buttons().end(),
            [state](const EditorToolbarButton& button)
            {
                return EditorToolbarModel::IsActive(button, state);
            });
    };

    if (frame == 0U)
    {
        const EditorToolbarState state = toolbarState();
        modernToolbarSmokeDisabled_ = !state.HasDocument &&
            std::none_of(
                EditorToolbarModel::Buttons().begin(),
                EditorToolbarModel::Buttons().end(),
                [state](const EditorToolbarButton& button)
                {
                    return EditorToolbarModel::IsEnabled(button, state) ||
                        EditorToolbarModel::IsActive(button, state);
                });
    }
    else if (frame == 1U)
    {
        if (!modernToolbarSmokeDisabled_) return false;
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"ToolbarSmoke", {16U, 16U, 16U}});
        modernToolbarSmokePath_ = flow.Creation.ModelPath;
        Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        modernToolbarSmokeToolsEnabled_ = flow.Ready() && document != nullptr &&
            EditorToolbarModel::Buttons().size() == 3U &&
            std::all_of(
                EditorToolbarModel::Buttons().begin(),
                EditorToolbarModel::Buttons().end(),
                [state = toolbarState()](const EditorToolbarButton& button)
                {
                    const bool expectedEnabled =
                        button.Action == EditorToolbarAction::Pencil ||
                        button.Action == EditorToolbarAction::Selection;
                    return EditorToolbarModel::IsEnabled(button, state) ==
                        expectedEnabled;
                });
        if (!modernToolbarSmokeToolsEnabled_) return false;

        modernToolbarSmokeSingleActive_ = true;
        for (const EditorToolbarButton& button : EditorToolbarModel::Buttons())
        {
            if (!EditorToolbarModel::IsEnabled(button, toolbarState()) ||
                button.Tool == ActiveVoxelTool::None)
                continue;
            voxelToolState_.SetActiveTool(button.Tool);
            modernToolbarSmokeSingleActive_ &=
                activeToolCount(toolbarState()) == 1;
        }
        toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
        toolContext_.Smart.SetAction(SmartAction::Add);
        voxelToolState_.SetActiveTool(ActiveVoxelTool::Pencil);
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid,
            Asset::Voxel::VoxelPosition{8, 0, 8}, 1.0F};
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        modernToolbarSmokeSingleActive_ &= request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
            document->IsDirty() &&
            EditorToolbarModel::IsEnabled(
                EditorToolbarModel::Buttons().front(), toolbarState());
    }
    else if (frame == 2U)
    {
        Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        modernToolbarSmokeSaved_ = modernToolbarSmokeSingleActive_ &&
            document != nullptr && SaveVoxelModel() && !document->IsDirty();
    }
    else if (frame == 3U)
    {
        if (!modernToolbarSmokeSaved_) return false;
        CloseProject();
        const EditorToolbarState state = toolbarState();
        modernToolbarSmokeCleaned_ = !projectManager_.HasActiveProject() &&
            !state.HasDocument && activeToolCount(state) == 0 &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !std::filesystem::exists(
                modernToolbarSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                modernToolbarSmokePath_.string() + ".vfsave.bak");
    }
    return ModernToolbarSmokePassed();
}

bool EditorWorkspace::ModernToolbarSmokePassed() const noexcept
{
    return modernToolbarSmokeDisabled_ && modernToolbarSmokeToolsEnabled_ &&
        modernToolbarSmokeSingleActive_ && modernToolbarSmokeSaved_ &&
        modernToolbarSmokeCleaned_;
}

bool EditorWorkspace::RunKeyboardShortcutsSmokeStep(const std::size_t frame)
{
    const auto runShortcut = [this](
        const EditorInputKey key, const bool control = false)
    {
        EditorInputFrame input;
        input.Control = control;
        input.SetPressed(key);
        const EditorInputCommand command = editorInputService_.Resolve(
            input, CurrentCommandAvailability());
        ExecuteInputCommand(command);
        return command;
    };
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (frame == 0U)
    {
        const DirectCreationFlowResult flow = directCreationFlowService_.Create(
            voxelModelCreationService_, {"KeyboardSmoke", {16U, 16U, 16U}});
        keyboardShortcutsSmokePath_ = flow.Creation.ModelPath;
        document = voxelDocumentSession_.ActiveDocument();
        if (!flow.Ready() || document == nullptr ||
            runShortcut(EditorInputKey::P) !=
                EditorInputCommand::ToolPencil ||
            !voxelToolState_.IsPencilActive())
            return false;
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid,
            Asset::Voxel::VoxelPosition{8, 0, 8}, 1.0F};
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        keyboardShortcutsSmokeEdited_ = request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
            document->GetVoxelCount() == 1U;
    }
    else if (frame == 1U)
    {
        if (!document || !keyboardShortcutsSmokeEdited_) return false;
        const bool boxSelected = runShortcut(EditorInputKey::B) ==
                EditorInputCommand::ToolBox &&
            voxelToolState_.IsBoxActive();
        const bool boxApplied = boxSelected && voxelBoxInteraction_.Begin(
                {1, 0, 1}, voxelDocumentSession_.Generation()) &&
            voxelBoxInteraction_.Update(
                Asset::Voxel::VoxelPosition{2, 1, 2}) && ApplyVoxelBox();
        const bool lineSelected = runShortcut(EditorInputKey::L) ==
                EditorInputCommand::ToolLine &&
            voxelToolState_.IsLineActive();
        const bool lineApplied = lineSelected && voxelLineInteraction_.Begin(
                {4, 0, 4}, voxelDocumentSession_.Generation()) &&
            voxelLineInteraction_.Update(
                Asset::Voxel::VoxelPosition{6, 0, 4}) && ApplyVoxelLine();
        keyboardShortcutsSmokeTools_ = boxApplied && lineApplied &&
            document->GetVoxelCount() == 12U &&
            voxelEditHistory_.UndoCount() == 3U;
    }
    else if (frame == 2U)
    {
        if (!document || !keyboardShortcutsSmokeTools_) return false;
        keyboardShortcutsSmokeSaved_ =
            runShortcut(EditorInputKey::S, true) ==
                EditorInputCommand::FileSave &&
            std::filesystem::is_regular_file(keyboardShortcutsSmokePath_) &&
            !document->IsDirty();
    }
    else if (frame == 3U)
    {
        if (!document || !keyboardShortcutsSmokeSaved_) return false;
        keyboardShortcutsSmokeUndone_ =
            runShortcut(EditorInputKey::Z, true) ==
                EditorInputCommand::EditUndo &&
            document->GetVoxelCount() == 9U && document->IsDirty() &&
            voxelEditHistory_.CanRedo();
    }
    else if (frame == 4U)
    {
        if (!document || !keyboardShortcutsSmokeUndone_) return false;
        keyboardShortcutsSmokeRedone_ =
            runShortcut(EditorInputKey::Y, true) ==
                EditorInputCommand::EditRedo &&
            document->GetVoxelCount() == 12U && !document->IsDirty();
        SelectVoxelTool(ActiveVoxelTool::Box);
        const bool previewStarted = voxelBoxInteraction_.Begin(
            {10, 0, 10}, voxelDocumentSession_.Generation());
        keyboardShortcutsSmokeCancelled_ = keyboardShortcutsSmokeRedone_ &&
            previewStarted && runShortcut(EditorInputKey::Escape) ==
                EditorInputCommand::InteractionCancel &&
            !voxelBoxInteraction_.IsActive() &&
            document->GetVoxelCount() == 12U;
    }
    else if (frame == 5U)
    {
        if (!keyboardShortcutsSmokeCancelled_) return false;
        CloseProject();
        keyboardShortcutsSmokeCleaned_ =
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument() &&
            !voxelDocumentMeshCache_.HasMesh() &&
            !viewportRenderer_.HasModelMesh() &&
            !voxelEditHistory_.CanUndo() && !voxelEditHistory_.CanRedo() &&
            !voxelBoxInteraction_.IsActive() &&
            !voxelLineInteraction_.IsActive() &&
            !voxelSphereInteraction_.IsActive() &&
            !std::filesystem::exists(
                keyboardShortcutsSmokePath_.string() + ".vfsave.tmp") &&
            !std::filesystem::exists(
                keyboardShortcutsSmokePath_.string() + ".vfsave.bak");
    }
    return KeyboardShortcutsSmokePassed();
}

bool EditorWorkspace::KeyboardShortcutsSmokePassed() const noexcept
{
    return keyboardShortcutsSmokeTools_ && keyboardShortcutsSmokeEdited_ &&
        keyboardShortcutsSmokeSaved_ && keyboardShortcutsSmokeUndone_ &&
        keyboardShortcutsSmokeRedone_ && keyboardShortcutsSmokeCancelled_ &&
        keyboardShortcutsSmokeCleaned_;
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
        const ImGuiWindow* const inspectorWindow =
            ImGui::FindWindowByName(InspectorPanelWindowName);
        const ImGuiWindow* const transformWindow =
            ImGui::FindWindowByName(TransformPanelWindowName);
        layoutStabilitySmokeTransformDocked_ = inspectorWindow != nullptr &&
            transformWindow != nullptr && inspectorWindow->DockNode != nullptr &&
            inspectorWindow->DockNode == transformWindow->DockNode;
        if (!layoutStabilitySmokeCreated_ ||
            !layoutStabilitySmokeTransformDocked_ || document == nullptr ||
            !recordRectangle() ||
            voxelPlacementPreview_.Status !=
                VoxelPlacementPreviewStatus::Valid)
            return false;
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        layoutStabilitySmokePencilled_ = request &&
            smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() && ApplyVoxelPencil() &&
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
        layoutStabilitySmokeTransformDocked_ &&
        layoutStabilitySmokeRectanglesStable_ &&
        layoutStabilitySmokeReadyForShutdown_;
}

bool EditorWorkspace::RunCreateWorkspaceSmokeStep(const std::size_t frame)
{
    const auto officialLayoutIsValid = [this]()
    {
        const ImGuiWindow* const tools = ImGui::FindWindowByName(
            ToolsPanelWindowName);
        const ImGuiWindow* const toolOptions = ImGui::FindWindowByName(
            ToolOptionsPanelWindowName);
        const ImGuiWindow* const style = ImGui::FindWindowByName(
            StylePanelWindowName);
        const ImGuiWindow* const viewport = ImGui::FindWindowByName(
            ViewportPanelWindowName);
        const ImGuiWindow* const assets = ImGui::FindWindowByName(
            AssetsPanelWindowName);
        const ImGuiWindow* const scene = ImGui::FindWindowByName(
            ScenePanelWindowName);
        const ImGuiWindow* const inspector = ImGui::FindWindowByName(
            InspectorPanelWindowName);
        const ImGuiWindow* const transform = ImGui::FindWindowByName(
            TransformPanelWindowName);
        const ImGuiWindow* const console = ImGui::FindWindowByName("Console");
        if (tools == nullptr || toolOptions == nullptr || style == nullptr ||
            viewport == nullptr || assets == nullptr || scene == nullptr ||
            inspector == nullptr || transform == nullptr || console == nullptr ||
            tools->DockNode == nullptr || toolOptions->DockNode == nullptr ||
            style->DockNode == nullptr || viewport->DockNode == nullptr ||
            assets->DockNode == nullptr || scene->DockNode == nullptr ||
            inspector->DockNode == nullptr || transform->DockNode == nullptr ||
            console->DockNode == nullptr)
            return false;

        const bool leftStacked = tools->DockNode != toolOptions->DockNode &&
            toolOptions->DockNode != style->DockNode &&
            std::abs(tools->Pos.x - toolOptions->Pos.x) <= 1.0F &&
            std::abs(tools->Pos.x - style->Pos.x) <= 1.0F &&
            tools->Pos.y < toolOptions->Pos.y &&
            toolOptions->Pos.y < style->Pos.y;
        const bool rightTabs = assets->DockNode == scene->DockNode &&
            assets->DockNode == inspector->DockNode &&
            assets->DockNode == transform->DockNode &&
            assets->DockNode->SelectedTabId == ImHashStr(AssetsPanelWindowName);
        const float upperWidth = tools->Size.x + viewport->Size.x + assets->Size.x;
        const bool viewportDominant = upperWidth > 0.0F &&
            viewport->Size.x >= upperWidth * 0.75F - 2.0F;
        const bool consoleIsBottomOnly = console->DockNode != tools->DockNode &&
            console->DockNode != viewport->DockNode &&
            console->DockNode != assets->DockNode &&
            console->Pos.y >= viewport->Pos.y + viewport->Size.y - 2.0F;
        return leftStacked && rightTabs && viewportDominant && consoleIsBottomOnly;
    };

    if (frame == 0U)
    {
        createWorkspaceSmokeSeedLegacy_ = true;
        createWorkspaceSmokeMigrated_ = false;
        createWorkspaceSmokeReset_ = false;
        createWorkspaceSmokePassed_ = false;
        return true;
    }
    if (frame == 1U)
    {
        return createWorkspaceMigrationApplied_;
    }
    if (frame == 2U)
    {
        createWorkspaceSmokeMigrated_ = createWorkspaceMigrationApplied_ &&
            officialLayoutIsValid();
        resetLayoutRequested_ = true;
        return createWorkspaceSmokeMigrated_;
    }
    if (frame == 3U)
    {
        return createWorkspaceSmokeMigrated_;
    }
    if (frame == 4U)
    {
        createWorkspaceSmokeReset_ = officialLayoutIsValid();
        createWorkspaceSmokePassed_ = createWorkspaceSmokeMigrated_ &&
            createWorkspaceSmokeReset_;
    }
    return CreateWorkspaceSmokePassed();
}

bool EditorWorkspace::CreateWorkspaceSmokePassed() const noexcept
{
    return createWorkspaceSmokePassed_;
}

bool EditorWorkspace::RunDoubleClickCameraSmokeStep(const std::size_t frame)
{
    const auto applyActions = [this](const ViewportCameraActions& actions)
    {
        if (actions.FocusRequested) FocusSelectionOrFrameAll();
        if (actions.ResetRequested) viewportCamera_.Reset();
    };
    const auto verifyDoubleClick = [this, &applyActions]()
    {
        const ViewportCameraActions actions =
            ResolveViewportCameraActions({true, false, false, 2U});
        applyActions(actions);
        return !actions.FocusRequested && !actions.ResetRequested &&
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
        doubleClickCameraSmokeReference_ = viewportCamera_.CaptureState();
        return true;
    case 1U:
    {
        // The input resolver deliberately leaves pointer gestures to the
        // viewport host. The host selection is then focused as one gesture.
        const bool doubleClickIsNotAStandaloneCommand = verifyDoubleClick();
        static_cast<void>(selectionService_.Select({0, 1, 1}));
        static_cast<void>(viewportNavigation_.FocusSelection(
            SelectionNavigationBounds()));
        doubleClickCameraSmokeGridStable_ =
            doubleClickIsNotAStandaloneCommand && viewportNavigation_.IsFocusing();
        return doubleClickCameraSmokeGridStable_;
    }
    case 2U:
        viewportNavigation_.Tick(0.125F);
        doubleClickCameraSmokeVoxelStable_ = viewportNavigation_.IsFocusing() &&
            viewportCamera_.GetTarget() != doubleClickCameraSmokeReference_.Target;
        return doubleClickCameraSmokeVoxelStable_;
    case 3U:
        viewportNavigation_.Tick(0.125F);
        doubleClickCameraSmokeEmptyStable_ = !viewportNavigation_.IsFocusing() &&
            viewportCamera_.GetTarget() ==
                (SelectionNavigationBounds().Minimum +
                 SelectionNavigationBounds().Maximum) * 0.5F;
        return doubleClickCameraSmokeEmptyStable_;
    case 4U:
    {
        const EditorCameraState before = viewportCamera_.CaptureState();
        viewportNavigation_.Orbit(12.0F, -8.0F);
        const EditorCameraState after = viewportCamera_.CaptureState();
        doubleClickCameraSmokeOrbitWorked_ =
            after.RotationDegrees != before.RotationDegrees &&
            after.Target == before.Target && after.Distance == before.Distance;
        return doubleClickCameraSmokeOrbitWorked_;
    }
    case 5U:
    {
        const EditorCameraState before = viewportCamera_.CaptureState();
        viewportNavigation_.Pan(-14.0F, 7.0F, 720.0F);
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
        viewportNavigation_.Zoom(-1.0F);
        const EditorCameraState after = viewportCamera_.CaptureState();
        doubleClickCameraSmokeZoomWorked_ =
            after.Distance != before.Distance &&
            after.RotationDegrees == before.RotationDegrees &&
            after.Target == before.Target;
        return doubleClickCameraSmokeZoomWorked_;
    }
    case 7U:
    {
        static_cast<void>(selectionService_.Clear());
        const ViewportCameraActions actions =
            ResolveViewportCameraActions({true, true, false, 0U});
        applyActions(actions);
        doubleClickCameraSmokeShortcutsWorked_ =
            actions.FocusRequested && !actions.ResetRequested &&
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
            !actions.FocusRequested && actions.ResetRequested &&
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
        const Asset::Voxel::VoxelDocument* createdDocument =
            voxelDocumentSession_.ActiveDocument();
        if (!projectManager_.HasActiveProject() || createdDocument == nullptr ||
            createdDocument->SourcePath().filename() != "New Model.vox" ||
            createdDocument->GetVoxelCount() != 0U ||
            !voxelToolState_.IsPencilActive() ||
            !paletteService_.ActiveColor().has_value() ||
            !workplaneService_.Grid(*createdDocument).has_value() ||
            !viewportFocusRequested_)
            return false;
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
        Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!projectManager_.HasActiveProject() || document == nullptr)
            return false;
        workplaneHit_ = WorkplaneHit{
            WorkplaneHitStatus::Valid,
            Asset::Voxel::VoxelPosition{0, 0, 0}, 1.0F};
        const std::optional<SmartToolRequest> request = BuildSmartPencilRequest();
        if (!request || !smartToolController_.ResolvePreview(
                smartToolSession_, *request).HasPlan() ||
            !ApplyVoxelPencil() || !document->IsDirty()) return false;
        RequestCloseProject();
        if (!dirtyActionConfirmation_.IsPending()) return false;
    }
    else if (frame == 3U)
    {
        dirtyActionConfirmation_.Cancel();
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        if (!projectManager_.HasActiveProject() || document == nullptr ||
            !document->IsDirty())
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
            !projectManager_.HasActiveProject() &&
            !voxelDocumentSession_.HasActiveDocument();
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

TransformPanelSource EditorWorkspace::CurrentTransformPanelSource() noexcept
{
    TransformPanelSource source;
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionBounds selectionBounds = selectionService_.EditableBounds();
    source.Available = document != nullptr && !selectionService_.Empty() &&
        selectionBounds.Valid && selectionService_.DocumentGeneration() ==
            voxelDocumentSession_.Generation();
    source.SourceBounds = transformPreviewModel_.IsActive()
        ? transformPreviewModel_.SourceBounds() : selectionBounds;
    if (!source.Available) return source;

    if (!transformGizmoManager_.IsDragging())
        static_cast<void>(transformPivotManager_.UpdateFromBounds(
            selectionBounds, voxelModelCenter_));
    if (!transformPivotManager_.HasValidPivot())
    {
        source.Available = false;
        return source;
    }
    source.Pivot = transformPivotManager_.GetPivot();

    if (transformPreviewModel_.IsActive() &&
        voxelToolState_.IsRotateActive())
    {
        source.RotationPreview = TransformPanelRotation{
            voxelRotateAxis_, voxelRotateQuarterTurns_};
    }
    if (transformPreviewModel_.IsActive() &&
        voxelToolState_.IsScaleActive())
    {
        if (voxelScaleTargetDimensions_)
            source.ScalePreviewDimensions = *voxelScaleTargetDimensions_;
        else if (transformGizmoManager_.IsDragging())
        {
            const Asset::Voxel::VoxelDimensions dimensions =
                transformGizmoManager_.TargetDimensions();
            if (dimensions.X > 0U && dimensions.Y > 0U && dimensions.Z > 0U)
                source.ScalePreviewDimensions = dimensions;
        }
    }
    return source;
}

bool EditorWorkspace::ApplyTransformPanelPosition(const Vec3 position)
{
    const TransformPanelPositionEdit edit =
        transformPanelViewModel_.PreparePosition(
            CurrentTransformPanelSource(), position);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document)
    {
        transformPanelStatusMessage_ = "The active document is unavailable.";
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            0U, TransformPreviewCollisionPolicy::IgnoreSource) ||
        !transformPreviewModel_.SetDelta(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            ConstrainMoveDelta(edit.Delta)) || !ApplyVoxelMove())
    {
        transformPanelStatusMessage_ = voxelMoveStatusMessage_.empty()
            ? "Position could not be applied."
            : voxelMoveStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyTransformPanelRotation(const Vec3 degrees)
{
    const TransformPanelRotationEdit edit =
        transformPanelViewModel_.PrepareRotation(
            CurrentTransformPanelSource(), degrees);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!BeginVoxelRotatePreview(edit.Axis, edit.QuarterTurns) ||
        !ApplyVoxelRotate())
    {
        transformPanelStatusMessage_ = voxelRotateStatusMessage_.empty()
            ? "Rotation could not be applied."
            : voxelRotateStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyTransformPanelScale(const Vec3 scale)
{
    const TransformPanelScaleEdit edit = transformPanelViewModel_.PrepareScale(
        CurrentTransformPanelSource(), scale);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!UpdateVoxelScalePreview(
            VoxelScaleMode::Uniform, edit.TargetDimensions) ||
        !ApplyVoxelScale())
    {
        transformPanelStatusMessage_ = voxelScaleStatusMessage_.empty()
            ? "Scale could not be applied."
            : voxelScaleStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

Asset::Voxel::VoxelPosition EditorWorkspace::ConstrainMoveDelta(
    const Asset::Voxel::VoxelPosition delta) const noexcept
{
    ConstraintRequest request;
    request.Transform.Position = {
        static_cast<float>(delta.X),
        static_cast<float>(delta.Y),
        static_cast<float>(delta.Z)};
    request.Settings = constraintSettings_;
    const Vec3 constrained = ConstraintEngine::Solve(request).Transform.Position;
    return {
        static_cast<std::int32_t>(std::lround(constrained.X)),
        static_cast<std::int32_t>(std::lround(constrained.Y)),
        static_cast<std::int32_t>(std::lround(constrained.Z))};
}

std::int32_t EditorWorkspace::ConstrainRotationQuarterTurns(
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns) const noexcept
{
    ConstraintRequest request;
    const float degrees = static_cast<float>(quarterTurns) * 90.0F;
    if (axis == VoxelRotationAxis::X)
        request.Transform.RotationDegrees.X = degrees;
    else if (axis == VoxelRotationAxis::Y)
        request.Transform.RotationDegrees.Y = degrees;
    else
        request.Transform.RotationDegrees.Z = degrees;
    request.Settings = constraintSettings_;
    const Vec3 constrained =
        ConstraintEngine::Solve(request).Transform.RotationDegrees;
    const float axisDegrees = axis == VoxelRotationAxis::X
        ? constrained.X
        : axis == VoxelRotationAxis::Y ? constrained.Y : constrained.Z;
    return static_cast<std::int32_t>(std::lround(axisDegrees / 90.0F));
}

bool EditorWorkspace::ApplyVoxelMove()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const Asset::Voxel::VoxelPosition constrainedDelta =
        ConstrainMoveDelta(transformPreviewModel_.Delta());
    if (constrainedDelta != transformPreviewModel_.Delta() &&
        !transformPreviewModel_.SetDelta(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            constrainedDelta))
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelMoveStatusMessage_ = "Move constraint could not be applied.";
        UpdateVoxelHighlights();
        return false;
    }

    MoveVoxelSelectionResult prepared = MoveVoxelSelectionOperation::Build(
        *document, selectionService_, voxelDocumentSession_.Generation(),
        transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelMoveStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelMoveStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Move failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelMoveStatusMessage_.clear();
    AddConsoleMessage("[Edit] Moved " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    return true;
}

bool EditorWorkspace::ApplyVoxelDuplicate()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    DuplicateVoxelSelectionResult prepared =
        DuplicateVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelDuplicateStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelDuplicateStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Duplicate failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelDuplicateStatusMessage_.clear();
    AddConsoleMessage("[Edit] Duplicated " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    return true;
}

bool EditorWorkspace::BeginVoxelRotatePreview(
    const VoxelRotationDirection direction)
{
    return BeginVoxelRotatePreview(VoxelRotationAxis::Y,
        direction == VoxelRotationDirection::Clockwise ? 1 : -1);
}

bool EditorWorkspace::BeginVoxelRotatePreview(
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanRotateSelection())
    {
        voxelRotateStatusMessage_ = "Select voxels before using Rotate";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::int32_t constrainedQuarterTurns =
        ConstrainRotationQuarterTurns(axis, quarterTurns);

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsRotateActive() &&
        voxelRotateAxis_ == axis &&
        voxelRotateQuarterTurns_ == constrainedQuarterTurns &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExplicitDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelRotateStatusMessage_ = "Rotate preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelRotationGeometry geometry =
        RotateVoxelSelectionOperation::BuildGeometry(
            selectionService_.Voxels(), selectionService_.EditableBounds(),
            axis, constrainedQuarterTurns);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelRotateStatusMessage_ = geometry.Message.empty()
            ? "Rotate preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelRotateAxis_ = axis;
    voxelRotateQuarterTurns_ = constrainedQuarterTurns;
    voxelRotateDirection_ = constrainedQuarterTurns < 0
        ? VoxelRotationDirection::CounterClockwise
        : VoxelRotationDirection::Clockwise;
    voxelRotateStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelRotate()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelRotate();
        return false;
    }

    RotateVoxelSelectionResult prepared =
        RotateVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelRotateAxis_,
            voxelRotateQuarterTurns_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelRotateStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelRotateStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Rotate failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelRotateStatusMessage_.clear();
    AddConsoleMessage("[Edit] Rotated " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelRotate() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelRotateStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelMirrorPreview(const VoxelMirrorAxis axis)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanMirrorSelection())
    {
        voxelMirrorStatusMessage_ = "Select voxels before using Mirror";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsMirrorActive() && voxelMirrorAxis_ == axis &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExplicitDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelMirrorStatusMessage_ =
            "Mirror preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelMirrorGeometry geometry =
        MirrorVoxelSelectionOperation::BuildGeometry(
            selectionService_.Voxels(), selectionService_.EditableBounds(),
            axis);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelMirrorStatusMessage_ = geometry.Message.empty()
            ? "Mirror preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelMirrorAxis_ = axis;
    voxelMirrorStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelMirror()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelMirror();
        return false;
    }

    MirrorVoxelSelectionResult prepared =
        MirrorVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelMirrorAxis_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (prepared.Code == MirrorVoxelSelectionResultCode::NoChange)
    {
        voxelMirrorStatusMessage_ = "Mirror has no visible effect";
        AddConsoleMessage("[Edit] Mirror has no visible effect.");
        UpdateVoxelHighlights();
        return true;
    }
    if (!prepared.Ready())
    {
        voxelMirrorStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelMirrorStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Mirror failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelMirrorStatusMessage_.clear();
    AddConsoleMessage("[Edit] Mirrored " +
        std::to_string(selectionService_.Count()) + " voxel(s) on " +
        VoxelMirrorAxisName(voxelMirrorAxis_) + ".");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelMirror() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelMirrorStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelScalePreview(const VoxelScaleMode mode)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanScaleSelection())
    {
        voxelScaleStatusMessage_ = "Select voxels before using Scale";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsScaleActive() && voxelScaleMode_ == mode &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExpandedDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelScaleStatusMessage_ = "Scale preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelScaleGeometry geometry =
        ScaleVoxelSelectionOperation::BuildGeometry(
            transformPreviewModel_.SourceVoxels(),
            selectionService_.EditableBounds(), mode);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitVoxelDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelScaleStatusMessage_ = geometry.Message.empty()
            ? "Scale preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelScaleMode_ = mode;
    voxelScaleTargetDimensions_.reset();
    voxelScaleStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::UpdateVoxelScalePreview(
    const VoxelScaleMode mode,
    const Asset::Voxel::VoxelDimensions targetDimensions)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (!document || !CanScaleSelection())
    {
        voxelScaleStatusMessage_ = "Scale preview source is no longer valid";
        return false;
    }
    if (!transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        !transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelScaleStatusMessage_ = "Scale preview source is no longer valid";
        return false;
    }
    if (voxelScaleTargetDimensions_ == targetDimensions &&
        transformPreviewModel_.HasExpandedDestinations())
        return true;

    const VoxelScaleGeometry geometry =
        ScaleVoxelSelectionOperation::BuildGeometry(
            transformPreviewModel_.SourceVoxels(),
            transformPreviewModel_.SourceBounds(), mode, targetDimensions);
    if (!geometry.Valid())
    {
        voxelScaleStatusMessage_ = geometry.Message.empty()
            ? "Scale preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelScaleTargetDimensions_.reset();
        return false;
    }
    const bool rebuilt = transformPreviewModel_.SetExplicitVoxelDestinations(
        *document, selectionService_, generation, geometry.Destinations);
    const auto current = transformPreviewModel_.Voxels();
    const bool alreadyMatches = current.size() == geometry.Destinations.size() &&
        std::equal(current.begin(), current.end(), geometry.Destinations.begin(),
            [](const TransformPreviewVoxel& voxel,
               const TransformPreviewDestinationVoxel& destination)
            {
                return voxel.SourcePosition == destination.SourcePosition &&
                    voxel.PreviewPosition == destination.DestinationPosition &&
                    voxel.Value == destination.Value;
            });
    if (!rebuilt && !alreadyMatches)
    {
        voxelScaleStatusMessage_ = "Scale preview could not be built";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelScaleTargetDimensions_.reset();
        return false;
    }
    voxelScaleMode_ = mode;
    voxelScaleTargetDimensions_ = targetDimensions;
    voxelScaleStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyVoxelScale()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelScale();
        return false;
    }

    ScaleVoxelSelectionResult prepared = voxelScaleTargetDimensions_
        ? ScaleVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelScaleMode_,
            *voxelScaleTargetDimensions_)
        : ScaleVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelScaleMode_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelScaleTargetDimensions_.reset();
    if (!prepared.Ready())
    {
        voxelScaleStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelScaleStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Scale failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelScaleStatusMessage_.clear();
    AddConsoleMessage("[Edit] Scaled selection " +
        std::string(VoxelScaleModeName(voxelScaleMode_)) + " to " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelScale() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelScaleTargetDimensions_.reset();
    voxelScaleStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelAlignPreview(
    const VoxelAlignDirection direction)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanAlignSelection())
    {
        voxelAlignStatusMessage_ = "Select voxels before using Align";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const auto dimensions = document->GetDimensions(0U);
    const auto delta = dimensions
        ? AlignVoxelSelectionOperation::CalculateDelta(
            selectionService_.EditableBounds(), *dimensions, direction)
        : std::nullopt;
    if (!delta)
    {
        voxelAlignStatusMessage_ = "Align could not calculate a valid delta";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    const bool previewStarted = transformPreviewModel_.BeginPreview(
        *document, selectionService_, generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource);
    const bool previewPositioned = previewStarted &&
        (*delta == Asset::Voxel::VoxelPosition{} ||
         transformPreviewModel_.SetDelta(
             *document, selectionService_, generation, *delta));
    if (!previewPositioned)
    {
        voxelAlignStatusMessage_ = "Align preview could not be built";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    voxelAlignDirection_ = direction;
    voxelAlignStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    voxelScaleStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelAlign()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelAlign();
        return false;
    }

    MoveVoxelSelectionResult prepared = AlignVoxelSelectionOperation::Build(
        *document, selectionService_, voxelDocumentSession_.Generation(),
        transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelAlignStatusMessage_ =
            prepared.Code == MoveVoxelSelectionResultCode::NoChange
            ? "Selection is already aligned to this face"
            : prepared.Message;
        if (!voxelAlignStatusMessage_.empty())
            AddConsoleMessage("[Edit] " + voxelAlignStatusMessage_);
        UpdateVoxelHighlights();
        return false;
    }

    prepared.Operation.Label = "Align Voxels";
    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelAlignStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Align failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelAlignStatusMessage_.clear();
    AddConsoleMessage("[Edit] Aligned " +
        std::to_string(selectionService_.Count()) + " voxel(s) " +
        VoxelAlignDirectionName(voxelAlignDirection_) + ".");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelAlign() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelAlignStatusMessage_.clear();
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

void EditorWorkspace::CancelTransformGizmoInteraction() noexcept
{
    const TransformGizmoMode mode = transformGizmoManager_.Mode();
    const bool rotate = mode ==
        TransformGizmoMode::Rotate;
    const bool scale = mode ==
        TransformGizmoMode::Scale;
    const bool changed = static_cast<bool>(
        transformGizmoManager_.CancelInteraction());
    const bool previewCancelled = transformPreviewModel_.CancelPreview();
    if (rotate) voxelRotateStatusMessage_.clear();
    if (scale)
    {
        voxelScaleTargetDimensions_.reset();
        voxelScaleStatusMessage_.clear();
    }
    if (changed || previewCancelled) UpdateVoxelHighlights();
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

void EditorWorkspace::UpdateVoxelHighlights() noexcept
{
    universalCursor2DTarget_.reset();
    const auto coordinates = [](const std::optional<VoxelRaycastHit>& hit)
        -> std::optional<VoxelCoordinates>
    {
        return hit ? std::optional<VoxelCoordinates>(hit->Coordinates)
                   : std::nullopt;
    };
    std::optional<VoxelCoordinates> hoveredCoordinates =
        coordinates(voxelSelection_.Hovered());
    std::span<const Asset::Voxel::VoxelPosition> selectedCoordinates;
    std::array<Asset::Voxel::VoxelPosition, 1U> legacySelection{};
    std::optional<VoxelBoxBounds> selectionBounds;
    std::optional<SelectionBounds> editableSelectionBounds;
    const bool selectionVisualActive =
        voxelToolState_.IsSelectionActive() || voxelToolState_.IsMoveActive() ||
        voxelToolState_.IsDuplicateActive() || voxelToolState_.IsRotateActive() ||
        voxelToolState_.IsMirrorActive() || voxelToolState_.IsScaleActive() ||
        voxelToolState_.IsAlignActive();
    if (selectionVisualActive)
    {
        const auto selected = selectionService_.Voxels();
        const SelectionHighlightPlan highlightPlan =
            SelectionHighlightPolicy::Build(
                selected.size(), selectionInteraction_.IsActive());
        if (highlightPlan.DrawIndividualVoxels)
            selectedCoordinates = selected;
        const SelectionBounds& bounds = selectionService_.Bounds();
        if (bounds.Valid)
            selectionBounds = VoxelBoxBounds{bounds.Minimum, bounds.Maximum};
        const SelectionBounds& editable =
            (voxelToolState_.IsRotateActive() ||
             voxelToolState_.IsMirrorActive() ||
             voxelToolState_.IsScaleActive() ||
             voxelToolState_.IsAlignActive()) &&
                transformPreviewModel_.IsActive()
            ? transformPreviewModel_.PreviewBounds()
            :
            selectionInteraction_.IsActive() &&
            selectionInteraction_.IsDragRecognized()
            ? selectionInteraction_.CurrentBounds()
            : selectionService_.EditableBounds();
        if (editable.Valid) editableSelectionBounds = editable;
    }
    else if (const auto& selected = voxelSelection_.Selected())
    {
        legacySelection[0] = {
            static_cast<std::int32_t>(selected->Coordinates.X),
            static_cast<std::int32_t>(selected->Coordinates.Y),
            static_cast<std::int32_t>(selected->Coordinates.Z)};
        selectedCoordinates = legacySelection;
    }
    std::optional<Asset::Voxel::VoxelPosition> placementPosition;
    std::span<const Asset::Voxel::VoxelPosition> brushPreview;
    std::span<const Asset::Voxel::VoxelPosition> brushOccupiedPreview;
    std::optional<VoxelBoxBounds> brushAggregatePreview;
    std::optional<VoxelSpherePreview> brushAggregateSpherePreview;
    std::optional<VoxelBoxBounds> boxPreview;
    std::vector<Asset::Voxel::VoxelPosition> linePreview;
    std::optional<VoxelSpherePreview> spherePreview;
    std::span<const GhostVoxel> smartBrushGhostPreview;
    const SmartToolExactPreviewMesh* exactSmartToolPreview = nullptr;
    SmartToolPlanPtr exactSmartToolPlan;
    smartBrushGhostPreview_ = nullptr;
    VoxelPlacementPreviewStyle placementStyle =
        VoxelPlacementPreviewStyle::PencilInvalid;
    // Pencil V2 owns the Pencil preview when explicitly enabled.  Keep the
    // legacy Smart Tool presentation entirely dormant in that mode: having
    // both paths resolve the same hover would violate the one-preview rule.
    const bool pencilV2ToolActive = usePencilViewportInteractionV2_ &&
        voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational() &&
        toolContext_.Smart.Geometry() == SmartGeometry::Pencil;
    const bool smartGeometryActive = !pencilV2ToolActive &&
        voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational();
    const bool smartAddActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Add;
    const bool smartPaintActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Paint;
    const bool smartEraseActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Erase;
    const bool faceAddPlanGhostPresentation =
        ShouldPresentFaceAddAsPlanGhosts(
            smartGeometryActive &&
                toolContext_.Smart.Geometry() == SmartGeometry::Face,
            smartAddActive,
            smartToolStroke_.IsActive());
    if (!smartAddActive && !smartEraseActive && !smartPaintActive)
        pencilPreviewCacheValid_ = false;
    if (!smartPaintActive && !voxelToolState_.IsFillActive())
        paintPreviewCacheValid_ = false;
    if (!smartAddActive && !smartEraseActive && !smartPaintActive &&
        !voxelToolState_.IsFillActive())
    {
        toolContext_.Smart.SetPreview(SmartToolPreviewState::Unavailable);
        toolContext_.Smart.ClearStatistics();
    }
    if (smartAddActive || smartEraseActive || smartPaintActive)
    {
        const SmartToolStroke* const activeStroke = smartToolStroke_.IsActive()
            ? &smartToolStroke_ : nullptr;
        SmartToolPlanPtr plan;
        if (ShouldResolvePreviewForPresentation(activeStroke != nullptr))
        {
            const std::optional<SmartToolRequest> request =
                BuildSmartPencilRequest();
            const SmartToolResult planning = request
                ? smartToolController_.ResolvePreview(
                    smartToolSession_, *request)
                : SmartToolResult{};
            plan = planning.Plan;
        }
        else
        {
            // ContinueSmartToolStroke already resolved and accepted this
            // immutable plan. Replanning from the raw hover here would make
            // the cursor outrun the actual stroke and duplicate expensive
            // planner work on every pointer update.
            plan = smartToolStrokePreviewPlan_;
        }
        const std::optional<Asset::Voxel::VoxelPosition> anchor = plan != nullptr
            ? std::optional<Asset::Voxel::VoxelPosition>{plan->Placement().Target}
            : std::nullopt;
        if (plan != nullptr)
        {
            // This engine consumes only the materialized immutable plan. It
            // never asks a document, palette, or planner for another value.
            smartBrushGhostPreview_ = &smartPreviewCache_.Resolve(plan);
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            if (document != nullptr)
            {
                exactSmartToolPlan = plan;
                if (faceAddPlanGhostPresentation)
                {
                    // Face depth replaces the stroke with one complete,
                    // immutable plan. Present those exact cells directly:
                    // rebuilding a private copy of the entire document and
                    // remeshing it on every depth step is unnecessary.
                }
                else if (activeStroke != nullptr)
                {
                    const bool rebuild = smartToolStrokePreviewPlanId_ !=
                            plan->PlanId() ||
                        smartToolStrokePreviewPlanRevision_ != plan->Revision() ||
                        smartToolStrokePreviewStrokeRevision_ !=
                            activeStroke->Revision();
                    if (rebuild)
                    {
                        smartToolStrokePreviewMesh_ =
                            SmartToolExactPreviewComposer::Compose(*document,
                                activeStroke->Changes());
                        smartToolStrokePreviewPlanId_ = plan->PlanId();
                        smartToolStrokePreviewPlanRevision_ = plan->Revision();
                        smartToolStrokePreviewStrokeRevision_ =
                            activeStroke->Revision();
                    }
                    exactSmartToolPreview = &smartToolStrokePreviewMesh_;
                }
                else
                {
                    exactSmartToolPreview = &smartToolExactPreviewCache_.Resolve(
                        *document, voxelDocumentSession_.Generation(), plan);
                }
            }
        }
        else
        {
            if (activeStroke == nullptr) smartToolSession_.Clear();
            smartToolExactPreviewCache_.Clear();
            // Keep the accumulated exact state visible while a target is
            // temporarily invalid. The stroke remains suspended and the next
            // valid target starts a fresh segment; no missing target is ever
            // interpolated across.
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            const bool invalidReplacementEndpoint =
                (smartLineLockedStart_ && !smartLineEndpointValid_) ||
                (smartGeometryPlane_ && !smartGeometryEndpointValid_);
            if (!invalidReplacementEndpoint && activeStroke != nullptr && document != nullptr &&
                smartToolStrokePreviewPlan_ != nullptr)
            {
                exactSmartToolPlan = smartToolStrokePreviewPlan_;
                if (smartToolStrokePreviewStrokeRevision_ !=
                    activeStroke->Revision())
                {
                    smartToolStrokePreviewMesh_ =
                        SmartToolExactPreviewComposer::Compose(*document,
                            activeStroke->Changes());
                    smartToolStrokePreviewStrokeRevision_ =
                        activeStroke->Revision();
                }
                exactSmartToolPreview = &smartToolStrokePreviewMesh_;
            }
        }
        voxelPlacementPreview_ = {};
        if (smartBrushGhostPreview_ != nullptr)
        {
            const SmartPreviewData& preview = *smartBrushGhostPreview_;
            if (faceAddPlanGhostPresentation)
                smartBrushGhostPreview = preview.GhostVoxels;
            voxelPlacementPreview_.Tool = smartEraseActive
                ? VoxelPreviewTool::Eraser : VoxelPreviewTool::Pencil;
            voxelPlacementPreview_.Position = anchor;
            voxelPlacementPreview_.RenderPlan = preview.RenderPlan;
            voxelPlacementPreview_.Statistics = {preview.Statistics.Total,
                smartEraseActive ? preview.Statistics.Unchanged :
                    preview.Statistics.Changed,
                smartEraseActive ? preview.Statistics.Changed :
                    preview.Statistics.Unchanged,
                preview.Statistics.Clipped};
            for (const GhostVoxel& ghost : preview.GhostVoxels)
            {
                if (ghost.State == GhostVoxelState::Added ||
                    ghost.State == GhostVoxelState::Painted ||
                    (smartEraseActive && ghost.State == GhostVoxelState::Ignored))
                    voxelPlacementPreview_.AddablePositions.push_back(ghost.Position);
                else if (ghost.State == GhostVoxelState::Erased ||
                    (!smartEraseActive && ghost.State == GhostVoxelState::Ignored))
                    voxelPlacementPreview_.OccupiedPositions.push_back(ghost.Position);
                if (ghost.State != GhostVoxelState::Clipped &&
                    ghost.State != GhostVoxelState::Invalid)
                    voxelPlacementPreview_.Positions.push_back(ghost.Position);
            }
            voxelPlacementPreview_.Status = preview.Code ==
                    SmartBrushResultCode::OutOfBounds
                ? VoxelPlacementPreviewStatus::OutOfBounds
                : preview.Code == SmartBrushResultCode::Valid
                ? preview.Statistics.Changed == 0U
                    ? VoxelPlacementPreviewStatus::Occupied
                    : VoxelPlacementPreviewStatus::Valid
                : VoxelPlacementPreviewStatus::Unavailable;
            toolContext_.Smart.SetStatistics(preview.Statistics.Total,
                preview.Statistics.Changed, preview.Statistics.Unchanged,
                preview.Statistics.Clipped);
            toolContext_.Smart.SetPreview(preview.Code ==
                    SmartBrushResultCode::OutOfBounds
                ? SmartToolPreviewState::OutOfBounds
                : preview.Code != SmartBrushResultCode::Valid
                ? SmartToolPreviewState::Unavailable
                : preview.Statistics.Changed == 0U
                ? SmartToolPreviewState::NoChange : SmartToolPreviewState::Valid,
                preview.RenderPlan);
        }
        else
        {
            toolContext_.Smart.SetPreview(SmartToolPreviewState::Unavailable);
            toolContext_.Smart.ClearStatistics();
        }
        placementStyle = smartEraseActive ? VoxelPlacementPreviewStyle::Eraser :
            voxelPlacementPreview_.Status == VoxelPlacementPreviewStatus::Valid
            ? VoxelPlacementPreviewStyle::PencilValid
            : voxelPlacementPreview_.Status == VoxelPlacementPreviewStatus::Occupied
            ? VoxelPlacementPreviewStyle::PencilOccupied
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
    else if (voxelToolState_.IsFillActive())
    {
        Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        SmartBrushState previewState = toolContext_.Smart.Brush();
        previewState.Shape = ResolveSmartBrushShape(
            toolContext_.Smart.Geometry(), previewState.Shape);
        previewState.Mode = SmartBrushMode::Paint;
        if (const std::optional<PaletteColorSelection> activeColor =
                paletteService_.ActiveColor())
            toolContext_.Smart.Brush().PaletteIndex = activeColor->Index;
        else
            toolContext_.Smart.Brush().PaletteIndex = 0U;
        previewState.PaletteIndex = toolContext_.Smart.Brush().PaletteIndex;
        const std::optional<VoxelRaycastHit>& hit = voxelSelection_.Hovered();
        const std::optional<VoxelCoordinates> hitCoordinates = hit
            ? std::optional<VoxelCoordinates>{hit->Coordinates}
            : std::nullopt;
        const VoxelHitFace face = hit ? hit->Face : VoxelHitFace::None;
        const std::size_t hitSubModelIndex = hit ? hit->SubModelIndex : 0U;
        const std::uint64_t revision = document ? document->GetRevision() : 0U;
        const std::uint64_t generation = voxelDocumentSession_.Generation();
        voxelPlacementPreview_ = {};
        if (!paintPreviewCacheValid_ || paintPreviewDocument_ != document ||
            paintPreviewRevision_ != revision ||
            paintPreviewGeneration_ != generation ||
            paintPreviewCoordinates_ != hitCoordinates ||
            paintPreviewFace_ != face ||
            paintPreviewHitSubModelIndex_ != hitSubModelIndex ||
            paintPreviewState_ != previewState)
        {
            try
            {
                paintPreviewEvaluation_ = VoxelPaintBrushTool::Evaluate({
                    nullptr, document, 0U, hit, previewState, false, nullptr});
            }
            catch (...)
            {
                paintPreviewEvaluation_ = {};
            }
            paintPreviewDocument_ = document;
            paintPreviewRevision_ = revision;
            paintPreviewGeneration_ = generation;
            paintPreviewCoordinates_ = hitCoordinates;
            paintPreviewFace_ = face;
            paintPreviewHitSubModelIndex_ = hitSubModelIndex;
            paintPreviewState_ = previewState;
            paintPreviewCacheValid_ = true;
        }
        toolContext_.Smart.SetStatistics(
            paintPreviewEvaluation_.Statistics.Total,
            paintPreviewEvaluation_.Statistics.Painted,
            paintPreviewEvaluation_.Statistics.Ignored,
            paintPreviewEvaluation_.Statistics.Clipped);
        toolContext_.Smart.SetPreview(
            paintPreviewEvaluation_.Code == VoxelPaintBrushResultCode::Applied
                ? SmartToolPreviewState::Valid
                : paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::NoChange
                ? SmartToolPreviewState::NoChange
                : paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::TargetOutOfBounds
                ? SmartToolPreviewState::OutOfBounds
                : SmartToolPreviewState::Unavailable,
            paintPreviewEvaluation_.RenderPlan);
        if (paintPreviewEvaluation_.IsResolved())
        {
            brushPreview = paintPreviewEvaluation_.PaintablePositions;
            brushOccupiedPreview = paintPreviewEvaluation_.IgnoredPositions;
            placementStyle = paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::Applied
                ? VoxelPlacementPreviewStyle::PencilValid
                : VoxelPlacementPreviewStyle::PencilOccupied;
        }
    }
    else if (voxelToolState_.IsBoxActive())
    {
        voxelPlacementPreview_ = {};
        if (const auto* document = voxelDocumentSession_.ActiveDocument();
            document && voxelBoxInteraction_.CornerA() &&
            voxelBoxInteraction_.CornerB())
        {
            boxPreview = VoxelBoxService::CalculateBounds(
                *document, 0U, *voxelBoxInteraction_.CornerA(),
                *voxelBoxInteraction_.CornerB());
        }
    }
    else if (voxelToolState_.IsLineActive())
    {
        voxelPlacementPreview_ = {};
        if (const auto* document = voxelDocumentSession_.ActiveDocument();
            document && voxelLineInteraction_.PointA() &&
            voxelLineInteraction_.PointB())
        {
            try
            {
                linePreview = VoxelLineService::CalculatePositions(
                    *document, 0U, *voxelLineInteraction_.PointA(),
                    *voxelLineInteraction_.PointB());
            }
            catch (...)
            {
                linePreview.clear();
            }
        }
    }
    else if (voxelToolState_.IsSphereActive())
    {
        voxelPlacementPreview_ = {};
        if (voxelSphereInteraction_.Center() &&
            voxelSphereInteraction_.RadiusPoint())
        {
            spherePreview = VoxelSpherePreview{
                *voxelSphereInteraction_.Center(),
                VoxelSphereService::CalculateRadius(
                    *voxelSphereInteraction_.Center(),
                    *voxelSphereInteraction_.RadiusPoint())};
        }
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
    if (pencilV2ToolActive)
    {
        // Presentation LOD only: the immutable compact plans remain the exact
        // commit source.  Small footprints are rendered as exact cells;
        // larger ones never expand into millions of CPU ghost voxels.
        const InteractionV2::PencilCompactPresentation& presentation =
            pencilViewportInteractionV2_.Presentation();
        if (pencilV2RenderedPresentationRevision_ != presentation.Revision)
        {
            pencilV2PreviewPositions_.clear();
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            if (document && presentation.Detail ==
                    InteractionV2::PencilPreviewDetail::Exact)
            {
                // Presentation and commit both consume this pure shared
                // resolver.  Paint/Erase/overlap previews therefore expose
                // exactly the resulting cells, never the raw footprint.
                const auto resolved = InteractionV2::PencilCompactChangeResolver::
                    Resolve(*document, voxelDocumentSession_.Generation(),
                        presentation.Plans);
                if (resolved)
                {
                    pencilV2PreviewPositions_.reserve(resolved->size());
                    for (const VoxelChange& change : *resolved)
                        pencilV2PreviewPositions_.push_back(change.Position);
                }
            }
            pencilV2RenderedPresentationRevision_ = presentation.Revision;
        }
        if (!pencilV2PreviewPositions_.empty())
            brushPreview = pencilV2PreviewPositions_;
        else if (presentation.Bounds &&
                 (presentation.Detail ==
                      InteractionV2::PencilPreviewDetail::CompactDeferred ||
                  presentation.Validity ==
                      InteractionV2::PencilPreviewValidity::OutOfBounds))
            brushAggregatePreview = VoxelBoxBounds{
                presentation.Bounds->Minimum, presentation.Bounds->Maximum};
        placementPosition.reset();
        placementStyle = presentation.Validity ==
                InteractionV2::PencilPreviewValidity::OutOfBounds
            ? VoxelPlacementPreviewStyle::PencilInvalid
            : presentation.Detail ==
                    InteractionV2::PencilPreviewDetail::CompactDeferred
            ? VoxelPlacementPreviewStyle::PencilOccupied
            : presentation.Detail == InteractionV2::PencilPreviewDetail::Exact &&
                    pencilV2PreviewPositions_.empty()
            ? VoxelPlacementPreviewStyle::PencilOccupied
            : VoxelPlacementPreviewStyle::PencilValid;
    }
    const UniversalCursorPreviewSubject previewSubject =
        (smartGeometryActive || pencilV2ToolActive) &&
            toolContext_.Smart.Geometry() == SmartGeometry::Pencil
        ? toolContext_.Smart.Mode() == SmartToolMode::SingleVoxel
            ? UniversalCursorPreviewSubject::PencilSingleVoxel
            : UniversalCursorPreviewSubject::PencilBrush
        : UniversalCursorPreviewSubject::Geometric;
    const bool universalCursorToolActive = smartGeometryActive ||
        pencilV2ToolActive || voxelToolState_.IsFillActive();
    if (universalCursorToolActive)
    {
        std::optional<UniversalCursor2DTarget> hoveredCursorTarget;
        const std::optional<VoxelRaycastHit>& hit = voxelSelection_.Hovered();
        if (hit && hit->Face != VoxelHitFace::None)
        {
            hoveredCursorTarget = MakeVoxelFaceCursor2DTarget(
                {static_cast<std::int32_t>(hit->Coordinates.X),
                 static_cast<std::int32_t>(hit->Coordinates.Y),
                 static_cast<std::int32_t>(hit->Coordinates.Z)},
                VoxelHitFaceIntegerNormal(hit->Face), voxelModelCenter_);
        }

        const SmartToolPlan* plannedCursorPlan = exactSmartToolPlan.get();
        const bool activeStrokeCursor = smartToolStroke_.IsActive();
        const bool lockedFaceStroke = activeStrokeCursor &&
            toolContext_.Smart.Geometry() == SmartGeometry::Face &&
            faceDepthLockedSeed_.has_value();
        if (plannedCursorPlan == nullptr && activeStrokeCursor &&
            smartToolStrokePreviewPlan_ != nullptr)
            plannedCursorPlan = smartToolStrokePreviewPlan_.get();
        std::optional<UniversalCursor2DTarget> plannedCursorTarget;
        if (plannedCursorPlan != nullptr)
        {
            const SmartBrushPlacement& placement =
                plannedCursorPlan->Placement();
            Asset::Voxel::VoxelPosition cursorVoxel = placement.Target;
            if (lockedFaceStroke && faceDepthLockedSeed_)
            {
                cursorVoxel = faceDepthLockedSeed_->Position;
                if (plannedCursorPlan->Action() == SmartAction::Add)
                {
                    const Asset::Voxel::VoxelPosition normal =
                        faceDepthLockedSeed_->Normal;
                    const auto presentedTarget =
                        MakeOutermostVoxelFaceCursor2DTarget(
                            smartBrushGhostPreview_ != nullptr
                                ? std::span<const Asset::Voxel::VoxelPosition>{
                                    smartBrushGhostPreview_->AffectedPositions}
                                : std::span<const Asset::Voxel::VoxelPosition>{
                                    plannedCursorPlan->AffectedPositions()},
                            faceDepthLockedSeed_->Position,
                            normal, voxelModelCenter_);
                    if (presentedTarget)
                        plannedCursorTarget = *presentedTarget;
                }
                if (!plannedCursorTarget)
                    plannedCursorTarget = MakeVoxelFaceCursor2DTarget(
                        cursorVoxel, faceDepthLockedSeed_->Normal,
                        voxelModelCenter_);
            }
            else
            {
                if (plannedCursorPlan->Action() == SmartAction::Add)
                {
                    cursorVoxel.X -= placement.Normal.X;
                    cursorVoxel.Y -= placement.Normal.Y;
                    cursorVoxel.Z -= placement.Normal.Z;
                }
                plannedCursorTarget = MakeVoxelFaceCursor2DTarget(
                    cursorVoxel, placement.Normal, voxelModelCenter_);
            }
        }
        universalCursor2DTarget_ = SelectUniversalCursor2DTarget(
            hoveredCursorTarget, plannedCursorTarget,
            activeStrokeCursor
                ? UniversalCursorAnchorPolicy::PreferPlannedTarget
                : UniversalCursorAnchorPolicy::PreferHoveredTarget);
    }
    if (universalCursorToolActive)
    {
        // The universal cursor replaces only the legacy volumetric hover
        // marker. Tool geometry remains an independent preview layer.
        hoveredCoordinates.reset();
    }
    // Smart Add/Erase/Paint render exclusively from the immutable planner
    // output through the exact final-state preview.
    if (exactSmartToolPreview == nullptr) smartToolExactPreviewCache_.Clear();
    if (smartAddActive || smartEraseActive || smartPaintActive)
    {
        brushPreview = {};
        brushOccupiedPreview = {};
        brushAggregatePreview.reset();
        brushAggregateSpherePreview.reset();
    }
    if (exactSmartToolPreview != nullptr && exactSmartToolPreview->Succeeded())
    {
        // The exact final-state mesh replaces the base model for this frame;
        // legacy hover/selection highlights would otherwise falsely describe
        // the pre-commit document, most visibly for Remove.
        hoveredCoordinates.reset();
        selectedCoordinates = {};
        selectionBounds.reset();
        editableSelectionBounds.reset();
    }
    viewportRenderer_.ConfigureHighlights(
        hoveredCoordinates,
        selectedCoordinates,
        selectionBounds,
        editableSelectionBounds,
        selectionVisualActive,
        (selectionInteraction_.Mode() == SelectionInteractionMode::MovingBox ||
         selectionInteraction_.Mode() == SelectionInteractionMode::MovingContent ||
         selectionInteraction_.Mode() ==
             SelectionInteractionMode::DuplicatingContent)
            ? SelectionBoxVisualState::Moving
            : selectionBoxInteriorHovered_
            ? SelectionBoxVisualState::Hovered
            : SelectionBoxVisualState::Normal,
        placementPosition,
        placementStyle,
        brushPreview,
        brushOccupiedPreview,
        brushAggregatePreview,
        brushAggregateSpherePreview,
        boxPreview,
        linePreview,
        spherePreview,
        smartBrushGhostPreview,
        faceAddPlanGhostPresentation
            ? SmartBrushGhostGeometryStyle::ExposedFaceSurface
            : SmartBrushGhostGeometryStyle::VoxelBoxes,
        voxelModelCenter_);
    const Asset::Voxel::VoxelDocument* const activeDocument =
        voxelDocumentSession_.ActiveDocument();
    if (ShouldRenderExactPreviewGeometry(
            previewSubject, smartToolStroke_.IsActive()) &&
        exactSmartToolPreview != nullptr && exactSmartToolPlan != nullptr &&
        exactSmartToolPreview->Succeeded())
    {
        static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
            &exactSmartToolPreview->Mesh, &exactSmartToolPreview->Palette,
            voxelModelCenter_, exactSmartToolPreview->Active,
            voxelDocumentSession_.Generation(), activeDocument
                ? activeDocument->GetRevision() : 0U,
            exactSmartToolPlan->PlanId(), smartToolStroke_.IsActive()
                ? smartToolStroke_.Revision() : exactSmartToolPlan->Revision()));
    }
    else if (!ShouldRetainExactPreviewOnMissingFrame(
                 previewSubject, smartToolStroke_.IsActive()))
    {
        static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
            nullptr, nullptr, {}, false, 0U, 0U, 0U, 0U));
    }
    viewportRenderer_.ConfigureVoxelPreview(
        stampPlacementSession_.CurrentPreview());
    if (activeDocument && transformPreviewModel_.IsValidFor(
            *activeDocument, selectionService_, voxelDocumentSession_.Generation()))
    {
        const TransformPreviewRenderData preview =
            transformPreviewModel_.RenderData();
        viewportRenderer_.ConfigureTransformPreview(&preview);
    }
    else
    {
        viewportRenderer_.ConfigureTransformPreview(nullptr);
    }
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
    if (useViewportInteractionV2_ &&
        (voxelToolState_.IsSelectionActive() ||
         voxelToolState_.IsMoveActive()))
    {
        transformPivotManager_.Invalidate();
        viewportRenderer_.ConfigureTransformGizmo(nullptr);
        return;
    }
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionBounds gizmoBounds =
        transformGizmoManager_.IsDragging() &&
            transformPreviewModel_.IsActive()
        ? transformPreviewModel_.PreviewBounds()
        : selectionService_.EditableBounds();
    if (document != nullptr && !selectionService_.Empty() &&
        selectionService_.DocumentGeneration() ==
            voxelDocumentSession_.Generation() && gizmoBounds.Valid)
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
    context.SelectionEmpty = selectionService_.Empty();
    context.Closing =
        closeRequest_.State() != EditorCloseRequestState::None;
    context.ActiveDocumentGeneration = voxelDocumentSession_.Generation();
    context.SelectionDocumentGeneration =
        selectionService_.DocumentGeneration();
    context.Bounds = gizmoBounds;
    context.ActiveTool = voxelToolState_.ActiveTool();
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
