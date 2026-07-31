#include "ViewportInteractionV2/PencilCompactChangeResolver.h"

#include <algorithm>
#include <unordered_map>

namespace VoxelForge::Editor::InteractionV2
{
std::optional<std::vector<VoxelChange>> PencilCompactChangeResolver::Resolve(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::span<const PencilCompactPlanPtr> plans)
{
    using Position = Asset::Voxel::VoxelPosition;
    const std::optional<Asset::Voxel::VoxelDimensions> dimensions =
        document.GetDimensions(0U);
    if (!dimensions) return std::nullopt;

    std::unordered_map<Position, VoxelChange, Asset::Voxel::VoxelPositionHash>
        changes;
    for (const PencilCompactPlanPtr& plan : plans)
    {
        if (!plan || plan->DocumentGeneration() != documentGeneration ||
            plan->DocumentRevision() != document.GetRevision())
            return std::nullopt;

        Position position;
        PencilCompactPlan::Iterator iterator = plan->Iterate();
        while (iterator.Next(position))
        {
            if (position.X < 0 || position.Y < 0 || position.Z < 0 ||
                static_cast<std::uint32_t>(position.X) >= dimensions->X ||
                static_cast<std::uint32_t>(position.Y) >= dimensions->Y ||
                static_cast<std::uint32_t>(position.Z) >= dimensions->Z)
                continue;

            const auto prior = changes.find(position);
            const std::optional<Asset::Voxel::Voxel> documentBefore =
                document.GetVoxel(position);
            const PencilCompactCellState before = prior != changes.end()
                ? PencilCompactCellState{prior->second.ExistsAfter,
                    prior->second.PaletteIndexAfter}
                : PencilCompactCellState{documentBefore.has_value(),
                    documentBefore ? documentBefore->PaletteIndex : 0U};
            const PencilCompactCellState original = prior != changes.end()
                ? PencilCompactCellState{prior->second.ExistedBefore,
                    prior->second.PaletteIndexBefore}
                : before;
            const PencilCompactCellState after = plan->ResolveCell(before);
            if (original != after)
                changes[position] = {0U, position, original.Exists,
                    original.PaletteIndex, after.Exists, after.PaletteIndex};
            else changes.erase(position);
        }
    }
    std::vector<VoxelChange> resolved;
    resolved.reserve(changes.size());
    for (auto& [_, change] : changes) resolved.push_back(change);
    std::sort(resolved.begin(), resolved.end(),
        [](const VoxelChange& left, const VoxelChange& right)
    {
        return left.Position.X != right.Position.X
            ? left.Position.X < right.Position.X
            : left.Position.Y != right.Position.Y
            ? left.Position.Y < right.Position.Y
            : left.Position.Z < right.Position.Z;
    });
    return resolved;
}
} // namespace VoxelForge::Editor::InteractionV2
