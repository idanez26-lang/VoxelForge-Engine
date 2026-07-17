#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

struct PaletteColorSelection final
{
    std::size_t Index = 0U;
    Asset::Voxel::VoxelColor Color{};

    [[nodiscard]] bool operator==(
        const PaletteColorSelection&) const noexcept = default;
};

class PaletteService final
{
public:
    static constexpr std::size_t PaletteSize = 256U;
    static constexpr std::size_t FirstSelectableIndex = 1U;
    static constexpr std::size_t RecentColorLimit = 8U;

    using Palette = std::array<Asset::Voxel::VoxelColor, PaletteSize>;

    void SetPalette(Palette palette, std::string name);
    void Clear() noexcept;

    [[nodiscard]] bool SelectColor(std::size_t index) noexcept;
    [[nodiscard]] bool RecordActiveColorUsage();

    [[nodiscard]] bool HasActivePalette() const noexcept;
    [[nodiscard]] const Palette* ActivePalette() const noexcept;
    [[nodiscard]] const std::string& ActivePaletteName() const noexcept;
    [[nodiscard]] std::optional<std::size_t> ActiveIndex() const noexcept;
    [[nodiscard]] std::optional<PaletteColorSelection>
        ActiveColor() const noexcept;
    [[nodiscard]] const std::vector<PaletteColorSelection>&
        RecentColors() const noexcept;

    [[nodiscard]] static bool IsSelectableIndex(std::size_t index) noexcept;

private:
    std::optional<Palette> palette_;
    std::string paletteName_;
    std::size_t activeIndex_ = FirstSelectableIndex;
    std::vector<PaletteColorSelection> recentColors_;
};

} // namespace VoxelForge::Editor
