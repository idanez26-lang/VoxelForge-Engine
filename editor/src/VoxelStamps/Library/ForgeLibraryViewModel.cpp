#include "VoxelStamps/Library/ForgeLibraryViewModel.h"

#include <algorithm>
#include <cctype>
#include <new>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] std::string LowerAscii(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

[[nodiscard]] std::string DisplayName(const StampCatalogEntry& entry)
{
    std::string result = entry.FileName;
    constexpr std::string_view extension = ".vfstamp";
    if (result.size() >= extension.size() &&
        LowerAscii(std::string_view(result).substr(result.size() - extension.size())) ==
            extension)
    {
        result.resize(result.size() - extension.size());
    }
    return result.empty() ? std::string("Untitled Stamp") : result;
}

[[nodiscard]] bool NameLess(
    const ForgeLibraryItem& left,
    const ForgeLibraryItem& right)
{
    const std::string leftName = LowerAscii(left.DisplayName);
    const std::string rightName = LowerAscii(right.DisplayName);
    if (leftName != rightName) return leftName < rightName;
    return left.CatalogEntry.Reference.Id.Value() <
        right.CatalogEntry.Reference.Id.Value();
}

} // namespace

ForgeLibraryViewModel::ForgeLibraryViewModel(
    StampCatalogService& catalogue,
    IStampLibraryRepository& repository,
    StampPlacementSession& placementSession)
    : catalogue_(catalogue),
      repository_(repository),
      placementSession_(placementSession)
{
}

ForgeLibraryOperationResult ForgeLibraryViewModel::Refresh()
{
    try
    {
        StampCatalogResult result = catalogue_.Query({.Text = searchText_});
        if (result.Error == StampCatalogError::Missing)
        {
            result = catalogue_.RebuildCatalogue();
            if (result.Succeeded() && !searchText_.empty())
                result = catalogue_.Query({.Text = searchText_});
        }
        if (!result.Succeeded())
        {
            items_.clear();
            ReconcileSelection();
            statusMessage_ = result.Message;
            needsRefresh_ = false;
            return {.Message = statusMessage_};
        }

        items_.clear();
        if (filter_ != ForgeLibraryFilter::Favorites)
        {
            items_.reserve(result.Catalog.Entries.size());
            for (StampCatalogEntry& entry : result.Catalog.Entries)
            {
                ForgeLibraryItem item;
                item.DisplayName = DisplayName(entry);
                item.CatalogEntry = std::move(entry);
                items_.push_back(std::move(item));
            }
        }
        SortItems();
        ReconcileSelection();
        needsRefresh_ = false;
        statusMessage_ = filter_ == ForgeLibraryFilter::Favorites
            ? "Favorites are prepared but empty in Forge Library V1."
            : (items_.empty() ? "No Project Library Stamps match this view."
                              : std::to_string(items_.size()) +
                                    (items_.size() == 1U ? " Stamp" : " Stamps"));
        return {.Succeeded = true, .Message = statusMessage_};
    }
    catch (const std::bad_alloc&)
    {
        items_.clear();
        ReconcileSelection();
        needsRefresh_ = false;
        statusMessage_ = "Forge Library ran out of memory while building the view.";
        return {.Message = statusMessage_};
    }
}

void ForgeLibraryViewModel::ResetForProjectChange() noexcept
{
    catalogue_.InvalidateCache();
    items_.clear();
    selectedId_.reset();
    selectedStamp_.reset();
    selectedDetails_.reset();
    searchText_.clear();
    statusMessage_.clear();
    filter_ = ForgeLibraryFilter::All;
    sortMode_ = ForgeLibrarySortMode::Name;
    // Grid/List is an artist preference and intentionally survives project changes.
    needsRefresh_ = true;
}

void ForgeLibraryViewModel::SetSearchText(std::string text)
{
    if (searchText_ == text) return;
    searchText_ = std::move(text);
    needsRefresh_ = true;
}

const std::string& ForgeLibraryViewModel::SearchText() const noexcept
{
    return searchText_;
}

void ForgeLibraryViewModel::SetDisplayMode(
    const ForgeLibraryDisplayMode mode) noexcept
{
    displayMode_ = mode;
}

ForgeLibraryDisplayMode ForgeLibraryViewModel::DisplayMode() const noexcept
{
    return displayMode_;
}

void ForgeLibraryViewModel::SetSortMode(const ForgeLibrarySortMode mode)
{
    if (sortMode_ == mode) return;
    sortMode_ = mode;
    SortItems();
}

ForgeLibrarySortMode ForgeLibraryViewModel::SortMode() const noexcept
{
    return sortMode_;
}

void ForgeLibraryViewModel::SetFilter(const ForgeLibraryFilter filter)
{
    if (filter_ == filter) return;
    filter_ = filter;
    needsRefresh_ = true;
}

ForgeLibraryFilter ForgeLibraryViewModel::Filter() const noexcept
{
    return filter_;
}

bool ForgeLibraryViewModel::Select(const Core::UUID& id)
{
    const ForgeLibraryItem* const item = FindItem(id);
    if (item == nullptr)
    {
        ClearSelection();
        statusMessage_ = "The selected Stamp is no longer in this Forge Library view.";
        return false;
    }

    StampLibraryResult loaded = repository_.Read(item->CatalogEntry.Reference);
    if (!loaded.Succeeded() || !loaded.Stamp)
    {
        selectedId_ = id;
        selectedStamp_.reset();
        selectedDetails_ = ForgeLibrarySelectionDetails{
            .Name = item->DisplayName,
            .Dimensions = item->CatalogEntry.Dimensions,
            .VoxelCount = item->CatalogEntry.VoxelCount,
            .PaletteCount = item->CatalogEntry.PaletteCount};
        statusMessage_ = loaded.Message;
        return false;
    }

    selectedId_ = id;
    selectedStamp_ = std::move(*loaded.Stamp);
    selectedDetails_ = ForgeLibrarySelectionDetails{
        .Name = item->DisplayName,
        .Dimensions = item->CatalogEntry.Dimensions,
        .VoxelCount = item->CatalogEntry.VoxelCount,
        .PaletteCount = item->CatalogEntry.PaletteCount,
        .DateLabel = "Not indexed in V1",
        .PreviewAvailable = true};
    statusMessage_ = "Stamp selected. Double-click it or choose Use.";
    return true;
}

void ForgeLibraryViewModel::ClearSelection() noexcept
{
    selectedId_.reset();
    selectedStamp_.reset();
    selectedDetails_.reset();
}

const Core::UUID* ForgeLibraryViewModel::SelectedId() const noexcept
{
    return selectedId_ ? &*selectedId_ : nullptr;
}

const ForgeLibrarySelectionDetails*
ForgeLibraryViewModel::SelectedDetails() const noexcept
{
    return selectedDetails_ ? &*selectedDetails_ : nullptr;
}

const VoxelStamp* ForgeLibraryViewModel::SelectedPreviewStamp() const noexcept
{
    return selectedStamp_ ? &*selectedStamp_ : nullptr;
}

ForgeLibraryOperationResult ForgeLibraryViewModel::ActivateSelected(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel)
{
    if (!selectedId_)
        return {.Message = "Select a Stamp before choosing Use."};

    const ForgeLibraryItem* const item = FindItem(*selectedId_);
    if (item == nullptr)
        return {.Message = "The selected Stamp is no longer available."};

    StampLibraryResult loaded = repository_.Read(item->CatalogEntry.Reference);
    if (!loaded.Succeeded() || !loaded.Stamp)
        return {.Message = loaded.Message};

    const StampFixedPoint initialTarget = loaded.Stamp->Pivot().LocalPosition;
    const StampPlacementSessionResult activated = placementSession_.Begin(
        std::move(*loaded.Stamp), document, documentGeneration,
        targetSubModel, initialTarget);
    if (!activated.Succeeded)
    {
        return {
            .Message = std::string(StampPlacementDiagnosticMessage(
                activated.Diagnostic))};
    }

    statusMessage_ = "Stamp placement active. Move in the viewport, click to place, Esc to finish.";
    return {
        .Succeeded = true,
        .SessionActivated = true,
        .Message = statusMessage_};
}

const std::vector<ForgeLibraryItem>&
ForgeLibraryViewModel::Items() const noexcept
{
    return items_;
}

const std::string& ForgeLibraryViewModel::StatusMessage() const noexcept
{
    return statusMessage_;
}

bool ForgeLibraryViewModel::NeedsRefresh() const noexcept
{
    return needsRefresh_;
}

void ForgeLibraryViewModel::SortItems()
{
    // Date/type metadata is deliberately not synthesized in V1. Those modes
    // are stable, prepared ordering contracts and fall back to name until the
    // authoritative catalogue gains the corresponding derived fields.
    std::stable_sort(items_.begin(), items_.end(), NameLess);
}

void ForgeLibraryViewModel::ReconcileSelection()
{
    if (!selectedId_) return;
    if (FindItem(*selectedId_) == nullptr) ClearSelection();
}

const ForgeLibraryItem* ForgeLibraryViewModel::FindItem(
    const Core::UUID& id) const noexcept
{
    const auto item = std::find_if(items_.begin(), items_.end(),
        [&id](const ForgeLibraryItem& candidate) {
            return candidate.CatalogEntry.Reference.Id == id;
        });
    return item == items_.end() ? nullptr : &*item;
}

} // namespace VoxelForge::Editor::Stamps
