#include "AssetBrowser.h"

#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
constexpr const char* NewFolderPopupName = "New Asset Folder";
constexpr const char* RenamePopupName = "Rename Asset Entry";
constexpr const char* DeletePopupName = "Delete Asset Entry";
constexpr const char* VoxInspectionPopupName = "VOX Inspection";
constexpr float DeletePopupContentWidth = 420.0F;
constexpr float MinimumSearchWidth = 120.0F;

template<std::size_t Size>
void CopyToBuffer(
    std::array<char, Size>& destination,
    const std::string_view source)
{
    destination.fill('\0');
    const std::size_t characterCount =
        std::min(source.size(), destination.size() - 1U);
    std::copy_n(source.data(), characterCount, destination.data());
}

std::string DisplayedFolderPath(const AssetDirectory& directory)
{
    std::string path = "Assets/";
    const std::filesystem::path relativePath =
        directory.CurrentRelativePath();

    if (!relativePath.empty())
    {
        path += relativePath.generic_string();
        path += '/';
    }

    return path;
}

void DrawError(const std::string_view error)
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

const char* FilterLabel(const AssetBrowserFilter filter)
{
    switch (filter)
    {
    case AssetBrowserFilter::All:
        return "All";
    case AssetBrowserFilter::Folders:
        return "Folders";
    case AssetBrowserFilter::Voxel:
        return "Voxel";
    case AssetBrowserFilter::Models:
        return "Models";
    case AssetBrowserFilter::Images:
        return "Images";
    case AssetBrowserFilter::Text:
        return "Text";
    case AssetBrowserFilter::Other:
        return "Other";
    }

    return "All";
}

const char* SortLabel(const AssetBrowserSortMode sortMode)
{
    switch (sortMode)
    {
    case AssetBrowserSortMode::Name:
        return "Name";
    case AssetBrowserSortMode::Type:
        return "Type";
    case AssetBrowserSortMode::Size:
        return "Size";
    case AssetBrowserSortMode::Modified:
        return "Modified";
    }

    return "Name";
}

std::string DisplayedFileSize(const AssetEntry& entry)
{
    if (entry.IsDirectory() || !entry.FileSize())
    {
        return "-";
    }

    return std::to_string(*entry.FileSize()) + " B";
}

bool IsVoxFile(const AssetEntry& entry)
{
    if (!entry.IsFile())
    {
        return false;
    }

    std::string extension = entry.Extension();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char character)
        {
            return character >= 'A' && character <= 'Z'
                ? static_cast<char>(character + ('a' - 'A'))
                : static_cast<char>(character);
        });
    return extension == ".vox";
}
}

bool AssetBrowser::SetAssetsRoot(
    const std::filesystem::path& assetsRoot)
{
    if (directory_.UsesAssetsRoot(assetsRoot))
    {
        return true;
    }

    selectedRelativePath_.reset();
    voxInspectionReport_.reset();
    ResetPendingOperations();
    viewModel_.OnProjectChanged();

    if (!directory_.SetAssetsRoot(assetsRoot))
    {
        const std::string rootError = directory_.LastError();
        directory_.Clear();
        SetError(rootError);
        return false;
    }

    error_.clear();
    statusMessage_.clear();
    return true;
}

void AssetBrowser::ClearAssetsRoot() noexcept
{
    directory_.Clear();
    selectedRelativePath_.reset();
    voxInspectionReport_.reset();
    newFolderName_.fill('\0');
    renameName_.fill('\0');
    error_.clear();
    statusMessage_.clear();
    ResetPendingOperations();
    viewModel_.OnProjectChanged();
}

void AssetBrowser::Draw(bool* open)
{
    if (!ImGui::Begin("Asset Browser", open))
    {
        ImGui::End();
        return;
    }

    if (!directory_.HasAssetsRoot())
    {
        ImGui::TextDisabled("No project loaded.");
        DrawError(error_);
        ImGui::End();
        return;
    }

    DrawToolbar();
    ImGui::TextUnformatted("Current Folder:");
    ImGui::SameLine();
    const std::string currentFolder = DisplayedFolderPath(directory_);
    ImGui::TextUnformatted(currentFolder.c_str());
    ImGui::Separator();

    DrawEntries();
    ImGui::Separator();
    DrawSelection();
    DrawStatusMessage();
    DrawError(error_);
    DrawNewFolderPopup();
    DrawRenamePopup();
    DrawDeletePopup();
    DrawVoxInspectionPopup();
    ImGui::End();
}

bool AssetBrowser::Refresh()
{
    const bool refreshed = directory_.Refresh();
    SynchronizeSelection();

    if (!refreshed)
    {
        SetError(directory_.LastError());
        return false;
    }

    error_.clear();
    statusMessage_ = "Assets refreshed.";
    return true;
}

bool AssetBrowser::SelectEntry(
    const std::filesystem::path& relativePath)
{
    const std::filesystem::path normalizedPath =
        relativePath.lexically_normal();
    const auto entry = std::ranges::find_if(
        directory_.Entries(),
        [&normalizedPath](const AssetEntry& candidate)
        {
            return candidate.RelativePath().lexically_normal() ==
                normalizedPath;
        });

    if (entry == directory_.Entries().end())
    {
        return false;
    }

    selectedRelativePath_ = entry->RelativePath();
    return true;
}

AssetOperationResult AssetBrowser::RenameSelectedEntry(
    const std::string_view newName)
{
    if (!selectedRelativePath_)
    {
        AssetOperationResult result{
            false,
            "No asset is selected.",
            std::nullopt};
        SetError(result.Message);
        return result;
    }

    const AssetOperationResult result = directory_.RenameEntry(
        *selectedRelativePath_,
        newName);

    if (!result.Succeeded)
    {
        SynchronizeSelection();
        SetError(result.Message);
        return result;
    }

    selectedRelativePath_ = result.ResultingRelativePath;
    SynchronizeSelection();
    SetStatus(result.Message);
    return result;
}

AssetDeleteAssessment AssetBrowser::CanDeleteSelectedEntry() const
{
    if (!selectedRelativePath_)
    {
        return {
            false,
            false,
            false,
            false,
            {},
            "No asset is selected."};
    }

    return directory_.CanDeleteEntry(*selectedRelativePath_);
}

AssetOperationResult AssetBrowser::DeleteSelectedEntry()
{
    if (!selectedRelativePath_)
    {
        AssetOperationResult result{
            false,
            "No asset is selected.",
            std::nullopt};
        SetError(result.Message);
        return result;
    }

    const AssetOperationResult result = directory_.DeleteEntry(
        *selectedRelativePath_);

    if (!result.Succeeded)
    {
        SynchronizeSelection();
        SetError(result.Message);
        return result;
    }

    selectedRelativePath_.reset();
    SetStatus(result.Message);
    return result;
}

const AssetDirectory& AssetBrowser::Directory() const noexcept
{
    return directory_;
}

const std::optional<std::filesystem::path>&
AssetBrowser::SelectedRelativePath() const noexcept
{
    return selectedRelativePath_;
}

AssetBrowserViewSettings& AssetBrowser::ViewSettings() noexcept
{
    return viewModel_.Settings();
}

const AssetBrowserViewSettings& AssetBrowser::ViewSettings() const noexcept
{
    return viewModel_.Settings();
}

void AssetBrowser::SetSearchText(const std::string_view searchText) noexcept
{
    viewModel_.SetSearchText(searchText);
}

std::vector<const AssetEntry*> AssetBrowser::VisibleEntries() const
{
    return viewModel_.VisibleEntries(directory_.Entries());
}

void AssetBrowser::SetMessageCallback(MessageCallback callback)
{
    messageCallback_ = std::move(callback);
}

void AssetBrowser::DrawToolbar()
{
    AssetBrowserViewSettings& settings = viewModel_.Settings();

    if (ImGui::Button("Assets"))
    {
        if (directory_.GoToAssetsRoot())
        {
            selectedRelativePath_.reset();
            error_.clear();
        }
        else
        {
            SetError(directory_.LastError());
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!directory_.CanGoBack());

    if (ImGui::Button("Back"))
    {
        if (directory_.Back())
        {
            selectedRelativePath_.reset();
            error_.clear();
        }
        else
        {
            SetError(directory_.LastError());
        }
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
    {
        static_cast<void>(Refresh());
    }

    ImGui::SameLine();

    if (ImGui::Button("New Folder"))
    {
        RequestNewFolder();
    }

    ImGui::TextUnformatted("View:");
    ImGui::SameLine();

    if (ImGui::RadioButton(
            "Grid",
            settings.DisplayMode == AssetBrowserDisplayMode::Grid))
    {
        settings.DisplayMode = AssetBrowserDisplayMode::Grid;
    }

    ImGui::SameLine();

    if (ImGui::RadioButton(
            "List",
            settings.DisplayMode == AssetBrowserDisplayMode::List))
    {
        settings.DisplayMode = AssetBrowserDisplayMode::List;
    }

    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(
        1.0F,
        std::min(
            std::max(120.0F, ImGui::GetFontSize() * 9.0F),
            ImGui::GetContentRegionAvail().x)));

    if (ImGui::BeginCombo("##AssetFilter", FilterLabel(settings.Filter)))
    {
        constexpr std::array filters = {
            AssetBrowserFilter::All,
            AssetBrowserFilter::Folders,
            AssetBrowserFilter::Voxel,
            AssetBrowserFilter::Models,
            AssetBrowserFilter::Images,
            AssetBrowserFilter::Text,
            AssetBrowserFilter::Other};

        for (const AssetBrowserFilter filter : filters)
        {
            const bool selected = settings.Filter == filter;

            if (ImGui::Selectable(FilterLabel(filter), selected))
            {
                settings.Filter = filter;
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::TextUnformatted("Sort by:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(
        1.0F,
        std::min(
            std::max(120.0F, ImGui::GetFontSize() * 9.0F),
            ImGui::GetContentRegionAvail().x)));

    if (ImGui::BeginCombo("##AssetSort", SortLabel(settings.SortMode)))
    {
        constexpr std::array sortModes = {
            AssetBrowserSortMode::Name,
            AssetBrowserSortMode::Type,
            AssetBrowserSortMode::Size,
            AssetBrowserSortMode::Modified};

        for (const AssetBrowserSortMode sortMode : sortModes)
        {
            const bool selected = settings.SortMode == sortMode;

            if (ImGui::Selectable(SortLabel(sortMode), selected))
            {
                settings.SortMode = sortMode;
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    if (ImGui::Button(
            settings.SortAscending
                ? "Order: Ascending"
                : "Order: Descending"))
    {
        settings.SortAscending = !settings.SortAscending;
    }

    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    const float clearButtonWidth = ImGui::CalcTextSize("Clear").x +
        ImGui::GetStyle().FramePadding.x * 2.0F;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool clearOnSameLine =
        availableWidth >= MinimumSearchWidth + clearButtonWidth +
            ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(
        clearOnSameLine
            ? availableWidth - clearButtonWidth -
                ImGui::GetStyle().ItemSpacing.x
            : std::max(1.0F, availableWidth));
    ImGui::InputText(
        "##AssetSearch",
        settings.SearchText.data(),
        settings.SearchText.size());

    if (clearOnSameLine)
    {
        ImGui::SameLine();
    }

    if (ImGui::Button("Clear"))
    {
        viewModel_.ClearSearch();
    }
}

void AssetBrowser::DrawEntries()
{
    const std::vector<AssetEntry>& entries = directory_.Entries();
    const std::vector<const AssetEntry*> visibleEntries = VisibleEntries();
    std::optional<std::filesystem::path> directoryToEnter;

    if (entries.empty())
    {
        ImGui::TextDisabled("Folder is empty.");
    }
    else if (visibleEntries.empty())
    {
        ImGui::TextDisabled("No matching assets.");
    }
    else if (viewModel_.Settings().DisplayMode ==
        AssetBrowserDisplayMode::Grid)
    {
        DrawGrid(visibleEntries, directoryToEnter);
    }
    else
    {
        DrawList(visibleEntries, directoryToEnter);
    }

    DrawBackgroundContextMenu();

    if (!directoryToEnter)
    {
        return;
    }

    if (directory_.EnterDirectory(*directoryToEnter))
    {
        selectedRelativePath_.reset();
        error_.clear();
    }
    else
    {
        SetError(directory_.LastError());
    }
}

void AssetBrowser::DrawGrid(
    const std::vector<const AssetEntry*>& entries,
    std::optional<std::filesystem::path>& directoryToEnter)
{
    const AssetBrowserViewSettings& settings = viewModel_.Settings();
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float cellWidth = std::max(
        settings.GridCellSize,
        ImGui::GetFontSize() * 7.0F);
    const float cellHeight = std::max(
        ImGui::GetFontSize() * 4.0F,
        cellWidth * 0.62F);
    const float availableWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    const int columnCount = std::max(
        1,
        static_cast<int>(std::floor(
            (availableWidth + itemSpacing) / (cellWidth + itemSpacing))));

    if (!ImGui::BeginTable(
            "##AssetGrid",
            columnCount,
            ImGuiTableFlags_SizingFixedFit))
    {
        return;
    }

    for (const AssetEntry* entry : entries)
    {
        ImGui::TableNextColumn();
        const std::string identifier = entry->RelativePath().generic_string();
        ImGui::PushID(identifier.c_str());
        const bool selected = selectedRelativePath_ &&
            *selectedRelativePath_ == entry->RelativePath();
        const std::string label = std::string(AssetEntryMarker(*entry)) +
            "\n" + entry->Name();

        if (ImGui::Selectable(
                label.c_str(),
                selected,
                ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(cellWidth, cellHeight)))
        {
            selectedRelativePath_ = entry->RelativePath();
        }

        if (entry->IsDirectory() && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            directoryToEnter = entry->AbsolutePath();
        }

        DrawEntryContextMenu(*entry, directoryToEnter);
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void AssetBrowser::DrawList(
    const std::vector<const AssetEntry*>& entries,
    std::optional<std::filesystem::path>& directoryToEnter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##AssetList", 4, tableFlags))
    {
        return;
    }

    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Extension", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (const AssetEntry* entry : entries)
    {
        ImGui::TableNextRow();
        const std::string identifier = entry->RelativePath().generic_string();
        ImGui::PushID(identifier.c_str());
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(AssetEntryMarker(*entry).data());
        ImGui::TableSetColumnIndex(1);
        const bool selected = selectedRelativePath_ &&
            *selectedRelativePath_ == entry->RelativePath();

        if (ImGui::Selectable(
                entry->Name().c_str(),
                selected,
                ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectedRelativePath_ = entry->RelativePath();
        }

        if (entry->IsDirectory() && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            directoryToEnter = entry->AbsolutePath();
        }

        DrawEntryContextMenu(*entry, directoryToEnter);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(
            entry->Extension().empty() ? "-" : entry->Extension().c_str());
        ImGui::TableSetColumnIndex(3);
        const std::string size = DisplayedFileSize(*entry);
        ImGui::TextUnformatted(size.c_str());
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void AssetBrowser::DrawEntryContextMenu(
    const AssetEntry& entry,
    std::optional<std::filesystem::path>& directoryToEnter)
{
    if (!ImGui::BeginPopupContextItem())
    {
        return;
    }

    selectedRelativePath_ = entry.RelativePath();

    if (ImGui::MenuItem("Open", nullptr, false, entry.IsDirectory()))
    {
        directoryToEnter = entry.AbsolutePath();
    }

    if (ImGui::MenuItem("Rename"))
    {
        RequestRename(entry);
    }

    if (ImGui::MenuItem("Delete"))
    {
        RequestDelete(entry);
    }

    if (IsVoxFile(entry))
    {
        ImGui::Separator();

        if (ImGui::MenuItem("Open in Viewport") && openVoxCallback_)
        {
            openVoxCallback_(entry.AbsolutePath());
        }

        if (ImGui::MenuItem("Inspect VOX"))
        {
            InspectVox(entry);
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::SetOpenVoxCallback(OpenVoxCallback callback)
{
    openVoxCallback_ = std::move(callback);
}

void AssetBrowser::DrawSelection() const
{
    ImGui::TextUnformatted("Selected:");

    if (!selectedRelativePath_)
    {
        ImGui::TextDisabled("Nothing selected.");
        return;
    }

    const auto selectedEntry = std::ranges::find_if(
        directory_.Entries(),
        [this](const AssetEntry& entry)
        {
            return entry.RelativePath() == *selectedRelativePath_;
        });

    if (selectedEntry == directory_.Entries().end())
    {
        ImGui::TextDisabled("Nothing selected.");
        return;
    }

    ImGui::TextUnformatted(selectedEntry->Name().c_str());
    const std::vector<const AssetEntry*> visibleEntries = VisibleEntries();
    const bool visible = std::ranges::any_of(
        visibleEntries,
        [this](const AssetEntry* entry)
        {
            return entry->RelativePath() == *selectedRelativePath_;
        });

    if (!visible)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(hidden by search or filter)");
    }
}

void AssetBrowser::DrawStatusMessage() const
{
    if (statusMessage_.empty())
    {
        return;
    }

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(0.40F, 0.80F, 0.50F, 1.0F));
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    ImGui::PopStyleColor();
}

void AssetBrowser::DrawBackgroundContextMenu()
{
    constexpr ImGuiPopupFlags popupFlags =
        ImGuiPopupFlags_MouseButtonRight |
        ImGuiPopupFlags_NoOpenOverItems;

    if (!ImGui::BeginPopupContextWindow(
            "##AssetBrowserBackgroundContext",
            popupFlags))
    {
        return;
    }

    if (ImGui::MenuItem("New Folder"))
    {
        RequestNewFolder();
    }

    if (ImGui::MenuItem("Refresh"))
    {
        static_cast<void>(Refresh());
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawNewFolderPopup()
{
    if (openNewFolderPopup_)
    {
        ImGui::OpenPopup(NewFolderPopupName);
        openNewFolderPopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            NewFolderPopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (!newFolderAssetsRoot_ ||
        !directory_.UsesAssetsRoot(*newFolderAssetsRoot_))
    {
        newFolderAssetsRoot_.reset();
        newFolderName_.fill('\0');
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (ImGui::InputText(
            "Folder Name",
            newFolderName_.data(),
            newFolderName_.size()))
    {
        error_.clear();
    }

    DrawError(error_);

    if (ImGui::Button("Create"))
    {
        if (directory_.CreateFolder(newFolderName_.data()))
        {
            SynchronizeSelection();
            const std::string createdFolderName(newFolderName_.data());
            newFolderName_.fill('\0');
            newFolderAssetsRoot_.reset();
            SetStatus("Folder created: " + createdFolderName + ".");
            ImGui::CloseCurrentPopup();
        }
        else
        {
            SetError(directory_.LastError());
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        newFolderAssetsRoot_.reset();
        newFolderName_.fill('\0');
        error_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawRenamePopup()
{
    if (openRenamePopup_)
    {
        ImGui::OpenPopup(RenamePopupName);
        openRenamePopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            RenamePopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (!pendingRename_ ||
        !IsPendingOperationCurrent(*pendingRename_))
    {
        pendingRename_.reset();
        renameName_.fill('\0');
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Current name: %s", pendingRename_->Name.c_str());
    ImGui::TextDisabled(
        "For files, the current extension is kept when the new name has no extension.");

    if (ImGui::InputText(
            "New Name",
            renameName_.data(),
            renameName_.size()))
    {
        error_.clear();
        statusMessage_.clear();
    }

    DrawError(error_);

    if (ImGui::Button("Rename"))
    {
        selectedRelativePath_ = pendingRename_->RelativePath;
        const AssetOperationResult result =
            RenameSelectedEntry(renameName_.data());

        if (result.Succeeded)
        {
            pendingRename_.reset();
            renameName_.fill('\0');
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        pendingRename_.reset();
        renameName_.fill('\0');
        error_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawDeletePopup()
{
    if (openDeletePopup_)
    {
        ImGui::OpenPopup(DeletePopupName);
        openDeletePopup_ = false;
    }

    ImGui::SetNextWindowContentSize(
        ImVec2(DeletePopupContentWidth, 0.0F));

    if (!ImGui::BeginPopupModal(
            DeletePopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (!pendingDelete_ ||
        !IsPendingOperationCurrent(*pendingDelete_))
    {
        pendingDelete_.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const AssetDeleteAssessment assessment =
        directory_.CanDeleteEntry(pendingDelete_->RelativePath);
    const char* typeName = assessment.IsSymbolicLink
        ? "Symbolic link"
        : pendingDelete_->Type == AssetEntryType::Directory
            ? "Folder"
            : "File";

    ImGui::Text("Name: %s", pendingDelete_->Name.c_str());
    ImGui::Text("Type: %s", typeName);
    ImGui::TextWrapped(
        "Relative path: %s",
        pendingDelete_->RelativePath.generic_string().c_str());
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Warning: this permanently deletes the selected entry. This action cannot be undone.");

    if (assessment.IsNonEmptyDirectory)
    {
        ImGui::TextWrapped(
            "Non-empty folders are never deleted in this version.");
    }

    if (!assessment.CanDelete)
    {
        DrawError(assessment.Message);
    }

    DrawError(error_);
    ImGui::BeginDisabled(assessment.IsNonEmptyDirectory);

    if (ImGui::Button("Delete"))
    {
        selectedRelativePath_ = pendingDelete_->RelativePath;
        const AssetOperationResult result = DeleteSelectedEntry();

        if (result.Succeeded)
        {
            pendingDelete_.reset();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        pendingDelete_.reset();
        error_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawVoxInspectionPopup()
{
    if (openVoxInspectionPopup_)
    {
        ImGui::OpenPopup(VoxInspectionPopupName);
        openVoxInspectionPopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            VoxInspectionPopupName,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (!voxInspectionReport_)
    {
        ImGui::TextDisabled("No VOX inspection report is available.");
    }
    else
    {
        const VoxInspectionReport& report = *voxInspectionReport_;
        ImGui::TextWrapped(
            "Relative path: %s",
            report.RelativePath.generic_string().c_str());

        if (!report.Succeeded)
        {
            DrawError(report.Message);
        }
        else
        {
            ImGui::Text("Version: %u", report.Version);
            ImGui::Text("Models: %zu", report.Models.size());
            ImGui::Text("Total voxels: %llu",
                static_cast<unsigned long long>(report.TotalVoxelCount));
            ImGui::Text(
                "Palette: %s",
                report.HasCustomPalette ? "Custom RGBA" : "Default");
            ImGui::Separator();

            for (std::size_t index = 0; index < report.Models.size(); ++index)
            {
                const VoxModelReport& model = report.Models[index];
                ImGui::BulletText(
                    "Model %zu: %u x %u x %u, %u voxels",
                    index + 1U,
                    model.X,
                    model.Y,
                    model.Z,
                    model.VoxelCount);
            }
        }

        if (!report.Warnings.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Warnings:");

            for (const std::string& warning : report.Warnings)
            {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
    }

    ImGui::Spacing();

    if (ImGui::Button("Close"))
    {
        ImGui::CloseCurrentPopup();
        voxInspectionReport_.reset();
    }

    ImGui::EndPopup();
}

void AssetBrowser::RequestNewFolder()
{
    newFolderName_.fill('\0');
    newFolderAssetsRoot_ = directory_.AssetsRoot();
    error_.clear();
    statusMessage_.clear();
    openNewFolderPopup_ = true;
}

void AssetBrowser::RequestRename(const AssetEntry& entry)
{
    selectedRelativePath_ = entry.RelativePath();
    pendingRename_ = PendingEntryOperation{
        directory_.AssetsRoot(),
        entry.RelativePath(),
        entry.Name(),
        entry.Type()};
    CopyToBuffer(renameName_, entry.Name());
    error_.clear();
    statusMessage_.clear();
    openRenamePopup_ = true;
}

void AssetBrowser::RequestDelete(const AssetEntry& entry)
{
    selectedRelativePath_ = entry.RelativePath();
    pendingDelete_ = PendingEntryOperation{
        directory_.AssetsRoot(),
        entry.RelativePath(),
        entry.Name(),
        entry.Type()};
    error_.clear();
    statusMessage_.clear();
    openDeletePopup_ = true;
}

void AssetBrowser::InspectVox(const AssetEntry& entry)
{
    using Asset::AssetImportOutcome;
    using Asset::Vox::VoxImporter;
    using Asset::Vox::VoxModel;

    const std::string relativePath = entry.RelativePath().generic_string();
    EmitMessage("VOX inspection started: " + relativePath);

    const AssetImportOutcome<VoxModel> outcome =
        VoxImporter{}.Inspect(entry.AbsolutePath());
    VoxInspectionReport report;
    report.RelativePath = entry.RelativePath();
    report.Succeeded = outcome.Result.Succeeded;
    report.Message = outcome.Result.Message;
    report.Warnings = outcome.Result.Warnings;

    if (outcome.Asset)
    {
        report.Version = outcome.Asset->Version;
        report.TotalVoxelCount = outcome.Asset->TotalVoxelCount();
        report.HasCustomPalette = outcome.Asset->HasCustomPalette;
        report.Models.reserve(outcome.Asset->Models.size());

        for (const Asset::Vox::VoxModelMetadata& model :
             outcome.Asset->Models)
        {
            report.Models.push_back({
                model.Dimensions.X,
                model.Dimensions.Y,
                model.Dimensions.Z,
                static_cast<std::uint32_t>(model.VoxelCount())});
        }
    }

    if (outcome.Result.Succeeded)
    {
        EmitMessage("VOX inspection succeeded: " + relativePath);
    }
    else
    {
        EmitMessage(
            "VOX inspection failed: " + relativePath + " - " +
            outcome.Result.Message);
    }

    for (const std::string& warning : outcome.Result.Warnings)
    {
        EmitMessage("VOX inspection warning: " + warning);
    }

    voxInspectionReport_ = std::move(report);
    openVoxInspectionPopup_ = true;
}

void AssetBrowser::EmitMessage(std::string message) const
{
    if (messageCallback_)
    {
        messageCallback_(std::move(message));
    }
}

void AssetBrowser::ResetPendingOperations() noexcept
{
    newFolderAssetsRoot_.reset();
    pendingRename_.reset();
    pendingDelete_.reset();
    openNewFolderPopup_ = false;
    openRenamePopup_ = false;
    openDeletePopup_ = false;
    newFolderName_.fill('\0');
    renameName_.fill('\0');
}

bool AssetBrowser::IsPendingOperationCurrent(
    const PendingEntryOperation& operation) const
{
    return directory_.UsesAssetsRoot(operation.AssetsRoot);
}

void AssetBrowser::SynchronizeSelection()
{
    if (!selectedRelativePath_)
    {
        return;
    }

    const bool selectionExists = std::ranges::any_of(
        directory_.Entries(),
        [this](const AssetEntry& entry)
        {
            return entry.RelativePath() == *selectedRelativePath_;
        });

    if (!selectionExists)
    {
        selectedRelativePath_.reset();
    }
}

void AssetBrowser::SetError(std::string error)
{
    error_ = std::move(error);
    statusMessage_.clear();
}

void AssetBrowser::SetStatus(std::string message)
{
    statusMessage_ = std::move(message);
    error_.clear();
}

} // namespace VoxelForge::Editor
