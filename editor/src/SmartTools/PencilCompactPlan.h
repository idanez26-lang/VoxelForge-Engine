#pragma once

#include "BrushEngine/SmartBrushCompactFootprint.h"
#include "SmartTools/SmartTool.h"

#include <cstdint>
#include <memory>
#include <string>

namespace VoxelForge::Editor
{
struct PencilCompactRequest final
{
    Asset::Voxel::VoxelDimensions Dimensions{};
    SmartBrushState Brush{};
    SmartBrushPlacement Placement{};
    SmartAction Action = SmartAction::Add;
    std::size_t PaletteIndex = 1U;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::uint64_t ProfileIdentity = 0U;
    std::uint64_t ProfileRevision = 0U;
};

// The compact plan owns the action rule as well as its procedural geometry.
// It is deliberately independent of VoxelDocument so both presentation and
// the commit gateway can resolve exactly the same before/after cell state.
struct PencilCompactCellState final
{
    bool Exists = false;
    std::uint8_t PaletteIndex = 0U;

    [[nodiscard]] bool operator==(const PencilCompactCellState&) const noexcept = default;
};

enum class PencilCompactPlanCode : std::uint8_t
{
    Valid,
    OutOfBounds,
    Unsupported,
    InvalidRequest
};

class PencilCompactPlan final
{
public:
    class Iterator final
    {
    public:
        [[nodiscard]] bool Next(Asset::Voxel::VoxelPosition& position) noexcept;

    private:
        friend class PencilCompactPlan;
        explicit Iterator(const PencilCompactPlan& plan) noexcept;

        const PencilCompactPlan* plan_ = nullptr;
        SmartBrushCompactFootprint::Iterator offsets_{};
    };

    [[nodiscard]] const SmartBrushCompactCacheKey& CacheKey() const noexcept;
    [[nodiscard]] const SmartBrushCompactDescriptor& Descriptor() const noexcept;
    [[nodiscard]] const SmartBrushPlacement& Placement() const noexcept;
    [[nodiscard]] SmartAction Action() const noexcept;
    [[nodiscard]] std::size_t PaletteIndex() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept;
    [[nodiscard]] const SmartBrushBounds& Bounds() const noexcept;
    [[nodiscard]] std::size_t ExactVoxelCount() const noexcept;
    [[nodiscard]] std::uint64_t PlanId() const noexcept;
    [[nodiscard]] bool IsInsideDocument() const noexcept;
    [[nodiscard]] std::size_t MaterializedPositionCount() const noexcept;
    [[nodiscard]] PencilCompactCellState ResolveCell(
        PencilCompactCellState before) const noexcept;
    [[nodiscard]] Iterator Iterate() const noexcept;

private:
    friend class SmartToolPlanner;
    PencilCompactPlan(SmartBrushCompactCacheKey cacheKey,
        std::shared_ptr<const SmartBrushCompactFootprint> footprint,
        SmartBrushPlacement placement, Asset::Voxel::VoxelPosition anchor,
        SmartBrushBounds bounds, std::uint64_t planId, bool insideDocument,
        SmartAction action, std::size_t paletteIndex,
        std::uint64_t documentGeneration, std::uint64_t documentRevision) noexcept;

    SmartBrushCompactCacheKey cacheKey_{};
    std::shared_ptr<const SmartBrushCompactFootprint> footprint_;
    SmartBrushPlacement placement_{};
    Asset::Voxel::VoxelPosition anchor_{};
    SmartBrushBounds bounds_{};
    std::uint64_t planId_ = 0U;
    bool insideDocument_ = false;
    SmartAction action_ = SmartAction::Add;
    std::size_t paletteIndex_ = 1U;
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t documentRevision_ = 0U;
};

using PencilCompactPlanPtr = std::shared_ptr<const PencilCompactPlan>;

struct PencilCompactPlanResult final
{
    PencilCompactPlanCode Code = PencilCompactPlanCode::InvalidRequest;
    PencilCompactPlanPtr Plan;
    std::string Error;

    [[nodiscard]] bool HasPlan() const noexcept { return Plan != nullptr; }
};
} // namespace VoxelForge::Editor
