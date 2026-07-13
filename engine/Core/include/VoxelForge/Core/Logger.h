#pragma once
#include <mutex>
#include <string_view>

namespace VoxelForge::Core
{
    enum class LogLevel { Info, Warning, Error };

    class Logger final
    {
    public:
        static Logger& Instance();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        void Log(LogLevel level, std::string_view message);
        void Info(std::string_view message);
        void Warning(std::string_view message);
        void Error(std::string_view message);

    private:
        Logger() = default;
        std::mutex mutex_;
    };
}
