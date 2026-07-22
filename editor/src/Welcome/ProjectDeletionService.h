#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class ProjectDeletionStatus
{
    Success,
    ConfirmationRequired,
    InvalidPath,
    ProjectNotFound,
    InvalidProject,
    IdentityMismatch,
    DangerousPath,
    ActiveProject,
    SymbolicLink,
    RecycleBinFailed
};

struct ProjectDeletionRequest
{
    std::filesystem::path ProjectFilePath;
    std::optional<std::filesystem::path> ActiveProjectRoot;
    std::vector<std::filesystem::path> ProtectedRoots;
    bool Confirmed = false;
};

struct ProjectDeletionResult
{
    ProjectDeletionStatus Status = ProjectDeletionStatus::InvalidPath;
    std::filesystem::path ProjectRoot;
    std::string Message;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Status == ProjectDeletionStatus::Success;
    }
};

class IProjectRecycleBin
{
public:
    virtual ~IProjectRecycleBin() = default;
    [[nodiscard]] virtual bool MoveDirectory(
        const std::filesystem::path& directory,
        std::string& error) = 0;
};

class WindowsProjectRecycleBin final : public IProjectRecycleBin
{
public:
    [[nodiscard]] bool MoveDirectory(
        const std::filesystem::path& directory,
        std::string& error) override;
};

class ProjectDeletionService final
{
public:
    explicit ProjectDeletionService(IProjectRecycleBin& recycleBin) noexcept;

    [[nodiscard]] ProjectDeletionResult DeleteProject(
        const ProjectDeletionRequest& request) const;

private:
    IProjectRecycleBin* recycleBin_ = nullptr;
};

} // namespace VoxelForge::Editor
