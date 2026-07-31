#pragma once

#include "SmartTools/PencilCompactPlan.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <optional>
#include <span>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{
// Pure bridge from immutable compact plans to document changes.  It does not
// own history or mutate the document, so detailed preview and CommitGateway
// necessarily observe the same virtual stroke state.
class PencilCompactChangeResolver final
{
public:
    [[nodiscard]] static std::optional<std::vector<VoxelChange>> Resolve(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::span<const PencilCompactPlanPtr> plans);
};
} // namespace VoxelForge::Editor::InteractionV2
