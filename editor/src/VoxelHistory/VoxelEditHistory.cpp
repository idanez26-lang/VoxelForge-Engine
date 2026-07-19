#include "VoxelEditHistory.h"

#include "Commands/Voxel/VoxelEditSession.h"
#include "Commands/Voxel/VoxelEditTransaction.h"

#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
VoxelEditHistoryResult Result(
    const VoxelEditHistoryResultCode code,
    const bool changed,
    std::string label,
    std::string message = {},
    std::shared_ptr<const VoxelEditSelectionTransition> selection = {},
    const VoxelEditSelectionState selectionState =
        VoxelEditSelectionState::None)
{
    return {code, changed, std::move(label), std::move(message),
        std::move(selection), selectionState};
}

class BusyGuard final
{
public:
    explicit BusyGuard(bool& busy) noexcept : busy_(busy) { busy_ = true; }
    ~BusyGuard() { busy_ = false; }

private:
    bool& busy_;
};
}

VoxelEditHistory::VoxelEditHistory(
    const VoxelEditHistoryLimits limits) noexcept
    : limits_(limits)
{
}

VoxelEditHistoryResult VoxelEditHistory::Execute(
    VoxelEditSession& session,
    VoxelEditOperation operation)
{
    if (busy_)
        return Result(VoxelEditHistoryResultCode::Busy, false, {},
            "A voxel history operation is already in progress.");
    if (operation.Label.empty() || operation.Changes.empty())
        return Result(VoxelEditHistoryResultCode::InvalidOperation, false,
            std::move(operation.Label),
            "Voxel edit operations require a label and at least one change.");

    const std::size_t memory = EstimateVoxelEditOperationMemory(operation);
    if (limits_.MaximumCommandCount == 0U ||
        memory == std::numeric_limits<std::size_t>::max() ||
        memory > limits_.MaximumEstimatedMemory)
    {
        return Result(VoxelEditHistoryResultCode::LimitExceeded, false,
            std::move(operation.Label),
            "Voxel edit exceeds the configured undo history limit.");
    }
    if (nextState_ == std::numeric_limits<std::uint64_t>::max())
        return Result(VoxelEditHistoryResultCode::LimitExceeded, false,
            std::move(operation.Label),
            "Voxel history state identifiers are exhausted.");

    try
    {
        undoStack_.push_back({
            std::move(operation), currentState_, nextState_, memory});
    }
    catch (const std::bad_alloc&)
    {
        return Result(VoxelEditHistoryResultCode::Failed, false, {},
            "Unable to allocate voxel history storage.");
    }

    BusyGuard guard(busy_);
    StoredOperation& pending = undoStack_.back();
    const CommandResult applied = ApplyVoxelChanges(
        session,
        session.VoxelModelGeneration(),
        pending.Operation.Changes,
        VoxelChangeDirection::Forward);
    if (!applied)
    {
        const std::string label = pending.Operation.Label;
        undoStack_.pop_back();
        return Result(VoxelEditHistoryResultCode::Failed, false, label,
            applied.Message);
    }

    DiscardRedo();
    while (undoStack_.size() > 1U &&
           estimatedMemory_ >
               limits_.MaximumEstimatedMemory - memory)
    {
        estimatedMemory_ -= undoStack_.front().EstimatedMemory;
        undoStack_.pop_front();
    }
    estimatedMemory_ += memory;
    currentState_ = nextState_++;
    Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument();
    if (document != nullptr) SynchronizeDirty(session, *document);
    const std::string label = pending.Operation.Label;
    EnforceLimits();
    return Result(VoxelEditHistoryResultCode::Applied, true, label, {},
        pending.Operation.SelectionTransition,
        VoxelEditSelectionState::After);
}

VoxelEditHistoryResult VoxelEditHistory::Undo(VoxelEditSession& session)
{
    if (busy_)
        return Result(VoxelEditHistoryResultCode::Busy, false, {},
            "A voxel history operation is already in progress.");
    if (undoStack_.empty())
        return Result(VoxelEditHistoryResultCode::NothingToUndo, false, {},
            "Nothing to undo.");

    BusyGuard guard(busy_);
    StoredOperation& operation = undoStack_.back();
    const CommandResult applied = ApplyVoxelChanges(
        session,
        session.VoxelModelGeneration(),
        operation.Operation.Changes,
        VoxelChangeDirection::Backward);
    if (!applied)
        return Result(VoxelEditHistoryResultCode::Failed, false,
            operation.Operation.Label, applied.Message);

    const std::string label = operation.Operation.Label;
    currentState_ = operation.BeforeState;
    redoStack_.splice(
        redoStack_.end(), undoStack_, std::prev(undoStack_.end()));
    if (Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument())
        SynchronizeDirty(session, *document);
    return Result(VoxelEditHistoryResultCode::Applied, true, label, {},
        operation.Operation.SelectionTransition,
        VoxelEditSelectionState::Before);
}

VoxelEditHistoryResult VoxelEditHistory::Redo(VoxelEditSession& session)
{
    if (busy_)
        return Result(VoxelEditHistoryResultCode::Busy, false, {},
            "A voxel history operation is already in progress.");
    if (redoStack_.empty())
        return Result(VoxelEditHistoryResultCode::NothingToRedo, false, {},
            "Nothing to redo.");

    BusyGuard guard(busy_);
    StoredOperation& operation = redoStack_.back();
    const CommandResult applied = ApplyVoxelChanges(
        session,
        session.VoxelModelGeneration(),
        operation.Operation.Changes,
        VoxelChangeDirection::Forward);
    if (!applied)
        return Result(VoxelEditHistoryResultCode::Failed, false,
            operation.Operation.Label, applied.Message);

    const std::string label = operation.Operation.Label;
    currentState_ = operation.AfterState;
    undoStack_.splice(
        undoStack_.end(), redoStack_, std::prev(redoStack_.end()));
    if (Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument())
        SynchronizeDirty(session, *document);
    return Result(VoxelEditHistoryResultCode::Applied, true, label, {},
        operation.Operation.SelectionTransition,
        VoxelEditSelectionState::After);
}

void VoxelEditHistory::MarkSavedState(
    Asset::Voxel::VoxelDocument& document) noexcept
{
    savedState_ = currentState_;
    document.UpdateDirtyFromHistory(true);
}

void VoxelEditHistory::Clear() noexcept
{
    undoStack_.clear();
    redoStack_.clear();
    estimatedMemory_ = 0U;
    currentState_ = 0U;
    savedState_ = 0U;
    nextState_ = 1U;
    busy_ = false;
}

bool VoxelEditHistory::CanUndo() const noexcept { return !undoStack_.empty(); }
bool VoxelEditHistory::CanRedo() const noexcept { return !redoStack_.empty(); }
bool VoxelEditHistory::IsAtSavedState() const noexcept
{
    return currentState_ == savedState_;
}
bool VoxelEditHistory::IsBusy() const noexcept { return busy_; }
std::size_t VoxelEditHistory::UndoCount() const noexcept
{
    return undoStack_.size();
}
std::size_t VoxelEditHistory::RedoCount() const noexcept
{
    return redoStack_.size();
}
std::size_t VoxelEditHistory::EstimatedMemory() const noexcept
{
    return estimatedMemory_;
}
std::string_view VoxelEditHistory::UndoLabel() const noexcept
{
    return CanUndo() ? undoStack_.back().Operation.Label : std::string_view{};
}
std::string_view VoxelEditHistory::RedoLabel() const noexcept
{
    return CanRedo() ? redoStack_.back().Operation.Label : std::string_view{};
}
const VoxelEditHistoryLimits& VoxelEditHistory::Limits() const noexcept
{
    return limits_;
}

void VoxelEditHistory::SynchronizeDirty(
    VoxelEditSession& session,
    Asset::Voxel::VoxelDocument& document) noexcept
{
    const bool atSavedState = IsAtSavedState();
    document.UpdateDirtyFromHistory(atSavedState);
    session.UpdateVoxelEditSavedState(atSavedState);
}

void VoxelEditHistory::DiscardRedo() noexcept
{
    for (const StoredOperation& operation : redoStack_)
        estimatedMemory_ -= operation.EstimatedMemory;
    redoStack_.clear();
}

void VoxelEditHistory::EnforceLimits() noexcept
{
    while (undoStack_.size() > limits_.MaximumCommandCount ||
           estimatedMemory_ > limits_.MaximumEstimatedMemory)
    {
        estimatedMemory_ -= undoStack_.front().EstimatedMemory;
        undoStack_.pop_front();
    }
}

const char* VoxelEditHistoryResultCodeName(
    const VoxelEditHistoryResultCode code) noexcept
{
    switch (code)
    {
    case VoxelEditHistoryResultCode::Applied: return "Applied";
    case VoxelEditHistoryResultCode::NothingToUndo: return "Nothing to undo";
    case VoxelEditHistoryResultCode::NothingToRedo: return "Nothing to redo";
    case VoxelEditHistoryResultCode::InvalidOperation: return "Invalid operation";
    case VoxelEditHistoryResultCode::LimitExceeded: return "History limit exceeded";
    case VoxelEditHistoryResultCode::Busy: return "History busy";
    case VoxelEditHistoryResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
