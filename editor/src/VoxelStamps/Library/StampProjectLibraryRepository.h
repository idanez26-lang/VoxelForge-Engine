#pragma once

#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <memory>

namespace VoxelForge::Editor::Stamps
{

/// Small fault-injection seam for the two publish transitions. Production uses
/// the standard implementation; tests can fail the temporary-to-final rename.
class IStampLibraryTransactionFileSystem
{
public:
    virtual ~IStampLibraryTransactionFileSystem() = default;
    [[nodiscard]] virtual bool Rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) = 0;
};

[[nodiscard]] std::shared_ptr<IStampLibraryTransactionFileSystem>
CreateStandardStampLibraryTransactionFileSystem();

class StampProjectLibraryRepository final : public IStampLibraryRepository
{
public:
    explicit StampProjectLibraryRepository(
        std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem = {});

    [[nodiscard]] bool SetProjectRoot(const std::filesystem::path& projectRoot);
    void ClearProjectRoot() noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;

    [[nodiscard]] StampLibraryResult Install(
        const VoxelStamp& stamp,
        const StampInstallOptions& options = {}) override;
    [[nodiscard]] StampLibraryResult Read(
        const StampAssetReference& reference) const override;
    [[nodiscard]] StampLibraryResult EnumerateSourceAssets() const override;
    [[nodiscard]] StampLibraryResult Remove(
        const StampAssetReference& reference) override;
    [[nodiscard]] StampLibraryResult ResolvePortableReference(
        const std::filesystem::path& relativePath) const override;
    [[nodiscard]] StampLibraryResult RebuildSourceInventory() const override;

private:
    [[nodiscard]] StampLibraryResult ResolvePath(
        const std::filesystem::path& relativePath,
        std::filesystem::path& absolute) const;
    [[nodiscard]] StampLibraryResult EnsureCreationsDirectory() const;
    [[nodiscard]] StampLibraryResult ReadPath(
        const std::filesystem::path& absolute,
        const std::filesystem::path& relative) const;

    std::filesystem::path projectRoot_;
    std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem_;
};

} // namespace VoxelForge::Editor::Stamps
