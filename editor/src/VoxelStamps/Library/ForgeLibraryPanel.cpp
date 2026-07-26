#include "VoxelStamps/Library/ForgeLibraryPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr float CardWidth = 116.0F;
constexpr float CardHeight = 92.0F;

[[nodiscard]] const char* SortLabel(const ForgeLibrarySortMode mode) noexcept
{
    switch (mode)
    {
    case ForgeLibrarySortMode::Name: return "Name";
    case ForgeLibrarySortMode::Date: return "Date";
    case ForgeLibrarySortMode::Type: return "Type";
    }
    return "Name";
}

void DrawCardThumbnail(
    const ImVec2 minimum,
    const ImVec2 maximum,
    const StampDimensions dimensions,
    const bool selected)
{
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(
        selected ? ImGuiCol_Header : ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(
        selected ? ImGuiCol_HeaderActive : ImGuiCol_Border);
    drawList->AddRectFilled(minimum, maximum, background, 4.0F);
    drawList->AddRect(minimum, maximum, border, 4.0F, 0, selected ? 2.0F : 1.0F);

    const float width = maximum.x - minimum.x;
    const float height = maximum.y - minimum.y;
    const float largest = static_cast<float>(std::max({
        dimensions.X, dimensions.Y, dimensions.Z, 1U}));
    const float x = static_cast<float>(dimensions.X) / largest;
    const float y = static_cast<float>(dimensions.Y) / largest;
    const float z = static_cast<float>(dimensions.Z) / largest;
    const ImVec2 center{minimum.x + width * 0.50F, minimum.y + height * 0.50F};
    const float scale = std::min(width, height) * 0.27F;
    const ImU32 face = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    const ImU32 edge = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImVec2 left{center.x - scale * (x + z * 0.45F),
                      center.y + scale * (z * 0.30F)};
    const ImVec2 right{center.x + scale * (x + z * 0.45F),
                       center.y + scale * (z * 0.30F)};
    const ImVec2 top{center.x, center.y - scale * (y + z * 0.35F)};
    const ImVec2 bottom{center.x, center.y + scale * (y + z * 0.35F)};
    drawList->AddQuadFilled(left, top, right, bottom, face);
    drawList->AddQuad(left, top, right, bottom, edge, 1.0F);
}

} // namespace

ForgeLibraryPanel::ForgeLibraryPanel(ForgeLibraryViewModel& viewModel)
    : viewModel_(viewModel)
{
}

ForgeLibraryPanelResult ForgeLibraryPanel::Draw(
    bool* const open,
    const Asset::Voxel::VoxelDocument* const document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel)
{
    ForgeLibraryPanelResult panelResult;
    if (!ImGui::Begin("Forge Library", open))
    {
        ImGui::End();
        return panelResult;
    }

    ImGui::TextUnformatted("FORGE LIBRARY");
    ImGui::SameLine();
    ImGui::TextDisabled("Project");
    ImGui::Separator();
    DrawToolbar();
    if (viewModel_.NeedsRefresh())
    {
        const ForgeLibraryOperationResult refresh = viewModel_.Refresh();
        if (!refresh.Succeeded) panelResult.Message = refresh.Message;
    }

    const float detailsHeight =
        viewModel_.SelectedDetails() == nullptr ? 54.0F : 214.0F;
    ImGui::BeginChild("##ForgeLibraryContent", ImVec2(0.0F, -detailsHeight), true);
    const bool activate = DrawContent();
    ImGui::EndChild();

    DrawDetails();
    bool useRequested = activate;
    if (viewModel_.SelectedId() != nullptr)
    {
        ImGui::SameLine();
        useRequested |= ImGui::Button("Use", ImVec2(72.0F, 0.0F));
    }

    if (useRequested)
    {
        if (document == nullptr)
        {
            panelResult.Message =
                "Open a voxel document before using a Stamp.";
        }
        else
        {
            ForgeLibraryOperationResult activated =
                viewModel_.ActivateSelected(
                    *document, documentGeneration, targetSubModel);
            panelResult.SessionActivated = activated.SessionActivated;
            panelResult.Message = std::move(activated.Message);
        }
    }

    if (!viewModel_.StatusMessage().empty())
        ImGui::TextDisabled("%s", viewModel_.StatusMessage().c_str());
    ImGui::End();
    return panelResult;
}

void ForgeLibraryPanel::DrawToolbar()
{
    if (viewModel_.SearchText() != searchBuffer_.data())
    {
        std::snprintf(
            searchBuffer_.data(),
            searchBuffer_.size(),
            "%s",
            viewModel_.SearchText().c_str());
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint(
            "##ForgeLibrarySearch", "Search Stamps...",
            searchBuffer_.data(), searchBuffer_.size()))
    {
        viewModel_.SetSearchText(searchBuffer_.data());
    }

    if (ImGui::Button("Grid"))
        viewModel_.SetDisplayMode(ForgeLibraryDisplayMode::Grid);
    ImGui::SameLine();
    if (ImGui::Button("List"))
        viewModel_.SetDisplayMode(ForgeLibraryDisplayMode::List);
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        static_cast<void>(viewModel_.Refresh());

    if (ImGui::Button("All"))
        viewModel_.SetFilter(ForgeLibraryFilter::All);
    ImGui::SameLine();
    if (ImGui::Button("Favorites"))
        viewModel_.SetFilter(ForgeLibraryFilter::Favorites);
    ImGui::SameLine();
    if (ImGui::Button("Project"))
        viewModel_.SetFilter(ForgeLibraryFilter::Project);

    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo(
            "##ForgeLibrarySort", SortLabel(viewModel_.SortMode())))
    {
        constexpr ForgeLibrarySortMode modes[] = {
            ForgeLibrarySortMode::Name,
            ForgeLibrarySortMode::Date,
            ForgeLibrarySortMode::Type};
        for (const ForgeLibrarySortMode mode : modes)
        {
            if (ImGui::Selectable(
                    SortLabel(mode), viewModel_.SortMode() == mode))
                viewModel_.SetSortMode(mode);
        }
        ImGui::EndCombo();
    }
}

bool ForgeLibraryPanel::DrawContent()
{
    if (viewModel_.Items().empty())
    {
        const char* message = viewModel_.Filter() == ForgeLibraryFilter::Favorites
            ? "No favorites yet."
            : "No Stamps in this Project Library.";
        const ImVec2 size = ImGui::CalcTextSize(message);
        ImGui::SetCursorPosY(
            std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() * 0.45F));
        ImGui::SetCursorPosX(
            std::max(ImGui::GetCursorPosX(),
                (ImGui::GetWindowWidth() - size.x) * 0.5F));
        ImGui::TextDisabled("%s", message);
        return false;
    }
    return viewModel_.DisplayMode() == ForgeLibraryDisplayMode::Grid
        ? DrawGrid() : DrawList();
}

bool ForgeLibraryPanel::DrawGrid()
{
    bool activate = false;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(
        1, static_cast<int>(available / (CardWidth + ImGui::GetStyle().ItemSpacing.x)));
    int column = 0;
    for (const ForgeLibraryItem& item : viewModel_.Items())
    {
        const std::string id = item.CatalogEntry.Reference.Id.ToString();
        ImGui::PushID(id.c_str());
        const bool selected = viewModel_.SelectedId() != nullptr &&
            *viewModel_.SelectedId() == item.CatalogEntry.Reference.Id;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##Card", ImVec2(CardWidth, CardHeight));
        DrawCardThumbnail(
            start, {start.x + CardWidth, start.y + CardHeight - 22.0F},
            item.CatalogEntry.Dimensions, selected);
        ImGui::SetCursorScreenPos(
            {start.x + 5.0F, start.y + CardHeight - 20.0F});
        ImGui::TextUnformatted(item.DisplayName.c_str());
        if (ImGui::IsItemHovered() || ImGui::IsItemHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            ImGui::SetTooltip(
                "%s\n%u x %u x %u\n%llu voxels",
                item.DisplayName.c_str(),
                item.CatalogEntry.Dimensions.X,
                item.CatalogEntry.Dimensions.Y,
                item.CatalogEntry.Dimensions.Z,
                static_cast<unsigned long long>(
                    item.CatalogEntry.VoxelCount));
        }
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool cardHovered =
            mouse.x >= start.x && mouse.x <= start.x + CardWidth &&
            mouse.y >= start.y && mouse.y <= start.y + CardHeight;
        if (cardHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            static_cast<void>(viewModel_.Select(item.CatalogEntry.Reference.Id));
        if (cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate = true;
        ImGui::PopID();
        ++column;
        if (column < columns) ImGui::SameLine();
        else column = 0;
    }
    return activate;
}

bool ForgeLibraryPanel::DrawList()
{
    bool activate = false;
    for (const ForgeLibraryItem& item : viewModel_.Items())
    {
        const std::string id = item.CatalogEntry.Reference.Id.ToString();
        ImGui::PushID(id.c_str());
        const bool selected = viewModel_.SelectedId() != nullptr &&
            *viewModel_.SelectedId() == item.CatalogEntry.Reference.Id;
        char label[384]{};
        std::snprintf(
            label, sizeof(label), "%s    %u x %u x %u    %llu voxels",
            item.DisplayName.c_str(),
            item.CatalogEntry.Dimensions.X,
            item.CatalogEntry.Dimensions.Y,
            item.CatalogEntry.Dimensions.Z,
            static_cast<unsigned long long>(item.CatalogEntry.VoxelCount));
        if (ImGui::Selectable(label, selected))
            static_cast<void>(viewModel_.Select(item.CatalogEntry.Reference.Id));
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate = true;
        ImGui::PopID();
    }
    return activate;
}

void ForgeLibraryPanel::DrawDetails()
{
    const ForgeLibrarySelectionDetails* const details =
        viewModel_.SelectedDetails();
    if (details == nullptr)
    {
        ImGui::TextDisabled("Select a Stamp to inspect it.");
        return;
    }

    ImGui::SeparatorText("Selection");
    ImGui::TextUnformatted(details->Name.c_str());
    ImGui::Text(
        "%u x %u x %u  |  %llu voxels  |  %u colors",
        details->Dimensions.X, details->Dimensions.Y, details->Dimensions.Z,
        static_cast<unsigned long long>(details->VoxelCount),
        details->PaletteCount);
    ImGui::TextDisabled("Date: %s", details->DateLabel.c_str());
    if (const VoxelStamp* const stamp = viewModel_.SelectedPreviewStamp())
        DrawStampPreview(*stamp);
    else
        ImGui::TextDisabled("Preview unavailable.");
}

void ForgeLibraryPanel::DrawStampPreview(const VoxelStamp& stamp) const
{
    constexpr float previewHeight = 112.0F;
    ImGui::BeginChild(
        "##ForgeLibraryStampPreview", ImVec2(0.0F, previewHeight), true,
        ImGuiWindowFlags_NoScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const auto voxels = stamp.Voxels();
    const auto palette = stamp.Palette();
    if (!voxels.empty())
    {
        float minimumX = std::numeric_limits<float>::max();
        float minimumY = std::numeric_limits<float>::max();
        float maximumX = std::numeric_limits<float>::lowest();
        float maximumY = std::numeric_limits<float>::lowest();
        for (const StampVoxel& voxel : voxels)
        {
            const float x = static_cast<float>(voxel.Position.X - voxel.Position.Z);
            const float y = static_cast<float>(voxel.Position.X + voxel.Position.Z) * 0.45F -
                static_cast<float>(voxel.Position.Y);
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
        const float spanX = std::max(1.0F, maximumX - minimumX);
        const float spanY = std::max(1.0F, maximumY - minimumY);
        const float scale = std::min(
            (available.x - 12.0F) / spanX,
            (available.y - 12.0F) / spanY);
        const std::size_t stride = std::max<std::size_t>(
            1U, (voxels.size() + 2047U) / 2048U);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        for (std::size_t index = 0U; index < voxels.size(); index += stride)
        {
            const StampVoxel& voxel = voxels[index];
            const float projectedX =
                static_cast<float>(voxel.Position.X - voxel.Position.Z);
            const float projectedY =
                static_cast<float>(voxel.Position.X + voxel.Position.Z) * 0.45F -
                static_cast<float>(voxel.Position.Y);
            const float x = origin.x + 6.0F + (projectedX - minimumX) * scale;
            const float y = origin.y + available.y - 6.0F -
                (projectedY - minimumY) * scale;
            const StampColor color = palette[voxel.LocalColorId].Color;
            const ImU32 packed = IM_COL32(
                color.Red, color.Green, color.Blue, color.Alpha);
            const float point = std::clamp(scale * 0.65F, 1.5F, 7.0F);
            drawList->AddRectFilled(
                {x - point, y - point}, {x + point, y + point}, packed, 1.0F);
        }
    }
    ImGui::EndChild();
}

} // namespace VoxelForge::Editor::Stamps
