#include "Console/EditorConsoleService.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using VoxelForge::Editor::EditorConsoleService;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}
} // namespace

int main()
{
    try
    {
        EditorConsoleService console;
        Require(console.Messages().empty(), "console starts empty");
        Require(console.IsVisible(), "console starts visible");
        Require(console.MaximumMessageCount() ==
                EditorConsoleService::DefaultMaximumMessageCount,
            "default capacity");

        console.SetVisible(false);
        console.AddMessage("hello");
        Require(console.IsVisible(),
            "adding a message must reveal the console");
        Require(console.Messages().size() == 1U &&
                console.Messages().front() == "hello",
            "message stored");

        EditorConsoleService small(3U);
        for (int i = 0; i < 5; ++i)
            small.AddMessage("m" + std::to_string(i));
        Require(small.Messages().size() == 3U &&
                small.Messages().front() == "m2" &&
                small.Messages().back() == "m4",
            "bounded queue evicts the oldest messages");

        *small.VisibilityFlag() = false;
        Require(!small.IsVisible(), "visibility flag is shared");

        small.Clear();
        Require(small.Messages().empty(), "clear empties the log");

        EditorConsoleService degenerate(0U);
        degenerate.AddMessage("a");
        degenerate.AddMessage("b");
        Require(degenerate.Messages().size() == 1U &&
                degenerate.Messages().front() == "b",
            "zero capacity clamps to one");
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "Editor console service tests passed.\n";
    return 0;
}
