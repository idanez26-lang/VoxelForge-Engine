#include "VoxelStamps/Library/StampProjectLibraryRepository.h"

#include "VoxelStamps/Format/VfstampReader.h"
#include "VoxelStamps/Format/VfstampWriter.h"
#include "VoxelStamps/Library/StampLibraryPaths.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <new>
#include <optional>
#include <system_error>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

class StandardTransactionFileSystem final : public IStampLibraryTransactionFileSystem
{
public:
    bool Rename(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                std::string& error) override
    {
        std::error_code filesystemError;
        std::filesystem::rename(source, destination, filesystemError);
        if (!filesystemError) return true;
        error = filesystemError.message();
        return false;
    }
};

[[nodiscard]] StampLibraryResult Failure(
    const StampLibraryError error,
    std::string message = {})
{
    return {.Error = error,
            .Message = message.empty() ? std::string(StampLibraryErrorMessage(error))
                                       : std::move(message)};
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool IsSafeRegularFile(
    const std::filesystem::path& path,
    std::error_code& error)
{
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::exists(status) &&
        std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
}

[[nodiscard]] bool WriteAndFlush(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    output.close();
    return static_cast<bool>(output);
}

[[nodiscard]] bool IsValidUtf8(const std::string_view text) noexcept
{
    for (std::size_t index = 0U; index < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80U) continue;
        unsigned count = 0U;
        std::uint32_t point = 0U;
        std::uint32_t minimum = 0U;
        if ((first & 0xE0U) == 0xC0U) { count = 1U; point = first & 0x1FU; minimum = 0x80U; }
        else if ((first & 0xF0U) == 0xE0U) { count = 2U; point = first & 0x0FU; minimum = 0x800U; }
        else if ((first & 0xF8U) == 0xF0U) { count = 3U; point = first & 0x07U; minimum = 0x10000U; }
        else return false;
        if (index + count > text.size()) return false;
        for (unsigned offset = 0U; offset < count; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0U) != 0x80U) return false;
            point = (point << 6U) | (next & 0x3FU);
        }
        if (point < minimum || point > 0x10FFFFU || (point >= 0xD800U && point <= 0xDFFFU)) return false;
    }
    return true;
}

[[nodiscard]] std::string UpperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

[[nodiscard]] bool IsReservedWindowsStem(const std::string_view stem)
{
    const std::size_t extension = stem.find('.');
    const std::string upper = UpperAscii(std::string(stem.substr(0U, extension)));
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return true;
    return upper.size() == 4U && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
        upper[3] >= '1' && upper[3] <= '9';
}

[[nodiscard]] std::optional<std::string> ValidFileStem(const std::string_view requested)
{
    if (requested.empty() || requested.size() > 255U || !IsValidUtf8(requested) ||
        requested.back() == '.' || requested.back() == ' ') return std::nullopt;
    for (const unsigned char character : requested)
    {
        if (character < 0x20U || character == '<' || character == '>' || character == ':' ||
            character == '"' || character == '/' || character == '\\' || character == '|' ||
            character == '?' || character == '*') return std::nullopt;
    }
    if (IsReservedWindowsStem(requested)) return std::nullopt;
    return std::string(requested);
}

[[nodiscard]] std::filesystem::path Utf8Path(const std::string_view text)
{
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(std::u8string(first, first + text.size()));
}

[[nodiscard]] bool RemoveRegularFile(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return true;
    if (error || !std::filesystem::exists(status) || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) return false;
    return std::filesystem::remove(path, error) && !error;
}

[[nodiscard]] bool InspectTransactionEntry(
    const std::filesystem::path& path,
    bool& exists,
    bool& symbolicLink,
    std::error_code& error) noexcept
{
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        exists = false;
        symbolicLink = false;
        return true;
    }
    if (error) return false;
    exists = std::filesystem::exists(status) || std::filesystem::is_symlink(status);
    symbolicLink = std::filesystem::is_symlink(status);
    return true;
}

} // namespace

std::shared_ptr<IStampLibraryTransactionFileSystem>
CreateStandardStampLibraryTransactionFileSystem()
{
    return std::make_shared<StandardTransactionFileSystem>();
}

StampFilesystemLibraryRepository::StampFilesystemLibraryRepository(
    const StampLibraryScope scope,
    std::filesystem::path libraryRelativePath,
    std::filesystem::path creationsRelativePath,
    const bool createRepositoryRoot,
    std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem)
    : scope_(scope),
      libraryRelativePath_(std::move(libraryRelativePath)),
      creationsRelativePath_(std::move(creationsRelativePath)),
      createRepositoryRoot_(createRepositoryRoot),
      transactionFileSystem_(transactionFileSystem
          ? std::move(transactionFileSystem)
          : CreateStandardStampLibraryTransactionFileSystem())
{
}

StampProjectLibraryRepository::StampProjectLibraryRepository(
    std::shared_ptr<IStampLibraryTransactionFileSystem> transactionFileSystem)
    : StampFilesystemLibraryRepository(
          StampLibraryScope::Project,
          ProjectForgeLibraryRelativePath,
          ProjectCreationsRelativePath,
          false,
          std::move(transactionFileSystem))
{
}

bool StampProjectLibraryRepository::SetProjectRoot(const std::filesystem::path& projectRoot)
{
    ClearProjectRoot();
    if (projectRoot.empty()) return false;
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) return false;
    const std::filesystem::path assets = canonical / "Assets";
    const auto status = std::filesystem::symlink_status(assets, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) return false;
    const std::filesystem::path canonicalAssets = std::filesystem::weakly_canonical(assets, error);
    if (error || !IsPathWithin(canonicalAssets, canonical)) return false;
    SetValidatedRepositoryRoot(canonical);
    return true;
}

void StampProjectLibraryRepository::ClearProjectRoot() noexcept
{
    ClearRepositoryRoot();
}

const std::filesystem::path& StampProjectLibraryRepository::ProjectRoot() const noexcept
{
    return RepositoryRoot();
}

void StampFilesystemLibraryRepository::SetValidatedRepositoryRoot(
    std::filesystem::path root)
{
    repositoryRoot_ = std::move(root);
}

void StampFilesystemLibraryRepository::ClearRepositoryRoot() noexcept
{
    repositoryRoot_.clear();
}

const std::filesystem::path&
StampFilesystemLibraryRepository::RepositoryRoot() const noexcept
{
    return repositoryRoot_;
}

StampLibraryResult StampFilesystemLibraryRepository::EnsureCreationsDirectory() const
{
    if (repositoryRoot_.empty()) return Failure(StampLibraryError::NotConfigured);
    std::error_code error;
    std::vector<std::filesystem::path> directories;
    if (createRepositoryRoot_) directories.push_back(repositoryRoot_);
    const std::filesystem::path libraryParent =
        libraryRelativePath_.parent_path();
    if (!libraryParent.empty())
        directories.push_back(repositoryRoot_ / libraryParent);
    directories.push_back(repositoryRoot_ / libraryRelativePath_);
    directories.push_back(repositoryRoot_ / creationsRelativePath_);
    for (const std::filesystem::path& directory : directories)
    {
        const auto status = std::filesystem::symlink_status(directory, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return Failure(StampLibraryError::IoFailure, error.message());
        if (!std::filesystem::exists(status))
        {
            error.clear();
            if (!std::filesystem::create_directory(directory, error) || error)
                return Failure(StampLibraryError::IoFailure, error.message());
        }
        else if (std::filesystem::is_symlink(status))
        {
            return Failure(StampLibraryError::SymbolicLinkRejected);
        }
        else if (!std::filesystem::is_directory(status))
        {
            return Failure(StampLibraryError::IoFailure,
                "Stamp library component is not a directory.");
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
        if (error || canonical != directory.lexically_normal())
            return Failure(StampLibraryError::PathEscapesProjectLibrary);
    }
    return {};
}

StampLibraryResult StampFilesystemLibraryRepository::ResolvePath(
    const std::filesystem::path& relativePath,
    std::filesystem::path& absolute) const
{
    if (repositoryRoot_.empty()) return Failure(StampLibraryError::NotConfigured);
    if (!IsPortableRelativePath(relativePath) || LowerExtension(relativePath) != ".vfstamp")
        return Failure(StampLibraryError::InvalidReference);
    const std::filesystem::path normalized = relativePath.lexically_normal();
    const std::filesystem::path belowCreations =
        normalized.lexically_relative(creationsRelativePath_);
    if (belowCreations.empty() || belowCreations.is_absolute() || !IsPortableRelativePath(belowCreations))
        return Failure(StampLibraryError::PathEscapesProjectLibrary);
    absolute = (repositoryRoot_ / normalized).lexically_normal();
    if (!IsPathWithin(absolute, repositoryRoot_ / creationsRelativePath_))
        return Failure(StampLibraryError::PathEscapesProjectLibrary);
    return {};
}

StampLibraryResult StampFilesystemLibraryRepository::ResolvePortableReference(
    const std::filesystem::path& relativePath) const
{
    std::filesystem::path absolute;
    StampLibraryResult result = ResolvePath(relativePath, absolute);
    if (!result.Succeeded()) return result;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(absolute, error);
    if (error || !std::filesystem::exists(status)) return Failure(StampLibraryError::AssetNotFound);
    if (std::filesystem::is_symlink(status)) return Failure(StampLibraryError::SymbolicLinkRejected);
    if (!std::filesystem::is_regular_file(status)) return Failure(StampLibraryError::AssetNotRegularFile);
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
    if (error || !IsPathWithin(
            canonical, repositoryRoot_ / creationsRelativePath_))
        return Failure(StampLibraryError::PathEscapesProjectLibrary);
    return ReadPath(absolute, relativePath.lexically_normal());
}

StampLibraryResult StampFilesystemLibraryRepository::ReadPath(
    const std::filesystem::path& absolute,
    const std::filesystem::path& relative) const
{
    try
    {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(absolute, error);
    if (error || size > DefaultStampResourceLimits().HardFileBytes)
        return Failure(StampLibraryError::InvalidAsset);
    std::ifstream input(absolute, std::ios::binary);
    if (!input) return Failure(StampLibraryError::IoFailure);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) return Failure(StampLibraryError::IoFailure);
    const VfstampDecodeResult decoded = DecodeVfstampBytes(bytes);
    if (!decoded.IsSuccess())
        return Failure(StampLibraryError::InvalidAsset, std::string(decoded.Message));
    StampLibraryResult result{};
    result.Reference = {.Id = decoded.Stamp->Identity().Id,
                        .ContentHash = decoded.Stamp->Identity().ContentHash,
                        .RelativePath = relative,
                        .Scope = scope_};
    result.Stamp = std::move(*decoded.Stamp);
    return result;
    }
    catch (const std::bad_alloc&)
    {
        return Failure(StampLibraryError::AllocationFailure);
    }
}

StampLibraryResult StampFilesystemLibraryRepository::Read(
    const StampAssetReference& reference) const
{
    if (reference.Scope != scope_)
        return Failure(StampLibraryError::InvalidReference,
            "Stamp reference belongs to a different library scope.");
    std::filesystem::path absolute;
    StampLibraryResult resolved = ResolvePath(reference.RelativePath, absolute);
    if (!resolved.Succeeded()) return resolved;
    StampLibraryResult read = ResolvePortableReference(reference.RelativePath);
    if (!read.Succeeded()) return read;
    if (reference.Id.Value() != 0U && read.Reference.Id != reference.Id)
        return Failure(StampLibraryError::InvalidReference, "Stamp UUID does not match the portable reference.");
    if (!reference.ContentHash.empty() && read.Reference.ContentHash != reference.ContentHash)
        return Failure(StampLibraryError::InvalidReference, "Stamp content hash does not match the portable reference.");
    return read;
}

StampLibraryResult StampFilesystemLibraryRepository::Install(
    const VoxelStamp& stamp,
    const StampInstallOptions& options)
{
    try
    {
        const VfstampWriteResult written = WriteVfstampBytes(stamp);
        if (!written.IsSuccess())
            return Failure(StampLibraryError::SerializationFailed, std::string(written.Message));
        StampLibraryResult directory = EnsureCreationsDirectory();
        if (!directory.Succeeded()) return directory;
        const std::filesystem::path creations =
            repositoryRoot_ / creationsRelativePath_;
        const std::optional<std::string> stem = ValidFileStem(options.PreferredFileStem.empty()
            ? stamp.Identity().Id.ToString() : options.PreferredFileStem);
        if (!stem) return Failure(StampLibraryError::InvalidReference,
            "Stamp filename is empty, invalid UTF-8, forbidden by Windows or reserved.");
        std::filesystem::path destination = creations / Utf8Path(*stem + ".vfstamp");
        std::error_code error;
        if (!options.ReplaceExisting)
        {
            for (std::uint32_t suffix = 2U; std::filesystem::exists(destination, error); ++suffix)
            {
                if (error) return Failure(StampLibraryError::IoFailure, error.message());
                destination = creations / Utf8Path(*stem + "-" + std::to_string(suffix) + ".vfstamp");
            }
        }
        const std::filesystem::path temporary = StampTransactionTemporaryPath(destination);
        const std::filesystem::path backup = StampTransactionBackupPath(destination);
        for (const auto& transaction : {temporary, backup})
        {
            bool exists = false;
            bool symbolicLink = false;
            if (!InspectTransactionEntry(transaction, exists, symbolicLink, error))
                return Failure(StampLibraryError::IoFailure, error.message());
            if (symbolicLink) return Failure(StampLibraryError::SymbolicLinkRejected);
            if (exists)
                return Failure(StampLibraryError::StaleTransactionFile);
        }
        bool replacing = false;
        bool destinationSymlink = false;
        if (!InspectTransactionEntry(destination, replacing, destinationSymlink, error))
            return Failure(StampLibraryError::IoFailure, error.message());
        if (destinationSymlink) return Failure(StampLibraryError::SymbolicLinkRejected);
        if (replacing && !options.ReplaceExisting) return Failure(StampLibraryError::IoFailure);
        if (replacing && !IsSafeRegularFile(destination, error))
            return Failure(std::filesystem::is_symlink(std::filesystem::symlink_status(destination, error))
                ? StampLibraryError::SymbolicLinkRejected : StampLibraryError::AssetNotRegularFile);
        if (!WriteAndFlush(temporary, written.Bytes)) return Failure(StampLibraryError::IoFailure);
        const std::filesystem::path relative =
            destination.lexically_relative(repositoryRoot_);
        StampLibraryResult verify = ReadPath(temporary, relative);
        if (!verify.Succeeded()) { static_cast<void>(RemoveRegularFile(temporary)); return Failure(StampLibraryError::InvalidAsset); }
        std::string operationError;
        if (replacing && !transactionFileSystem_->Rename(destination, backup, operationError))
        {
            static_cast<void>(RemoveRegularFile(temporary));
            return Failure(StampLibraryError::TransactionFailed, "Unable to back up existing Stamp: " + operationError);
        }
        if (!transactionFileSystem_->Rename(temporary, destination, operationError))
        {
            if (replacing && !transactionFileSystem_->Rename(backup, destination, operationError))
                return Failure(StampLibraryError::RollbackFailed);
            static_cast<void>(RemoveRegularFile(temporary));
            return Failure(StampLibraryError::TransactionFailed);
        }
        StampLibraryResult final = ReadPath(destination, relative);
        if (!final.Succeeded())
        {
            if (!replacing)
            {
                if (!RemoveRegularFile(destination)) return Failure(StampLibraryError::RollbackFailed);
                return Failure(StampLibraryError::TransactionFailed);
            }
            if (replacing && !transactionFileSystem_->Rename(destination, temporary, operationError))
                return Failure(StampLibraryError::RollbackFailed);
            if (replacing && !transactionFileSystem_->Rename(backup, destination, operationError))
                return Failure(StampLibraryError::RollbackFailed);
            static_cast<void>(RemoveRegularFile(temporary));
            return Failure(StampLibraryError::TransactionFailed);
        }
        if (replacing && !RemoveRegularFile(backup))
            return Failure(StampLibraryError::TransactionFailed, "Stamp installed but transaction backup cleanup failed.");
        final.Reference = {
            .Id = stamp.Identity().Id,
            .ContentHash = written.LogicalContentHash,
            .RelativePath = relative,
            .Scope = scope_};
        return final;
    }
    catch (const std::bad_alloc&) { return Failure(StampLibraryError::AllocationFailure); }
}

StampLibraryResult StampFilesystemLibraryRepository::EnumerateSourceAssets() const
{
    StampLibraryResult result{};
    if (repositoryRoot_.empty()) return Failure(StampLibraryError::NotConfigured);
    const std::filesystem::path creations =
        repositoryRoot_ / creationsRelativePath_;
    std::error_code error;
    bool creationsExists = false;
    bool creationsSymlink = false;
    if (!InspectTransactionEntry(creations, creationsExists, creationsSymlink, error))
        return Failure(StampLibraryError::IoFailure, error.message());
    if (creationsSymlink)
        return Failure(StampLibraryError::SymbolicLinkRejected);
    if (!creationsExists) return result;
    const auto creationsStatus = std::filesystem::symlink_status(creations, error);
    if (error || !std::filesystem::is_directory(creationsStatus))
        return Failure(StampLibraryError::IoFailure,
            "Stamp Creations path is not a directory.");
    const std::filesystem::path canonicalCreations = std::filesystem::weakly_canonical(creations, error);
    if (error || canonicalCreations != creations.lexically_normal())
        return Failure(StampLibraryError::PathEscapesProjectLibrary);
    for (std::filesystem::recursive_directory_iterator it(creations, error), end; it != end && !error; it.increment(error))
    {
        const auto status = it->symlink_status(error);
        if (error) break;
        const std::filesystem::path relative =
            it->path().lexically_relative(repositoryRoot_);
        if (std::filesystem::is_symlink(status))
        {
            result.Diagnostics.push_back({StampLibraryError::SymbolicLinkRejected, relative,
                std::string(StampLibraryErrorMessage(StampLibraryError::SymbolicLinkRejected))});
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        const std::string filename = it->path().filename().string();
        if (filename.ends_with(".install.tmp") || filename.ends_with(".install.bak"))
        {
            result.Diagnostics.push_back({StampLibraryError::StaleTransactionFile, relative,
                std::string(StampLibraryErrorMessage(StampLibraryError::StaleTransactionFile))});
            continue;
        }
        if (!std::filesystem::is_regular_file(status) || LowerExtension(it->path()) != ".vfstamp") continue;
        StampLibraryResult read = ReadPath(it->path(), relative);
        if (!read.Succeeded())
        {
            result.Diagnostics.push_back({read.Error, relative, read.Message});
            continue;
        }
        std::uintmax_t bytes = std::filesystem::file_size(it->path(), error);
        if (error) break;
        result.Assets.push_back({read.Reference, bytes});
    }
    if (error) return Failure(StampLibraryError::IoFailure, error.message());
    std::sort(result.Assets.begin(), result.Assets.end(), [](const StampLibraryAsset& left, const StampLibraryAsset& right) {
        return left.Reference.RelativePath.generic_string() < right.Reference.RelativePath.generic_string();
    });
    std::sort(result.Diagnostics.begin(), result.Diagnostics.end(),
        [](const StampLibraryDiagnostic& left, const StampLibraryDiagnostic& right) {
            if (left.RelativePath != right.RelativePath)
                return left.RelativePath.generic_string() < right.RelativePath.generic_string();
            if (left.Code != right.Code) return left.Code < right.Code;
            return left.Message < right.Message;
        });
    return result;
}

StampLibraryResult StampFilesystemLibraryRepository::RebuildSourceInventory() const
{
    // STAMP-07 owns the derived catalogue. This method deliberately scans and
    // validates source assets only; it never deletes or repairs them silently.
    return EnumerateSourceAssets();
}

StampLibraryResult StampFilesystemLibraryRepository::Remove(
    const StampAssetReference& reference)
{
    if (reference.Scope != scope_)
        return Failure(StampLibraryError::InvalidReference,
            "Stamp reference belongs to a different library scope.");
    if (reference.Id.Value() == 0U || reference.ContentHash.empty())
        return Failure(StampLibraryError::InvalidReference,
            "Removing a Project Stamp requires its UUID and content hash.");
    std::filesystem::path absolute;
    StampLibraryResult resolved = ResolvePath(reference.RelativePath, absolute);
    if (!resolved.Succeeded()) return resolved;
    StampLibraryResult read = ResolvePortableReference(reference.RelativePath);
    if (!read.Succeeded()) return read;
    if (read.Reference.Id != reference.Id)
        return Failure(StampLibraryError::InvalidReference, "Stamp UUID does not match the portable reference.");
    if (read.Reference.ContentHash != reference.ContentHash)
        return Failure(StampLibraryError::InvalidReference, "Stamp content hash does not match the portable reference.");
    std::error_code error;
    if (!IsSafeRegularFile(absolute, error)) return Failure(StampLibraryError::AssetNotRegularFile);
    if (!std::filesystem::remove(absolute, error) || error) return Failure(StampLibraryError::IoFailure, error.message());
    return {};
}

} // namespace VoxelForge::Editor::Stamps
