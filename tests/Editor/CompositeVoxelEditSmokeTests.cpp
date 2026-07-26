#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using namespace VoxelForge;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument TestDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{16U, 16U, 16U}, {{0U, 0U, 0U, 3U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "composite-edit-smoke.vox");
    Require(loaded.Succeeded(), "Unable to open the smoke voxel document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("Composite edit smoke");
    const auto& palette = document.GetPalette();
    for (std::size_t index = 0U; index < palette.size(); ++index)
    {
        const auto color = palette[index];
        Require(model.Palette().Set(index,
            {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize the smoke compatibility palette.");
    }
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Smoke document has no sub-model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the smoke compatibility grid.");
    source->ForEachVoxel([&grid](
        const Asset::Voxel::VoxelPosition position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize the smoke compatibility grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeSession final : public Editor::VoxelEditSession
{
public:
    explicit SmokeSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 1U;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds_;
        const auto result = cache_.Synchronize(*document_, 1U);
        return result.Succeeded
            ? Editor::CommandResult::Success()
            : Editor::CommandResult::Failure(result.Message);
    }
    void CompleteVoxelEdit() noexcept override { ++completedEdits_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        saved_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;
    bool saved_ = true;
};

Editor::VoxelChange Add(
    const Asset::Voxel::VoxelPosition position,
    const std::uint8_t paletteIndex)
{
    return {0U, position, false, 0U, true, paletteIndex};
}
}

int main()
{
    try
    {
        auto document = TestDocument();
        SmokeSession session(document);
        Editor::VoxelEditHistory history;
        history.MarkSavedState(document);

        Asset::Voxel::VoxelDocumentPaletteChange paletteChange;
        paletteChange.Before = document.GetPaletteSnapshot();
        paletteChange.After = paletteChange.Before;
        paletteChange.After.Colors[77U] = {31U, 181U, 227U, 255U};
        paletteChange.After.HasCustomPalette = true;

        Editor::VoxelEditOperation operation;
        operation.Label = "Composite Edit Smoke";
        operation.Changes = {
            Add({1, 1, 1}, 77U),
            Add({2, 1, 1}, 77U),
            Add({3, 1, 1}, 77U)};
        operation.PaletteChange =
            std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
                paletteChange);

        const std::uint64_t revision = document.GetRevision();
        Require(history.Execute(session, operation) &&
            document.GetPaletteSnapshot() == paletteChange.After &&
            document.GetVoxel({1, 1, 1})->PaletteIndex == 77U &&
            document.GetVoxel({2, 1, 1})->PaletteIndex == 77U &&
            document.GetVoxel({3, 1, 1})->PaletteIndex == 77U &&
            document.GetRevision() == revision + 1U &&
            history.UndoCount() == 1U && history.RedoCount() == 0U,
            "Composite smoke commit failed.");

        Require(history.Undo(session) &&
            document.GetPaletteSnapshot() == paletteChange.Before &&
            !document.HasVoxel({1, 1, 1}) &&
            !document.HasVoxel({2, 1, 1}) &&
            !document.HasVoxel({3, 1, 1}) &&
            document.GetRevision() == revision + 2U &&
            history.UndoCount() == 0U && history.RedoCount() == 1U,
            "Composite smoke Undo failed.");

        Require(history.Redo(session) &&
            document.GetPaletteSnapshot() == paletteChange.After &&
            document.GetVoxel({1, 1, 1})->PaletteIndex == 77U &&
            document.GetVoxel({2, 1, 1})->PaletteIndex == 77U &&
            document.GetVoxel({3, 1, 1})->PaletteIndex == 77U &&
            document.GetRevision() == revision + 3U &&
            history.UndoCount() == 1U && history.RedoCount() == 0U &&
            session.rebuilds_ == 3U && session.completedEdits_ == 3U,
            "Composite smoke Redo or lifecycle accounting failed.");

        history.Clear();
        std::cout << "Composite voxel edit smoke passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Composite voxel edit smoke failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
