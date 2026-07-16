#include "VoxelHistoryInput.h"

namespace VoxelForge::Editor
{

VoxelHistoryInputDecision VoxelHistoryInputController::Update(
    const VoxelHistoryInputFrame& frame) noexcept
{
    const bool undoDown = frame.ControlDown && !frame.ShiftDown && frame.ZDown;
    const bool redoDown = frame.ControlDown &&
        (frame.YDown || (frame.ShiftDown && frame.ZDown));
    const bool undoPressed = undoDown && !undoWasDown_;
    const bool redoPressed = redoDown && !redoWasDown_;
    undoWasDown_ = undoDown;
    redoWasDown_ = redoDown;

    const bool allowed = frame.HasDocument && !frame.TextInput &&
        !frame.PopupOpen && !frame.KeyboardCaptured &&
        !frame.EditInProgress && !frame.HistoryInProgress;
    if (!allowed) return VoxelHistoryInputDecision::None;
    if (redoPressed) return VoxelHistoryInputDecision::Redo;
    if (undoPressed) return VoxelHistoryInputDecision::Undo;
    return VoxelHistoryInputDecision::None;
}

void VoxelHistoryInputController::Reset() noexcept
{
    undoWasDown_ = false;
    redoWasDown_ = false;
}

} // namespace VoxelForge::Editor
