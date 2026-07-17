#include "Palette/PaletteService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelBoxService.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument Document(
    const Asset::Vox::VoxDimensions dimensions,
    std::vector<Asset::Vox::VoxVoxel> voxels = {})
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({dimensions, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-box-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Box test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto dimensions = *document.GetDimensions(0U);
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Box compatibility grid.");
    if (const auto* source = document.GetModel(0U))
        source->ForEachVoxel([&grid](const auto position, const auto voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize Box compatibility grid.");
        });
    model.AddGrid(std::move(grid));
    return model;
}

class TestEditSession final : public Editor::VoxelEditSession
{
public:
    explicit TestEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }

    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
};

Editor::VoxelBoxResult Box(
    TestEditSession& session,
    Editor::VoxelEditHistory& history,
    Asset::Voxel::VoxelDocument& document,
    const Asset::Voxel::VoxelPosition a,
    const Asset::Voxel::VoxelPosition b,
    const std::size_t palette)
{
    return Editor::VoxelBoxService::Apply(
        {&session, &document, 0U, a, b, palette, false, &history});
}

void TestSingleVoxelAndAtomicHistory()
{
    auto document = Document({16U, 16U, 16U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Box(session, history, document, {4, 3, 2}, {4, 3, 2}, 9U);
    Require(result.Code == Editor::VoxelBoxResultCode::Applied &&
        result.ChangedVoxelCount == 1U && document.GetVoxelCount() == 1U &&
        document.GetVoxel({4, 3, 2})->PaletteIndex == 9U &&
        document.GetRevision() == 1U && history.UndoCount() == 1U &&
        history.UndoLabel() == "Create Voxel Box" && session.rebuildCount_ == 1U,
        "A 1x1x1 Box was not one atomic palette-aware edit.");
    Require(history.Undo(session) && document.GetVoxelCount() == 0U &&
        document.GetRevision() == 2U && history.Redo(session) &&
        document.GetVoxelCount() == 1U && document.GetRevision() == 3U &&
        session.rebuildCount_ == 3U,
        "Box Undo/Redo was not atomic.");
}

void TestSolidBoxAndOccupiedCells()
{
    auto document = Document({16U, 16U, 16U}, {{2U, 2U, 2U, 3U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Box(session, history, document, {1, 1, 1}, {3, 3, 3}, 7U);
    Require(result.ChangedVoxelCount == 26U && document.GetVoxelCount() == 27U &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 7U &&
        document.GetVoxel({3, 3, 3})->PaletteIndex == 7U &&
        document.GetVoxel({2, 2, 2})->PaletteIndex == 3U,
        "Solid Box did not fill its volume or overwrote an existing voxel.");
}

void TestClippingAndMaximumExtent()
{
    auto document = Document({128U, 80U, 70U});
    const auto forward = Editor::VoxelBoxService::CalculateBounds(
        document, 0U, {10, 5, 3}, {120, 79, 69});
    const auto reverse = Editor::VoxelBoxService::CalculateBounds(
        document, 0U, {127, 79, 69}, {-50, -20, -10});
    Require(forward && forward->Minimum == Asset::Voxel::VoxelPosition{10, 5, 3} &&
        forward->Maximum == Asset::Voxel::VoxelPosition{73, 68, 66} &&
        reverse && reverse->Minimum == Asset::Voxel::VoxelPosition{64, 16, 6} &&
        reverse->Maximum == Asset::Voxel::VoxelPosition{127, 79, 69},
        "Box clipping did not enforce document limits and 64 cells per axis.");
}

void TestLargeBox()
{
    auto document = Document({80U, 80U, 80U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Box(
        session, history, document, {0, 0, 0}, {79, 79, 79}, 11U);
    constexpr std::size_t expected = 64U * 64U * 64U;
    Require(result.Code == Editor::VoxelBoxResultCode::Applied &&
        result.ChangedVoxelCount == expected &&
        document.GetVoxelCount() == expected &&
        document.HasVoxel({63, 63, 63}) &&
        !document.HasVoxel({64, 64, 64}) && document.GetRevision() == 1U &&
        history.UndoCount() == 1U && session.rebuildCount_ == 1U,
        "Large Box was not clipped, applied, and rebuilt atomically.");
}

void TestWorkplaneFaceAndSaveCompatibility()
{
    auto document = Document({16U, 16U, 16U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const Asset::Voxel::VoxelPosition workplaneCorner{1, 0, 1};
    const Asset::Voxel::VoxelPosition adjacentFaceCorner{3, 2, 3};
    const auto result = Box(
        session, history, document, workplaneCorner, adjacentFaceCorner, 42U);
    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(result.Code == Editor::VoxelBoxResultCode::Applied &&
        result.ChangedVoxelCount == 27U &&
        document.HasVoxel(workplaneCorner) &&
        document.HasVoxel(adjacentFaceCorner) && serialized.Succeeded() &&
        !serialized.Bytes.empty(),
        "Workplane/face corners or VOX Save compatibility failed.");
}

void TestNoOpCancellationAndToolState()
{
    auto document = Document({4U, 4U, 4U}, {{1U, 1U, 1U, 5U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Box(session, history, document, {1, 1, 1}, {1, 1, 1}, 8U);
    Require(result.Code == Editor::VoxelBoxResultCode::NoChanges &&
        document.GetRevision() == 0U && history.UndoCount() == 0U,
        "An occupied Box created an empty history operation.");

    Editor::VoxelBoxInteraction interaction;
    Require(interaction.Begin({0, 0, 0}, 12U) && interaction.IsActive() &&
        interaction.Update(Asset::Voxel::VoxelPosition{3, 3, 3}) &&
        interaction.CornerB() == Asset::Voxel::VoxelPosition{3, 3, 3},
        "Box preview interaction did not track both corners.");
    interaction.Cancel();
    Require(!interaction.IsActive() && !interaction.CornerA() &&
        !interaction.CornerB(),
        "Right-click/Escape cancellation state did not clear immediately.");

    Editor::VoxelToolState state;
    state.SetActiveTool(Editor::ActiveVoxelTool::Box);
    Require(state.IsBoxActive() && state.IsEditingToolActive() &&
        std::string_view(Editor::ActiveVoxelToolName(state.ActiveTool())) == "Box",
        "Box tool state is incorrect.");
}
}

int main()
{
    try
    {
        TestSingleVoxelAndAtomicHistory();
        TestSolidBoxAndOccupiedCells();
        TestClippingAndMaximumExtent();
        TestLargeBox();
        TestWorkplaneFaceAndSaveCompatibility();
        TestNoOpCancellationAndToolState();
        std::cout << "Voxel Box tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Box tests failed: " << exception.what() << '\n';
        return 1;
    }
}
