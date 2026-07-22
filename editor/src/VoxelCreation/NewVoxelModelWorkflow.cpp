#include "VoxelCreation/NewVoxelModelWorkflow.h"

namespace VoxelForge::Editor
{

DirectCreationFlowResult NewVoxelModelWorkflow::Create(
    VoxelModelCreationService& creationService,
    const DirectCreationFlowService& directCreation) const
{
    return directCreation.Create(
        creationService,
        DefaultRequest(),
        VoxelModelCreationCollisionAction::Rename);
}

VoxelModelCreationRequest NewVoxelModelWorkflow::DefaultRequest()
{
    VoxelModelCreationRequest request;
    request.Name = DefaultModelName;
    return request;
}

} // namespace VoxelForge::Editor
