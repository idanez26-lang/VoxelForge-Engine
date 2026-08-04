#include "VoxelStamps/Diagnostics/VoxelStampSmokeTest.h"

#include <array>
#include <cstdlib>
#include <iostream>

int main()
{
    using namespace VoxelForge::Editor::Stamps;
    constexpr std::array modes{
        VoxelStampSmokeMode::Mvp,
        VoxelStampSmokeMode::Corruption,
        VoxelStampSmokeMode::Library,
        VoxelStampSmokeMode::Variant,
        VoxelStampSmokeMode::SmartPlacement};
    for (const VoxelStampSmokeMode mode : modes)
    {
        const VoxelStampSmokeResult result = RunVoxelStampSmokeTest(mode);
        (result.Succeeded ? std::cout : std::cerr)
            << result.Message << '\n';
        if (!result.Succeeded) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
