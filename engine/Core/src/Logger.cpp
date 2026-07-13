#include "VoxelForge/Core/Logger.h"
#include <iostream>
#include <string_view>

namespace
{
    constexpr std::string_view PrefixFor(VoxelForge::Core::LogLevel level) noexcept
    {
        using VoxelForge::Core::LogLevel;
        switch (level)
        {
        case LogLevel::Info: return "[INFO]";
        case LogLevel::Warning: return "[WARNING]";
        case LogLevel::Error: return "[ERROR]";
        }
        return "[UNKNOWN]";
    }
}

namespace VoxelForge::Core
{
    Logger& Logger::Instance()
    {
        static Logger logger;
        return logger;
    }

    void Logger::Log(LogLevel level, std::string_view message)
    {
        const std::scoped_lock lock(mutex_);
        std::ostream& output = level == LogLevel::Error ? std::cerr : std::cout;
        output << PrefixFor(level) << ' ' << message << '\n';
    }

    void Logger::Info(std::string_view message) { Log(LogLevel::Info, message); }
    void Logger::Warning(std::string_view message) { Log(LogLevel::Warning, message); }
    void Logger::Error(std::string_view message) { Log(LogLevel::Error, message); }
}
