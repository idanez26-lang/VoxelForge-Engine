#include "CommandHistory.h"

#include <exception>
#include <iterator>
#include <new>
#include <string>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
template <typename Operation>
CommandResult InvokeCommand(Operation&& operation)
{
    try
    {
        return operation();
    }
    catch (const std::exception& exception)
    {
        return CommandResult::Failure(
            std::string("Command threw an exception: ") + exception.what());
    }
    catch (...)
    {
        return CommandResult::Failure("Command threw an unknown exception.");
    }
}
}

CommandHistory::CommandHistory(const std::size_t limit) noexcept
    : limit_(limit)
{
}

CommandResult CommandHistory::Execute(
    std::unique_ptr<EditorCommand> command)
{
    if (!command)
    {
        return CommandResult::Failure("Cannot execute a null command.");
    }

    CommandStack pending;
    try
    {
        pending.push_back(std::move(command));
    }
    catch (const std::bad_alloc&)
    {
        return CommandResult::Failure(
            "Unable to allocate command history storage.");
    }

    EditorCommand& pendingCommand = *pending.back();
    CommandResult result = InvokeCommand(
        [&pendingCommand]() { return pendingCommand.Execute(); });
    if (!result)
    {
        return result;
    }

    redoStack_.clear();
    if (limit_ == 0U)
    {
        return result;
    }

    if (undoStack_.size() == limit_)
    {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.splice(undoStack_.end(), pending, pending.begin());
    return result;
}

CommandResult CommandHistory::Undo()
{
    if (undoStack_.empty())
    {
        return CommandResult::Failure("Nothing to undo.");
    }

    EditorCommand& command = *undoStack_.back();
    CommandResult result = InvokeCommand([&command]() { return command.Undo(); });
    if (!result)
    {
        return result;
    }

    redoStack_.splice(
        redoStack_.end(), undoStack_, std::prev(undoStack_.end()));
    return result;
}

CommandResult CommandHistory::Redo()
{
    if (redoStack_.empty())
    {
        return CommandResult::Failure("Nothing to redo.");
    }

    EditorCommand& command = *redoStack_.back();
    CommandResult result = InvokeCommand([&command]() { return command.Redo(); });
    if (!result)
    {
        return result;
    }

    undoStack_.splice(
        undoStack_.end(), redoStack_, std::prev(redoStack_.end()));
    return result;
}

void CommandHistory::Clear() noexcept
{
    undoStack_.clear();
    redoStack_.clear();
}

bool CommandHistory::CanUndo() const noexcept
{
    return !undoStack_.empty();
}

bool CommandHistory::CanRedo() const noexcept
{
    return !redoStack_.empty();
}

std::string_view CommandHistory::UndoName() const noexcept
{
    return CanUndo() ? undoStack_.back()->Name() : std::string_view{};
}

std::string_view CommandHistory::RedoName() const noexcept
{
    return CanRedo() ? redoStack_.back()->Name() : std::string_view{};
}

std::size_t CommandHistory::UndoCount() const noexcept
{
    return undoStack_.size();
}

std::size_t CommandHistory::RedoCount() const noexcept
{
    return redoStack_.size();
}

std::size_t CommandHistory::Limit() const noexcept
{
    return limit_;
}

} // namespace VoxelForge::Editor
