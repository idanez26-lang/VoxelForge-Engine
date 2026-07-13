#include "VoxelForge/Core/UUID.h"
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
    std::uint64_t GenerateRandomValue()
    {
        static std::random_device randomDevice;
        static std::mt19937_64 generator(randomDevice());
        static std::uniform_int_distribution<std::uint64_t> distribution;
        return distribution(generator);
    }
}

namespace VoxelForge::Core
{
    UUID::UUID() : value_(GenerateRandomValue()) {}
    UUID::UUID(std::uint64_t value) noexcept : value_(value) {}
    std::uint64_t UUID::Value() const noexcept { return value_; }

    std::string UUID::ToString() const
    {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value_;
        return stream.str();
    }
}
