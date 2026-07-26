#pragma once

#include "VoxelHistory/VoxelEditOperation.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>

namespace VoxelForge::Asset::Voxel
{
class VoxelDocument;
}

namespace VoxelForge::Editor
{

class VoxelEditSession;

enum class VoxelEditHistoryResultCode
{
    Applied,
    NoChange,
    NothingToUndo,
    NothingToRedo,
    InvalidOperation,
    LimitExceeded,
    Busy,
    Failed
};

enum class VoxelEditSelectionState : std::uint8_t
{
    None,
    Before,
    After
};

struct VoxelEditHistoryResult final
{
    VoxelEditHistoryResultCode Code = VoxelEditHistoryResultCode::Failed;
    bool Changed = false;
    std::string Label;
    std::string Message;
    std::shared_ptr<const VoxelEditSelectionTransition> SelectionTransition;
    VoxelEditSelectionState SelectionState = VoxelEditSelectionState::None;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Code == VoxelEditHistoryResultCode::Applied;
    }
};

struct VoxelEditHistoryLimits final
{
    std::size_t MaximumCommandCount = 100U;
    std::size_t MaximumEstimatedMemory = 256U * 1024U * 1024U;
};

class VoxelEditHistory final
{
public:
    explicit VoxelEditHistory(
        VoxelEditHistoryLimits limits = {}) noexcept;

    [[nodiscard]] VoxelEditHistoryResult Execute(
        VoxelEditSession& session,
        VoxelEditOperation operation);
    [[nodiscard]] VoxelEditHistoryResult Undo(VoxelEditSession& session);
    [[nodiscard]] VoxelEditHistoryResult Redo(VoxelEditSession& session);

    void MarkSavedState(Asset::Voxel::VoxelDocument& document) noexcept;
    void Clear() noexcept;

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool IsAtSavedState() const noexcept;
    [[nodiscard]] bool IsBusy() const noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;
    [[nodiscard]] std::size_t EstimatedMemory() const noexcept;
    [[nodiscard]] std::string_view UndoLabel() const noexcept;
    [[nodiscard]] std::string_view RedoLabel() const noexcept;
    [[nodiscard]] const VoxelEditHistoryLimits& Limits() const noexcept;

private:
    struct StoredOperation final
    {
        VoxelEditOperation Operation;
        std::uint64_t BeforeState = 0U;
        std::uint64_t AfterState = 0U;
        std::size_t EstimatedMemory = 0U;
    };

    using OperationStack = std::list<StoredOperation>;

    void SynchronizeDirty(
        VoxelEditSession& session,
        Asset::Voxel::VoxelDocument& document) noexcept;
    void DiscardRedo() noexcept;
    void EnforceLimits() noexcept;

    VoxelEditHistoryLimits limits_{};
    OperationStack undoStack_;
    OperationStack redoStack_;
    std::size_t estimatedMemory_ = 0U;
    std::uint64_t currentState_ = 0U;
    std::uint64_t savedState_ = 0U;
    std::uint64_t nextState_ = 1U;
    bool busy_ = false;
};

[[nodiscard]] const char* VoxelEditHistoryResultCodeName(
    VoxelEditHistoryResultCode code) noexcept;

} // namespace VoxelForge::Editor
