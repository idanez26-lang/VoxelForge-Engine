#include "VoxelStamps/Library/StampCatalogService.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <new>
#include <string_view>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] StampCatalogResult Failure(
    const StampCatalogError error,
    std::string message = {})
{
    return {.Error = error,
            .Message = message.empty() ? std::string(StampCatalogErrorMessage(error))
                                       : std::move(message)};
}

[[nodiscard]] std::string LowerAscii(const std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

[[nodiscard]] bool ContainsInsensitive(
    const std::string_view haystack,
    const std::string_view needle)
{
    return needle.empty() || LowerAscii(haystack).find(LowerAscii(needle)) != std::string::npos;
}

[[nodiscard]] bool EntryLess(const StampCatalogEntry& left, const StampCatalogEntry& right)
{
    if (left.Reference.Id.Value() != right.Reference.Id.Value())
        return left.Reference.Id.Value() < right.Reference.Id.Value();
    const auto asUtf8 = [](const std::filesystem::path& path) {
        const std::u8string value = path.lexically_normal().generic_u8string();
        std::string result;
        result.reserve(value.size());
        for (const char8_t character : value) result.push_back(static_cast<char>(character));
        return result;
    };
    return asUtf8(left.Reference.RelativePath) < asUtf8(right.Reference.RelativePath);
}

[[nodiscard]] std::string GenericUtf8Path(const std::filesystem::path& path)
{
    const std::u8string value = path.lexically_normal().generic_u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

[[nodiscard]] std::string FilenameUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.filename().u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

[[nodiscard]] std::string PortablePathKey(const std::filesystem::path& path)
{
    const std::string normalized = GenericUtf8Path(path);
    std::string result;
    result.reserve(normalized.size());
    for (const unsigned char character : normalized)
    {
        result.push_back(character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] StampCatalogDiagnostic FromLibraryDiagnostic(
    const StampLibraryDiagnostic& diagnostic)
{
    return {.Code = StampCatalogError::Invalid,
            .RelativePath = diagnostic.RelativePath,
            .Message = diagnostic.Message};
}

} // namespace

StampCatalogService::StampCatalogService(
    IStampLibraryRepository& sources,
    IStampCatalogStore& store)
    : sources_(sources), store_(store)
{
}

void StampCatalogService::InvalidateCache() noexcept
{
    cachedCatalogue_ = {};
    cacheValid_ = false;
}

void StampCatalogService::StoreCache(const StampCatalog& catalogue)
{
    // Copy before invalidating/replacing the cache. If allocation fails, callers
    // catch it and explicitly invalidate rather than serving a stale catalogue.
    StampCatalog replacement = catalogue;
    cachedCatalogue_ = std::move(replacement);
    cacheValid_ = true;
}

StampCatalogResult StampCatalogService::LoadCachedCatalogue()
{
    try
    {
        if (cacheValid_) return {.Catalog = cachedCatalogue_};
        StampCatalogResult result = store_.LoadCatalogue();
        if (result.Succeeded()) StoreCache(result.Catalog);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        InvalidateCache();
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampCatalogService::RebuildCatalogue()
{
    try
    {
        StampLibraryResult inventory = sources_.RebuildSourceInventory();
        if (!inventory.Succeeded())
            return Failure(StampCatalogError::IoFailure, inventory.Message);

        StampCatalog rebuilt;
        StampCatalogResult result;
        for (const StampLibraryDiagnostic& diagnostic : inventory.Diagnostics)
            result.Diagnostics.push_back(FromLibraryDiagnostic(diagnostic));

        struct SourceIdentity final
        {
            std::string ContentHash;
            std::string PortablePath;
        };
        std::map<std::uint64_t, SourceIdentity> identifiers;
        std::map<std::string, std::uint64_t> paths;
        rebuilt.Entries.reserve(inventory.Assets.size());
        for (const StampLibraryAsset& asset : inventory.Assets)
        {
            const SourceIdentity identity{
                .ContentHash = asset.Reference.ContentHash,
                .PortablePath = PortablePathKey(asset.Reference.RelativePath)};
            const auto [idIt, insertedId] = identifiers.emplace(asset.Reference.Id.Value(), identity);
            const auto [pathIt, insertedPath] = paths.emplace(identity.PortablePath, asset.Reference.Id.Value());
            if (!insertedId || !insertedPath)
            {
                result.Error = StampCatalogError::IdentityCollision;
                result.Message = StampCatalogErrorMessage(StampCatalogError::IdentityCollision);
                result.Diagnostics.push_back({
                    .Code = StampCatalogError::IdentityCollision,
                    .RelativePath = asset.Reference.RelativePath,
                    .Message = !insertedId
                        ? ((idIt->second.ContentHash != identity.ContentHash ||
                            idIt->second.PortablePath != identity.PortablePath)
                            ? "Two Project Library source assets expose one UUID with incompatible hash or portable path."
                            : "Project Library inventory contains a duplicate Stamp UUID entry.")
                        : "Two Project Library source assets expose the same portable path under Windows case-insensitive semantics."});
                return result;
            }

            StampLibraryResult read = sources_.Read(asset.Reference);
            if (!read.Succeeded() || !read.Stamp || read.Reference != asset.Reference)
            {
                result.Diagnostics.push_back({
                    .Code = StampCatalogError::Invalid,
                    .RelativePath = asset.Reference.RelativePath,
                    .Message = read.Succeeded()
                        ? "Source inventory identity no longer matches the decoded Stamp."
                        : read.Message});
                continue;
            }

            rebuilt.Entries.push_back({
                .Reference = read.Reference,
                .FileName = FilenameUtf8(read.Reference.RelativePath),
                .FileBytes = asset.FileBytes,
                .Dimensions = read.Stamp->Bounds().Dimensions,
                .VoxelCount = static_cast<std::uint64_t>(read.Stamp->Voxels().size()),
                .PaletteCount = static_cast<std::uint32_t>(read.Stamp->Palette().size())});
        }

        std::sort(rebuilt.Entries.begin(), rebuilt.Entries.end(), EntryLess);
        StampCatalogResult written = store_.WriteCatalogueAtomically(rebuilt);
        if (!written.Succeeded())
        {
            written.Diagnostics.insert(
                written.Diagnostics.end(), result.Diagnostics.begin(), result.Diagnostics.end());
            return written;
        }
        result.Catalog = written.Catalog;
        StoreCache(result.Catalog);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        InvalidateCache();
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampCatalogService::Query(const StampCatalogQuery& query)
{
    try
    {
        StampCatalogResult result = LoadCachedCatalogue();
        if (!result.Succeeded()) return result;

        result.Catalog.Entries.erase(
            std::remove_if(result.Catalog.Entries.begin(), result.Catalog.Entries.end(),
                [&query](const StampCatalogEntry& entry) {
                    if (query.Id && entry.Reference.Id != *query.Id) return true;
                    if (!query.ContentHash.empty() && entry.Reference.ContentHash != query.ContentHash)
                        return true;
                    if (!query.RelativePath.empty() && PortablePathKey(entry.Reference.RelativePath) !=
                        PortablePathKey(query.RelativePath))
                        return true;
                    return !ContainsInsensitive(entry.FileName, query.Text) &&
                           !ContainsInsensitive(GenericUtf8Path(entry.Reference.RelativePath), query.Text) &&
                           !ContainsInsensitive(entry.Reference.ContentHash, query.Text) &&
                           !ContainsInsensitive(entry.Reference.Id.ToString(), query.Text);
                }),
            result.Catalog.Entries.end());
        std::sort(result.Catalog.Entries.begin(), result.Catalog.Entries.end(), EntryLess);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampCatalogService::UpsertDerivedEntry(const StampCatalogEntry& entry)
{
    try
    {
        StampCatalogResult loaded = LoadCachedCatalogue();
        if (loaded.Error == StampCatalogError::Missing)
        {
            loaded = {};
        }
        if (!loaded.Succeeded()) return loaded;

        auto byId = std::find_if(loaded.Catalog.Entries.begin(), loaded.Catalog.Entries.end(),
            [&entry](const StampCatalogEntry& existing) {
                return existing.Reference.Id == entry.Reference.Id;
            });
        auto byPath = std::find_if(loaded.Catalog.Entries.begin(), loaded.Catalog.Entries.end(),
            [&entry](const StampCatalogEntry& existing) {
                return PortablePathKey(existing.Reference.RelativePath) ==
                    PortablePathKey(entry.Reference.RelativePath);
            });
        if (byPath != loaded.Catalog.Entries.end() && byPath != byId)
            return Failure(StampCatalogError::IdentityCollision,
                "A different Stamp UUID already owns the requested portable catalogue path.");
        if (byId == loaded.Catalog.Entries.end()) loaded.Catalog.Entries.push_back(entry);
        else *byId = entry;

        StampCatalogResult written = store_.WriteCatalogueAtomically(loaded.Catalog);
        if (written.Succeeded())
        {
            InvalidateCache();
            StoreCache(written.Catalog);
        }
        return written;
    }
    catch (const std::bad_alloc&)
    {
        InvalidateCache();
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampCatalogService::RemoveDerivedEntry(const Core::UUID& id)
{
    try
    {
        if (id.Value() == 0U)
            return Failure(StampCatalogError::Invalid, "A derived catalogue entry requires a non-zero Stamp UUID.");
        StampCatalogResult loaded = LoadCachedCatalogue();
        if (!loaded.Succeeded()) return loaded;
        loaded.Catalog.Entries.erase(
            std::remove_if(loaded.Catalog.Entries.begin(), loaded.Catalog.Entries.end(),
                [&id](const StampCatalogEntry& entry) { return entry.Reference.Id == id; }),
            loaded.Catalog.Entries.end());
        StampCatalogResult written = store_.WriteCatalogueAtomically(loaded.Catalog);
        if (written.Succeeded())
        {
            InvalidateCache();
            StoreCache(written.Catalog);
        }
        return written;
    }
    catch (const std::bad_alloc&)
    {
        InvalidateCache();
        return Failure(StampCatalogError::AllocationFailure);
    }
}

} // namespace VoxelForge::Editor::Stamps
