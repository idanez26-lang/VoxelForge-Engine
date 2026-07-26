#pragma once

#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class ForgeLibraryDisplayMode : std::uint8_t
{
    Grid,
    List
};

enum class ForgeLibrarySortMode : std::uint8_t
{
    Name,
    Date,
    Type
};

enum class ForgeLibraryFilter : std::uint8_t
{
    All,
    Favorites,
    Project
};

struct ForgeLibraryItem final
{
    StampCatalogEntry CatalogEntry;
    std::string DisplayName;

    [[nodiscard]] bool operator==(const ForgeLibraryItem&) const noexcept = default;
};

struct ForgeLibrarySelectionDetails final
{
    std::string Name;
    StampDimensions Dimensions{};
    std::uint64_t VoxelCount = 0U;
    std::uint32_t PaletteCount = 0U;
    std::string DateLabel{"Not indexed in V1"};
    bool PreviewAvailable = false;
};

struct ForgeLibraryOperationResult final
{
    bool Succeeded = false;
    bool SessionActivated = false;
    std::string Message;
};

/// UI-independent presentation model for the Project Forge Library. It owns no
/// placement rules: catalogue discovery is delegated to StampCatalogService,
/// source loading to IStampLibraryRepository and preview activation to the
/// existing StampPlacementSession.
class ForgeLibraryViewModel final
{
public:
    ForgeLibraryViewModel(
        StampCatalogService& catalogue,
        IStampLibraryRepository& repository,
        StampPlacementSession& placementSession);

    [[nodiscard]] ForgeLibraryOperationResult Refresh();
    void ResetForProjectChange() noexcept;

    void SetSearchText(std::string text);
    [[nodiscard]] const std::string& SearchText() const noexcept;
    void SetDisplayMode(ForgeLibraryDisplayMode mode) noexcept;
    [[nodiscard]] ForgeLibraryDisplayMode DisplayMode() const noexcept;
    void SetSortMode(ForgeLibrarySortMode mode);
    [[nodiscard]] ForgeLibrarySortMode SortMode() const noexcept;
    void SetFilter(ForgeLibraryFilter filter);
    [[nodiscard]] ForgeLibraryFilter Filter() const noexcept;

    [[nodiscard]] bool Select(const Core::UUID& id);
    void ClearSelection() noexcept;
    [[nodiscard]] const Core::UUID* SelectedId() const noexcept;
    [[nodiscard]] const ForgeLibrarySelectionDetails* SelectedDetails() const noexcept;
    [[nodiscard]] const VoxelStamp* SelectedPreviewStamp() const noexcept;

    [[nodiscard]] ForgeLibraryOperationResult ActivateSelected(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U);

    [[nodiscard]] const std::vector<ForgeLibraryItem>& Items() const noexcept;
    [[nodiscard]] const std::string& StatusMessage() const noexcept;
    [[nodiscard]] bool NeedsRefresh() const noexcept;

private:
    void SortItems();
    void ReconcileSelection();
    [[nodiscard]] const ForgeLibraryItem* FindItem(const Core::UUID& id) const noexcept;

    StampCatalogService& catalogue_;
    IStampLibraryRepository& repository_;
    StampPlacementSession& placementSession_;
    std::vector<ForgeLibraryItem> items_;
    std::optional<Core::UUID> selectedId_;
    std::optional<VoxelStamp> selectedStamp_;
    std::optional<ForgeLibrarySelectionDetails> selectedDetails_;
    std::string searchText_;
    std::string statusMessage_;
    ForgeLibraryDisplayMode displayMode_ = ForgeLibraryDisplayMode::Grid;
    ForgeLibrarySortMode sortMode_ = ForgeLibrarySortMode::Name;
    ForgeLibraryFilter filter_ = ForgeLibraryFilter::All;
    bool needsRefresh_ = true;
};

} // namespace VoxelForge::Editor::Stamps
