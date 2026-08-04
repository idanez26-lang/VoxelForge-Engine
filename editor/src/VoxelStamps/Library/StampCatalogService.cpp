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
    cachedSearchText_.clear();
    cachedSearchBytes_ = 0U;
    cacheValid_ = false;
    ++metrics_.CacheInvalidations;
}

StampCatalogServiceMetrics StampCatalogService::Metrics() const noexcept
{
    StampCatalogServiceMetrics result = metrics_;
    result.SearchIndexEntries = cachedSearchText_.size();
    result.SearchIndexBytes = cachedSearchBytes_;
    return result;
}

void StampCatalogService::ResetMetrics() noexcept
{
    metrics_ = {};
}

void StampCatalogService::StoreCache(const StampCatalog& catalogue)
{
    // Copy before invalidating/replacing the cache. If allocation fails, callers
    // catch it and explicitly invalidate rather than serving a stale catalogue.
    StampCatalog replacement = catalogue;
    std::sort(replacement.Entries.begin(), replacement.Entries.end(), EntryLess);
    std::vector<std::string> searchText;
    searchText.reserve(replacement.Entries.size());
    std::size_t searchBytes = 0U;
    for (const StampCatalogEntry& entry : replacement.Entries)
    {
        const std::string path = GenericUtf8Path(entry.Reference.RelativePath);
        const std::string uuid = entry.Reference.Id.ToString();
        std::string searchable;
        searchable.reserve(entry.FileName.size() + path.size() +
            entry.Reference.ContentHash.size() + uuid.size() + 3U);
        searchable += LowerAscii(entry.FileName);
        searchable.push_back('\n');
        searchable += LowerAscii(path);
        searchable.push_back('\n');
        searchable += LowerAscii(entry.Reference.ContentHash);
        searchable.push_back('\n');
        searchable += LowerAscii(uuid);
        searchBytes += searchable.size();
        searchText.push_back(std::move(searchable));
    }
    cachedCatalogue_ = std::move(replacement);
    cachedSearchText_ = std::move(searchText);
    cachedSearchBytes_ = searchBytes;
    cacheValid_ = true;
}

StampCatalogResult StampCatalogService::LoadCachedCatalogue()
{
    try
    {
        if (cacheValid_)
        {
            ++metrics_.CacheHits;
            return {.Catalog = cachedCatalogue_};
        }
        ++metrics_.StoreLoads;
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
        ++metrics_.Rebuilds;
        ++metrics_.SourceInventoryScans;
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

            ++metrics_.SourceReads;
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
        ++metrics_.Queries;
        StampCatalogResult result;
        if (!cacheValid_)
        {
            ++metrics_.StoreLoads;
            StampCatalogResult loaded = store_.LoadCatalogue();
            if (!loaded.Succeeded())
            {
                return loaded;
            }
            StoreCache(loaded.Catalog);
            result.Diagnostics = std::move(loaded.Diagnostics);
        }
        else
        {
            ++metrics_.CacheHits;
        }

        result.Catalog.Version = cachedCatalogue_.Version;
        const std::string portablePath = query.RelativePath.empty()
            ? std::string{}
            : PortablePathKey(query.RelativePath);
        const std::string text = LowerAscii(query.Text);
        const bool broadQuery = !query.Id && query.ContentHash.empty() &&
            portablePath.empty() && text.empty();
        if (broadQuery)
        {
            result.Catalog.Entries.reserve(cachedCatalogue_.Entries.size());
        }
        for (std::size_t index = 0U;
             index < cachedCatalogue_.Entries.size(); ++index)
        {
            const StampCatalogEntry& entry = cachedCatalogue_.Entries[index];
            if (query.Id && entry.Reference.Id != *query.Id)
            {
                continue;
            }
            if (!query.ContentHash.empty() &&
                entry.Reference.ContentHash != query.ContentHash)
            {
                continue;
            }
            if (!portablePath.empty() &&
                PortablePathKey(entry.Reference.RelativePath) != portablePath)
            {
                continue;
            }
            if (!text.empty() &&
                cachedSearchText_[index].find(text) == std::string::npos)
            {
                continue;
            }
            result.Catalog.Entries.push_back(entry);
        }
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
