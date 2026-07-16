#include "VoxelPencilInput.h"

namespace VoxelForge::Editor
{

VoxelToolInputDecision VoxelToolInputController::Update(
    const VoxelToolInputFrame& frame) noexcept
{
    const bool cameraInteracting =
        frame.CameraInteraction != VoxelCameraInteraction::None;
    if (cameraInteracting) cameraRearmRequired_ = true;

    const bool risingEdge = frame.LeftButtonDown && !leftButtonWasDown_;
    if (!frame.LeftButtonDown)
    {
        leftButtonWasDown_ = false;
        pressedDocumentGeneration_.reset();
        if (!cameraInteracting) cameraRearmRequired_ = false;
        return VoxelToolInputDecision::None;
    }

    leftButtonWasDown_ = true;
    if (!risingEdge)
    {
        if (!frame.HasDocument ||
            (pressedDocumentGeneration_ &&
             *pressedDocumentGeneration_ != frame.DocumentGeneration))
        {
            pressedDocumentGeneration_.reset();
        }
        return VoxelToolInputDecision::None;
    }

    pressedDocumentGeneration_ = frame.HasDocument
        ? std::optional<std::uint64_t>(frame.DocumentGeneration)
        : std::nullopt;
    const bool allowed = frame.ToolActive && frame.HasDocument &&
        frame.ViewportHovered && frame.ViewportFocused &&
        !frame.InterfaceCapturedMouse && !frame.PopupOpen &&
        !frame.DragDropActive && !cameraInteracting &&
        !cameraRearmRequired_ && !frame.EditInProgress;
    return allowed
        ? VoxelToolInputDecision::Apply
        : VoxelToolInputDecision::None;
}

void VoxelToolInputController::Reset() noexcept
{
    leftButtonWasDown_ = false;
    cameraRearmRequired_ = false;
    pressedDocumentGeneration_.reset();
}

bool VoxelToolInputController::WaitingForRelease() const noexcept
{
    return leftButtonWasDown_;
}

bool VoxelToolInputController::CameraRearmRequired() const noexcept
{
    return cameraRearmRequired_;
}

} // namespace VoxelForge::Editor
