#include "AssetBrowser/AssetDirectory.h"
#include "AssetInspector/AssetInspectorViewModel.h"
#include "ModelImport/ModelImportService.h"
#include "Thumbnail/ThumbnailImage.h"
#include "Thumbnail/VoxThumbnailRenderer.h"
#include "Thumbnail/VoxThumbnailService.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;

int failures = 0;

void Check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendChunk(
    std::vector<std::uint8_t>& destination,
    const std::array<char, 4>& id,
    const std::vector<std::uint8_t>& content,
    const std::vector<std::uint8_t>& children = {})
{
    destination.insert(destination.end(), id.begin(), id.end());
    AppendU32(destination, static_cast<std::uint32_t>(content.size()));
    AppendU32(destination, static_cast<std::uint32_t>(children.size()));
    destination.insert(destination.end(), content.begin(), content.end());
    destination.insert(destination.end(), children.begin(), children.end());
}

bool WriteVox(
    const std::filesystem::path& path,
    const std::uint32_t sizeX = 3U,
    const std::uint32_t sizeY = 3U,
    const std::uint32_t sizeZ = 3U,
    const std::uint8_t color = 1U)
{
    std::vector<std::uint8_t> size;
    AppendU32(size, sizeX); AppendU32(size, sizeY); AppendU32(size, sizeZ);
    std::vector<std::uint8_t> xyzi;
    AppendU32(xyzi, 3U);
    xyzi.insert(xyzi.end(), {0U, 0U, 0U, color});
    xyzi.insert(xyzi.end(), {1U, 0U, 0U,
        static_cast<std::uint8_t>(color + 1U)});
    xyzi.insert(xyzi.end(), {0U, 1U, 1U,
        static_cast<std::uint8_t>(color + 2U)});
    std::vector<std::uint8_t> children;
    AppendChunk(children, {'S', 'I', 'Z', 'E'}, size);
    AppendChunk(children, {'X', 'Y', 'Z', 'I'}, xyzi);
    std::vector<std::uint8_t> bytes{'V', 'O', 'X', ' '};
    AppendU32(bytes, 150U);
    AppendChunk(bytes, {'M', 'A', 'I', 'N'}, {}, children);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

ThumbnailImage SolidImage(const std::uint8_t red)
{
    ThumbnailImage image;
    image.Width = VoxThumbnailWidth;
    image.Height = VoxThumbnailHeight;
    image.Pixels.resize(
        static_cast<std::size_t>(image.Width) * image.Height * 4U);
    for (std::size_t index = 0U; index < image.Pixels.size(); index += 4U)
    {
        image.Pixels[index] = red;
        image.Pixels[index + 1U] = 40U;
        image.Pixels[index + 2U] = 80U;
        image.Pixels[index + 3U] = 255U;
    }
    return image;
}

class ControlledRenderer final : public IVoxThumbnailRenderer
{
public:
    ThumbnailRenderResult Render(const std::filesystem::path&) override
    {
        ++Calls;
        if (Fail) return {false, {}, "simulated renderer failure"};
        return {true, SolidImage(Color), {}};
    }

    bool Fail = false;
    std::uint8_t Color = 120U;
    std::size_t Calls = 0U;
};

struct Fixture final
{
    Fixture()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("VoxelForgeThumbnailTests-" + std::to_string(unique));
        Models = Root / "Assets" / "Models";
        std::filesystem::create_directories(Models);
        std::filesystem::create_directories(Root / "Cache");
    }

    ~Fixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    std::filesystem::path Root;
    std::filesystem::path Models;
};

MetadataReadResult ReadMetadata(
    const Fixture& fixture,
    const std::filesystem::path& model)
{
    ModelAssetMetadataService service;
    static_cast<void>(service.SetModelsDirectory(fixture.Models));
    return service.ReadMetadata(service.MetadataPathFor(model));
}

void TestFramingAndRenderer()
{
    const std::vector<std::vector<VoxelForge::Asset::Vox::VoxDimensions>> cases{
        {{1U, 1U, 1U}}, {{64U, 2U, 2U}}, {{2U, 2U, 64U}},
        {{2U, 64U, 2U}}, {{17U, 5U, 31U}},
        {{3U, 4U, 5U}, {8U, 2U, 7U}}, {{2048U, 2048U, 2048U}}};
    for (const auto& dimensions : cases)
    {
        const ThumbnailFraming first = CalculateThumbnailFraming(dimensions);
        const ThumbnailFraming second = CalculateThumbnailFraming(dimensions);
        Check(first.IsFinite(), "framing must remain finite");
        Check(first == second, "framing must be deterministic");
        Check(first.Width == 256U && first.Height == 256U,
            "thumbnail framing must be 256 x 256");
        Check(first.Scale > 0.0F, "framing scale must be positive");
    }
    Check(!CalculateThumbnailFraming({}, 0U, 0U).IsFinite(),
        "zero-sized output must not produce finite framing");

    Fixture fixture;
    const auto model = fixture.Models / "deterministic.vox";
    Check(WriteVox(model, 7U, 4U, 9U), "write deterministic VOX");
    SoftwareVoxThumbnailRenderer renderer;
    const auto first = renderer.Render(model);
    const auto second = renderer.Render(model);
    Check(first.Succeeded && second.Succeeded,
        "software thumbnail renderer must succeed");
    Check(first.Image == second.Image,
        "software thumbnail pixels must be deterministic");
    Check(first.Image.Width == 256U && first.Image.Height == 256U,
        "software output must be 256 x 256");
}

void TestCacheLifecycle()
{
    Fixture fixture;
    const auto model = fixture.Models / "castle.vox";
    Check(WriteVox(model), "write cache lifecycle VOX");
    auto renderer = std::make_shared<ControlledRenderer>();
    VoxThumbnailService service(renderer);
    Check(service.SetProjectRoot(fixture.Root), "configure thumbnail service");
    Check(std::filesystem::is_directory(
        fixture.Root / "Cache" / "Thumbnails"),
        "Cache/Thumbnails must be created");
    Check(service.CachePathForAssetId("bad").empty(),
        "invalid asset id must be refused");
    Check(service.Describe(model).Status == ThumbnailStatus::Missing,
        "model without metadata must present a missing thumbnail");

    const auto generated = service.Generate(model);
    Check(generated.Status == ThumbnailGenerationStatus::Generated,
        "missing thumbnail must be generated");
    Check(std::filesystem::is_regular_file(generated.CachePath),
        "generated cache file must exist");
    Check(!std::filesystem::exists(generated.CachePath.string() + ".tmp") &&
        !std::filesystem::exists(generated.CachePath.string() + ".bak"),
        "transaction files must be cleaned");
    ThumbnailImage decoded;
    std::string error;
    Check(ReadThumbnailImage(generated.CachePath, decoded, error),
        "generated thumbnail must be readable");
    Check(service.Generate(model).Status ==
        ThumbnailGenerationStatus::Unchanged,
        "valid thumbnail must be reused");
    Check(renderer->Calls == 1U, "valid cache must avoid rerendering");

    const MetadataReadResult metadata = ReadMetadata(fixture, model);
    Check(metadata.Succeeded && metadata.Metadata.Thumbnail.has_value(),
        "thumbnail metadata must be saved");
    ModelAssetMetadataService metadataService;
    Check(metadataService.SetModelsDirectory(fixture.Models),
        "configure metadata for presentation states");
    ModelAssetMetadata presentationMetadata = metadata.Metadata;
    std::string metadataError;
    presentationMetadata.Thumbnail->Status = ThumbnailStatus::Generating;
    Check(metadataService.WriteMetadata(
        model, presentationMetadata, metadataError) &&
        service.Describe(model).Status == ThumbnailStatus::Generating,
        "generating presentation state must be preserved");
    presentationMetadata.Thumbnail->Status = ThumbnailStatus::Failed;
    presentationMetadata.Thumbnail->Error = "expected test failure";
    Check(metadataService.WriteMetadata(
        model, presentationMetadata, metadataError) &&
        service.Describe(model).Status == ThumbnailStatus::Failed,
        "failed presentation state must be preserved");
    presentationMetadata.Thumbnail->Status = ThumbnailStatus::Outdated;
    Check(metadataService.WriteMetadata(
        model, presentationMetadata, metadataError) &&
        service.Describe(model).Status == ThumbnailStatus::Outdated,
        "outdated presentation state must be preserved");
    Check(metadataService.WriteMetadata(model, metadata.Metadata, metadataError),
        "restore valid thumbnail metadata");
    ModelAssetMetadata stale = metadata.Metadata;
    stale.Thumbnail->SourceSize += 1U;
    Check(service.NeedsRegeneration(model, stale),
        "source size change must invalidate cache");
    stale = metadata.Metadata;
    stale.Thumbnail->SourceModifiedTime += 1;
    Check(service.NeedsRegeneration(model, stale),
        "source time change must invalidate cache");
    stale = metadata.Metadata;
    stale.Thumbnail->GeneratorVersion = 0U;
    Check(service.NeedsRegeneration(model, stale),
        "generator version change must invalidate cache");
    stale = metadata.Metadata;
    stale.Thumbnail->AnalysisVoxelCount += 1U;
    Check(service.NeedsRegeneration(model, stale),
        "analysis change must invalidate cache");

    const ThumbnailImage original = decoded;
    renderer->Fail = true;
    const auto failed = service.Generate(model, true);
    Check(failed.Status == ThumbnailGenerationStatus::Failed,
        "renderer failure must be reported");
    Check(ReadThumbnailImage(generated.CachePath, decoded, error) &&
        decoded == original,
        "old valid cache must survive failed replacement");

    renderer->Fail = false;
    renderer->Color = 200U;
    Check(service.Generate(model, true).Succeeded(),
        "explicit regeneration must recover from failure");
    Check(ReadThumbnailImage(generated.CachePath, decoded, error) &&
        decoded != original,
        "regeneration must replace thumbnail pixels");

    {
        std::ofstream truncated(generated.CachePath,
            std::ios::binary | std::ios::trunc);
        truncated << "VFTN";
    }
    const ThumbnailPresentation invalid = service.Describe(model);
    Check(invalid.Status == ThumbnailStatus::Outdated,
        "truncated thumbnail must be outdated");
    Check(service.Generate(model).Succeeded(),
        "invalid thumbnail must be regenerated");

    AssetDirectory directory;
    Check(directory.SetAssetsRoot(fixture.Root / "Assets"),
        "configure asset directory for rename and delete");
    const auto renamed = fixture.Models / "fortress.vox";
    const AssetOperationResult rename =
        directory.RenameEntry(model, "fortress.vox");
    Check(rename.Succeeded && std::filesystem::exists(generated.CachePath),
        "renaming a VOX must preserve its asset-id cache");
    const MetadataReadResult renamedMetadata = ReadMetadata(fixture, renamed);
    Check(renamedMetadata.Succeeded &&
        renamedMetadata.Metadata.AssetId == metadata.Metadata.AssetId &&
        service.Describe(renamed).Status == ThumbnailStatus::Valid,
        "rename must preserve asset id and valid thumbnail state");
    const AssetOperationResult deletion = directory.DeleteEntry(renamed);
    Check(deletion.Succeeded && !std::filesystem::exists(generated.CachePath),
        "deleting a VOX must remove its thumbnail cache");
    Check(service.RemoveByAssetId(metadata.Metadata.AssetId, error),
        "deleting an already absent thumbnail must succeed");
}

void TestOrphansAndPathSafety()
{
    Fixture fixture;
    VoxThumbnailService service;
    Check(service.SetProjectRoot(fixture.Root), "configure orphan service");
    const auto orphan = service.CacheDirectory() /
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.vfthumb";
    const auto unrelated = service.CacheDirectory() / "notes.vfthumb";
    std::string error;
    Check(WriteThumbnailImage(orphan, SolidImage(10U), error),
        "write recognized orphan");
    Check(WriteThumbnailImage(unrelated, SolidImage(20U), error),
        "write unrelated cache file");
    const ThumbnailRebuildReport report = service.Rebuild();
    Check(report.RemovedOrphans == 1U && !std::filesystem::exists(orphan),
        "rebuild must remove recognized orphan");
    Check(std::filesystem::exists(unrelated),
        "rebuild must preserve unrecognized file");
    Check(!service.RemoveByAssetId("../outside", error),
        "external asset id must be refused");

    const auto linkedRoot = fixture.Root / "LinkedProject";
    const auto external = fixture.Root.parent_path() /
        (fixture.Root.filename().string() + "-external");
    std::filesystem::create_directories(linkedRoot / "Assets" / "Models");
    std::filesystem::create_directories(linkedRoot / "Cache");
    std::filesystem::create_directories(external);
    std::error_code linkError;
    std::filesystem::create_directory_symlink(
        external, linkedRoot / "Cache" / "Thumbnails", linkError);
    if (!linkError)
    {
        VoxThumbnailService linkedService;
        Check(!linkedService.SetProjectRoot(linkedRoot),
            "external thumbnail directory symlink must be refused");
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(external, cleanupError);
}

void TestImportAndPresentation()
{
    Fixture fixture;
    const auto sourceDirectory = fixture.Root / "Sources";
    std::filesystem::create_directories(sourceDirectory);
    const auto first = sourceDirectory / "first.vox";
    const auto second = sourceDirectory / "second.vox";
    Check(WriteVox(first, 3U, 4U, 5U, 2U), "write first import VOX");
    Check(WriteVox(second, 8U, 2U, 3U, 8U), "write second import VOX");
    std::size_t refreshes = 0U;
    ModelImportService importer;
    Check(importer.SetProjectRoot(fixture.Root), "configure importer");
    importer.SetRefreshCallback([&refreshes]() { ++refreshes; });
    const auto results = importer.ImportModels({first, second});
    Check(results.size() == 2U && results[0].Succeeded() &&
        results[1].Succeeded(), "multiple imports must succeed");
    Check(refreshes == 1U, "multiple import must refresh once");
    Check(results[0].ThumbnailStatus == ThumbnailGenerationStatus::Generated &&
        results[1].ThumbnailStatus == ThumbnailGenerationStatus::Generated,
        "imports must generate thumbnails");
    VoxThumbnailService rebuildService;
    Check(rebuildService.SetProjectRoot(fixture.Root),
        "configure rebuild service");
    const ThumbnailRebuildReport rebuild = rebuildService.Rebuild();
    Check(rebuild.Generated == 0U && rebuild.Unchanged == 2U &&
        rebuild.Failed == 0U,
        "global rebuild must preserve valid caches");

    AssetInspectorViewModel inspector;
    Check(inspector.SetAssetsRoot(fixture.Root / "Assets"),
        "configure inspector");
    const auto importedPath = fixture.Models / "first.vox";
    AssetEntry entry("first.vox", importedPath,
        std::filesystem::path("Models") / "first.vox",
        AssetEntryType::File, ".vox",
        std::filesystem::file_size(importedPath),
        std::filesystem::last_write_time(importedPath));
    Check(inspector.UpdateSelection(entry), "select thumbnail asset");
    Check(inspector.State().ThumbnailStatus == "valid" &&
        inspector.State().ThumbnailResolution == "256 x 256" &&
        inspector.State().ThumbnailGeneratorVersion == "1" &&
        inspector.State().CanRevealThumbnail,
        "inspector must present ready thumbnail");
    Check(inspector.RegenerateThumbnail(),
        "inspector regenerate action must succeed");
    inspector.ClearProject();
    Check(inspector.State().Kind == AssetInspectorKind::None,
        "closing project must clear thumbnail presentation");

    auto failingRenderer = std::make_shared<ControlledRenderer>();
    failingRenderer->Fail = true;
    Fixture failingFixture;
    const auto failedSource = failingFixture.Root / "failed.vox";
    Check(WriteVox(failedSource), "write failing-thumbnail source");
    ModelImportService failingImporter({}, {}, failingRenderer);
    Check(failingImporter.SetProjectRoot(failingFixture.Root),
        "configure failing thumbnail importer");
    const ModelImportResult failedThumbnail =
        failingImporter.ImportModel(failedSource);
    Check(failedThumbnail.Succeeded(),
        "thumbnail failure must not fail valid model import");
    Check(failedThumbnail.ThumbnailStatus == ThumbnailGenerationStatus::Failed,
        "thumbnail failure must be reported separately");
    const MetadataReadResult failedMetadata =
        ReadMetadata(failingFixture, failedThumbnail.DestinationPath);
    Check(failedMetadata.Succeeded,
        "thumbnail failure must still save asset metadata");
    Check(!std::filesystem::exists(
        failingFixture.Root / "Cache" / "Thumbnails" /
        (failedMetadata.Metadata.AssetId + VoxThumbnailExtension)),
        "failed thumbnail import must not leave a partial cache");

    Fixture invalidFixture;
    const auto invalidSource = invalidFixture.Root / "invalid.vox";
    {
        std::ofstream invalid(invalidSource, std::ios::binary);
        invalid << "not a vox file";
    }
    ModelImportService invalidImporter;
    Check(invalidImporter.SetProjectRoot(invalidFixture.Root),
        "configure invalid VOX importer");
    const ModelImportResult invalidResult =
        invalidImporter.ImportModel(invalidSource);
    Check(!invalidResult.Succeeded(),
        "invalid VOX import must fail before thumbnail generation");
    Check(std::filesystem::is_empty(
        invalidFixture.Root / "Cache" / "Thumbnails"),
        "invalid VOX must not create a thumbnail");
}
}

int RunVoxThumbnailTests()
{
    TestFramingAndRenderer();
    TestCacheLifecycle();
    TestOrphansAndPathSafety();
    TestImportAndPresentation();
    if (failures != 0)
        std::cerr << failures << " thumbnail test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
