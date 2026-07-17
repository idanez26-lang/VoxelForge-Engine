#include "Palette/PaletteService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelFillService.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
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
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({dimensions, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-fill-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Fill test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Fill test document has no model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Fill compatibility grid.");
    source->ForEachVoxel([&grid](
        const Asset::Voxel::VoxelPosition position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Fill compatibility grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class TestEditSession final : public Editor::VoxelEditSession
{
public:
    explicit TestEditSession(Asset::Voxel::VoxelDocument& document)
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
        ++rebuildCount_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override
    {
        ++completedCount_;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
};

Editor::VoxelRaycastHit Hit(
    const Asset::Voxel::VoxelPosition position,
    const std::uint64_t revision)
{
    Editor::VoxelRaycastHit hit;
    hit.Coordinates = {
        static_cast<std::uint32_t>(position.X),
        static_cast<std::uint32_t>(position.Y),
        static_cast<std::uint32_t>(position.Z)};
    hit.Face = Editor::VoxelHitFace::PositiveY;
    hit.SubModelIndex = 0U;
    hit.DocumentRevision = revision;
    return hit;
}

Editor::VoxelFillResult Fill(
    TestEditSession& session,
    Editor::VoxelEditHistory& history,
    Asset::Voxel::VoxelDocument& document,
    const Asset::Voxel::VoxelPosition position,
    const std::size_t paletteIndex)
{
    return Editor::VoxelFillService::Apply({
        &session, &document, 0U, Hit(position, document.GetRevision()),
        paletteIndex, false, &history});
}

void TestSingleVoxelAndAtomicHistory()
{
    auto document = Document({2U, 2U, 2U}, {{0U, 0U, 0U, 3U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Fill(session, history, document, {0, 0, 0}, 9U);
    Require(result.Code == Editor::VoxelFillResultCode::Applied &&
        result.Changed && result.ChangedVoxelCount == 1U &&
        result.PreviousPaletteIndex == 3U && result.NewPaletteIndex == 9U &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == 9U &&
        document.GetRevision() == 1U && history.UndoCount() == 1U &&
        history.UndoLabel() == "Fill Voxels" && session.rebuildCount_ == 1U,
        "Single-voxel Fill was not one atomic edit.");
    Require(history.Undo(session) &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == 3U &&
        document.GetRevision() == 2U && history.RedoCount() == 1U,
        "Fill Undo did not restore the original color.");
    Require(history.Redo(session) &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == 9U &&
        document.GetRevision() == 3U && session.rebuildCount_ == 3U,
        "Fill Redo did not restore the replacement color.");
}

void TestSixNeighborBoundariesAndColors()
{
    auto document = Document(
        {4U, 4U, 4U},
        {{0U, 0U, 0U, 2U}, {1U, 0U, 0U, 2U},
         {1U, 1U, 0U, 2U}, {3U, 3U, 3U, 2U},
         {2U, 1U, 0U, 5U}, {2U, 2U, 0U, 2U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const auto result = Fill(session, history, document, {0, 0, 0}, 8U);
    Require(result.ChangedVoxelCount == 3U &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == 8U &&
        document.GetVoxel({1, 0, 0})->PaletteIndex == 8U &&
        document.GetVoxel({1, 1, 0})->PaletteIndex == 8U,
        "Fill did not follow the six-neighbor connected region.");
    Require(document.GetVoxel({3, 3, 3})->PaletteIndex == 2U &&
        document.GetVoxel({2, 1, 0})->PaletteIndex == 5U &&
        document.GetVoxel({2, 2, 0})->PaletteIndex == 2U,
        "Fill crossed empty space, another color, or a diagonal boundary.");
}

void TestNoOpAndInvalidCases()
{
    auto document = Document({2U, 2U, 2U}, {{1U, 1U, 1U, 4U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const std::uint64_t revision = document.GetRevision();
    Require(Fill(session, history, document, {1, 1, 1}, 4U).Code ==
            Editor::VoxelFillResultCode::SameColor &&
        Fill(session, history, document, {0, 0, 0}, 7U).Code ==
            Editor::VoxelFillResultCode::TargetMissing,
        "Same-color or empty-cell Fill was not ignored.");
    auto empty = Document({2U, 2U, 2U}, {});
    TestEditSession emptySession(empty);
    Editor::VoxelEditHistory emptyHistory;
    Require(Fill(emptySession, emptyHistory, empty, {0, 0, 0}, 7U).Code ==
            Editor::VoxelFillResultCode::TargetMissing &&
        document.GetRevision() == revision && history.UndoCount() == 0U &&
        session.rebuildCount_ == 0U,
        "A no-op Fill changed revision, history, or rendering.");
}

void TestLargeRegionAndPaletteIntegration()
{
    constexpr std::uint32_t side = 24U;
    std::vector<Asset::Vox::VoxVoxel> voxels;
    voxels.reserve(side * side * side);
    for (std::uint32_t z = 0U; z < side; ++z)
        for (std::uint32_t y = 0U; y < side; ++y)
            for (std::uint32_t x = 0U; x < side; ++x)
                voxels.push_back({
                    static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    1U});
    auto document = Document({side, side, side}, std::move(voxels));
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    Editor::PaletteService palette;
    palette.SetPalette(document.GetPalette(), "Fill test palette");
    Require(palette.SelectColor(37U),
        "Fill test palette could not select an active color.");
    const auto active = palette.ActiveColor();
    Require(active.has_value(), "Fill test palette has no active color.");
    const auto result = Fill(
        session, history, document, {side - 1, side - 1, side - 1},
        active->Index);
    Require(result.Code == Editor::VoxelFillResultCode::Applied &&
        result.ChangedVoxelCount == side * side * side &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == active->Index &&
        document.GetVoxel({static_cast<std::int32_t>(side - 1),
            static_cast<std::int32_t>(side - 1),
            static_cast<std::int32_t>(side - 1)})->PaletteIndex ==
                active->Index &&
        document.GetRevision() == 1U && session.rebuildCount_ == 1U,
        "Iterative Fill failed on a large bounded region or ignored PaletteService.");
}

void TestToolState()
{
    Editor::VoxelToolState state;
    state.SetActiveTool(Editor::ActiveVoxelTool::Fill);
    Require(state.IsFillActive() && state.IsEditingToolActive() &&
        !state.IsPencilActive() && !state.IsEraserActive() &&
        std::string_view(Editor::ActiveVoxelToolName(state.ActiveTool())) ==
            "Fill",
        "Fill tool state is incorrect.");
}
}

int main()
{
    try
    {
        TestSingleVoxelAndAtomicHistory();
        TestSixNeighborBoundariesAndColors();
        TestNoOpAndInvalidCases();
        TestLargeRegionAndPaletteIntegration();
        TestToolState();
        std::cout << "Voxel Fill tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Fill tests failed: " << exception.what() << '\n';
        return 1;
    }
}
