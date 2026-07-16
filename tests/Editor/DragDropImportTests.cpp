#include "DragDropImport/DragDropImportController.h"
#include "ModelImport/ModelImportService.h"
#include "Thumbnail/ThumbnailImage.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void WriteU32(std::ofstream& output, const std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.put(static_cast<char>(value >> shift));
}

bool WriteVox(const std::filesystem::path& path, const std::uint8_t color)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write("VOX ", 4); WriteU32(output, 150U);
    output.write("MAIN", 4); WriteU32(output, 0U); WriteU32(output, 44U);
    output.write("SIZE", 4); WriteU32(output, 12U); WriteU32(output, 0U);
    WriteU32(output, 2U); WriteU32(output, 2U); WriteU32(output, 2U);
    output.write("XYZI", 4); WriteU32(output, 8U); WriteU32(output, 0U);
    WriteU32(output, 1U);
    output.put(0); output.put(0); output.put(0);
    output.put(static_cast<char>(color));
    return static_cast<bool>(output);
}

struct Fixture final
{
    Fixture()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("VoxelForgeDragDrop-" + std::to_string(unique));
        std::filesystem::create_directories(Root / "Assets" / "Models");
        std::filesystem::create_directories(Root / "Cache");
    }

    ~Fixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    std::filesystem::path Root;
};

DragDropImportRequest Drop(
    DragDropImportController& controller,
    const DragDropRect& assetRect,
    const DragDropRect& viewportRect,
    const float x,
    const std::vector<std::filesystem::path>& paths,
    const bool hasProject = true)
{
    controller.Begin(x, 50.0F);
    controller.UpdatePosition(x, 50.0F, assetRect, viewportRect);
    for (const auto& path : paths)
        controller.AddFile(path, x, 50.0F, assetRect, viewportRect);
    return controller.Complete(hasProject);
}

void TestController()
{
    const DragDropRect assetRect{0.0F, 0.0F, 100.0F, 100.0F};
    const DragDropRect viewportRect{110.0F, 0.0F, 210.0F, 100.0F};
    DragDropImportController controller;

    auto request = Drop(controller, assetRect, viewportRect, 50.0F,
        {"C:/Drops/castle.VOX"});
    Check(request.Accepted &&
        request.Target == DragDropImportTarget::AssetBrowser &&
        !request.ShouldOpenViewport(),
        "single Asset Browser drop must be accepted without viewport open");
    Check(controller.State() == DragDropImportState::AwaitingConfirmation,
        "valid drop must await confirmation");
    controller.MarkImporting();
    Check(controller.State() == DragDropImportState::Importing,
        "confirmed drop must enter importing state");
    controller.Begin(150.0F, 50.0F);
    Check(controller.State() == DragDropImportState::Importing &&
        controller.Paths().size() == 1U,
        "a concurrent drag must not replace an active import");
    controller.MarkCompleted();
    Check(controller.State() == DragDropImportState::Completed,
        "completed drop must expose completed state");

    request = Drop(controller, assetRect, viewportRect, 150.0F,
        {"C:/Drops/model.Vox"});
    Check(request.Accepted && request.ShouldOpenViewport(),
        "single Viewport drop must request opening imported destination");
    controller.Cancel();

    request = Drop(controller, assetRect, viewportRect, 50.0F,
        {"C:/A/shared.vox", "C:/A/shared.vox", "C:/B/shared.vox"});
    Check(request.Accepted && request.Paths.size() == 2U,
        "exact duplicates must be ignored without merging same names");
    Check(request.Paths[0].generic_string() < request.Paths[1].generic_string(),
        "batch ordering must be deterministic");
    controller.Cancel();

    controller.Begin(50.0F, 50.0F);
    controller.UpdatePosition(50.0F, 50.0F, assetRect, viewportRect);
    controller.AddFile("C:/Drops/readme.txt", 50.0F, 50.0F,
        assetRect, viewportRect);
    Check(controller.State() == DragDropImportState::HoverInvalid &&
        controller.HoverMessage() == "Unsupported file type",
        "unsupported extension must produce invalid hover state");
    request = controller.Complete(true);
    Check(!request.Accepted && request.Message == "Unsupported file type",
        "unsupported drop must be refused cleanly");

    request = Drop(controller, assetRect, viewportRect, 50.0F,
        {"C:/Drops/castle.vox"}, false);
    Check(!request.Accepted && request.Message ==
        "Open or create a project before importing models.",
        "drop without project must be refused with user guidance");

    request = Drop(controller, assetRect, viewportRect, 300.0F,
        {"C:/Drops/castle.vox"});
    Check(!request.Accepted &&
        request.Target == DragDropImportTarget::None,
        "drop outside supported panels must do nothing");
    controller.Cancel();
    Check(controller.State() == DragDropImportState::Cancelled,
        "cancel must expose cancelled state");
}

void TestPipelineReuse()
{
    Fixture fixture;
    const auto firstSource = fixture.Root / "DropA" / "castle.vox";
    const auto secondSource = fixture.Root / "DropB" / "castle.vox";
    const auto treeSource = fixture.Root / "DropC" / "tree.vox";
    Check(WriteVox(firstSource, 2U) && WriteVox(secondSource, 3U) &&
        WriteVox(treeSource, 4U), "write VOX drop fixtures");

    ModelImportService importer;
    std::size_t refreshCount = 0U;
    importer.SetRefreshCallback([&refreshCount]() { ++refreshCount; });
    Check(importer.SetProjectRoot(fixture.Root),
        "configure existing import pipeline");

    const ModelImportResult first = importer.ImportModel(firstSource);
    Check(first.Succeeded() && refreshCount == 1U,
        "single dropped VOX must use the existing import pipeline");
    const MetadataReadResult metadata =
        importer.ReadMetadataForModel(first.DestinationPath);
    Check(metadata.Succeeded && metadata.Metadata.Analysis &&
        metadata.Metadata.Analysis->Valid && metadata.Metadata.Thumbnail &&
        metadata.Metadata.Thumbnail->Status == ThumbnailStatus::Valid,
        "drop import must generate analysis and thumbnail metadata");
    ThumbnailImage thumbnail;
    std::string imageError;
    const auto thumbnailPath = fixture.Root / "Cache" / "Thumbnails" /
        metadata.Metadata.Thumbnail->File;
    Check(ReadThumbnailImage(thumbnailPath, thumbnail, imageError),
        "drop import must generate a readable thumbnail");

    Check(importer.ImportModel(secondSource).Status ==
        ModelImportStatus::Collision,
        "same destination name must reuse collision detection");
    Check(importer.ImportModel(secondSource,
        ModelImportCollisionAction::Rename).Status ==
        ModelImportStatus::Renamed,
        "Rename collision action must be reused");
    Check(importer.ImportModel(secondSource,
        ModelImportCollisionAction::Replace).Status ==
        ModelImportStatus::Replaced,
        "Replace collision action must be reused");
    Check(importer.ImportModel(secondSource,
        ModelImportCollisionAction::Skip).Status ==
        ModelImportStatus::Skipped,
        "Skip collision action must be reused");
    Check(importer.ImportModel(secondSource,
        ModelImportCollisionAction::Cancel).Status ==
        ModelImportStatus::Cancelled,
        "Cancel collision action must be reused");

    const std::size_t beforeBatch = refreshCount;
    const auto batch = importer.ImportModels({treeSource});
    Check(batch.size() == 1U && batch.front().Succeeded() &&
        refreshCount == beforeBatch + 1U,
        "batch import must issue one refresh");
}
}

int RunDragDropImportTests()
{
    TestController();
    TestPipelineReuse();
    if (failures != 0)
        std::cerr << failures << " drag-drop import test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
