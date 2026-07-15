#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace VoxelForge::Editor
{

struct CommandResult final
{
    bool Succeeded = false;
    std::string Message;

    [[nodiscard]] static CommandResult Success()
    {
        return {true, {}};
    }

    [[nodiscard]] static CommandResult Failure(std::string message)
    {
        return {false, std::move(message)};
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Succeeded;
    }
};

class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    EditorCommand(const EditorCommand&) = delete;
    EditorCommand& operator=(const EditorCommand&) = delete;
    EditorCommand(EditorCommand&&) = delete;
    EditorCommand& operator=(EditorCommand&&) = delete;

    [[nodiscard]] virtual CommandResult Execute() = 0;
    [[nodiscard]] virtual CommandResult Undo() = 0;
    [[nodiscard]] virtual CommandResult Redo() = 0;
    // The returned view must remain valid for the lifetime of the command.
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

protected:
    EditorCommand() = default;
};

} // namespace VoxelForge::Editor
