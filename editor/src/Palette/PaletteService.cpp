#include "Palette/PaletteService.h"

#include <algorithm>
#include <utility>

namespace VoxelForge::Editor
{

void PaletteService::SetPalette(Palette palette, std::string name)
{
    palette_ = std::move(palette);
    paletteName_ = std::move(name);
    activeIndex_ = FirstSelectableIndex;
    recentColors_.clear();
}

void PaletteService::Clear() noexcept
{
    palette_.reset();
    paletteName_.clear();
    activeIndex_ = FirstSelectableIndex;
    recentColors_.clear();
}

bool PaletteService::SelectColor(const std::size_t index) noexcept
{
    if (!palette_ || !IsSelectableIndex(index)) return false;
    activeIndex_ = index;
    return true;
}

bool PaletteService::RecordActiveColorUsage()
{
    const std::optional<PaletteColorSelection> selection = ActiveColor();
    if (!selection) return false;
    recentColors_.erase(std::remove_if(
        recentColors_.begin(), recentColors_.end(),
        [selection](const PaletteColorSelection& recent)
        {
            return recent.Index == selection->Index;
        }), recentColors_.end());
    recentColors_.insert(recentColors_.begin(), *selection);
    if (recentColors_.size() > RecentColorLimit)
        recentColors_.resize(RecentColorLimit);
    return true;
}

bool PaletteService::HasActivePalette() const noexcept
{
    return palette_.has_value();
}

const PaletteService::Palette* PaletteService::ActivePalette() const noexcept
{
    return palette_ ? &*palette_ : nullptr;
}

const std::string& PaletteService::ActivePaletteName() const noexcept
{
    return paletteName_;
}

std::optional<std::size_t> PaletteService::ActiveIndex() const noexcept
{
    return palette_ ? std::optional<std::size_t>(activeIndex_) : std::nullopt;
}

std::optional<PaletteColorSelection> PaletteService::ActiveColor() const noexcept
{
    if (!palette_ || !IsSelectableIndex(activeIndex_)) return std::nullopt;
    return PaletteColorSelection{activeIndex_, (*palette_)[activeIndex_]};
}

const std::vector<PaletteColorSelection>&
PaletteService::RecentColors() const noexcept
{
    return recentColors_;
}

bool PaletteService::IsSelectableIndex(const std::size_t index) noexcept
{
    return index >= FirstSelectableIndex && index < PaletteSize;
}

} // namespace VoxelForge::Editor
