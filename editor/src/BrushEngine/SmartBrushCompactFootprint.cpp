#include "BrushEngine/SmartBrushCompactFootprint.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace VoxelForge::Editor
{
namespace
{
[[nodiscard]] bool IsSupported(const SmartBrushShape shape) noexcept
{
    return shape == SmartBrushShape::Cube || shape == SmartBrushShape::Sphere ||
        shape == SmartBrushShape::Cylinder;
}

[[nodiscard]] bool IsKnownDimension(const SmartBrushDimension dimension) noexcept
{
    return dimension == SmartBrushDimension::Volume3D ||
        dimension == SmartBrushDimension::Surface2D;
}

[[nodiscard]] bool IsKnownOrientation(const SmartBrushOrientation orientation) noexcept
{
    return orientation == SmartBrushOrientation::Auto ||
        orientation == SmartBrushOrientation::X ||
        orientation == SmartBrushOrientation::Y ||
        orientation == SmartBrushOrientation::Z;
}

[[nodiscard]] int MinimumOffset(const int size) noexcept
{
    return -((size - 1) / 2);
}

[[nodiscard]] int MaximumOffset(const int size) noexcept
{
    return size / 2;
}

[[nodiscard]] int EvenCenterOffset(const int size) noexcept
{
    return size % 2 == 0 ? 1 : 0;
}

[[nodiscard]] std::uint64_t Square(const int value) noexcept
{
    const std::int64_t wide = value;
    return static_cast<std::uint64_t>(wide * wide);
}

[[nodiscard]] std::uint64_t IntegerSquareRoot(std::uint64_t value) noexcept
{
    std::uint64_t root = 0U;
    std::uint64_t bit = std::uint64_t{1} << 62U;
    while (bit > value) bit >>= 2U;
    while (bit != 0U)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1U) + bit;
        }
        else root >>= 1U;
        bit >>= 2U;
    }
    return root;
}

// `2 * offset - parity` has one fixed parity.  This counts every allowed
// coordinate on that axis without scanning it.  It is exact for the legacy
// raster rule used by SmartBrushEngine.
[[nodiscard]] std::size_t AxisCountForSquaredRadius(
    const std::uint64_t squaredRadius, const int size) noexcept
{
    int maximum = static_cast<int>(IntegerSquareRoot(squaredRadius));
    const int parity = EvenCenterOffset(size);
    if ((maximum & 1) != parity) --maximum;
    return maximum < 0 ? 0U : static_cast<std::size_t>(maximum + 1);
}

[[nodiscard]] std::size_t ExactDiscCount(const int size) noexcept
{
    const std::uint64_t radiusSquared = Square(size);
    const int centerOffset = EvenCenterOffset(size);
    std::size_t count = 0U;
    for (int first = MinimumOffset(size); first <= MaximumOffset(size); ++first)
    {
        const std::uint64_t firstSquared = Square(2 * first - centerOffset);
        if (firstSquared <= radiusSquared)
            count += AxisCountForSquaredRadius(radiusSquared - firstSquared, size);
    }
    return count;
}

[[nodiscard]] std::size_t ExactSphereCount(const int size) noexcept
{
    const std::uint64_t radiusSquared = Square(size);
    const int centerOffset = EvenCenterOffset(size);
    std::size_t count = 0U;
    for (int second = MinimumOffset(size); second <= MaximumOffset(size); ++second)
    {
        const std::uint64_t secondSquared = Square(2 * second - centerOffset);
        for (int first = MinimumOffset(size); first <= MaximumOffset(size); ++first)
        {
            const std::uint64_t firstSquared = Square(2 * first - centerOffset);
            if (firstSquared + secondSquared <= radiusSquared)
                count += AxisCountForSquaredRadius(
                    radiusSquared - firstSquared - secondSquared, size);
        }
    }
    return count;
}
} // namespace

std::size_t SmartBrushCompactCacheKeyHash::operator()(
    const SmartBrushCompactCacheKey& key) const noexcept
{
    const SmartBrushCompactDescriptor& value = key.Descriptor;
    std::size_t hash = static_cast<std::size_t>(value.Shape);
    hash = hash * 31U + static_cast<std::size_t>(value.Dimension);
    hash = hash * 31U + static_cast<std::size_t>(value.Orientation);
    hash = hash * 31U + static_cast<std::size_t>(value.Size);
    hash = hash * 31U + static_cast<std::size_t>(value.ProfileIdentity);
    return hash * 31U + static_cast<std::size_t>(value.ProfileRevision);
}

bool SmartBrushCompactFootprint::IsValid(
    const SmartBrushCompactDescriptor& descriptor) noexcept
{
    return IsSupported(descriptor.Shape) && IsKnownDimension(descriptor.Dimension) &&
        IsKnownOrientation(descriptor.Orientation) && descriptor.Size >= 1 &&
        descriptor.Size <= MaximumCompactSmartBrushSize;
}

SmartBrushCompactFootprint SmartBrushCompactFootprint::Create(
    SmartBrushCompactDescriptor descriptor)
{
    if (!IsValid(descriptor))
        throw std::invalid_argument("The compact Smart Brush descriptor is invalid.");
    return SmartBrushCompactFootprint(std::move(descriptor));
}

SmartBrushCompactFootprint::SmartBrushCompactFootprint(
    SmartBrushCompactDescriptor descriptor) noexcept
    : descriptor_(std::move(descriptor))
{
    const int minimum = MinimumOffset(descriptor_.Size);
    const int maximum = MaximumOffset(descriptor_.Size);
    if (descriptor_.Dimension == SmartBrushDimension::Volume3D)
    {
        localBounds_ = {{minimum, minimum, minimum}, {maximum, maximum, maximum}};
        if (descriptor_.Shape == SmartBrushShape::Cube)
        {
            const std::size_t side = static_cast<std::size_t>(descriptor_.Size);
            exactVoxelCount_ = side * side * side;
        }
        else if (descriptor_.Shape == SmartBrushShape::Cylinder)
        {
            exactVoxelCount_ = ExactDiscCount(descriptor_.Size) *
                static_cast<std::size_t>(descriptor_.Size);
        }
        else exactVoxelCount_ = ExactSphereCount(descriptor_.Size);
        return;
    }

    switch (descriptor_.Orientation)
    {
    case SmartBrushOrientation::X:
        localBounds_ = {{0, minimum, minimum}, {0, maximum, maximum}};
        break;
    case SmartBrushOrientation::Z:
        localBounds_ = {{minimum, minimum, 0}, {maximum, maximum, 0}};
        break;
    case SmartBrushOrientation::Auto:
    case SmartBrushOrientation::Y:
        localBounds_ = {{minimum, 0, minimum}, {maximum, 0, maximum}};
        break;
    }
    exactVoxelCount_ = descriptor_.Shape == SmartBrushShape::Cube
        ? static_cast<std::size_t>(descriptor_.Size) *
            static_cast<std::size_t>(descriptor_.Size)
        : ExactDiscCount(descriptor_.Size);
}

const SmartBrushCompactDescriptor& SmartBrushCompactFootprint::Descriptor() const noexcept
{ return descriptor_; }

std::size_t SmartBrushCompactFootprint::ExactVoxelCount() const noexcept
{ return exactVoxelCount_; }

const SmartBrushCompactLocalBounds& SmartBrushCompactFootprint::LocalBounds() const noexcept
{ return localBounds_; }

SmartBrushCompactFootprint::Iterator SmartBrushCompactFootprint::Iterate() const noexcept
{ return Iterator(*this); }

std::size_t SmartBrushCompactFootprint::MaterializedPositionCount() const noexcept
{ return 0U; }

bool SmartBrushCompactFootprint::Contains(
    const int first, const int second, const int third) const noexcept
{
    if (descriptor_.Shape == SmartBrushShape::Cube) return true;
    const int centerOffset = EvenCenterOffset(descriptor_.Size);
    const std::uint64_t radiusSquared = Square(descriptor_.Size);
    const std::uint64_t firstSquared = Square(2 * first - centerOffset);
    const std::uint64_t secondSquared = Square(2 * second - centerOffset);
    if (descriptor_.Shape == SmartBrushShape::Cylinder)
    {
        const std::uint64_t radialSecond = descriptor_.Dimension ==
                SmartBrushDimension::Volume3D
            ? Square(2 * third - centerOffset)
            : secondSquared;
        return firstSquared + radialSecond <= radiusSquared;
    }
    if (descriptor_.Dimension == SmartBrushDimension::Surface2D)
        return firstSquared + secondSquared <= radiusSquared;
    return firstSquared + secondSquared +
        Square(2 * third - centerOffset) <= radiusSquared;
}

Asset::Voxel::VoxelPosition SmartBrushCompactFootprint::ToOffset(
    const int first, const int second, const int third) const noexcept
{
    if (descriptor_.Dimension == SmartBrushDimension::Volume3D)
        return {first, second, third};
    switch (descriptor_.Orientation)
    {
    case SmartBrushOrientation::X: return {0, first, second};
    case SmartBrushOrientation::Z: return {first, second, 0};
    case SmartBrushOrientation::Auto:
    case SmartBrushOrientation::Y: return {first, 0, second};
    }
    return {};
}

SmartBrushCompactFootprint::Iterator::Iterator(
    const SmartBrushCompactFootprint& footprint) noexcept
    : footprint_(&footprint), first_(MinimumOffset(footprint.descriptor_.Size)),
      second_(MinimumOffset(footprint.descriptor_.Size)),
      third_(MinimumOffset(footprint.descriptor_.Size))
{
}

bool SmartBrushCompactFootprint::Iterator::Next(
    Asset::Voxel::VoxelPosition& offset) noexcept
{
    if (complete_ || !footprint_) return false;
    const int minimum = MinimumOffset(footprint_->descriptor_.Size);
    const int maximum = MaximumOffset(footprint_->descriptor_.Size);
    const bool volume = footprint_->descriptor_.Dimension ==
        SmartBrushDimension::Volume3D;
    for (;;)
    {
        const int first = first_;
        const int second = second_;
        const int third = third_;
        ++first_;
        if (first_ > maximum)
        {
            first_ = minimum;
            ++second_;
            if (second_ > maximum)
            {
                second_ = minimum;
                if (volume) ++third_;
                else complete_ = true;
                if (third_ > maximum) complete_ = true;
            }
        }
        if (!footprint_->Contains(first, second, third))
        {
            if (complete_) return false;
            continue;
        }
        offset = footprint_->ToOffset(first, second, third);
        return true;
    }
}
} // namespace VoxelForge::Editor
