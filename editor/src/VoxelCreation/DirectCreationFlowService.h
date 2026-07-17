#pragma once

#include "VoxelCreation/VoxelModelCreationService.h"

#include <filesystem>
#include <functional>
#include <string>

namespace VoxelForge::Editor
{

struct DirectCreationStepResult final
{
    bool Succeeded = false;
    std::string Message;
};

struct DirectCreationFlowResult final
{
    VoxelModelCreationResult Creation;
    bool ViewportPrepared = false;
    bool PencilActivated = false;
    bool WorkplaneReady = false;
    bool FocusRequested = false;
    std::string Message;
    std::string Warning;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Creation.Succeeded() && Creation.Opened &&
            Creation.AssetBrowserRefreshed && ViewportPrepared &&
            PencilActivated && WorkplaneReady && FocusRequested;
    }
};

class DirectCreationFlowService final
{
public:
    using StepCallback = std::function<DirectCreationStepResult(
        const std::filesystem::path&)>;

    void SetViewportPreparationCallback(StepCallback callback);
    void SetPencilActivationCallback(StepCallback callback);
    void SetWorkplanePreparationCallback(StepCallback callback);
    void SetViewportFocusCallback(StepCallback callback);

    [[nodiscard]] DirectCreationFlowResult Create(
        VoxelModelCreationService& creationService,
        const VoxelModelCreationRequest& request,
        VoxelModelCreationCollisionAction collisionAction =
            VoxelModelCreationCollisionAction::Ask) const;

private:
    static void AppendWarning(std::string& warning, std::string message);

    StepCallback viewportPreparationCallback_;
    StepCallback pencilActivationCallback_;
    StepCallback workplanePreparationCallback_;
    StepCallback viewportFocusCallback_;
};

} // namespace VoxelForge::Editor
