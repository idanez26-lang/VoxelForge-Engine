#include "VoxelCreation/DirectCreationFlowService.h"

#include <utility>

namespace VoxelForge::Editor
{

void DirectCreationFlowService::SetViewportPreparationCallback(
    StepCallback callback)
{
    viewportPreparationCallback_ = std::move(callback);
}

void DirectCreationFlowService::SetPencilActivationCallback(
    StepCallback callback)
{
    pencilActivationCallback_ = std::move(callback);
}

void DirectCreationFlowService::SetWorkplanePreparationCallback(
    StepCallback callback)
{
    workplanePreparationCallback_ = std::move(callback);
}

void DirectCreationFlowService::SetViewportFocusCallback(
    StepCallback callback)
{
    viewportFocusCallback_ = std::move(callback);
}

DirectCreationFlowResult DirectCreationFlowService::Create(
    VoxelModelCreationService& creationService,
    const VoxelModelCreationRequest& request,
    const VoxelModelCreationCollisionAction collisionAction) const
{
    DirectCreationFlowResult result;
    result.Creation = creationService.CreateModel(request, collisionAction);
    result.Warning = result.Creation.Warning;
    if (!result.Creation.Succeeded())
    {
        result.Message = result.Creation.Message;
        return result;
    }
    if (!result.Creation.Opened)
    {
        AppendWarning(result.Warning,
            "The created model could not be opened automatically.");
        result.Message = "Direct creation stopped before viewport preparation.";
        return result;
    }

    const auto runStep = [&result](
        const StepCallback& callback,
        bool& completed,
        const char* unavailableMessage)
    {
        if (!callback)
        {
            AppendWarning(result.Warning, unavailableMessage);
            return false;
        }
        const DirectCreationStepResult step = callback(result.Creation.ModelPath);
        completed = step.Succeeded;
        if (!step.Succeeded)
            AppendWarning(result.Warning,
                step.Message.empty() ? unavailableMessage : step.Message);
        return step.Succeeded;
    };

    if (!runStep(viewportPreparationCallback_, result.ViewportPrepared,
            "Viewport preparation is unavailable."))
    {
        result.Message = "Direct creation stopped during viewport preparation.";
        return result;
    }
    if (!runStep(pencilActivationCallback_, result.PencilActivated,
            "Pencil activation is unavailable."))
    {
        result.Message = "Direct creation stopped during Pencil activation.";
        return result;
    }
    if (!runStep(workplanePreparationCallback_, result.WorkplaneReady,
            "Workplane preparation is unavailable."))
    {
        result.Message = "Direct creation stopped during Workplane preparation.";
        return result;
    }
    static_cast<void>(runStep(viewportFocusCallback_, result.FocusRequested,
        "Viewport focus is unavailable."));
    result.Message = result.Ready()
        ? "Voxel model is ready for direct creation."
        : "Voxel model was created, but direct creation is incomplete.";
    return result;
}

void DirectCreationFlowService::AppendWarning(
    std::string& warning,
    std::string message)
{
    if (message.empty()) return;
    if (!warning.empty() && warning.back() != ' ') warning += ' ';
    warning += std::move(message);
}

} // namespace VoxelForge::Editor
