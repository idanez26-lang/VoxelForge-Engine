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

/// Shared confined-filesystem implementation used by both Project Library and
/// My Library. Its constructor is protected so callers always select an
/// explicit scope through one of the concrete repositories.
class StampFilesystemLibraryRepository : public IStampLibraryRepository
{
public:
    [[nodiscard]] StampLibraryResult Install(
        const VoxelStamp& stamp,
        const StampInstallOptions& options = {}) override;
    [[nodiscard]] StampLibraryResult Read(
        const StampAssetReference& reference) const override;
    [[nodiscard]] StampLibrarySourceFactsResult InspectSource(
        const StampAssetReference& reference) const override;
    [[nodiscard]] StampLibraryResult EnumerateSourceAssets() const override;
    [[nodiscard]] StampLibraryResult Remove(
        const StampAssetReference& reference) override;
    [[nodiscard]] StampLibraryResult ResolvePortableReference(
        const std::filesystem::path& relativePath) const override;
    [[nodiscard]] StampLibraryResult RebuildSourceInventory() const override;

protected:
    StampFilesystemLibraryRepository(
        StampLibraryScope scope,
        std::filesystem::path libraryRelativePath,
        std::filesystem::path creationsRelativePath,
        bool createRepositoryRoot,
        std::shared_ptr<IStampLibraryTransactionFileSystem>
            transactionFileSystem = {});

    void SetValidatedRepositoryRoot(std::filesystem::path root);
    void ClearRepositoryRoot() noexcept;
    [[nodiscard]] const std::filesystem::path& RepositoryRoot() const noexcept;

private:
    [[nodiscard]] StampLibraryResult ResolvePath(
        const std::filesystem::path& relativePath,
        std::filesystem::path& absolute) const;
    [[nodiscard]] StampLibraryResult EnsureCreationsDirectory() const;
    [[nodiscard]] StampLibraryResult ReadPath(
        const std::filesystem::path& absolute,
        const std::filesystem::path& relative) const;

    StampLibraryScope scope_ = StampLibraryScope::Project;
    std::filesystem::path libraryRelativePath_;
    std::filesystem::path creationsRelativePath_;
    std::filesystem::path repositoryRoot_;
    bool createRepositoryRoot_ = false;
    std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem_;
};

} // namespace VoxelForge::Editor::Stamps
