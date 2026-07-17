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

Asset::Voxel::VoxelDimensions SelectionBounds::Dimensions() const noexcept
{
    if (!Valid) return {};
    return {
        static_cast<std::uint32_t>(Maximum.X - Minimum.X + 1),
        static_cast<std::uint32_t>(Maximum.Y - Minimum.Y + 1),
        static_cast<std::uint32_t>(Maximum.Z - Minimum.Z + 1)};
}

bool SelectionService::Apply(
    const std::span<const Asset::Voxel::VoxelPosition> positions,
    const SelectionMode mode)
{
    std::vector<Asset::Voxel::VoxelPosition> normalized(
        positions.begin(), positions.end());
    Normalize(normalized);

    std::vector<Asset::Voxel::VoxelPosition> result;
    switch (mode)
    {
    case SelectionMode::Replace:
        result = std::move(normalized);
        break;
    case SelectionMode::Add:
        result.reserve(positions_.size() + normalized.size());
        std::set_union(
            positions_.begin(), positions_.end(),
            normalized.begin(), normalized.end(),
            std::back_inserter(result), PositionLess);
        break;
    case SelectionMode::Subtract:
        result.reserve(positions_.size());
        std::set_difference(
            positions_.begin(), positions_.end(),
            normalized.begin(), normalized.end(),
            std::back_inserter(result), PositionLess);
        break;
    case SelectionMode::Intersect:
        result.reserve(std::min(positions_.size(), normalized.size()));
        std::set_intersection(
            positions_.begin(), positions_.end(),
            normalized.begin(), normalized.end(),
            std::back_inserter(result), PositionLess);
        break;
    }
    if (result == positions_) return false;
    positions_ = std::move(result);
    RecalculateBounds();
    return true;
}

bool SelectionService::Select(
    const Asset::Voxel::VoxelPosition position,
    const SelectionMode mode)
{
    return Apply(std::span(&position, 1U), mode);
}

bool SelectionService::Clear() noexcept
{
    if (positions_.empty()) return false;
    positions_.clear();
    bounds_ = {};
    return true;
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

std::optional<SelectionCenter> SelectionService::Center() const noexcept
{
    if (!bounds_.Valid) return std::nullopt;
    return SelectionCenter{
        (static_cast<float>(bounds_.Minimum.X) +
            static_cast<float>(bounds_.Maximum.X)) * 0.5F,
        (static_cast<float>(bounds_.Minimum.Y) +
            static_cast<float>(bounds_.Maximum.Y)) * 0.5F,
        (static_cast<float>(bounds_.Minimum.Z) +
            static_cast<float>(bounds_.Maximum.Z)) * 0.5F};
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
