#pragma once

#include "EditorCommand.h"

#include <cstddef>
#include <list>
#include <memory>
#include <string_view>

namespace VoxelForge::Editor
{

class CommandHistory final
{
public:
    static constexpr std::size_t DefaultLimit = 100U;

    // A zero limit executes commands without retaining undo history.
    explicit CommandHistory(std::size_t limit = DefaultLimit) noexcept;

    [[nodiscard]] CommandResult Execute(
        std::unique_ptr<EditorCommand> command);
    [[nodiscard]] CommandResult Undo();
    [[nodiscard]] CommandResult Redo();
    void Clear() noexcept;

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] std::string_view UndoName() const noexcept;
    [[nodiscard]] std::string_view RedoName() const noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;
    [[nodiscard]] std::size_t Limit() const noexcept;

private:
    using CommandStack = std::list<std::unique_ptr<EditorCommand>>;

    std::size_t limit_ = DefaultLimit;
    CommandStack undoStack_;
    CommandStack redoStack_;
};

} // namespace VoxelForge::Editor
