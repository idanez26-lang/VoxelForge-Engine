#pragma once
#include <cstdint>
#include <string>

namespace VoxelForge::Core
{
    class UUID final
    {
    public:
        UUID();
        explicit UUID(std::uint64_t value) noexcept;

        [[nodiscard]] std::uint64_t Value() const noexcept;
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] bool operator==(const UUID& other) const noexcept = default;

    private:
        std::uint64_t value_;
    };
}
