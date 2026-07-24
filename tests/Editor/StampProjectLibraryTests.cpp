#include "VoxelStamps/Library/StampLibraryPaths.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;
namespace fs = std::filesystem;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryProject final
{
public:
    TemporaryProject()
    {
        path_ = fs::temp_directory_path() /
            ("VoxelForgeStampProjectLibrary-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(fs::create_directories(path_ / "Assets"), "Unable to create temporary project Assets.");
    }
    ~TemporaryProject()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& Root() const noexcept { return path_; }
private:
    fs::path path_;
};

VoxelStamp MakeStamp(const std::uint64_t id, const std::uint8_t red)
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = {}},
        {.Minimum = {}, .Maximum = {}, .Dimensions = {.X = 1U, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Auto,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {.X = 128, .Y = 128, .Z = 128},
         .AutoPolicyVersion = 1U},
        {},
        {{.LocalColorId = 0U,
          .Color = {.Red = red, .Green = 2U, .Blue = 3U, .Alpha = 255U}}},
        {{.Position = {}, .LocalColorId = 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return *stamp;
}

VoxelStamp MakeMismatchedHashStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{99U}, .ContentHash = "not-the-logical-hash"},
        {.Minimum = {}, .Maximum = {}, .Dimensions = {.X = 1U, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Auto, .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {.X = 128, .Y = 128, .Z = 128}, .AutoPolicyVersion = 1U},
        {}, {{.LocalColorId = 0U, .Color = {.Red = 1U, .Green = 2U, .Blue = 3U, .Alpha = 255U}}},
        {{.Position = {}, .LocalColorId = 0U}}, DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return *stamp;
}

class FailingPublishFileSystem final : public IStampLibraryTransactionFileSystem
{
public:
    bool FailTemporaryPublish = false;

    bool Rename(const fs::path& source, const fs::path& destination, std::string& error) override
    {
        if (FailTemporaryPublish && source.filename().string().ends_with(".install.tmp"))
        {
            FailTemporaryPublish = false;
            error = "Injected temporary publish failure.";
            return false;
        }
        std::error_code filesystemError;
        fs::rename(source, destination, filesystemError);
        if (!filesystemError) return true;
        error = filesystemError.message();
        return false;
    }
};

void WriteBytes(const fs::path& path, const std::initializer_list<unsigned char> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "Unable to create test transaction file.");
    for (const unsigned char byte : bytes) output.put(static_cast<char>(byte));
    Require(static_cast<bool>(output), "Unable to write test transaction file.");
}

void TestCanonicalInstallReadUniqueAndRemove(TemporaryProject& project)
{
    StampProjectLibraryRepository repository;
    Require(repository.SetProjectRoot(project.Root()), "Project repository root must configure.");
    const StampLibraryResult first = repository.Install(
        MakeStamp(41U, 10U), {.PreferredFileStem = "stone-wall"});
    Require(first.Succeeded() && first.Reference.RelativePath ==
                fs::path{"Assets/ForgeLibrary/Creations/stone-wall.vfstamp"} &&
                fs::exists(project.Root() / first.Reference.RelativePath),
        "Install must publish under the canonical portable Creations path.");
    Require(StampTransactionTemporaryPath(project.Root() / first.Reference.RelativePath).filename() ==
                "stone-wall.vfstamp.install.tmp" &&
                StampTransactionBackupPath(project.Root() / first.Reference.RelativePath).filename() ==
                "stone-wall.vfstamp.install.bak",
        "Transaction filenames must use the explicit .install temporary convention.");
    const StampLibraryResult read = repository.Read(first.Reference);
    Require(read.Succeeded() && read.Stamp && read.Reference == first.Reference,
        "Read must validate and return the installed portable source asset.");
    const StampLibraryResult resolved = repository.ResolvePortableReference(
        first.Reference.RelativePath);
    Require(resolved.Succeeded() && resolved.Reference.Id == first.Reference.Id &&
                resolved.Reference.ContentHash == first.Reference.ContentHash && resolved.Stamp,
        "Portable resolution must verify and return UUID/hash identity facts.");

    const StampLibraryResult second = repository.Install(
        MakeStamp(42U, 20U), {.PreferredFileStem = "stone-wall"});
    Require(second.Succeeded() && second.Reference.RelativePath.filename() == "stone-wall-2.vfstamp",
        "Duplicate preferred names must choose deterministic unique filenames.");
    const StampLibraryResult inventory = repository.EnumerateSourceAssets();
    Require(inventory.Succeeded() && inventory.Assets.size() == 2U &&
                inventory.Assets[0].Reference.RelativePath < inventory.Assets[1].Reference.RelativePath,
        "Source enumeration must be complete and deterministically ordered.");
    Require(repository.Remove(first.Reference).Succeeded() &&
                !fs::exists(project.Root() / first.Reference.RelativePath),
        "Remove must delete only the validated project source asset.");
}

void TestConfigurationAndValidationPreflight(TemporaryProject& project)
{
    StampProjectLibraryRepository repository;
    Require(!repository.SetProjectRoot({}) &&
                repository.EnumerateSourceAssets().Error == StampLibraryError::NotConfigured &&
                repository.ResolvePortableReference(
                    "Assets/ForgeLibrary/Creations/missing.vfstamp").Error ==
                    StampLibraryError::NotConfigured,
        "Empty project roots and unconfigured operations must be rejected explicitly.");
    Require(repository.SetProjectRoot(project.Root()), "Project repository root must configure.");
    const StampLibraryResult invalid = repository.Install(MakeMismatchedHashStamp());
    Require(invalid.Error == StampLibraryError::SerializationFailed &&
                !fs::exists(project.Root() / ProjectForgeLibraryRelativePath),
        "Invalid Stamp serialization must fail before ForgeLibrary directories are created.");
}

void TestConfinementSymlinkAndStaleTransactions(TemporaryProject& project)
{
    StampProjectLibraryRepository repository;
    Require(repository.SetProjectRoot(project.Root()), "Project repository root must configure.");
    const StampLibraryResult installed = repository.Install(MakeStamp(50U, 10U));
    Require(installed.Succeeded(), "Confinement fixture install failed.");
    for (const fs::path& path : {fs::path{"../escape.vfstamp"}, fs::path{"/absolute.vfstamp"},
                                 fs::path{"Assets/ForgeLibrary/Creations/../escape.vfstamp"}})
    {
        Require(!repository.ResolvePortableReference(path).Succeeded(),
            "Traversal or absolute portable references must be rejected.");
    }

    const fs::path stale = StampTransactionTemporaryPath(project.Root() / installed.Reference.RelativePath);
    const fs::path staleBackup = StampTransactionBackupPath(project.Root() / installed.Reference.RelativePath);
    WriteBytes(stale, {1U, 2U, 3U});
    WriteBytes(staleBackup, {4U, 5U, 6U});
    Require(repository.Install(MakeStamp(51U, 20U), {
                .PreferredFileStem = installed.Reference.RelativePath.stem().string(),
                .ReplaceExisting = true}).Error == StampLibraryError::StaleTransactionFile,
        "A stale install temporary must block publication without being deleted.");
    const StampLibraryResult rebuilt = repository.RebuildSourceInventory();
    Require(rebuilt.Succeeded() && rebuilt.Assets.size() == 1U &&
                std::count_if(rebuilt.Diagnostics.begin(), rebuilt.Diagnostics.end(),
                    [](const StampLibraryDiagnostic& diagnostic) {
                        return diagnostic.Code == StampLibraryError::StaleTransactionFile;
                    }) == 2,
        "Inventory rebuild must report stale transactions while retaining source assets.");

    const fs::path outside = project.Root().parent_path() / "stamp-library-outside.vfstamp";
    WriteBytes(outside, {1U});
    const fs::path link = project.Root() / ProjectCreationsRelativePath / "outside-link.vfstamp";
    std::error_code linkError;
    fs::create_symlink(outside, link, linkError);
    if (!linkError)
    {
        Require(repository.ResolvePortableReference(
                    fs::path{"Assets/ForgeLibrary/Creations/outside-link.vfstamp"}).Error ==
                    StampLibraryError::SymbolicLinkRejected,
            "External symlink references must be rejected.");
    }
    const fs::path brokenTemporary = project.Root() / ProjectCreationsRelativePath /
        "broken.vfstamp.install.tmp";
    linkError.clear();
    fs::create_symlink(project.Root().parent_path() / "missing-stamp-target.vfstamp",
        brokenTemporary, linkError);
    if (!linkError)
    {
        const StampLibraryResult blocked = repository.Install(MakeStamp(52U, 30U), {
            .PreferredFileStem = "broken", .ReplaceExisting = true});
        Require(blocked.Error == StampLibraryError::SymbolicLinkRejected,
            "Broken transaction symlinks must be rejected before any write follows them.");
        const StampLibraryResult withBrokenLink = repository.RebuildSourceInventory();
        Require(std::any_of(withBrokenLink.Diagnostics.begin(), withBrokenLink.Diagnostics.end(),
                    [](const StampLibraryDiagnostic& diagnostic) {
                        return diagnostic.RelativePath.filename() == "broken.vfstamp.install.tmp" &&
                            diagnostic.Code == StampLibraryError::SymbolicLinkRejected;
                    }),
            "Inventory must report a broken transaction symlink without following it.");
    }
    std::error_code cleanup;
    fs::remove(outside, cleanup);
}

void TestAtomicReplacementRollbackAndInvalidInventory(TemporaryProject& project)
{
    auto filesystem = std::make_shared<FailingPublishFileSystem>();
    StampProjectLibraryRepository repository(filesystem);
    Require(repository.SetProjectRoot(project.Root()), "Project repository root must configure.");
    const StampLibraryResult original = repository.Install(
        MakeStamp(61U, 10U), {.PreferredFileStem = "replace-me"});
    Require(original.Succeeded(), "Replacement fixture install failed.");
    filesystem->FailTemporaryPublish = true;
    const StampLibraryResult failed = repository.Install(
        MakeStamp(62U, 40U), {.PreferredFileStem = "replace-me", .ReplaceExisting = true});
    Require(failed.Error == StampLibraryError::TransactionFailed,
        "Injected final rename failure must produce a transaction failure.");
    const StampLibraryResult restored = repository.Read(original.Reference);
    Require(restored.Succeeded() && restored.Stamp->Identity().Id == Core::UUID{61U} &&
                !fs::exists(StampTransactionTemporaryPath(project.Root() / original.Reference.RelativePath)) &&
                !fs::exists(StampTransactionBackupPath(project.Root() / original.Reference.RelativePath)),
        "Failed replacement must restore the original source asset with no partial state.");

    const fs::path corrupt = project.Root() / ProjectCreationsRelativePath / "corrupt.vfstamp";
    WriteBytes(corrupt, {0U, 1U, 2U});
    const StampLibraryResult inventory = repository.RebuildSourceInventory();
    Require(inventory.Succeeded() && inventory.Assets.size() == 1U &&
                std::any_of(inventory.Diagnostics.begin(), inventory.Diagnostics.end(),
                    [](const StampLibraryDiagnostic& diagnostic) {
                        return diagnostic.RelativePath.filename() == "corrupt.vfstamp" &&
                            diagnostic.Code == StampLibraryError::InvalidAsset;
                    }),
        "Inventory rebuild must report invalid sources without treating them as assets.");
    const StampLibraryResult repeated = repository.RebuildSourceInventory();
    Require(repeated.Succeeded() && repeated.Assets == inventory.Assets &&
                repeated.Diagnostics.size() == inventory.Diagnostics.size() &&
                std::equal(repeated.Diagnostics.begin(), repeated.Diagnostics.end(),
                    inventory.Diagnostics.begin(), [](const StampLibraryDiagnostic& left,
                                                       const StampLibraryDiagnostic& right) {
                        return left.Code == right.Code && left.RelativePath == right.RelativePath &&
                            left.Message == right.Message;
                    }),
        "Rebuilding the same source inventory must be deterministic.");

    StampAssetReference wrongUuid = original.Reference;
    wrongUuid.Id = Core::UUID{999U};
    Require(repository.Read(wrongUuid).Error == StampLibraryError::InvalidReference &&
                repository.Remove(wrongUuid).Error == StampLibraryError::InvalidReference,
        "Read and Remove must reject references with a mismatched UUID.");
    StampAssetReference wrongHash = original.Reference;
    wrongHash.ContentHash = "wrong-hash";
    Require(repository.Read(wrongHash).Error == StampLibraryError::InvalidReference &&
                repository.Remove(wrongHash).Error == StampLibraryError::InvalidReference,
        "Read and Remove must reject references with a mismatched content hash.");
    StampAssetReference pathOnly = original.Reference;
    pathOnly.Id = Core::UUID{0U};
    pathOnly.ContentHash.clear();
    Require(repository.Remove(pathOnly).Error == StampLibraryError::InvalidReference,
        "Remove must not accept a path-only portable reference.");
}

} // namespace

int main()
{
    try
    {
        TemporaryProject canonicalProject;
        TestCanonicalInstallReadUniqueAndRemove(canonicalProject);
        TemporaryProject configurationProject;
        TestConfigurationAndValidationPreflight(configurationProject);
        TemporaryProject confinementProject;
        TestConfinementSymlinkAndStaleTransactions(confinementProject);
        TemporaryProject transactionProject;
        TestAtomicReplacementRollbackAndInvalidInventory(transactionProject);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
