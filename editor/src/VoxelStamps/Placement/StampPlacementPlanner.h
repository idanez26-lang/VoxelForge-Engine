#pragma once

#include "VoxelStamps/Palette/StampDocumentPaletteCache.h"
#include "VoxelStamps/Placement/StampPlacementPlan.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VoxelForge::Editor::Stamps
{

struct StampPlacementPlannerRequest final
{
    const VoxelStamp* Stamp = nullptr;
    std::optional<StampPlacementVariantIdentity> Variant;
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::uint64_t DocumentGeneration = 0U;
    std::size_t TargetSubModel = 0U;
    StampPlacementTransform Transform{};
    StampCollisionPolicy CollisionPolicy = StampCollisionPolicy::Overwrite;
    std::size_t PaletteCapacity = 256U;
    std::uint8_t ReservedDocumentPaletteIndex = 0U;
    StampResourceLimits ResourceLimits{};
    // PERF-FOUNDATION lot 2 : cache facultatif des indices de palette occupés,
    // détenu par l'appelant (la session de placement). Laissé à nullptr, le
    // planner refait le parcours complet du document, comme avant — les
    // appelants ponctuels (tests, bancs) n'ont donc rien à changer.
    StampDocumentPaletteCache* OccupiedPaletteCache = nullptr;
};

/// Pure placement planner. This is the only component allowed to resolve
/// positions, bounds, overlaps, out-of-bounds cells, palette mapping,
/// statistics and commit eligibility.
class StampPlacementPlanner final
{
public:
    [[nodiscard]] static StampPlacementPlan Build(
        const StampPlacementPlannerRequest& request) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
