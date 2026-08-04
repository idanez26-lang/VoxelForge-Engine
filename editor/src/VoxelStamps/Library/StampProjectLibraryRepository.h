#pragma once

#include "VoxelStamps/Library/StampFilesystemLibraryRepository.h"

namespace VoxelForge::Editor::Stamps
{

class StampProjectLibraryRepository final
    : public StampFilesystemLibraryRepository
{
public:
    explicit StampProjectLibraryRepository(
        std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem = {});

    [[nodiscard]] bool SetProjectRoot(const std::filesystem::path& projectRoot);
    void ClearProjectRoot() noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
};

} // namespace VoxelForge::Editor::Stamps
