#pragma once

#include "SmartTools/PencilCompactPlan.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <optional>
#include <span>

namespace VoxelForge::Editor::InteractionV2
{
// Commit-only bridge: planning and preview remain procedural. Iteration is
// deliberately deferred until MouseUp, where one atomic history operation is
// produced for the entire stroke.
class PencilCompactCommitGateway final
{
public:
    [[nodiscard]] static std::optional<VoxelEditOperation> Build(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::span<const PencilCompactPlanPtr> plans);
};
} // namespace VoxelForge::Editor::InteractionV2
