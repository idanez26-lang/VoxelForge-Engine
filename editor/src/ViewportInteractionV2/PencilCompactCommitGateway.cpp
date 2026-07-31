#include "ViewportInteractionV2/PencilCompactCommitGateway.h"
#include "ViewportInteractionV2/PencilCompactChangeResolver.h"

namespace VoxelForge::Editor::InteractionV2
{
std::optional<VoxelEditOperation> PencilCompactCommitGateway::Build(
    const Asset::Voxel::VoxelDocument& document, const std::uint64_t documentGeneration,
    const std::span<const PencilCompactPlanPtr> plans)
{
    std::optional<std::vector<VoxelChange>> changes =
        PencilCompactChangeResolver::Resolve(document, documentGeneration, plans);
    if (!changes || changes->empty()) return std::nullopt;
    VoxelEditOperation operation;
    operation.Label = "Pencil V2 Stroke";
    operation.Changes = std::move(*changes);
    return operation;
}
} // namespace VoxelForge::Editor
