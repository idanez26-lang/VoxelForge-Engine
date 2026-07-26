#pragma once

#include "VoxelStamps/Library/ForgeLibraryViewModel.h"

#include <array>
#include <cstdint>
#include <string>

namespace VoxelForge::Editor::Stamps
{

struct ForgeLibraryPanelResult final
{
    bool SessionActivated = false;
    bool SaveSelectionRequested = false;
    std::string Message;
};

/// ImGui adapter for ForgeLibraryViewModel. It renders catalogue facts and
/// forwards Use/double-click; it contains no placement calculations.
class ForgeLibraryPanel final
{
public:
    explicit ForgeLibraryPanel(ForgeLibraryViewModel& viewModel);

    [[nodiscard]] ForgeLibraryPanelResult Draw(
        bool* open,
        const Asset::Voxel::VoxelDocument* document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U);

private:
    void DrawToolbar();
    [[nodiscard]] bool DrawContent(
        const ForgeLibraryResponsiveLayout& layout,
        ForgeLibraryPanelResult& result);
    [[nodiscard]] bool DrawEmptyState(ForgeLibraryPanelResult& result);
    [[nodiscard]] bool DrawGrid(std::size_t columns);
    [[nodiscard]] bool DrawList(bool compact);
    void DrawDetails();
    void DrawThumbnail(
        const ForgeLibraryThumbnail& thumbnail,
        float height,
        bool framed) const;

    ForgeLibraryViewModel& viewModel_;
    std::array<char, 192U> searchBuffer_{};
};

} // namespace VoxelForge::Editor::Stamps
