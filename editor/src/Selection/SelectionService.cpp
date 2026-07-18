#include "SelectionService.h"

#include <algorithm>
#include <iterator>

namespace VoxelForge::Editor
{
namespace
{
bool PositionLess(
    const Asset::Voxel::VoxelPosition& left,
    const Asset::Voxel::VoxelPosition& right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}
}

SelectionBounds SelectionBounds::FromCorners(
    const Asset::Voxel::VoxelPosition first,
    const Asset::Voxel::VoxelPosition second) noexcept
{
    return {
        {std::min(first.X, second.X), std::min(first.Y, second.Y),
         std::min(first.Z, second.Z)},
        {std::max(first.X, second.X), std::max(first.Y, second.Y),
         std::max(first.Z, second.Z)},
        true};
}

Asset::Voxel::VoxelDimensions SelectionBounds::Dimensions() const noexcept
{
    if (!Valid) return {};
    return {
        static_cast<std::uint32_t>(Maximum.X - Minimum.X + 1),
        static_cast<std::uint32_t>(Maximum.Y - Minimum.Y + 1),
        static_cast<std::uint32_t>(Maximum.Z - Minimum.Z + 1)};
}


SelectionCenter SelectionBounds::Center() const noexcept
{
    if (!Valid) return {};
    return {
        (static_cast<float>(Minimum.X) + static_cast<float>(Maximum.X)) * 0.5F,
        (static_cast<float>(Minimum.Y) + static_cast<float>(Maximum.Y)) * 0.5F,
        (static_cast<float>(Minimum.Z) + static_cast<float>(Maximum.Z)) * 0.5F};
}

bool SelectionBounds::Contains(
    const Asset::Voxel::VoxelPosition position) const noexcept
{
    return Valid && position.X >= Minimum.X && position.X <= Maximum.X &&
        position.Y >= Minimum.Y && position.Y <= Maximum.Y &&
        position.Z >= Minimum.Z && position.Z <= Maximum.Z;
}

SelectionBounds SelectionBounds::ClampedTo(
    const Asset::Voxel::VoxelDimensions dimensions) const noexcept
{
    if (!Valid || dimensions.X == 0U || dimensions.Y == 0U ||
        dimensions.Z == 0U) return {};
    const auto clampAxis = [](const std::int32_t value, const std::uint32_t size)
    {
        return std::clamp(
            value, 0, static_cast<std::int32_t>(size - 1U));
    };
    return FromCorners(
        {clampAxis(Minimum.X, dimensions.X),
         clampAxis(Minimum.Y, dimensions.Y),
         clampAxis(Minimum.Z, dimensions.Z)},
        {clampAxis(Maximum.X, dimensions.X),
         clampAxis(Maximum.Y, dimensions.Y),
         clampAxis(Maximum.Z, dimensions.Z)});
}

bool SelectionService::Apply(
    const std::span<const Asset::Voxel::VoxelPosition> positions,
    const SelectionMode mode)
{
    normalizationScratch_.assign(positions.begin(), positions.end());
    Normalize(normalizationScratch_);
    return ApplySorted(normalizationScratch_, mode);
}

bool SelectionService::ApplySorted(
    const std::span<const Asset::Voxel::VoxelPosition> positions,
    const SelectionMode mode)
{
    if (mode == SelectionMode::Replace)
    {
        if (std::equal(
                positions_.begin(), positions_.end(),
                positions.begin(), positions.end()))
            return false;
        positions_.assign(positions.begin(), positions.end());
        RecalculateBounds();
        return true;
    }

    mergeScratch_.clear();
    switch (mode)
    {
    case SelectionMode::Add:
        mergeScratch_.reserve(positions_.size() + positions.size());
        std::set_union(
            positions_.begin(), positions_.end(),
            positions.begin(), positions.end(),
            std::back_inserter(mergeScratch_), PositionLess);
        break;
    case SelectionMode::Subtract:
        mergeScratch_.reserve(positions_.size());
        std::set_difference(
            positions_.begin(), positions_.end(),
            positions.begin(), positions.end(),
            std::back_inserter(mergeScratch_), PositionLess);
        break;
    case SelectionMode::Intersect:
        mergeScratch_.reserve(std::min(positions_.size(), positions.size()));
        std::set_intersection(
            positions_.begin(), positions_.end(),
            positions.begin(), positions.end(),
            std::back_inserter(mergeScratch_), PositionLess);
        break;
    case SelectionMode::Replace: break;
    }
    if (mergeScratch_ == positions_) return false;
    positions_.swap(mergeScratch_);
    RecalculateBounds();
    return true;
}

bool SelectionService::Select(
    const Asset::Voxel::VoxelPosition position,
    const SelectionMode mode)
{
    const bool changed = Apply(std::span(&position, 1U), mode);
    editableBounds_ = SelectionBounds::FromCorners(position, position);
    return changed;
}

bool SelectionService::SelectVolume(
    const std::span<const Asset::Voxel::VoxelPosition> existingVoxels,
    const SelectionBounds bounds,
    const SelectionMode mode)
{
    if (!bounds.Valid) return false;
    volumeScratch_.clear();
    if (volumeScratch_.capacity() < existingVoxels.size())
        volumeScratch_.reserve(existingVoxels.size());
    for (const auto position : existingVoxels)
        if (bounds.Contains(position)) volumeScratch_.push_back(position);
    Normalize(volumeScratch_);
    const bool changed = ApplySorted(volumeScratch_, mode);
    editableBounds_ = bounds;
    return changed;
}

bool SelectionService::ApplySortedVolume(
    const std::span<const Asset::Voxel::VoxelPosition> containedVoxels,
    const SelectionBounds bounds,
    const SelectionMode mode)
{
    if (!bounds.Valid) return false;
    const bool changed = ApplySorted(containedVoxels, mode);
    editableBounds_ = bounds;
    return changed;
}

bool SelectionService::Clear() noexcept
{
    const bool changed = !positions_.empty() || editableBounds_.Valid;
    positions_.clear();
    bounds_ = {};
    editableBounds_ = {};
    return changed;
}

void SelectionService::SetDocumentGeneration(
    const std::uint64_t generation) noexcept
{
    if (documentGeneration_ == generation) return;
    documentGeneration_ = generation;
    static_cast<void>(Clear());
}

void SelectionService::ClearDocument() noexcept
{
    documentGeneration_ = 0U;
    static_cast<void>(Clear());
}

bool SelectionService::Contains(
    const Asset::Voxel::VoxelPosition position) const noexcept
{
    return std::binary_search(
        positions_.begin(), positions_.end(), position, PositionLess);
}

bool SelectionService::Empty() const noexcept { return positions_.empty(); }
std::size_t SelectionService::Count() const noexcept { return positions_.size(); }

std::span<const Asset::Voxel::VoxelPosition>
SelectionService::Voxels() const noexcept
{
    return positions_;
}

const SelectionBounds& SelectionService::Bounds() const noexcept
{
    return bounds_;
}

const SelectionBounds& SelectionService::EditableBounds() const noexcept
{
    return editableBounds_;
}

std::optional<SelectionCenter> SelectionService::Center() const noexcept
{
    if (!editableBounds_.Valid && !bounds_.Valid) return std::nullopt;
    return (editableBounds_.Valid ? editableBounds_ : bounds_).Center();
}

std::uint64_t SelectionService::DocumentGeneration() const noexcept
{
    return documentGeneration_;
}

void SelectionService::Normalize(
    std::vector<Asset::Voxel::VoxelPosition>& positions)
{
    std::sort(positions.begin(), positions.end(), PositionLess);
    positions.erase(
        std::unique(positions.begin(), positions.end()), positions.end());
}

void SelectionService::RecalculateBounds() noexcept
{
    if (positions_.empty())
    {
        bounds_ = {};
        return;
    }
    bounds_.Minimum = positions_.front();
    bounds_.Maximum = positions_.front();
    for (const auto& position : positions_)
    {
        bounds_.Minimum.X = std::min(bounds_.Minimum.X, position.X);
        bounds_.Minimum.Y = std::min(bounds_.Minimum.Y, position.Y);
        bounds_.Minimum.Z = std::min(bounds_.Minimum.Z, position.Z);
        bounds_.Maximum.X = std::max(bounds_.Maximum.X, position.X);
        bounds_.Maximum.Y = std::max(bounds_.Maximum.Y, position.Y);
        bounds_.Maximum.Z = std::max(bounds_.Maximum.Z, position.Z);
    }
    bounds_.Valid = true;
}

} // namespace VoxelForge::Editor
