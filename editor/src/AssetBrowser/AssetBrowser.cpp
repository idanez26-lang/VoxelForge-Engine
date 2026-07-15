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

    if (!directory_.SetAssetsRoot(assetsRoot))
    {
        const std::string rootError = directory_.LastError();
        directory_.Clear();
        SetError(rootError);
        return false;
    }

    error_.clear();
    return true;
}

void AssetBrowser::ClearAssetsRoot() noexcept
{
    directory_.Clear();
    selectedRelativePath_.reset();
    newFolderName_.fill('\0');
    error_.clear();
    openNewFolderPopup_ = false;
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
    DrawError(error_);
    DrawNewFolderPopup();
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
        newFolderName_.fill('\0');
        error_.clear();
        openNewFolderPopup_ = true;
    }
}

void AssetBrowser::DrawEntries()
{
    const std::vector<AssetEntry>& entries = directory_.Entries();

    if (entries.empty())
    {
        ImGui::TextDisabled("Folder is empty.");
        return;
    }

    std::optional<std::filesystem::path> directoryToEnter;

    for (const AssetEntry& entry : entries)
    {
        const bool selected = selectedRelativePath_ &&
            *selectedRelativePath_ == entry.RelativePath();
        const std::string label =
            std::string(entry.IsDirectory() ? "[DIR]  " : "[FILE] ") +
            entry.Name() + "##" + entry.RelativePath().generic_string();

        if (ImGui::Selectable(label.c_str(), selected))
        {
            selectedRelativePath_ = entry.RelativePath();
        }

        if (entry.IsDirectory() && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            directoryToEnter = entry.AbsolutePath();
        }
    }

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
            newFolderName_.fill('\0');
            error_.clear();
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
        newFolderName_.fill('\0');
        error_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
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
}

} // namespace VoxelForge::Editor
