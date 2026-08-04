#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace VoxelForge::Editor
{

enum class VoxelPreviewSemantic : std::uint8_t
{
    Valid,
    Overlap,
    Invalid,
    Source,
    Added,
    Erased,
    Painted,
    Ignored,
    Clipped
};

struct VoxelPreviewInstance final
{
    Asset::Voxel::VoxelPosition Position{};
    VoxelPreviewSemantic Semantic = VoxelPreviewSemantic::Invalid;
    std::array<float, 4U> Color{};
    float Alpha = 0.5F;

    [[nodiscard]] bool operator==(const VoxelPreviewInstance&) const noexcept =
        default;
};

struct VoxelPreviewStats final
{
    std::size_t Total = 0U;
    std::size_t Valid = 0U;
    std::size_t Overlap = 0U;
    std::size_t Invalid = 0U;
    std::size_t Source = 0U;
    std::size_t Added = 0U;
    std::size_t Erased = 0U;
    std::size_t Painted = 0U;
    std::size_t Ignored = 0U;
    std::size_t Clipped = 0U;

    [[nodiscard]] std::size_t Count(
        VoxelPreviewSemantic semantic) const noexcept;
    [[nodiscard]] bool IsConsistent() const noexcept;
};

enum class VoxelPreviewRenderMode : std::uint8_t
{
    DetailedInstances,
    AggregateBounds
};

// Immutable renderer-neutral hand-off. Instance storage is shared read-only so
// a producer can advance the revision without copying a large preview.
class VoxelPlacementPreview final
{
  public:
    static constexpr std::size_t DetailedInstanceLimit = 512U;

    [[nodiscard]] static VoxelPlacementPreview FromInstances(
        std::uint64_t revision,
        std::vector<VoxelPreviewInstance> instances) noexcept;
    [[nodiscard]] static VoxelPlacementPreview Aggregate(
        std::uint64_t revision,
        VoxelPreviewStats statistics) noexcept;

    [[nodiscard]] VoxelPlacementPreview WithRevision(
        std::uint64_t revision) const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] std::span<const VoxelPreviewInstance> Instances()
        const noexcept;
    [[nodiscard]] const VoxelPreviewStats& Statistics() const noexcept;
    [[nodiscard]] VoxelPreviewRenderMode RenderMode() const noexcept;
    [[nodiscard]] bool InstancesComplete() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

  private:
    std::shared_ptr<const std::vector<VoxelPreviewInstance>> instances_;
    VoxelPreviewStats statistics_{};
    std::uint64_t revision_ = 0U;
    VoxelPreviewRenderMode renderMode_ =
        VoxelPreviewRenderMode::DetailedInstances;
    bool instancesComplete_ = true;
};

} // namespace VoxelForge::Editor
