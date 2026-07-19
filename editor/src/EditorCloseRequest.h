#pragma once

namespace VoxelForge::Editor
{

enum class EditorCloseRequestState
{
    None,
    Requested,
    WaitingForUser,
    SavingBeforeClose,
    ReadyToClose,
    Closing,
    Failed
};

class EditorCloseRequest final
{
public:
    [[nodiscard]] bool Request(const bool hasUnsavedChanges) noexcept
    {
        if (state_ != EditorCloseRequestState::None) return false;
        state_ = EditorCloseRequestState::Requested;
        state_ = hasUnsavedChanges
            ? EditorCloseRequestState::WaitingForUser
            : EditorCloseRequestState::ReadyToClose;
        return true;
    }

    [[nodiscard]] bool ScheduleSave() noexcept
    {
        if (state_ != EditorCloseRequestState::WaitingForUser &&
            state_ != EditorCloseRequestState::Failed)
            return false;
        state_ = EditorCloseRequestState::SavingBeforeClose;
        return true;
    }

    [[nodiscard]] bool CompleteSave(const bool succeeded) noexcept
    {
        if (state_ != EditorCloseRequestState::SavingBeforeClose) return false;
        state_ = succeeded
            ? EditorCloseRequestState::ReadyToClose
            : EditorCloseRequestState::Failed;
        return succeeded;
    }

    [[nodiscard]] bool Discard() noexcept
    {
        if (state_ != EditorCloseRequestState::WaitingForUser &&
            state_ != EditorCloseRequestState::Failed)
            return false;
        state_ = EditorCloseRequestState::ReadyToClose;
        return true;
    }

    [[nodiscard]] bool Cancel() noexcept
    {
        if (state_ != EditorCloseRequestState::WaitingForUser &&
            state_ != EditorCloseRequestState::Failed)
            return false;
        state_ = EditorCloseRequestState::None;
        return true;
    }

    [[nodiscard]] bool CompleteRenderedFrame() noexcept
    {
        if (state_ != EditorCloseRequestState::ReadyToClose) return false;
        state_ = EditorCloseRequestState::Closing;
        return true;
    }

    [[nodiscard]] bool ConsumeCloseRequest() noexcept
    {
        if (state_ != EditorCloseRequestState::Closing || closeIssued_)
            return false;
        closeIssued_ = true;
        return true;
    }

    [[nodiscard]] EditorCloseRequestState State() const noexcept
    {
        return state_;
    }

    [[nodiscard]] bool IsClosing() const noexcept
    {
        return state_ == EditorCloseRequestState::Closing;
    }

private:
    EditorCloseRequestState state_ = EditorCloseRequestState::None;
    bool closeIssued_ = false;
};

} // namespace VoxelForge::Editor
