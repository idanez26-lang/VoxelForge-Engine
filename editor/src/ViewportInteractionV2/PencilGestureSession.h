#pragma once

#include "SmartTools/PencilCompactPlan.h"

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{
enum class PencilGesturePhase : std::uint8_t { Idle, Armed, Dragging, Completed, Cancelled };

struct PencilSurfaceSample final
{
    Asset::Voxel::VoxelPosition Center{};
    Asset::Voxel::VoxelPosition Normal{};
    bool StartsNewSurfaceSegment = false;
};

class PencilGestureSession final
{
public:
    void Begin(std::uint64_t id, PencilCompactRequest request) noexcept;
    [[nodiscard]] bool Append(PencilCompactPlanPtr plan,
        Asset::Voxel::VoxelPosition center,
        Asset::Voxel::VoxelPosition normal) noexcept;
    [[nodiscard]] PencilSurfaceSample ResolveSurfaceSample(
        Asset::Voxel::VoxelPosition target,
        Asset::Voxel::VoxelPosition normal) noexcept;
    void Complete() noexcept;
    void Cancel() noexcept;
    [[nodiscard]] bool Suspend() noexcept;
    void Reset() noexcept;
    [[nodiscard]] PencilGesturePhase Phase() const noexcept;
    [[nodiscard]] bool OwnsPointer() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept;
    [[nodiscard]] const PencilCompactRequest& SourceRequest() const noexcept;
    [[nodiscard]] const std::vector<PencilCompactPlanPtr>& Plans() const noexcept;
    [[nodiscard]] const std::optional<Asset::Voxel::VoxelPosition>& LastCenter() const noexcept;
    [[nodiscard]] const std::optional<Asset::Voxel::VoxelPosition>& LastNormal() const noexcept;
    [[nodiscard]] bool HasVisited(Asset::Voxel::VoxelPosition center,
        Asset::Voxel::VoxelPosition normal) const noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;

private:
    struct SampleKey final
    {
        Asset::Voxel::VoxelPosition Center{};
        Asset::Voxel::VoxelPosition Normal{};
        [[nodiscard]] bool operator==(const SampleKey&) const noexcept = default;
    };
    struct SampleKeyHash final
    {
        [[nodiscard]] std::size_t operator()(const SampleKey& key) const noexcept;
    };
    PencilGesturePhase phase_ = PencilGesturePhase::Idle;
    std::uint64_t id_ = 0U;
    PencilCompactRequest request_{};
    std::optional<Asset::Voxel::VoxelPosition> lastCenter_;
    std::optional<Asset::Voxel::VoxelPosition> lastNormal_;
    std::optional<Asset::Voxel::VoxelPosition> lockedSurfaceNormal_;
    std::int32_t lockedSurfaceCoordinate_ = 0;
    bool suspended_ = false;
    std::unordered_set<SampleKey, SampleKeyHash> visited_;
    std::vector<PencilCompactPlanPtr> plans_;
};
} // namespace VoxelForge::Editor::InteractionV2
