// =============================================================================
// EditorWorkspaceSmoke.cpp — Harnais de smoke tests et de validations visuelles
// d'EditorWorkspace (méthodes Run*SmokeStep / Run*VisualStep / *SmokePassed).
//
// Lot 0 du plan VF-0260 : ces méthodes restent membres d'EditorWorkspace mais
// vivent dans cette unité de traduction dédiée. Déplacement mécanique strict :
// aucun corps de méthode modifié. L'espace anonyme d'EditorWorkspace.cpp est
// dupliqué ici (liaison interne, sans effet de bord).
// =============================================================================
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
    console_.SetVisible(false);
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

} // namespace VoxelForge::Editor
