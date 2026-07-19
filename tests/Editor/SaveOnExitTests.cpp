#include "EditorCloseRequest.h"

#include <iostream>

namespace
{
using VoxelForge::Editor::EditorCloseRequest;
using VoxelForge::Editor::EditorCloseRequestState;

bool Require(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}
}

int main()
{
    EditorCloseRequest clean;
    if (!Require(clean.Request(false), "Clean Quit must be accepted.") ||
        !Require(clean.State() == EditorCloseRequestState::ReadyToClose,
            "Clean Quit must wait for the current rendered frame.") ||
        !Require(clean.CompleteRenderedFrame(),
            "Clean Quit must advance only after a rendered frame.") ||
        !Require(clean.ConsumeCloseRequest(),
            "Clean Quit must close at the next safe frame boundary.") ||
        !Require(!clean.ConsumeCloseRequest(),
            "Shutdown must only be requested once."))
        return 1;

    EditorCloseRequest cancelled;
    if (!Require(cancelled.Request(true),
            "Dirty Quit must enter confirmation.") ||
        !Require(cancelled.State() == EditorCloseRequestState::WaitingForUser,
            "Dirty Quit must preserve the document while waiting.") ||
        !Require(cancelled.Cancel(), "Cancel must be accepted.") ||
        !Require(cancelled.State() == EditorCloseRequestState::None,
            "Cancel must keep the editor open."))
        return 2;

    EditorCloseRequest discarded;
    if (!Require(discarded.Request(true),
            "Dirty Quit must enter confirmation before discard.") ||
        !Require(discarded.Discard(), "Don't Save must be accepted.") ||
        !Require(discarded.State() == EditorCloseRequestState::ReadyToClose,
            "Don't Save must still defer closing to a safe frame.") ||
        !Require(discarded.CompleteRenderedFrame(),
            "Don't Save must finish the current frame."))
        return 3;

    EditorCloseRequest saved;
    if (!Require(saved.Request(true),
            "Dirty Quit must enter confirmation before Save.") ||
        !Require(saved.ScheduleSave(),
            "The UI callback must only schedule Save.") ||
        !Require(saved.State() == EditorCloseRequestState::SavingBeforeClose,
            "Scheduled Save must preserve the document until the next frame.") ||
        !Require(saved.CompleteSave(true),
            "A successful deferred Save must be recorded.") ||
        !Require(saved.State() == EditorCloseRequestState::ReadyToClose,
            "Successful Save must not close inside its callback.") ||
        !Require(saved.CompleteRenderedFrame(),
            "Successful Save must be followed by a safe rendered frame.") ||
        !Require(saved.State() == EditorCloseRequestState::Closing,
            "Closing may begin only after that frame."))
        return 4;

    EditorCloseRequest failed;
    if (!Require(failed.Request(true),
            "Dirty Quit must enter confirmation before a failed Save.") ||
        !Require(failed.ScheduleSave(), "Save must be scheduled.") ||
        !Require(!failed.CompleteSave(false),
            "A failed Save must not authorize closing.") ||
        !Require(failed.State() == EditorCloseRequestState::Failed,
            "A failed Save must keep the editor open.") ||
        !Require(!failed.CompleteRenderedFrame(),
            "A failed Save must never advance to Closing.") ||
        !Require(failed.ScheduleSave(), "A failed Save must be retryable.") ||
        !Require(failed.CompleteSave(true),
            "A successful retry must authorize deferred closing."))
        return 5;

    return 0;
}
