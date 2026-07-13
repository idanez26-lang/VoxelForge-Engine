#pragma once
#include <string>

namespace VoxelForge::Core
{
    struct Version final
    {
        int major;
        int minor;
        int patch;

        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] static constexpr Version Current() noexcept
        {
            return Version{0, 0, 3};
        }
    };
}
