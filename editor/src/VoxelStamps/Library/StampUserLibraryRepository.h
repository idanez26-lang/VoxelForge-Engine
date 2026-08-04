#pragma once

#include "VoxelForge/Core/UserDataPaths.h"
#include "VoxelStamps/Library/StampFilesystemLibraryRepository.h"

namespace VoxelForge::Editor::Stamps
{

/// Profile-level My Library repository. The caller injects UserDataPaths so
/// tests and tools never touch the real user profile accidentally.
class StampUserLibraryRepository final
    : public StampFilesystemLibraryRepository
{
public:
    explicit StampUserLibraryRepository(
        const Core::UserDataPaths& userDataPaths,
        std::shared_ptr<IStampLibraryTransactionFileSystem>
            transactionFileSystem = {});

    [[nodiscard]] bool SetUserDataPaths(
        const Core::UserDataPaths& userDataPaths);
    void ClearUserDataRoot() noexcept;
    [[nodiscard]] const std::filesystem::path& UserDataRoot() const noexcept;
};

} // namespace VoxelForge::Editor::Stamps
