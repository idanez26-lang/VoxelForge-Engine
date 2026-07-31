#pragma once
#include "PencilGestureSession.h"
#include "SmartTools/SmartToolPlanner.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{
struct PencilInteractionInput final { bool ViewportHovered=false, ViewportFocused=false, UiCapturesPointer=false, CameraActive=false, FocusLost=false; };
struct PencilInteractionMetrics final
{
    std::uint64_t BusinessResolveBatches = 0U;
    std::uint64_t PlanCacheHits = 0U;
    std::uint64_t PlanCacheMisses = 0U;
    std::uint64_t PlanBuilds = 0U;
    std::uint64_t FootprintTranslations = 0U;
    std::uint64_t MaterializedHoverPositions = 0U;
};
class PencilInteractionHandler final
{
public:
 [[nodiscard]] bool CanAccept(const PencilInteractionInput& input, const PencilGestureSession& session) const noexcept;
 [[nodiscard]] bool Begin(PencilGestureSession& session, std::uint64_t id, PencilCompactRequest request, const PencilInteractionInput& input);
 [[nodiscard]] PencilCompactPlanPtr PlanHover(SmartToolPlanner& planner,
     PencilCompactRequest request, Asset::Voxel::VoxelPosition center);
 [[nodiscard]] bool Drag(PencilGestureSession& session, SmartToolPlanner& planner,
     Asset::Voxel::VoxelPosition center, Asset::Voxel::VoxelPosition normal,
     const PencilInteractionInput& input);
 void Escape(PencilGestureSession& session) noexcept;
 [[nodiscard]] std::size_t CacheSize() const noexcept;
 [[nodiscard]] const PencilInteractionMetrics& Metrics() const noexcept;
private:
 struct Key final { PencilCompactRequest request{}; Asset::Voxel::VoxelPosition center{}; [[nodiscard]] bool operator==(const Key&) const noexcept; };
 struct Hash final { [[nodiscard]] std::size_t operator()(const Key&) const noexcept; };
 std::unordered_map<Key,PencilCompactPlanPtr,Hash> cache_; static constexpr std::size_t MaxCache=64U;
 [[nodiscard]] PencilCompactPlanPtr CacheOrPlan(SmartToolPlanner& planner,
     PencilCompactRequest request, Asset::Voxel::VoxelPosition center);
 PencilInteractionMetrics metrics_{};
};
} // namespace VoxelForge::Editor::InteractionV2
