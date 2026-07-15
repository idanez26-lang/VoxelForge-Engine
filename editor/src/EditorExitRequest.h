#pragma once

namespace VoxelForge::Editor
{

class EditorExitRequest final
{
public:
    void RequestExit() noexcept
    {
        exitRequested_ = true;
    }

    [[nodiscard]] bool IsExitRequested() const noexcept
    {
        return exitRequested_;
    }

    [[nodiscard]] bool ConsumeExitRequest() noexcept
    {
        const bool exitRequested = exitRequested_;
        exitRequested_ = false;
        return exitRequested;
    }

private:
    bool exitRequested_ = false;
};

} // namespace VoxelForge::Editor
