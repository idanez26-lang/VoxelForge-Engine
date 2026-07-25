#pragma once

#include "VoxelStamps/Library/IStampCatalogStore.h"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

/// Fault-injection seam for the two publication transitions.  The production
/// implementation delegates to std::filesystem::rename.
class IStampCatalogTransactionFileSystem
{
public:
    virtual ~IStampCatalogTransactionFileSystem() = default;
    [[nodiscard]] virtual bool Rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) = 0;
};

[[nodiscard]] std::shared_ptr<IStampCatalogTransactionFileSystem>
CreateStandardStampCatalogTransactionFileSystem();

/// Bounded, deterministic V1 JSON store for the Project Library catalogue.
/// It is deliberately independent of ImGui, renderer and repository scans.
class StampJsonCatalogStore final : public IStampCatalogStore
{
public:
    explicit StampJsonCatalogStore(
        std::shared_ptr<IStampCatalogTransactionFileSystem> transactionFileSystem = {});

    [[nodiscard]] bool SetProjectRoot(const std::filesystem::path& projectRoot);
    void ClearProjectRoot() noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

    [[nodiscard]] StampCatalogResult LoadCatalogue() const override;
    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(
        const StampCatalog& catalogue) override;

    /// These helpers are public for deterministic unit testing and future
    /// alternate stores. They never access the filesystem.
    [[nodiscard]] static StampCatalogResult SerializeToMemory(
        const StampCatalog& catalogue,
        std::vector<std::byte>& bytes);
    [[nodiscard]] static StampCatalogResult DeserializeFromMemory(
        std::span<const std::byte> bytes);

private:
    [[nodiscard]] StampCatalogResult ValidateConfiguredPath(
        bool createLibraryDirectory) const;
    [[nodiscard]] StampCatalogResult ReadPath(const std::filesystem::path& path) const;

    std::filesystem::path projectRoot_;
    std::filesystem::path path_;
    std::shared_ptr<IStampCatalogTransactionFileSystem> transactionFileSystem_;
};

} // namespace VoxelForge::Editor::Stamps
