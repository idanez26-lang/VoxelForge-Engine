#include "VoxelStamps/Library/ForgeLibraryPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr float MinimumCardHeight = 94.0F;
constexpr float MaximumCardHeight = 136.0F;
constexpr float CardLabelHeight = 24.0F;

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

[[nodiscard]] bool ActiveButton(
    const char* const label,
    const bool active)
{
    if (active)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    }
    const bool pressed = ImGui::Button(label);
    if (active) ImGui::PopStyleColor(2);
    return pressed;
}

[[nodiscard]] std::string ElideText(
    const std::string_view text,
    const float availableWidth)
{
    if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <=
        availableWidth)
    {
        return std::string(text);
    }

    std::string result(text);
    while (!result.empty())
    {
        std::size_t codePointStart = result.size() - 1U;
        while (codePointStart > 0U &&
               (static_cast<unsigned char>(result[codePointStart]) & 0xC0U) ==
                   0x80U)
        {
            --codePointStart;
        }
        result.resize(codePointStart);
        const std::string candidate = result + "...";
        if (ImGui::CalcTextSize(candidate.c_str()).x <= availableWidth)
            return candidate;
    }
    return "...";
}

void DrawProjectedThumbnail(
    const ImVec2 minimum,
    const ImVec2 maximum,
    const ForgeLibraryThumbnail* const thumbnail,
    const bool selected)
{
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(
        selected ? ImGuiCol_Header : ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(
        selected ? ImGuiCol_HeaderActive : ImGuiCol_Border);
    drawList->AddRectFilled(minimum, maximum, background, 4.0F);
    drawList->AddRect(minimum, maximum, border, 4.0F, 0, selected ? 2.0F : 1.0F);
    if (thumbnail == nullptr || !thumbnail->Available())
    {
        const char* const unavailable = "Preview unavailable";
        const ImVec2 textSize = ImGui::CalcTextSize(unavailable);
        drawList->AddText(
            {minimum.x + (maximum.x - minimum.x - textSize.x) * 0.5F,
             minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F},
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            unavailable);
        return;
    }

    const float width = std::max(1.0F, maximum.x - minimum.x - 12.0F);
    const float height = std::max(1.0F, maximum.y - minimum.y - 12.0F);
    const float point = std::clamp(
        std::min(width, height) /
            std::sqrt(static_cast<float>(
                std::max<std::uint64_t>(1U, thumbnail->SourceVoxelCount))),
        1.0F,
        4.5F);
    for (const ForgeLibraryThumbnailPoint& voxel : thumbnail->Points)
    {
        const float x = minimum.x + 6.0F + voxel.X * width;
        const float y = maximum.y - 6.0F - voxel.Y * height;
        const ImU32 color = IM_COL32(
            voxel.Color.Red,
            voxel.Color.Green,
            voxel.Color.Blue,
            voxel.Color.Alpha);
        drawList->AddRectFilled(
            {x - point, y - point},
            {x + point, y + point},
            color,
            1.0F);
    }
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

    const ForgeLibraryResponsiveLayout layout =
        ResolveForgeLibraryResponsiveLayout(
            ImGui::GetContentRegionAvail().x,
            viewModel_.DisplayMode(),
            viewModel_.SelectedId() != nullptr);
    const float detailsHeight =
        viewModel_.SelectedDetails() == nullptr ? 62.0F : 252.0F;
    ImGui::BeginChild("##ForgeLibraryContent", ImVec2(0.0F, -detailsHeight), true);
    const bool activate = DrawContent(layout, panelResult);
    ImGui::EndChild();

    DrawDetails();
    bool useRequested = activate;
    if (layout.UseButtonVisible)
    {
        useRequested |= ImGui::Button(
            "USE STAMP",
            ImVec2(layout.UseButtonFullWidth ? -1.0F : 120.0F, 0.0F));
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

    if (viewModel_.EmptyState() == ForgeLibraryEmptyState::None &&
        !viewModel_.StatusMessage().empty())
    {
        ImGui::TextDisabled("%s", viewModel_.StatusMessage().c_str());
    }
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
    const bool hasSearch = searchBuffer_[0] != '\0';
    ImGui::SetNextItemWidth(
        hasSearch
        ? std::max(60.0F, ImGui::GetContentRegionAvail().x - 58.0F)
        : -1.0F);
    if (ImGui::InputTextWithHint(
            "##ForgeLibrarySearch", "Search Stamps...",
            searchBuffer_.data(), searchBuffer_.size()))
    {
        viewModel_.SetSearchText(searchBuffer_.data());
    }
    if (hasSearch)
    {
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            searchBuffer_[0] = '\0';
            viewModel_.SetSearchText({});
        }
    }

    if (ActiveButton(
            "Grid",
            viewModel_.DisplayMode() == ForgeLibraryDisplayMode::Grid))
    {
        viewModel_.SetDisplayMode(ForgeLibraryDisplayMode::Grid);
    }
    ImGui::SameLine();
    if (ActiveButton(
            "List",
            viewModel_.DisplayMode() == ForgeLibraryDisplayMode::List))
    {
        viewModel_.SetDisplayMode(ForgeLibraryDisplayMode::List);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        static_cast<void>(viewModel_.Refresh());

    if (ActiveButton("All", viewModel_.Filter() == ForgeLibraryFilter::All))
        viewModel_.SetFilter(ForgeLibraryFilter::All);
    ImGui::SameLine();
    if (ActiveButton(
            "Favorites",
            viewModel_.Filter() == ForgeLibraryFilter::Favorites))
    {
        viewModel_.SetFilter(ForgeLibraryFilter::Favorites);
    }
    ImGui::SameLine();
    if (ActiveButton(
            "Project",
            viewModel_.Filter() == ForgeLibraryFilter::Project))
    {
        viewModel_.SetFilter(ForgeLibraryFilter::Project);
    }

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

bool ForgeLibraryPanel::DrawContent(
    const ForgeLibraryResponsiveLayout& layout,
    ForgeLibraryPanelResult& result)
{
    if (viewModel_.Items().empty()) return DrawEmptyState(result);
    if (layout.Mode == ForgeLibraryResponsiveMode::Grid)
        return DrawGrid(layout.GridColumns);
    return DrawList(layout.Mode == ForgeLibraryResponsiveMode::CompactList);
}

bool ForgeLibraryPanel::DrawEmptyState(ForgeLibraryPanelResult& result)
{
    const char* title = "Forge Library unavailable";
    const char* explanation =
        "The Project Stamp catalogue could not be loaded.";
    switch (viewModel_.EmptyState())
    {
    case ForgeLibraryEmptyState::NoProject:
        title = "No project is open";
        explanation = "Open or create a project to use its Forge Library.";
        break;
    case ForgeLibraryEmptyState::EmptyProject:
        title = "This Project Library is empty";
        explanation =
            "Select voxels, then save them as a reusable Stamp.";
        break;
    case ForgeLibraryEmptyState::NoSearchResults:
        title = "No matching Stamps";
        explanation = "Try another search or clear the current text.";
        break;
    case ForgeLibraryEmptyState::FavoritesEmpty:
        title = "No favorites yet";
        explanation =
            "Favorite Stamps will appear here in a future Forge Library update.";
        break;
    case ForgeLibraryEmptyState::CatalogUnavailable:
        if (!viewModel_.StatusMessage().empty())
            explanation = viewModel_.StatusMessage().c_str();
        break;
    case ForgeLibraryEmptyState::None:
        title = "No Stamps";
        explanation = "No Stamp is available in this view.";
        break;
    }

    ImGui::Dummy(ImVec2(0.0F, 14.0F));
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() +
        std::max(0.0F, (width - titleSize.x) * 0.5F));
    ImGui::TextUnformatted(title);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("%s", explanation);
    ImGui::PopTextWrapPos();

    if (viewModel_.EmptyState() == ForgeLibraryEmptyState::EmptyProject)
    {
        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        result.SaveSelectionRequested |= ImGui::Button(
            "Save Selection As Stamp",
            ImVec2(-1.0F, 0.0F));
    }
    else if (viewModel_.EmptyState() ==
             ForgeLibraryEmptyState::NoSearchResults)
    {
        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        if (ImGui::Button("Clear Search", ImVec2(-1.0F, 0.0F)))
        {
            searchBuffer_[0] = '\0';
            viewModel_.SetSearchText({});
        }
    }
    return false;
}

bool ForgeLibraryPanel::DrawGrid(const std::size_t requestedColumns)
{
    bool activate = false;
    const float available = ImGui::GetContentRegionAvail().x;
    const std::size_t columns = std::max<std::size_t>(1U, requestedColumns);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardWidth = std::max(
        72.0F,
        (available - spacing * static_cast<float>(columns - 1U)) /
            static_cast<float>(columns));
    const float cardHeight = std::clamp(
        cardWidth * 0.78F,
        MinimumCardHeight,
        MaximumCardHeight);
    std::size_t column = 0U;
    for (const ForgeLibraryItem& item : viewModel_.Items())
    {
        const std::string id = item.CatalogEntry.Reference.Id.ToString();
        ImGui::PushID(id.c_str());
        const bool selected = viewModel_.SelectedId() != nullptr &&
            *viewModel_.SelectedId() == item.CatalogEntry.Reference.Id;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##Card", ImVec2(cardWidth, cardHeight));
        DrawProjectedThumbnail(
            start,
            {start.x + cardWidth, start.y + cardHeight - CardLabelHeight},
            viewModel_.ThumbnailFor(item.CatalogEntry.Reference.Id),
            selected);
        ImGui::SetCursorScreenPos(
            {start.x + 5.0F, start.y + cardHeight - 20.0F});
        const std::string label =
            ElideText(item.DisplayName, std::max(1.0F, cardWidth - 10.0F));
        ImGui::TextUnformatted(label.c_str());
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool cardHovered =
            mouse.x >= start.x && mouse.x <= start.x + cardWidth &&
            mouse.y >= start.y && mouse.y <= start.y + cardHeight;
        if (cardHovered)
            ImGui::SetTooltip(
                "%s\n%u x %u x %u\n%llu voxels",
                item.DisplayName.c_str(),
                item.CatalogEntry.Dimensions.X,
                item.CatalogEntry.Dimensions.Y,
                item.CatalogEntry.Dimensions.Z,
                static_cast<unsigned long long>(
                    item.CatalogEntry.VoxelCount));
        if (cardHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            static_cast<void>(viewModel_.Select(item.CatalogEntry.Reference.Id));
        if (cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate = true;
        ImGui::PopID();
        ++column;
        if (column < columns) ImGui::SameLine();
        else column = 0U;
    }
    return activate;
}

bool ForgeLibraryPanel::DrawList(const bool compact)
{
    bool activate = false;
    for (const ForgeLibraryItem& item : viewModel_.Items())
    {
        const std::string id = item.CatalogEntry.Reference.Id.ToString();
        ImGui::PushID(id.c_str());
        const bool selected = viewModel_.SelectedId() != nullptr &&
            *viewModel_.SelectedId() == item.CatalogEntry.Reference.Id;
        char label[384]{};
        if (compact)
        {
            std::snprintf(
                label, sizeof(label), "%s", item.DisplayName.c_str());
        }
        else
        {
            std::snprintf(
                label, sizeof(label), "%s    %u x %u x %u    %llu voxels",
                item.DisplayName.c_str(),
                item.CatalogEntry.Dimensions.X,
                item.CatalogEntry.Dimensions.Y,
                item.CatalogEntry.Dimensions.Z,
                static_cast<unsigned long long>(
                    item.CatalogEntry.VoxelCount));
        }
        if (ImGui::Selectable(label, selected))
            static_cast<void>(viewModel_.Select(item.CatalogEntry.Reference.Id));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item.DisplayName.c_str());
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
    if (const ForgeLibraryThumbnail* const thumbnail =
            viewModel_.SelectedThumbnail())
    {
        DrawThumbnail(*thumbnail, 112.0F, true);
    }
    else
        ImGui::TextDisabled("Preview unavailable.");
}

void ForgeLibraryPanel::DrawThumbnail(
    const ForgeLibraryThumbnail& thumbnail,
    const float height,
    const bool framed) const
{
    ImGui::BeginChild(
        "##ForgeLibraryStampPreview", ImVec2(0.0F, height), framed,
        ImGuiWindowFlags_NoScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    DrawProjectedThumbnail(
        origin,
        {origin.x + available.x, origin.y + available.y},
        &thumbnail,
        false);
    ImGui::EndChild();
}

} // namespace VoxelForge::Editor::Stamps
