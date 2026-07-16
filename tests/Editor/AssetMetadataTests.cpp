#include "AssetBrowser/AssetBrowserViewModel.h"
#include "AssetBrowser/AssetDirectory.h"
#include "ModelImport/ModelAssetMetadataService.h"
#include "ModelImport/ModelImportService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;
namespace fs = std::filesystem;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        path_ = fs::temp_directory_path() /
            ("VoxelForgeAssetMetadata-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& Path() const noexcept { return path_; }
private:
    fs::path path_;
};

bool Check(const bool condition, const std::string& message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool WriteFile(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    return static_cast<bool>(output);
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

std::string Id(const unsigned int value)
{
    std::string result(32U, '0');
    const char digits[] = "0123456789abcdef";
    result[30] = digits[(value >> 4U) & 0xFU];
    result[31] = digits[value & 0xFU];
    return result;
}
}

int main()
{
    TemporaryDirectory temporary;
    const fs::path assets = temporary.Path() / "Project" / "Assets";
    const fs::path models = assets / "Models";
    fs::create_directories(models);
    unsigned int nextId = 1U;
    ModelAssetMetadataService service(
        [&nextId]() { return Id(nextId++); });
    if (!Check(service.SetModelsDirectory(models), "Models setup failed.")) return 1;

    const fs::path castle = models / "castle.vox";
    const fs::path tree = models / "tree.vox";
    WriteFile(castle, "castle");
    WriteFile(tree, "tree-content");
    const MetadataOperationResult created = service.CreateMetadata(castle);
    const fs::path castleMetadata = service.MetadataPathFor(castle);
    const std::string initialText = ReadFile(castleMetadata);
    if (!Check(created.Status == MetadataEnsureStatus::Created &&
            created.Metadata.AssetId == Id(1U) &&
            initialText.starts_with("# VoxelForge Asset Metadata\nformat_version=1\n") &&
            initialText.find("asset_type=voxel_model\n") != std::string::npos &&
            initialText.find("source_file=castle.vox\n") != std::string::npos &&
            initialText.find("source_extension=.vox\n") != std::string::npos &&
            initialText.find("importer=vox\n") != std::string::npos,
            "Metadata creation or deterministic format failed.")) return 1;

    const MetadataReadResult read = service.ReadMetadata(castleMetadata);
    const MetadataOperationResult unchanged = service.EnsureMetadata(castle);
    if (!Check(read.Succeeded && read.Metadata.AssetId == Id(1U) &&
            unchanged.Status == MetadataEnsureStatus::Unchanged &&
            ReadFile(castleMetadata) == initialText,
            "Read, stable id, or deterministic rewrite failed.")) return 1;

    const MetadataOperationResult treeCreated = service.CreateMetadata(tree);
    if (!Check(treeCreated.Succeeded() &&
            treeCreated.Metadata.AssetId != created.Metadata.AssetId,
            "Two assets received the same id.")) return 1;

    const fs::path unusual = models / "odd%=name.vox";
    WriteFile(unusual, "odd");
    const MetadataOperationResult unusualCreated = service.CreateMetadata(unusual);
    const MetadataReadResult unusualRead = service.ReadMetadata(
        service.MetadataPathFor(unusual));
    if (!Check(unusualCreated.Succeeded() && unusualRead.Succeeded &&
            unusualRead.Metadata.SourceFile == "odd%=name.vox",
            "Metadata escaping did not preserve a valid Windows filename.")) return 1;

    fs::remove(castleMetadata);
    const MetadataOperationResult recreated = service.EnsureMetadata(castle);
    if (!Check(recreated.Status == MetadataEnsureStatus::Created &&
            recreated.Metadata.AssetId != created.Metadata.AssetId,
            "Missing metadata was not recreated with a new id.")) return 1;

    std::string invalidText = ReadFile(castleMetadata);
    invalidText.replace(
        invalidText.find("format_version=1"),
        std::string("format_version=1").size(), "format_version=99");
    WriteFile(castleMetadata, invalidText);
    const std::string validIdBeforeRepair = recreated.Metadata.AssetId;
    const MetadataOperationResult repaired = service.EnsureMetadata(castle);
    if (!Check(repaired.Status == MetadataEnsureStatus::Repaired &&
            repaired.Metadata.AssetId == validIdBeforeRepair &&
            repaired.Metadata.FormatVersion == 1U,
            "Invalid version repair changed a valid id.")) return 1;

    WriteFile(castleMetadata, "broken metadata");
    const MetadataOperationResult malformedRepair = service.EnsureMetadata(castle);
    if (!Check(malformedRepair.Status == MetadataEnsureStatus::Repaired &&
            ModelAssetMetadataService::IsValidAssetId(
                malformedRepair.Metadata.AssetId),
            "Malformed metadata was not repaired.")) return 1;

    std::string invalidIdText = ReadFile(castleMetadata);
    const std::size_t assetIdStart = invalidIdText.find("asset_id=") + 9U;
    invalidIdText.replace(assetIdStart, 32U, "invalid");
    WriteFile(castleMetadata, invalidIdText);
    const MetadataOperationResult invalidIdRepair = service.EnsureMetadata(castle);
    if (!Check(invalidIdRepair.Status == MetadataEnsureStatus::Repaired &&
            ModelAssetMetadataService::IsValidAssetId(
                invalidIdRepair.Metadata.AssetId),
            "Invalid asset id was not regenerated.")) return 1;

    const fs::path outside = temporary.Path() / "outside.vox";
    WriteFile(outside, "outside");
    if (!Check(!service.EnsureMetadata(outside).Succeeded(),
            "Metadata escaped Assets/Models.")) return 1;

    std::error_code linkError;
    const fs::path link = models / "external.vox";
    fs::create_symlink(outside, link, linkError);
    if (linkError)
    {
        std::cout << "[SKIP] External symlink protection: "
                  << linkError.message() << '\n';
    }
    else if (!Check(!service.EnsureMetadata(link).Succeeded(),
                 "External symlink was accepted."))
    {
        return 1;
    }

    const fs::path failedModel = models / "failed.vox";
    WriteFile(failedModel, "failed");
    ModelAssetMetadataService failing(
        []() { return Id(90U); }, []() { return false; });
    if (!Check(failing.SetModelsDirectory(models),
            "Failing service setup failed.")) return 1;
    const MetadataOperationResult failed = failing.CreateMetadata(failedModel);
    if (!Check(!failed.Succeeded() &&
            !fs::exists(failedModel.string() + ".vfmeta") &&
            !fs::exists(failedModel.string() + ".vfmeta.tmp"),
            "Failed write left metadata or a temporary file.")) return 1;

    const fs::path emptyModels = temporary.Path() / "Empty" / "Assets" / "Models";
    ModelAssetMetadataService emptyService;
    if (!Check(emptyService.SetModelsDirectory(emptyModels),
            "Empty service setup failed.")) return 1;
    const MetadataRebuildReport emptyReport = emptyService.RebuildMetadata();
    if (!Check(emptyReport.Created == 0U && emptyReport.Errors == 0U,
            "Empty indexing failed.")) return 1;

    fs::remove(service.MetadataPathFor(tree));
    WriteFile(models / "notes.txt", "ignored");
    const MetadataRebuildReport indexed = service.RebuildMetadata();
    if (!Check(indexed.Created >= 1U && indexed.Unchanged >= 1U &&
            indexed.Ignored >= 1U && indexed.Errors == 0U,
            "Multiple/valid/missing metadata indexing failed.")) return 1;
    WriteFile(castleMetadata, "invalid");
    const MetadataRebuildReport repairedIndex = service.RebuildMetadata();
    if (!Check(repairedIndex.Repaired >= 1U,
            "Indexing did not repair invalid metadata.")) return 1;

    AssetBrowserViewModel viewModel;
    const std::vector<AssetEntry> entries{
        AssetEntry("castle.vox", castle, "Models/castle.vox",
            AssetEntryType::File, ".vox", 6U, std::nullopt),
        AssetEntry("castle.vox.vfmeta", castleMetadata,
            "Models/castle.vox.vfmeta", AssetEntryType::File,
            ".vfmeta", 10U, std::nullopt)};
    const auto visible = viewModel.VisibleEntries(entries);
    if (!Check(visible.size() == 1U && visible.front()->Name() == "castle.vox",
            "The Asset Browser exposes .vfmeta files.")) return 1;

    AssetDirectory directory;
    if (!Check(directory.SetAssetsRoot(assets), "Asset root setup failed.")) return 1;
    const MetadataReadResult beforeRename = service.ReadMetadata(castleMetadata);
    const AssetOperationResult renamed = directory.RenameEntry(
        fs::path("Models") / "castle.vox", "fortress.vox");
    const fs::path fortress = models / "fortress.vox";
    const MetadataReadResult afterRename = service.ReadMetadata(
        service.MetadataPathFor(fortress));
    if (!Check(renamed.Succeeded && !fs::exists(castleMetadata) &&
            afterRename.Succeeded &&
            afterRename.Metadata.AssetId == beforeRename.Metadata.AssetId &&
            afterRename.Metadata.SourceFile == "fortress.vox",
            "Browser rename did not preserve and update the companion.")) return 1;
    const AssetOperationResult deleted = directory.DeleteEntry(
        fs::path("Models") / "fortress.vox");
    if (!Check(deleted.Succeeded && !fs::exists(fortress) &&
            !fs::exists(service.MetadataPathFor(fortress)),
            "Browser delete did not remove the companion.")) return 1;

    const fs::path importProject = temporary.Path() / "ImportProject";
    const fs::path sources = temporary.Path() / "Sources";
    fs::create_directories(importProject / "Assets");
    WriteFile(sources / "one.vox", "one");
    WriteFile(sources / "two.vox", "two");
    unsigned int importId = 100U;
    ModelImportService importer([&importId]() { return Id(importId++); });
    std::size_t refreshCount = 0U;
    importer.SetRefreshCallback([&refreshCount]() { ++refreshCount; });
    if (!Check(importer.SetProjectRoot(importProject), "Import setup failed.")) return 1;
    const auto batch = importer.ImportModels(
        {sources / "one.vox", sources / "two.vox"});
    const fs::path importedOne = importer.ModelsDirectory() / "one.vox";
    const MetadataReadResult oneMetadata = importer.ReadMetadataForModel(importedOne);
    if (!Check(batch.size() == 2U && batch[0].Succeeded() && batch[1].Succeeded() &&
            oneMetadata.Succeeded && refreshCount == 1U,
            "Import metadata, multiple import, or single refresh failed.")) return 1;
    const std::string originalId = oneMetadata.Metadata.AssetId;
    WriteFile(sources / "one.vox", "one replaced");
    if (!Check(importer.ImportModel(
            sources / "one.vox", ModelImportCollisionAction::Replace).Succeeded() &&
            importer.ReadMetadataForModel(importedOne).Metadata.AssetId == originalId,
            "Replace did not preserve the asset id.")) return 1;
    fs::remove(importer.ModelsDirectory() / "one.vox.vfmeta");
    if (!Check(importer.ImportModel(
            sources / "one.vox", ModelImportCollisionAction::Replace).Succeeded() &&
            importer.ReadMetadataForModel(importedOne).Succeeded,
            "Replace did not recreate missing metadata.")) return 1;
    WriteFile(importer.ModelsDirectory() / "one.vox.vfmeta", "invalid");
    if (!Check(importer.ImportModel(
            sources / "one.vox", ModelImportCollisionAction::Replace).Succeeded() &&
            importer.ReadMetadataForModel(importedOne).Succeeded,
            "Replace did not repair invalid metadata.")) return 1;
    const auto collisionRename = importer.ImportModel(
        sources / "one.vox", ModelImportCollisionAction::Rename);
    if (!Check(collisionRename.Succeeded() &&
            importer.ReadMetadataForModel(collisionRename.DestinationPath).Succeeded &&
            importer.ReadMetadataForModel(collisionRename.DestinationPath)
                .Metadata.AssetId != originalId,
            "Collision rename did not create distinct metadata.")) return 1;
    const std::size_t filesBeforeNoChange = std::distance(
        fs::directory_iterator(importer.ModelsDirectory()),
        fs::directory_iterator{});
    static_cast<void>(importer.ImportModel(
        sources / "one.vox", ModelImportCollisionAction::Skip));
    static_cast<void>(importer.ImportModel(
        sources / "one.vox", ModelImportCollisionAction::Cancel));
    const std::size_t filesAfterNoChange = std::distance(
        fs::directory_iterator(importer.ModelsDirectory()),
        fs::directory_iterator{});
    if (!Check(filesBeforeNoChange == filesAfterNoChange,
            "Skip or cancel left partial files.")) return 1;
    const std::size_t beforeRebuildRefresh = refreshCount;
    static_cast<void>(importer.RebuildMetadata());
    if (!Check(refreshCount == beforeRebuildRefresh + 1U,
            "Metadata rebuild did not refresh exactly once.")) return 1;

    const fs::path rollbackProject = temporary.Path() / "RollbackProject";
    fs::create_directories(rollbackProject / "Assets");
    ModelImportService rollbackImporter(
        []() { return Id(200U); }, []() { return false; });
    if (!Check(rollbackImporter.SetProjectRoot(rollbackProject),
            "Rollback importer setup failed.")) return 1;
    const ModelImportResult rollback =
        rollbackImporter.ImportModel(sources / "one.vox");
    if (!Check(!rollback.Succeeded() &&
            !fs::exists(rollbackImporter.ModelsDirectory() / "one.vox") &&
            !fs::exists(rollbackImporter.ModelsDirectory() / "one.vox.vfmeta") &&
            !fs::exists(rollbackImporter.ModelsDirectory() / "one.vox.import.tmp"),
            "Failed metadata creation did not roll back the imported model.")) return 1;
    return 0;
}
