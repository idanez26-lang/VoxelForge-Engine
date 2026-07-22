#pragma once

#include "VoxelCreation/DirectCreationFlowService.h"

#include <string_view>

namespace VoxelForge::Editor
{

/**
 * Orchestrates the one-action New Model command without owning UI state.
 *
 * File creation remains in VoxelModelCreationService and editor preparation
 * remains in DirectCreationFlowService. This controller only supplies the
 * product defaults and enforces non-destructive automatic renaming.
 */
class NewVoxelModelWorkflow final
{
public:
    static constexpr std::string_view DefaultModelName = "New Model";

    [[nodiscard]] DirectCreationFlowResult Create(
        VoxelModelCreationService& creationService,
        const DirectCreationFlowService& directCreation) const;

    [[nodiscard]] static VoxelModelCreationRequest DefaultRequest();
};

} // namespace VoxelForge::Editor
