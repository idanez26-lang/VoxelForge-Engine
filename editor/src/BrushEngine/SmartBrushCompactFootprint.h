#pragma once

#include "BrushEngine/SmartBrushEngine.h"

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{
// The compact Pencil path deliberately has its own 256-voxel ceiling.  The
// legacy SmartBrushEngine::Resolve API remains capped at 64 because it
// materializes every cell for existing tools.
inline constexpr int MaximumCompactSmartBrushSize = 256;

// Immutable identity of a reusable procedural footprint.  Profile identity
// and revision are opaque values captured by the interaction boundary: the
// brush domain never retains a mutable Brush Profile object.
struct SmartBrushCompactDescriptor final
{
    SmartBrushShape Shape = SmartBrushShape::Cube;
    SmartBrushDimension Dimension = SmartBrushDimension::Volume3D;
    SmartBrushOrientation Orientation = SmartBrushOrientation::Auto;
    int Size = 1;
    std::uint64_t ProfileIdentity = 0U;
    std::uint64_t ProfileRevision = 0U;

    [[nodiscard]] bool operator==(const SmartBrushCompactDescriptor&) const noexcept = default;
};

struct SmartBrushCompactCacheKey final
{
    SmartBrushCompactDescriptor Descriptor{};

    [[nodiscard]] bool operator==(const SmartBrushCompactCacheKey&) const noexcept = default;
};

struct SmartBrushCompactCacheKeyHash final
{
    [[nodiscard]] std::size_t operator()(
        const SmartBrushCompactCacheKey& key) const noexcept;
};

struct SmartBrushCompactLocalBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
};

// A compact, exact brush footprint.  It owns only scalar descriptor metadata;
// it never owns a vector of voxel positions.  The deterministic iterator is
// reserved for a later commit boundary where touching each real cell is
// necessary and acceptable.
class SmartBrushCompactFootprint final
{
public:
    class Iterator final
    {
    public:
        Iterator() noexcept = default;
        [[nodiscard]] bool Next(Asset::Voxel::VoxelPosition& offset) noexcept;

    private:
        friend class SmartBrushCompactFootprint;
        explicit Iterator(const SmartBrushCompactFootprint& footprint) noexcept;

        const SmartBrushCompactFootprint* footprint_ = nullptr;
        int first_ = 0;
        int second_ = 0;
        int third_ = 0;
        bool complete_ = false;
    };

    [[nodiscard]] static bool IsValid(
        const SmartBrushCompactDescriptor& descriptor) noexcept;
    [[nodiscard]] static SmartBrushCompactFootprint Create(
        SmartBrushCompactDescriptor descriptor);

    [[nodiscard]] const SmartBrushCompactDescriptor& Descriptor() const noexcept;
    [[nodiscard]] std::size_t ExactVoxelCount() const noexcept;
    [[nodiscard]] const SmartBrushCompactLocalBounds& LocalBounds() const noexcept;
    [[nodiscard]] Iterator Iterate() const noexcept;
    [[nodiscard]] std::size_t MaterializedPositionCount() const noexcept;

private:
    explicit SmartBrushCompactFootprint(SmartBrushCompactDescriptor descriptor) noexcept;

    [[nodiscard]] bool Contains(int first, int second, int third) const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition ToOffset(
        int first, int second, int third) const noexcept;

    SmartBrushCompactDescriptor descriptor_{};
    SmartBrushCompactLocalBounds localBounds_{};
    std::size_t exactVoxelCount_ = 0U;
};
} // namespace VoxelForge::Editor
