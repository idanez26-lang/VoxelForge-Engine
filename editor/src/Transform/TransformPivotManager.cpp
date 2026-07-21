#include "TransformPivotManager.h"

#include <cassert>
#include <cmath>

namespace VoxelForge::Editor
{

bool TransformPivotManager::SetMode(const TransformPivotMode mode) noexcept
{
    if (mode_ == mode) return false;
    mode_ = mode;
    if (valid_ && inputsCached_)
        pivot_ = Calculate(mode_, cachedBounds_, cachedModelCenter_);
    return true;
}

TransformPivotMode TransformPivotManager::GetMode() const noexcept
{
    return mode_;
}

bool TransformPivotManager::UpdateFromBounds(
    const SelectionBounds& bounds,
    const Vec3 modelCenter) noexcept
{
    if (!BoundsAreValid(bounds) || !std::isfinite(modelCenter.X) ||
        !std::isfinite(modelCenter.Y) || !std::isfinite(modelCenter.Z))
    {
        const bool changed = valid_ || inputsCached_;
        Invalidate();
        return changed;
    }
    if (valid_ && inputsCached_ && cachedBounds_ == bounds &&
        cachedModelCenter_ == modelCenter)
        return false;

    cachedBounds_ = bounds;
    cachedModelCenter_ = modelCenter;
    inputsCached_ = true;
    pivot_ = Calculate(mode_, bounds, modelCenter);
    valid_ = true;
    return true;
}

void TransformPivotManager::Invalidate() noexcept
{
    valid_ = false;
    inputsCached_ = false;
    cachedBounds_ = {};
    cachedModelCenter_ = {};
    pivot_ = {mode_, {}};
}

bool TransformPivotManager::HasValidPivot() const noexcept
{
    return valid_;
}

const TransformPivot& TransformPivotManager::GetPivot() const noexcept
{
    assert(valid_ && "Transform pivot requested while invalid.");
    return pivot_;
}

bool TransformPivotManager::BoundsAreValid(
    const SelectionBounds& bounds) noexcept
{
    return bounds.Valid && bounds.Minimum.X <= bounds.Maximum.X &&
        bounds.Minimum.Y <= bounds.Maximum.Y &&
        bounds.Minimum.Z <= bounds.Maximum.Z;
}

TransformPivot TransformPivotManager::Calculate(
    const TransformPivotMode mode,
    const SelectionBounds& bounds,
    const Vec3 modelCenter) noexcept
{
    // Selection bounds use inclusive voxel-cell coordinates. Their spatial
    // box is [minimum, maximum + 1], matching selection handles and gizmos.
    const double minimumX = static_cast<double>(bounds.Minimum.X);
    const double minimumY = static_cast<double>(bounds.Minimum.Y);
    const double minimumZ = static_cast<double>(bounds.Minimum.Z);
    const double maximumX = static_cast<double>(bounds.Maximum.X) + 1.0;
    const double maximumY = static_cast<double>(bounds.Maximum.Y) + 1.0;
    const double maximumZ = static_cast<double>(bounds.Maximum.Z) + 1.0;
    const double centerX = (minimumX + maximumX) * 0.5;
    const double centerY = (minimumY + maximumY) * 0.5;
    const double centerZ = (minimumZ + maximumZ) * 0.5;

    double pivotY = centerY;
    if (mode == TransformPivotMode::Bottom) pivotY = minimumY;
    else if (mode == TransformPivotMode::Top) pivotY = maximumY;

    return {
        mode,
        {static_cast<float>(centerX) - modelCenter.X,
         static_cast<float>(pivotY) - modelCenter.Y,
         static_cast<float>(centerZ) - modelCenter.Z}};
}

} // namespace VoxelForge::Editor
