#include "AssetBrowser.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
constexpr const char* NewFolderPopupName = "New Asset Folder";
constexpr const char* RenamePopupName = "Rename Asset Entry";
constexpr const char* DeletePopupName = "Delete Asset Entry";
constexpr float DeletePopupContentWidth = 420.0F;

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
}

bool AssetBrowser::SetAssetsRoot(
    const std::filesystem::path& assetsRoot)
{
    if (directory_.UsesAssetsRoot(assetsRoot))
    {
        return true;
    }

    selectedRelativePath_.reset();
    ResetPendingOperations();

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
    newFolderName_.fill('\0');
    renameName_.fill('\0');
    error_.clear();
    statusMessage_.clear();
    ResetPendingOperations();
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

void AssetBrowser::DrawToolbar()
{
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
}

void AssetBrowser::DrawEntries()
{
    const std::vector<AssetEntry>& entries = directory_.Entries();
    std::optional<std::filesystem::path> directoryToEnter;

    if (entries.empty())
    {
        ImGui::TextDisabled("Folder is empty.");
    }
    else
    {
        for (const AssetEntry& entry : entries)
        {
            const bool selected = selectedRelativePath_ &&
                *selectedRelativePath_ == entry.RelativePath();
            const std::string label =
                std::string(entry.IsDirectory() ? "[DIR]  " : "[FILE] ") +
                entry.Name() + "##" +
                entry.RelativePath().generic_string();

            if (ImGui::Selectable(label.c_str(), selected))
            {
                selectedRelativePath_ = entry.RelativePath();
            }

            if (entry.IsDirectory() && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                directoryToEnter = entry.AbsolutePath();
            }

            if (ImGui::BeginPopupContextItem())
            {
                selectedRelativePath_ = entry.RelativePath();

                if (ImGui::MenuItem(
                        "Open",
                        nullptr,
                        false,
                        entry.IsDirectory()))
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

                ImGui::EndPopup();
            }
        }
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
