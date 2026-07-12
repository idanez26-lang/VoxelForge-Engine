#pragma once

namespace VoxelForge::Core
{
    class Application final
    {
    public:
        Application() = default;

        [[nodiscard]] int Run();

    private:
        bool Initialize();
        void Shutdown();

        bool initialized_ = false;
    };
}
