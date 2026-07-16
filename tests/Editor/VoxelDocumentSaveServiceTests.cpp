#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelSave/VoxelDocumentSaveService.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

std::vector<std::uint8_t> ReadBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

Asset::Vox::VoxModel Source()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = {{{8U, 8U, 8U}, {
        {0U, 0U, 0U, 2U}, {1U, 1U, 1U, 3U}}}};
    source.DeclaredModelCount = 1U;
    return source;
}

Asset::Voxel::VoxelDocument BuildDocument(const fs::path& sourcePath)
{
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        Source(), sourcePath, "0123456789abcdef0123456789abcdef");
    Require(loaded.Succeeded(), "Unable to build save service document.");
    return std::move(*loaded.Document);
}

void WriteDocument(
    const fs::path& path,
    const Asset::Voxel::VoxelDocument& document)
{
    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), serialized.Message);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
        static_cast<std::streamsize>(serialized.Bytes.size()));
    Require(static_cast<bool>(output), "Unable to write save service fixture.");
}

class Fixture final
{
public:
    explicit Fixture(const std::string_view name)
    {
        Root = fs::temp_directory_path() /
            ("VoxelForgeVoxSave-" + std::string(name) + "-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        Models = Root / "Assets" / "Models";
        Model = Models / "castle.vox";
        fs::create_directories(Models);
        fs::create_directories(Root / "Cache");
        Document = BuildDocument(Model);
        WriteDocument(Model, Document);
    }
    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }

    fs::path Root;
    fs::path Models;
    fs::path Model;
    Asset::Voxel::VoxelDocument Document;
};

Editor::ThumbnailImage SolidImage()
{
    Editor::ThumbnailImage image;
    image.Width = Editor::VoxThumbnailWidth;
    image.Height = Editor::VoxThumbnailHeight;
    image.Pixels.resize(
        static_cast<std::size_t>(image.Width) * image.Height * 4U, 255U);
    return image;
}

class ControlledRenderer final : public Editor::IVoxThumbnailRenderer
{
public:
    Editor::ThumbnailRenderResult Render(const fs::path&) override
    {
        ++Calls;
        return Fail
            ? Editor::ThumbnailRenderResult{false, {}, "simulated thumbnail failure"}
            : Editor::ThumbnailRenderResult{true, SolidImage(), {}};
    }
    bool Fail = false;
    std::size_t Calls = 0U;
};

enum class FileFailure
{
    None,
    Write,
    Truncate,
    Backup,
    Replace,
    CorruptFinal,
    CleanupBackup
};

class ControlledFileSystem final : public Editor::IVoxelSaveFileSystem
{
public:
    explicit ControlledFileSystem(const FileFailure failure)
        : delegate_(Editor::CreateStandardVoxelSaveFileSystem()),
          failure_(failure)
    {
    }

    bool Inspect(const fs::path& path, bool& exists, bool& regular,
        bool& symlink, std::string& error) const override
    {
        return delegate_->Inspect(path, exists, regular, symlink, error);
    }
    bool WriteAndFlush(const fs::path& path,
        const std::span<const std::uint8_t> bytes,
        std::string& error) override
    {
        if (failure_ == FileFailure::Write)
        {
            error = "simulated write or flush failure";
            return false;
        }
        if (!delegate_->WriteAndFlush(path, bytes, error)) return false;
        if (failure_ == FileFailure::Truncate)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.put('V');
        }
        return true;
    }
    bool FileSize(const fs::path& path, std::uintmax_t& size,
        std::string& error) const override
    {
        return delegate_->FileSize(path, size, error);
    }
    bool Rename(const fs::path& source, const fs::path& destination,
        std::string& error) override
    {
        ++RenameCalls;
        if ((failure_ == FileFailure::Backup && RenameCalls == 1U) ||
            (failure_ == FileFailure::Replace && RenameCalls == 2U))
        {
            error = "simulated rename failure";
            return false;
        }
        if (!delegate_->Rename(source, destination, error)) return false;
        if (failure_ == FileFailure::CorruptFinal && RenameCalls == 2U)
        {
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            output << "broken";
        }
        return true;
    }
    bool RemoveFile(const fs::path& path, std::string& error) override
    {
        if (failure_ == FileFailure::CleanupBackup &&
            path.extension() == ".bak")
        {
            error = "simulated backup cleanup failure";
            return false;
        }
        return delegate_->RemoveFile(path, error);
    }

    std::size_t RenameCalls = 0U;

private:
    std::shared_ptr<Editor::IVoxelSaveFileSystem> delegate_;
    FileFailure failure_;
};

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("Save history model");
    for (std::size_t index = 0U; index < document.GetModelCount(); ++index)
    {
        const Asset::Voxel::VoxelSubModel* source = document.GetModel(index);
        Require(source != nullptr, "Missing save history sub-model.");
        const auto dimensions = source->Dimensions();
        Voxel::VoxelGrid grid;
        Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
            "Unable to size save history grid.");
        source->ForEachVoxel([&grid](
            const Asset::Voxel::VoxelPosition position,
            const Asset::Voxel::Voxel voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize save history grid.");
        });
        model.AddGrid(std::move(grid));
    }
    return model;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : Document(&document), Model(CompatibilityModel(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &Model; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return Document;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++Completed; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        SavedStateReported = saved;
    }
    Asset::Voxel::VoxelDocument* Document;
    Voxel::VoxelModel Model;
    std::size_t Rebuilds = 0U;
    std::size_t Completed = 0U;
    bool SavedStateReported = true;
};

Editor::VoxelEditOperation AddOperation()
{
    return {"Add Voxel", {{
        0U, {2, 2, 2}, false, 0U, true, 7U}}};
}

Editor::ModelAssetMetadata ReadMetadata(const Fixture& fixture)
{
    Editor::ModelAssetMetadataService metadata;
    Require(metadata.SetModelsDirectory(fixture.Models),
        "Unable to configure metadata reader.");
    const auto read = metadata.ReadMetadata(
        metadata.MetadataPathFor(fixture.Model));
    Require(read.Succeeded, read.Message);
    return read.Metadata;
}

void TestSuccessfulSaveHistoryMetadataAndThumbnail()
{
    Fixture fixture("success");
    auto renderer = std::make_shared<ControlledRenderer>();
    Editor::VoxelDocumentSaveService service({}, renderer);
    Require(service.SetProjectRoot(fixture.Root),
        "Unable to configure save service.");
    std::size_t refreshes = 0U;
    std::size_t invalidations = 0U;
    service.SetRefreshCallback([&refreshes]() { ++refreshes; });
    service.SetThumbnailInvalidationCallback(
        [&invalidations]() { ++invalidations; });

    Editor::VoxelEditHistory history;
    history.MarkSavedState(fixture.Document);
    TestSession session(fixture.Document);
    const auto edited = history.Execute(session, AddOperation());
    Require(edited && fixture.Document.IsDirty(),
        "Unable to prepare dirty save document.");
    const std::uint64_t revision = fixture.Document.GetRevision();
    const auto saved = service.Save(fixture.Document, history);
    Require(saved.Succeeded() && saved.MetadataUpdated &&
        saved.ThumbnailUpdated && saved.AssetBrowserRefreshed,
        saved.Message);
    Require(!fixture.Document.IsDirty() && history.IsAtSavedState() &&
        fixture.Document.GetRevision() == revision,
        "Successful save changed revision or failed to mark saved state.");
    Require(refreshes == 1U && invalidations == 1U && renderer->Calls == 1U,
        "Save must invalidate and refresh exactly once.");
    Require(!fs::exists(Editor::VoxelDocumentSaveService::TemporaryPathFor(
                fixture.Model)) &&
            !fs::exists(Editor::VoxelDocumentSaveService::BackupPathFor(
                fixture.Model)),
        "Successful save left transaction files.");
    const auto reloaded = Asset::Voxel::VoxDocumentLoader{}.Load(fixture.Model);
    Require(reloaded.Succeeded() &&
        reloaded.Document->HasVoxel({2, 2, 2}),
        "Saved voxel did not persist after reload.");
    const Editor::ModelAssetMetadata firstMetadata = ReadMetadata(fixture);
    Require(firstMetadata.Analysis && firstMetadata.Analysis->Valid &&
        firstMetadata.Analysis->VoxelCount == fixture.Document.GetVoxelCount() &&
        firstMetadata.Thumbnail &&
        firstMetadata.Thumbnail->Status == Editor::ThumbnailStatus::Valid,
        "Save did not update analysis and thumbnail metadata.");
    Require(fs::is_regular_file(fixture.Root / "Cache" / "Thumbnails" /
        firstMetadata.Thumbnail->File),
        "Save did not generate thumbnail cache.");

    const auto undone = history.Undo(session);
    Require(undone && fixture.Document.IsDirty(),
        "Undo after Save must make the document dirty.");
    const auto redone = history.Redo(session);
    Require(redone && !fixture.Document.IsDirty(),
        "Redo to saved state must make the document clean.");

    Require(fixture.Document.RemoveVoxel({2, 2, 2}).Changed,
        "Unable to prepare second save.");
    const auto second = service.Save(fixture.Document, history);
    const Editor::ModelAssetMetadata secondMetadata = ReadMetadata(fixture);
    Require(second.Succeeded() &&
        firstMetadata.AssetId == secondMetadata.AssetId,
        "Save must preserve the asset identity.");
}

void TestThumbnailWarningAndBusyGuard()
{
    Fixture fixture("warning");
    auto renderer = std::make_shared<ControlledRenderer>();
    renderer->Fail = true;
    Editor::VoxelDocumentSaveService service({}, renderer);
    Require(service.SetProjectRoot(fixture.Root), "Save setup failed.");
    Editor::VoxelEditHistory history;
    history.MarkSavedState(fixture.Document);
    Require(fixture.Document.SetVoxel({2, 2, 2}, 4U).Changed,
        "Unable to dirty warning fixture.");
    std::optional<Editor::VoxelDocumentSaveResult> nested;
    service.SetRefreshCallback([&]()
    {
        nested = service.Save(fixture.Document, history);
    });
    const auto result = service.Save(fixture.Document, history);
    Require(result.Succeeded() &&
        result.Status == Editor::VoxelDocumentSaveStatus::SucceededWithWarning &&
        !result.Warning.empty() && !result.ThumbnailUpdated &&
        !fixture.Document.IsDirty(),
        "Thumbnail failure must be a non-destructive save warning.");
    Require(nested && nested->Status == Editor::VoxelDocumentSaveStatus::Busy,
        "Concurrent save transaction was not refused.");
}

void TestFailure(
    const FileFailure failure,
    const std::string_view name)
{
    Fixture fixture(name);
    const std::vector<std::uint8_t> original = ReadBytes(fixture.Model);
    Require(fixture.Document.SetVoxel({2, 2, 2}, 5U).Changed,
        "Unable to dirty failure fixture.");
    Editor::VoxelEditHistory history;
    auto fileSystem = std::make_shared<ControlledFileSystem>(failure);
    auto renderer = std::make_shared<ControlledRenderer>();
    Editor::VoxelDocumentSaveService service(fileSystem, renderer);
    Require(service.SetProjectRoot(fixture.Root), "Failure save setup failed.");
    const auto result = service.Save(fixture.Document, history);
    Require(!result.Succeeded() && fixture.Document.IsDirty() &&
        ReadBytes(fixture.Model) == original,
        "Failed save changed dirty state or original VOX.");
    Require(!fs::exists(Editor::VoxelDocumentSaveService::TemporaryPathFor(
                fixture.Model)),
        "Failed save left a temporary file.");
    if (failure != FileFailure::CleanupBackup)
    {
        Require(!fs::exists(Editor::VoxelDocumentSaveService::BackupPathFor(
                    fixture.Model)),
            "Recoverable failed save left a backup file.");
    }
}

void TestPathAndTransactionProtection()
{
    Fixture fixture("paths");
    Editor::VoxelDocumentSaveService service;
    Require(service.SetProjectRoot(fixture.Root), "Path save setup failed.");
    Editor::VoxelEditHistory history;
    Require(fixture.Document.SetVoxel({2, 2, 2}, 5U).Changed,
        "Unable to dirty path fixture.");
    const fs::path stale =
        Editor::VoxelDocumentSaveService::TemporaryPathFor(fixture.Model);
    std::ofstream(stale) << "manual recovery data";
    const auto staleResult = service.Save(fixture.Document, history);
    Require(!staleResult.Succeeded() && fs::exists(stale) &&
        fixture.Document.IsDirty(),
        "Recognized orphan transaction file must be refused and preserved.");

    const fs::path external = fixture.Root / "outside.vox";
    Asset::Voxel::VoxelDocument externalDocument = BuildDocument(external);
    WriteDocument(external, externalDocument);
    Require(externalDocument.SetVoxel({2, 2, 2}, 5U).Changed,
        "Unable to dirty external document.");
    const auto externalResult = service.Save(externalDocument, history);
    Require(!externalResult.Succeeded() && externalDocument.IsDirty(),
        "External VOX save path must be refused.");

    std::error_code linkError;
    const fs::path link = fixture.Models / "linked.vox";
    fs::create_symlink(fixture.Model, link, linkError);
    if (!linkError)
    {
        Asset::Voxel::VoxelDocument linkedDocument = BuildDocument(link);
        Require(linkedDocument.SetVoxel({2, 2, 2}, 5U).Changed,
            "Unable to dirty linked document.");
        const auto linkResult = service.Save(linkedDocument, history);
        Require(!linkResult.Succeeded(), "Symbolic VOX source must be refused.");
    }
}

void TestBackupCleanupFailureKeepsRecoveryCopy()
{
    Fixture fixture("cleanup-backup");
    const std::vector<std::uint8_t> original = ReadBytes(fixture.Model);
    Require(fixture.Document.SetVoxel({2, 2, 2}, 5U).Changed,
        "Unable to dirty backup cleanup fixture.");
    Editor::VoxelEditHistory history;
    auto fileSystem = std::make_shared<ControlledFileSystem>(
        FileFailure::CleanupBackup);
    Editor::VoxelDocumentSaveService service(
        fileSystem, std::make_shared<ControlledRenderer>());
    Require(service.SetProjectRoot(fixture.Root),
        "Backup cleanup save setup failed.");
    const auto result = service.Save(fixture.Document, history);
    const fs::path backup =
        Editor::VoxelDocumentSaveService::BackupPathFor(fixture.Model);
    Require(!result.Succeeded() && fixture.Document.IsDirty() &&
        fs::is_regular_file(backup) && ReadBytes(backup) == original,
        "Backup cleanup failure must retain a recoverable original copy.");
}

void TestMetadataFailureRollback()
{
    Fixture fixture("metadata");
    const auto original = ReadBytes(fixture.Model);
    fs::create_directory(fixture.Model.string() + ".vfmeta");
    Require(fixture.Document.SetVoxel({2, 2, 2}, 5U).Changed,
        "Unable to dirty metadata fixture.");
    Editor::VoxelEditHistory history;
    Editor::VoxelDocumentSaveService service;
    Require(service.SetProjectRoot(fixture.Root), "Metadata save setup failed.");
    const auto result = service.Save(fixture.Document, history);
    Require(!result.Succeeded() && fixture.Document.IsDirty() &&
        ReadBytes(fixture.Model) == original &&
        !fs::exists(Editor::VoxelDocumentSaveService::BackupPathFor(
            fixture.Model)),
        "Metadata failure must restore the original VOX and keep dirty.");
}
}

int main()
{
    try
    {
        TestSuccessfulSaveHistoryMetadataAndThumbnail();
        TestThumbnailWarningAndBusyGuard();
        TestFailure(FileFailure::Write, "write");
        TestFailure(FileFailure::Truncate, "truncate");
        TestFailure(FileFailure::Backup, "backup");
        TestFailure(FileFailure::Replace, "replace");
        TestFailure(FileFailure::CorruptFinal, "verify-final");
        TestPathAndTransactionProtection();
        TestBackupCleanupFailureKeepsRecoveryCopy();
        TestMetadataFailureRollback();
        std::cout << "Voxel document save service tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel document save service tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
