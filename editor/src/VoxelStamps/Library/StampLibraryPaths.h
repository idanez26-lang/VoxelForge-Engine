#pragma once

#include <filesystem>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{

inline const std::filesystem::path ProjectForgeLibraryRelativePath{
    "Assets/ForgeLibrary"};
inline const std::filesystem::path ProjectCreationsRelativePath{
    "Assets/ForgeLibrary/Creations"};

[[nodiscard]] inline bool IsPortableRelativePath(
    const std::filesystem::path& path) noexcept
{
    if (path.empty() || path.is_absolute()) return false;
    const std::filesystem::path normalized = path.lexically_normal();
    for (const auto& component : normalized)
        if (component == "..") return false;
    return normalized != ".";
}

[[nodiscard]] inline bool IsPathWithin(
    const std::filesystem::path& path,
    const std::filesystem::path& parent) noexcept
{
    const std::filesystem::path relative = path.lexically_relative(parent);
    if (relative.empty()) return path == parent;
    if (relative.is_absolute()) return false;
    for (const auto& component : relative)
        if (component == "..") return false;
    return true;
}

[[nodiscard]] inline std::filesystem::path StampTransactionTemporaryPath(
    const std::filesystem::path& asset) { return asset.string() + ".install.tmp"; }
[[nodiscard]] inline std::filesystem::path StampTransactionBackupPath(
    const std::filesystem::path& asset) { return asset.string() + ".install.bak"; }

} // namespace VoxelForge::Editor::Stamps
