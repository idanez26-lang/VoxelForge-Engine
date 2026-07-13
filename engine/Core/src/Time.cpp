#include "VoxelForge/Core/Time.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace VoxelForge::Core
{
    std::chrono::system_clock::time_point Time::Now() noexcept
    {
        return std::chrono::system_clock::now();
    }

    std::string Time::NowIso8601()
    {
        const auto now = Now();
        const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
        std::tm timeInfo{};

#if defined(_WIN32)
        localtime_s(&timeInfo, &rawTime);
#else
        localtime_r(&rawTime, &timeInfo);
#endif

        std::ostringstream stream;
        stream << std::put_time(&timeInfo, "%Y-%m-%dT%H:%M:%S");
        return stream.str();
    }
}
