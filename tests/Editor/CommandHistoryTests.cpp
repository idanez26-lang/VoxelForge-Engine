#include "Commands/CommandHistory.h"

#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
bool Check(const bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

struct CommandControl final
{
    bool FailExecute = false;
    bool FailUndo = false;
    bool FailRedo = false;
    bool ThrowExecute = false;
    bool ThrowUnknownExecute = false;
    bool ThrowUndo = false;
    bool ThrowRedo = false;
    int DestructionCount = 0;
};

class SetIntegerCommand final : public VoxelForge::Editor::EditorCommand
{
public:
    SetIntegerCommand(
        int& value,
        const int newValue,
        std::string name,
        CommandControl& control)
        : value_(value),
          newValue_(newValue),
          name_(std::move(name)),
          control_(control)
    {
    }

    ~SetIntegerCommand() override
    {
        ++control_.DestructionCount;
    }

    VoxelForge::Editor::CommandResult Execute() override
    {
        if (control_.ThrowExecute)
            throw std::runtime_error("simulated execute exception");
        if (control_.ThrowUnknownExecute) throw 42;
        if (control_.FailExecute)
            return VoxelForge::Editor::CommandResult::Failure(
                "simulated execute failure");
        oldValue_ = value_;
        value_ = newValue_;
        return VoxelForge::Editor::CommandResult::Success();
    }

    VoxelForge::Editor::CommandResult Undo() override
    {
        if (control_.ThrowUndo)
            throw std::runtime_error("simulated undo exception");
        if (control_.FailUndo)
            return VoxelForge::Editor::CommandResult::Failure(
                "simulated undo failure");
        value_ = oldValue_;
        return VoxelForge::Editor::CommandResult::Success();
    }

    VoxelForge::Editor::CommandResult Redo() override
    {
        if (control_.ThrowRedo)
            throw std::runtime_error("simulated redo exception");
        if (control_.FailRedo)
            return VoxelForge::Editor::CommandResult::Failure(
                "simulated redo failure");
        value_ = newValue_;
        return VoxelForge::Editor::CommandResult::Success();
    }

    std::string_view Name() const noexcept override
    {
        return name_;
    }

private:
    int& value_;
    int oldValue_ = 0;
    int newValue_ = 0;
    std::string name_;
    CommandControl& control_;
};

std::unique_ptr<SetIntegerCommand> MakeCommand(
    int& value,
    const int newValue,
    const char* name,
    CommandControl& control)
{
    return std::make_unique<SetIntegerCommand>(
        value, newValue, name, control);
}
}

int main()
{
    using VoxelForge::Editor::CommandHistory;
    bool passed = true;

    CommandHistory empty;
    passed &= Check(empty.Limit() == CommandHistory::DefaultLimit &&
        !empty.CanUndo() && !empty.CanRedo() &&
        empty.UndoCount() == 0U && empty.RedoCount() == 0U &&
        empty.UndoName().empty() && empty.RedoName().empty(),
        "An empty history has incorrect defaults.");
    passed &= Check(!empty.Undo() && !empty.Redo() &&
        !empty.Execute(nullptr),
        "Empty or null history operations must fail.");
    CommandHistory veryLarge(std::numeric_limits<std::size_t>::max());
    passed &= Check(
        veryLarge.Limit() == std::numeric_limits<std::size_t>::max() &&
        !veryLarge.CanUndo() && !veryLarge.CanRedo(),
        "A large configured limit must not preallocate proportional storage.");

    int value = 0;
    CommandControl control;
    CommandHistory history;
    passed &= Check(history.Execute(MakeCommand(
        value, 10, "Set Ten", control)).Succeeded &&
        value == 10 && history.CanUndo() && !history.CanRedo() &&
        history.UndoName() == "Set Ten",
        "Successful command execution was not recorded.");
    passed &= Check(history.Undo().Succeeded && value == 0 &&
        !history.CanUndo() && history.CanRedo() &&
        history.RedoName() == "Set Ten",
        "Undo did not move the command to Redo.");
    passed &= Check(history.Redo().Succeeded && value == 10 &&
        history.CanUndo() && !history.CanRedo(),
        "Redo did not restore the command to Undo.");

    control.FailUndo = true;
    passed &= Check(!history.Undo() && value == 10 &&
        history.UndoCount() == 1U && history.RedoCount() == 0U,
        "Failed Undo changed state or history stacks.");
    control.FailUndo = false;
    passed &= Check(history.Undo().Succeeded && value == 0,
        "Undo did not recover after a simulated failure.");
    control.FailRedo = true;
    passed &= Check(!history.Redo() && value == 0 &&
        history.UndoCount() == 0U && history.RedoCount() == 1U,
        "Failed Redo changed state or history stacks.");
    control.FailRedo = false;
    passed &= Check(history.Redo().Succeeded && value == 10,
        "Redo did not recover after a simulated failure.");

    passed &= Check(history.Undo().Succeeded, "Undo before branching failed.");
    CommandControl failureControl;
    failureControl.FailExecute = true;
    passed &= Check(!history.Execute(MakeCommand(
        value, 99, "Fail", failureControl)) && value == 0 &&
        history.UndoCount() == 0U && history.RedoCount() == 1U &&
        failureControl.DestructionCount == 1,
        "Failed Execute changed the stacks or leaked its command.");
    passed &= Check(history.Execute(MakeCommand(
        value, 20, "Set Twenty", control)).Succeeded && value == 20 &&
        history.UndoCount() == 1U && history.RedoCount() == 0U,
        "A new command after Undo did not clear Redo.");

    history.Clear();
    value = 0;
    passed &= Check(history.Execute(MakeCommand(
        value, 1, "One", control)).Succeeded &&
        history.Execute(MakeCommand(
            value, 2, "Two", control)).Succeeded &&
        history.Execute(MakeCommand(
            value, 3, "Three", control)).Succeeded,
        "Multiple command setup failed.");
    passed &= Check(history.UndoName() == "Three" &&
        history.Undo().Succeeded && value == 2 &&
        history.UndoName() == "Two" &&
        history.Undo().Succeeded && value == 1 &&
        history.RedoName() == "Two",
        "Multiple commands do not follow LIFO order.");

    int limitedValue = 0;
    CommandControl limitedControl;
    CommandHistory limited(2U);
    passed &= Check(limited.Execute(MakeCommand(
        limitedValue, 1, "Oldest", limitedControl)).Succeeded &&
        limited.Execute(MakeCommand(
            limitedValue, 2, "Middle", limitedControl)).Succeeded &&
        limited.Execute(MakeCommand(
            limitedValue, 3, "Newest", limitedControl)).Succeeded &&
        limited.UndoCount() == 2U && limitedControl.DestructionCount == 1,
        "History limit did not evict exactly the oldest command.");
    passed &= Check(limited.Undo().Succeeded && limitedValue == 2 &&
        limited.Undo().Succeeded && limitedValue == 1 &&
        !limited.CanUndo(),
        "Limited history order is incorrect.");

    int oneValue = 0;
    CommandControl oneControl;
    CommandHistory one(1U);
    passed &= Check(one.Execute(MakeCommand(
        oneValue, 4, "Four", oneControl)).Succeeded &&
        one.Execute(MakeCommand(
            oneValue, 5, "Five", oneControl)).Succeeded &&
        one.UndoCount() == 1U && one.UndoName() == "Five" &&
        oneControl.DestructionCount == 1,
        "A history limit of one is incorrect.");

    int disabledValue = 0;
    CommandControl disabledControl;
    CommandHistory disabled(0U);
    passed &= Check(disabled.Execute(MakeCommand(
        disabledValue, 8, "Eight", disabledControl)).Succeeded &&
        disabledValue == 8 && !disabled.CanUndo() && !disabled.CanRedo() &&
        disabledControl.DestructionCount == 1,
        "A zero limit must execute commands with history disabled.");

    CommandControl exceptionControl;
    exceptionControl.ThrowExecute = true;
    passed &= Check(!empty.Execute(MakeCommand(
        value, 100, "Throw", exceptionControl)) &&
        exceptionControl.DestructionCount == 1 &&
        !empty.CanUndo() && !empty.CanRedo(),
        "A throwing command changed history or escaped the boundary.");
    CommandControl unknownExceptionControl;
    unknownExceptionControl.ThrowUnknownExecute = true;
    passed &= Check(!empty.Execute(MakeCommand(
        value, 101, "Throw Unknown", unknownExceptionControl)) &&
        unknownExceptionControl.DestructionCount == 1 &&
        !empty.CanUndo() && !empty.CanRedo(),
        "An unknown command exception escaped or changed history.");

    int exceptionValue = 0;
    CommandControl transitionExceptionControl;
    CommandHistory transitionExceptions;
    passed &= Check(transitionExceptions.Execute(MakeCommand(
        exceptionValue, 7, "Seven", transitionExceptionControl)).Succeeded,
        "Exception transition setup failed.");
    transitionExceptionControl.ThrowUndo = true;
    passed &= Check(!transitionExceptions.Undo() && exceptionValue == 7 &&
        transitionExceptions.UndoCount() == 1U &&
        transitionExceptions.RedoCount() == 0U,
        "An Undo exception changed the stacks or escaped.");
    transitionExceptionControl.ThrowUndo = false;
    passed &= Check(transitionExceptions.Undo().Succeeded && exceptionValue == 0,
        "Undo did not recover after an exception.");
    transitionExceptionControl.ThrowRedo = true;
    passed &= Check(!transitionExceptions.Redo() && exceptionValue == 0 &&
        transitionExceptions.UndoCount() == 0U &&
        transitionExceptions.RedoCount() == 1U,
        "A Redo exception changed the stacks or escaped.");
    transitionExceptionControl.ThrowRedo = false;
    passed &= Check(transitionExceptions.Redo().Succeeded && exceptionValue == 7,
        "Redo did not recover after an exception.");

    const int destroyedBeforeClear = control.DestructionCount;
    history.Clear();
    passed &= Check(!history.CanUndo() && !history.CanRedo() &&
        control.DestructionCount > destroyedBeforeClear,
        "Clear did not destroy all retained commands.");

    return passed ? 0 : 1;
}
