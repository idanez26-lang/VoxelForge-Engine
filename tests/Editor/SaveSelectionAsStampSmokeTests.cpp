#include "Selection/SelectionService.h"
#include "VoxelStamps/Library/StampJsonCatalogStore.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Workflow/SaveSelectionAsStampWorkflow.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;
namespace fs = std::filesystem;

void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeSmokeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({.Dimensions = {.X = 4U, .Y = 4U, .Z = 4U},
        .Voxels = {{.X = 1U, .Y = 1U, .Z = 1U, .ColorIndex = 3U}}});
    source.Palette[3U] = {.Red = 8U, .Green = 18U, .Blue = 28U, .Alpha = 255U};
    const auto document = Asset::Voxel::VoxDocumentLoader{}.Build(source, "stamp-smoke.vox");
    Require(document.Succeeded(), "Unable to create Save Selection As smoke document.");
    return std::move(*document.Document);
}

} // namespace

int main()
{
    std::error_code error;
    const fs::path root = fs::temp_directory_path() / ("VoxelForgeSaveSelectionAsSmoke-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        Require(fs::create_directories(root / "Assets"), "Unable to create Save Selection As smoke project.");
        auto cleanup = [&]() { fs::remove_all(root, error); };
        Asset::Voxel::VoxelDocument document = MakeSmokeDocument();
        SelectionService selection;
        selection.SetDocumentGeneration(1U);
        Require(selection.Select({1, 1, 1}), "Unable to select voxel for Save Selection As smoke.");
        StampProjectLibraryRepository repository;
        StampJsonCatalogStore store;
        Require(repository.SetProjectRoot(root) && store.SetProjectRoot(root),
            "Unable to configure Save Selection As smoke services.");
        SaveSelectionAsStampWorkflow workflow(repository, store);
        const SaveSelectionAsStampBeginRequest begin{
            .ProjectRoot = root, .Document = &document, .Selection = &selection,
            .DocumentGeneration = 1U, .DocumentRevision = document.GetRevision()};
        Require(workflow.Begin(begin).Status == SaveSelectionAsStampStatus::Ready,
            "Save Selection As smoke capture did not become ready.");
        const SaveSelectionAsStampCurrentContext current{
            .ProjectRoot = root, .Document = &document, .Selection = &selection,
            .DocumentGeneration = 1U, .DocumentRevision = document.GetRevision()};
        const auto saved = workflow.Save({.Name = "smoke-stamp"}, current);
        Require(saved.Status == SaveSelectionAsStampStatus::CompleteSuccess && saved.InstalledAsset,
            "Save Selection As smoke install/catalogue workflow failed.");
        Require(repository.Read(*saved.InstalledAsset).Succeeded() && store.LoadCatalogue().Succeeded(),
            "Save Selection As smoke did not leave a readable Stamp and coherent catalogue.");
        cleanup();
    }
    catch (const std::exception& exception)
    {
        fs::remove_all(root, error);
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
