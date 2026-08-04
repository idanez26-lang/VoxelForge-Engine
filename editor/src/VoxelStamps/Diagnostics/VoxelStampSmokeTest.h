#pragma once

#include <cstdint>
#include <string>

namespace VoxelForge::Editor::Stamps
{

enum class VoxelStampSmokeMode : std::uint8_t
{
    Mvp,
    Corruption,
    Library,
    Variant,
    SmartPlacement
};

struct VoxelStampSmokeResult final
{
    VoxelStampSmokeMode Mode = VoxelStampSmokeMode::Mvp;
    bool Succeeded = false;
    std::string Message;
};

/// Runs one isolated, headless V1 release scenario. Every filesystem-backed
/// mode injects disposable project/profile roots and removes them before
/// returning; it never consults the real user profile.
[[nodiscard]] VoxelStampSmokeResult RunVoxelStampSmokeTest(
    VoxelStampSmokeMode mode) noexcept;

[[nodiscard]] const char* VoxelStampSmokeModeName(
    VoxelStampSmokeMode mode) noexcept;

} // namespace VoxelForge::Editor::Stamps
