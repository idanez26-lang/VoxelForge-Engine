#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Library/StampJsonCatalogStore.h"
#include "VoxelStamps/Library/StampLibraryPaths.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Format/VfstampWriter.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
        root_ = fs::temp_directory_path() / ("VoxelForgeStampCatalog-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(fs::create_directories(root_ / "Assets"), "Unable to create isolated Stamp catalogue project.");
    }

    ~TemporaryProject()
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] const fs::path& Root() const noexcept { return root_; }

private:
    fs::path root_;
};

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    for (std::size_t index = 0U; index < text.size(); ++index)
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(text[index]));
    return bytes;
}

[[nodiscard]] fs::path Utf8Path(const std::string_view text)
{
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    return fs::path(std::u8string(first, first + text.size()));
}

void WriteText(const fs::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "Unable to create isolated catalogue fixture.");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    Require(static_cast<bool>(output), "Unable to write isolated catalogue fixture.");
}

[[nodiscard]] std::string ReadText(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(static_cast<bool>(input), "Unable to read isolated catalogue fixture.");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] StampCatalogEntry Entry(
    const std::uint64_t id,
    const std::string_view fileName = "stone.vfstamp")
{
    StampCatalogEntry entry;
    entry.Reference.Id = Core::UUID{id};
    const std::string decimal = std::to_string(id);
    entry.Reference.ContentHash = std::string(16U - decimal.size(), '0') + decimal;
    entry.Reference.RelativePath = ProjectCreationsRelativePath / Utf8Path(fileName);
    entry.FileName = std::string(fileName);
    entry.FileBytes = 128U + id;
    entry.Dimensions = {.X = 2U, .Y = 3U, .Z = 4U};
    entry.VoxelCount = 11U;
    entry.PaletteCount = 2U;
    return entry;
}

[[nodiscard]] VoxelStamp Stamp(const std::uint64_t id, const std::uint8_t red)
{
    StampValidationResult validation;
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
        {{.Position = {}, .LocalColorId = 0U}}, DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return *stamp;
}

struct Source final
{
    StampAssetReference Reference;
    VoxelStamp Value;
    std::uintmax_t FileBytes = 0U;
    bool Readable = true;

    Source(StampAssetReference reference, VoxelStamp value, const std::uintmax_t fileBytes)
        : Reference(std::move(reference)), Value(std::move(value)), FileBytes(fileBytes)
    {
    }
};

[[nodiscard]] Source MakeSource(
    const std::uint64_t id,
    const std::string_view name,
    const std::uint8_t red)
{
    VoxelStamp stamp = Stamp(id, red);
    return {{.Id = stamp.Identity().Id,
             .ContentHash = CalculateVfstampLogicalContentHash(stamp),
             .RelativePath = ProjectCreationsRelativePath / std::string(name)},
            std::move(stamp), 512U + id};
}

class CountingRepository final : public IStampLibraryRepository
{
public:
    std::vector<Source> Sources;
    unsigned RebuildCalls = 0U;
    unsigned ReadCalls = 0U;
    unsigned EnumerateCalls = 0U;

    [[nodiscard]] StampLibraryResult Install(const VoxelStamp&, const StampInstallOptions&) override
    {
        return {.Error = StampLibraryError::InvalidReference, .Message = "Install is not part of this catalogue fake."};
    }

    [[nodiscard]] StampLibraryResult Read(const StampAssetReference& reference) const override
    {
        ++const_cast<CountingRepository*>(this)->ReadCalls;
        const auto source = std::find_if(Sources.begin(), Sources.end(), [&reference](const Source& item) {
            return item.Reference == reference;
        });
        if (source == Sources.end() || !source->Readable)
            return {.Error = StampLibraryError::InvalidAsset, .Message = "Fixture source is unreadable."};
        return {.Reference = source->Reference, .Stamp = source->Value};
    }

    [[nodiscard]] StampLibraryResult EnumerateSourceAssets() const override
    {
        ++const_cast<CountingRepository*>(this)->EnumerateCalls;
        StampLibraryResult result;
        for (const Source& source : Sources)
            result.Assets.push_back({.Reference = source.Reference, .FileBytes = source.FileBytes});
        return result;
    }

    [[nodiscard]] StampLibraryResult Remove(const StampAssetReference&) override
    {
        return {.Error = StampLibraryError::InvalidReference, .Message = "Remove is not part of this catalogue fake."};
    }

    [[nodiscard]] StampLibraryResult ResolvePortableReference(const fs::path&) const override
    {
        return {.Error = StampLibraryError::InvalidReference, .Message = "Resolve is not part of this catalogue fake."};
    }

    [[nodiscard]] StampLibraryResult RebuildSourceInventory() const override
    {
        ++const_cast<CountingRepository*>(this)->RebuildCalls;
        return EnumerateSourceAssets();
    }
};

class CountingStore final : public IStampCatalogStore
{
public:
    StampCatalog Catalogue;
    bool Missing = false;
    unsigned LoadCalls = 0U;
    unsigned WriteCalls = 0U;

    [[nodiscard]] StampCatalogResult LoadCatalogue() const override
    {
        ++const_cast<CountingStore*>(this)->LoadCalls;
        if (Missing) return {.Error = StampCatalogError::Missing, .Message = "Fixture catalogue is missing."};
        return {.Catalog = Catalogue};
    }

    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(const StampCatalog& catalogue) override
    {
        ++WriteCalls;
        std::vector<std::byte> bytes;
        StampCatalogResult validation = StampJsonCatalogStore::SerializeToMemory(catalogue, bytes);
        if (!validation.Succeeded()) return validation;
        Catalogue = validation.Catalog;
        Missing = false;
        return {.Catalog = Catalogue};
    }
};

class FailingPublishFileSystem final : public IStampCatalogTransactionFileSystem
{
public:
    bool FailPublish = false;

    [[nodiscard]] bool Rename(const fs::path& source, const fs::path& destination, std::string& error) override
    {
        if (FailPublish && source.filename() == "ForgeCatalog.json.tmp")
        {
            FailPublish = false;
            error = "Injected catalogue publication failure.";
            return false;
        }
        std::error_code filesystemError;
        fs::rename(source, destination, filesystemError);
        if (!filesystemError) return true;
        error = filesystemError.message();
        return false;
    }
};

void TestMemorySchemaBoundariesAndDeterminism()
{
    StampCatalog unordered;
    unordered.Entries = {Entry(20U, "zeta.vfstamp"), Entry(10U, "alpha.vfstamp")};
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    Require(StampJsonCatalogStore::SerializeToMemory(unordered, first).Succeeded() &&
                StampJsonCatalogStore::SerializeToMemory(unordered, second).Succeeded() && first == second,
        "Equivalent V1 catalogues must serialize byte-for-byte deterministically.");
    const StampCatalogResult decoded = StampJsonCatalogStore::DeserializeFromMemory(first);
    Require(decoded.Succeeded() && decoded.Catalog.Entries.size() == 2U &&
                decoded.Catalog.Entries[0].Reference.Id == Core::UUID{10U} &&
                decoded.Catalog.Entries[1].Reference.Id == Core::UUID{20U},
        "Canonical catalogue serialization must impose a stable UUID/path ordering.");

    const std::string utf8Name{"rocher-\xC3\xA9.vfstamp"};
    StampCatalog utf8Catalogue{.Entries = {Entry(21U, utf8Name)}};
    std::vector<std::byte> utf8First;
    std::vector<std::byte> utf8Second;
    const StampCatalogResult utf8Decoded =
        StampJsonCatalogStore::SerializeToMemory(utf8Catalogue, utf8First);
    Require(utf8Decoded.Succeeded() && utf8Decoded.Catalog.Entries.size() == 1U &&
                utf8Decoded.Catalog.Entries[0].FileName == utf8Name &&
                utf8Decoded.Catalog.Entries[0].Reference.RelativePath.filename().u8string() ==
                    Utf8Path(utf8Name).filename().u8string() &&
                StampJsonCatalogStore::SerializeToMemory(utf8Decoded.Catalog, utf8Second).Succeeded() &&
                utf8First == utf8Second,
        "UTF-8 catalogue names and paths must survive canonical serialize/read/serialize byte-for-byte.");
    CountingRepository utf8Repository;
    CountingStore utf8Store;
    utf8Store.Catalogue = utf8Decoded.Catalog;
    StampCatalogService utf8Service(utf8Repository, utf8Store);
    Require(utf8Service.Query({.Text = "\xC3\xA9"}).Succeeded() &&
                utf8Service.Query({.Text = "\xC3\xA9"}).Catalog.Entries.size() == 1U,
        "UTF-8 filename text must remain searchable without a filesystem scan.");

    const std::vector<std::byte> empty = Bytes("{\"version\":1,\"entries\":[]}");
    Require(StampJsonCatalogStore::DeserializeFromMemory(empty).Succeeded(),
        "A structurally valid empty V1 catalogue must be accepted.");
    Require(!StampJsonCatalogStore::DeserializeFromMemory({}).Succeeded(),
        "An empty JSON byte stream must be rejected.");
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes("{\"version\":2,\"entries\":[]}"))
                .Error == StampCatalogError::UnsupportedVersion,
        "An unknown catalogue major version must be rejected explicitly.");
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes(
                "{\"version\":1,\"futureCompatible\":true,\"entries\":[]}"))
                .Succeeded(),
        "Compatible unknown fields must not break V1 catalogue loading.");
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes("{\"version\":\"1\",\"entries\":[]}"))
                .Error == StampCatalogError::Invalid,
        "Invalid JSON field types must be rejected.");
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes("{\"version\":1,\"entries\":["))
                .Error == StampCatalogError::Invalid &&
                StampJsonCatalogStore::DeserializeFromMemory(Bytes(
                    "{\"version\":1,\"version\":1,\"entries\":[]}"))
                    .Error == StampCatalogError::Invalid,
        "Truncated JSON and duplicate JSON keys must be rejected deterministically.");

    std::string hugeString = "{\"version\":1,\"future\":\"";
    hugeString.append(4097U, 'x');
    hugeString += "\",\"entries\":[]}";
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes(hugeString)).Error == StampCatalogError::Invalid,
        "Excessive JSON strings must be bounded before they can become catalogue data.");
    std::string hugeArray = "{\"version\":1,\"entries\":[";
    for (std::size_t index = 0U; index <= StampCatalogMaximumEntries; ++index)
    {
        if (index != 0U) hugeArray.push_back(',');
        hugeArray += "null";
    }
    hugeArray += "]}";
    Require(StampJsonCatalogStore::DeserializeFromMemory(Bytes(hugeArray)).Error == StampCatalogError::Invalid,
        "Excessive JSON arrays must be rejected by the configured V1 bound.");

    const StampCatalogEntry valid = Entry(31U);
    StampCatalog invalidPath{.Entries = {valid}};
    invalidPath.Entries[0].Reference.RelativePath = fs::path{"C:/Users/tony/Library/stone.vfstamp"};
    Require(!StampJsonCatalogStore::SerializeToMemory(invalidPath, first).Succeeded(),
        "Absolute paths must be refused before catalogue bytes are produced.");
    invalidPath.Entries[0].Reference.RelativePath = fs::path{"Assets/ForgeLibrary/Creations/../../escape.vfstamp"};
    Require(!StampJsonCatalogStore::SerializeToMemory(invalidPath, first).Succeeded(),
        "Portable path traversal must be refused before catalogue bytes are produced.");
    invalidPath.Entries[0].Reference.RelativePath = fs::path{"My Library/stone.vfstamp"};
    Require(!StampJsonCatalogStore::SerializeToMemory(invalidPath, first).Succeeded(),
        "A My Library path must never be accepted by the Project Library catalogue.");
}

void TestEmptyProjectRebuild(TemporaryProject& project)
{
    StampProjectLibraryRepository repository;
    StampJsonCatalogStore store;
    Require(repository.SetProjectRoot(project.Root()) && store.SetProjectRoot(project.Root()),
        "Empty project fixtures must configure both repository and catalogue store.");
    StampCatalogService service(repository, store);
    const StampCatalogResult rebuilt = service.RebuildCatalogue();
    Require(rebuilt.Succeeded() && rebuilt.Catalog.Entries.empty() && store.LoadCatalogue().Succeeded() &&
                store.LoadCatalogue().Catalog.Entries.empty(),
        "A Project Library with no source assets must rebuild to a valid empty derived catalogue.");
}

void TestFileStoreLoadTransactionsAndRollback(TemporaryProject& project)
{
    StampJsonCatalogStore store;
    Require(store.SetProjectRoot(project.Root()), "Catalogue store must configure a valid isolated project root.");
    Require(fs::weakly_canonical(store.Path()) ==
            fs::weakly_canonical(project.Root() / ProjectForgeLibraryRelativePath / "ForgeCatalog.json"),
        "Catalogue store must use the official portable Project Library catalogue path.");
    Require(store.LoadCatalogue().Error == StampCatalogError::Missing,
        "An absent project catalogue must report Missing without creating an asset.");
    const StampCatalog original{.Entries = {Entry(41U)}};
    Require(store.WriteCatalogueAtomically(original).Succeeded() && fs::exists(store.Path()),
        "Atomic catalogue write must publish a valid catalogue.");
    Require(store.LoadCatalogue().Catalog == original,
        "Published catalogue data must be readable through the same store.");

    const fs::path temporary = store.Path().string() + ".tmp";
    const fs::path backup = store.Path().string() + ".bak";
    WriteText(temporary, "transaction evidence");
    Require(store.LoadCatalogue().Error == StampCatalogError::StaleTransaction && fs::exists(temporary),
        "A stale temporary must be diagnosed and never silently deleted.");
    std::error_code error;
    fs::remove(temporary, error);
    WriteText(backup, "transaction evidence");
    Require(store.LoadCatalogue().Error == StampCatalogError::StaleTransaction && fs::exists(backup),
        "A stale backup must be diagnosed and never silently deleted.");
    fs::remove(backup, error);

    auto transactionFileSystem = std::make_shared<FailingPublishFileSystem>();
    StampJsonCatalogStore failingStore(transactionFileSystem);
    Require(failingStore.SetProjectRoot(project.Root()), "Failing store must configure the same isolated project root.");
    transactionFileSystem->FailPublish = true;
    const StampCatalog replacement{.Entries = {Entry(42U)}};
    Require(failingStore.WriteCatalogueAtomically(replacement).Error == StampCatalogError::TransactionFailed,
        "A simulated publication failure must report a transaction failure.");
    Require(failingStore.LoadCatalogue().Succeeded() && failingStore.LoadCatalogue().Catalog == original &&
                !fs::exists(temporary) && !fs::exists(backup),
        "Failed publication must restore the previous catalogue without transaction residue.");

    WriteText(store.Path(), "{not valid json");
    Require(store.LoadCatalogue().Error == StampCatalogError::Invalid,
        "Catalogue corruption must be rejected without touching source Stamp assets.");
}

void TestServiceCacheQueriesAndDerivedMutations()
{
    CountingRepository repository;
    repository.Sources = {MakeSource(51U, "wall.vfstamp", 10U), MakeSource(52U, "tree.vfstamp", 20U)};
    CountingStore store;
    StampCatalogService service(repository, store);
    const StampCatalogResult rebuilt = service.RebuildCatalogue();
    Require(rebuilt.Succeeded() && rebuilt.Catalog.Entries.size() == 2U && repository.RebuildCalls == 1U &&
                repository.EnumerateCalls == 1U && repository.ReadCalls == 2U && store.WriteCalls == 1U,
        "Catalogue rebuild must derive a complete catalogue exclusively through one repository inventory/read pass "
        "(entries=" + std::to_string(rebuilt.Catalog.Entries.size()) + ", rebuild=" +
        std::to_string(repository.RebuildCalls) + ", enumerate=" + std::to_string(repository.EnumerateCalls) +
        ", read=" + std::to_string(repository.ReadCalls) + ", writes=" + std::to_string(store.WriteCalls) + ").");
    const unsigned repositoryCalls = repository.RebuildCalls + repository.EnumerateCalls + repository.ReadCalls;
    const unsigned storeLoads = store.LoadCalls;
    const StampCatalogQuery uuidQuery{.Id = repository.Sources[0].Reference.Id};
    Require(service.Query(uuidQuery).Catalog.Entries.size() == 1U &&
                service.Query({.ContentHash = repository.Sources[1].Reference.ContentHash}).Catalog.Entries.size() == 1U &&
                service.Query({.RelativePath = repository.Sources[0].Reference.RelativePath}).Catalog.Entries.size() == 1U &&
                service.Query({.Text = "TREE"}).Catalog.Entries.size() == 1U &&
                service.Query({.Text = repository.Sources[0].Reference.Id.ToString()}).Catalog.Entries.size() == 1U,
        "In-memory queries must support UUID, hash, portable path and case-insensitive derivable text.");
    Require(repositoryCalls == repository.RebuildCalls + repository.EnumerateCalls + repository.ReadCalls &&
                storeLoads == store.LoadCalls,
        "Repeated Query calls must not rescan source assets or access the catalogue filesystem/store.");

    StampCatalogEntry update = rebuilt.Catalog.Entries[0];
    update.FileBytes += 1U;
    Require(service.UpsertDerivedEntry(update).Succeeded() && store.Catalogue.Entries.size() == 2U &&
                store.Catalogue.Entries[0].FileBytes == update.FileBytes,
        "Upsert must update an existing derived entry atomically without creating a duplicate.");
    const StampCatalogEntry added = Entry(53U, "arch.vfstamp");
    Require(service.UpsertDerivedEntry(added).Succeeded() && store.Catalogue.Entries.size() == 3U &&
                service.RemoveDerivedEntry(added.Reference.Id).Succeeded() && store.Catalogue.Entries.size() == 2U,
        "Upsert add and Remove must affect only catalogue entries and retain canonical order.");
}

void TestRebuildRecoveryAndCollisions(TemporaryProject& project)
{
    StampProjectLibraryRepository repository;
    Require(repository.SetProjectRoot(project.Root()), "Project repository must configure the isolated project.");
    const StampLibraryResult first = repository.Install(Stamp(61U, 10U), {.PreferredFileStem = "wall"});
    const StampLibraryResult second = repository.Install(Stamp(62U, 20U), {.PreferredFileStem = "tree"});
    Require(first.Succeeded() && second.Succeeded(), "Real source fixture installation failed.");
    StampJsonCatalogStore store;
    Require(store.SetProjectRoot(project.Root()), "Catalogue store must configure real source fixture project.");
    StampCatalogService service(repository, store);
    const StampCatalogResult initial = service.RebuildCatalogue();
    Require(initial.Succeeded() && initial.Catalog.Entries.size() == 2U,
        "A Project Library rebuild must index each valid installed .vfstamp exactly once.");
    const std::string firstBytes = ReadText(store.Path());
    std::error_code error;
    fs::remove(store.Path(), error);
    service.InvalidateCache();
    const StampCatalogResult rebuilt = service.RebuildCatalogue();
    Require(rebuilt.Succeeded() && rebuilt.Catalog == initial.Catalog && ReadText(store.Path()) == firstBytes,
        "Deleting the derived catalogue then rebuilding must recreate an equivalent deterministic index.");
    WriteText(store.Path(), "{corrupt catalogue");
    service.InvalidateCache();
    Require(service.RebuildCatalogue().Succeeded() && ReadText(store.Path()) == firstBytes,
        "A corrupt derived catalogue must be recoverable by rebuilding from intact source assets.");

    const StampLibraryResult third = repository.Install(Stamp(63U, 30U), {.PreferredFileStem = "rock"});
    Require(third.Succeeded(), "Added-source fixture installation failed.");
    Require(service.RebuildCatalogue().Catalog.Entries.size() == 3U,
        "Assets added outside the catalogue must appear after explicit rebuild.");
    Require(repository.Remove(second.Reference).Succeeded() && service.RebuildCatalogue().Catalog.Entries.size() == 2U,
        "Assets removed outside the catalogue must disappear after explicit rebuild.");

    const fs::path invalid = project.Root() / ProjectCreationsRelativePath / "invalid.vfstamp";
    WriteText(invalid, "bad source");
    const StampCatalogResult withInvalid = service.RebuildCatalogue();
    Require(withInvalid.Succeeded() && withInvalid.Catalog.Entries.size() == 2U &&
                std::any_of(withInvalid.Diagnostics.begin(), withInvalid.Diagnostics.end(),
                    [](const StampCatalogDiagnostic& diagnostic) {
                        return diagnostic.RelativePath.filename() == "invalid.vfstamp";
                    }),
        "Invalid source assets must be excluded with a rebuild diagnostic, not deleted.");
    Require(fs::exists(invalid) && fs::exists(project.Root() / first.Reference.RelativePath),
        "Catalogue rebuild, upsert and remove must never delete Project Library source assets.");

    CountingRepository collisionRepository;
    Source left = MakeSource(71U, "first.vfstamp", 1U);
    Source right = MakeSource(71U, "second.vfstamp", 2U);
    right.Reference.ContentHash = "00000000000000af";
    collisionRepository.Sources = {left, right};
    CountingStore collisionStore;
    StampCatalogService collisionService(collisionRepository, collisionStore);
    Require(collisionService.RebuildCatalogue().Error == StampCatalogError::IdentityCollision &&
                collisionStore.WriteCalls == 0U,
        "The same UUID with an incompatible path/hash must be rejected before a catalogue is published.");
}

} // namespace

int main()
{
    try
    {
        TestMemorySchemaBoundariesAndDeterminism();
        TemporaryProject emptyProject;
        TestEmptyProjectRebuild(emptyProject);
        TemporaryProject storeProject;
        TestFileStoreLoadTransactionsAndRollback(storeProject);
        TestServiceCacheQueriesAndDerivedMutations();
        TemporaryProject rebuildProject;
        TestRebuildRecoveryAndCollisions(rebuildProject);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
