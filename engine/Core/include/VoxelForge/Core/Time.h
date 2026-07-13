#pragma once
#include <chrono>
#include <string>

namespace VoxelForge::Core
{
    class Time final
    {
    public:
        [[nodiscard]] static std::chrono::system_clock::time_point Now() noexcept;
        [[nodiscard]] static std::string NowIso8601();
    };
}
