#include "VoxelPencilInput.h"

namespace VoxelForge::Editor
{

VoxelPencilInputDecision VoxelPencilInputController::Update(
    const VoxelPencilInputFrame& frame) noexcept
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
        return VoxelPencilInputDecision::None;
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
        return VoxelPencilInputDecision::None;
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
        ? VoxelPencilInputDecision::Apply
        : VoxelPencilInputDecision::None;
}

void VoxelPencilInputController::Reset() noexcept
{
    leftButtonWasDown_ = false;
    cameraRearmRequired_ = false;
    pressedDocumentGeneration_.reset();
}

bool VoxelPencilInputController::WaitingForRelease() const noexcept
{
    return leftButtonWasDown_;
}

bool VoxelPencilInputController::CameraRearmRequired() const noexcept
{
    return cameraRearmRequired_;
}

} // namespace VoxelForge::Editor
