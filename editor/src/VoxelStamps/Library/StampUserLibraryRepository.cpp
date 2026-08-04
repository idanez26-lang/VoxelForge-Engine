#include "VoxelStamps/Library/StampUserLibraryRepository.h"

#include "VoxelStamps/Library/StampLibraryPaths.h"

#include <system_error>
#include <utility>

namespace VoxelForge::Editor::Stamps
{

StampUserLibraryRepository::StampUserLibraryRepository(
    const Core::UserDataPaths& userDataPaths,
    std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem)
    : StampFilesystemLibraryRepository(
          StampLibraryScope::User,
          UserForgeLibraryRelativePath,
          UserCreationsRelativePath,
          true,
          std::move(transactionFileSystem))
{
    static_cast<void>(SetUserDataPaths(userDataPaths));
}

bool StampUserLibraryRepository::SetUserDataPaths(
    const Core::UserDataPaths& userDataPaths)
{
    ClearUserDataRoot();
    const std::filesystem::path root =
        userDataPaths.LocalDataDirectory().lexically_normal();
    if (root.empty() || !root.is_absolute()) return false;

    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(root, error);
    if (error || canonical.empty()) return false;

    const auto status = std::filesystem::symlink_status(root, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
    }
    else if (error || std::filesystem::is_symlink(status) ||
             !std::filesystem::is_directory(status))
    {
        return false;
    }

    SetValidatedRepositoryRoot(canonical);
    return true;
}

void StampUserLibraryRepository::ClearUserDataRoot() noexcept
{
    ClearRepositoryRoot();
}

const std::filesystem::path&
StampUserLibraryRepository::UserDataRoot() const noexcept
{
    return RepositoryRoot();
}

} // namespace VoxelForge::Editor::Stamps
