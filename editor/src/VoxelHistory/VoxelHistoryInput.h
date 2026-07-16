#pragma once

namespace VoxelForge::Editor
{

enum class VoxelHistoryInputDecision
{
    None,
    Undo,
    Redo
};

struct VoxelHistoryInputFrame final
{
    bool ControlDown = false;
    bool ShiftDown = false;
    bool ZDown = false;
    bool YDown = false;
    bool TextInput = false;
    bool PopupOpen = false;
    bool KeyboardCaptured = false;
    bool HasDocument = false;
    bool EditInProgress = false;
    bool HistoryInProgress = false;
};

class VoxelHistoryInputController final
{
public:
    [[nodiscard]] VoxelHistoryInputDecision Update(
        const VoxelHistoryInputFrame& frame) noexcept;
    void Reset() noexcept;

private:
    bool undoWasDown_ = false;
    bool redoWasDown_ = false;
};

} // namespace VoxelForge::Editor
