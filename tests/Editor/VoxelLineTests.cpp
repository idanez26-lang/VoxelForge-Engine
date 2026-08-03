#include "Palette/PaletteService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelLineService.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;

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
        source, "voxel-line-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Line test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto dimensions = *document.GetDimensions(0U);
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Line compatibility grid.");
    if (const auto* source = document.GetModel(0U))
        source->ForEachVoxel([&grid](const auto position, const auto voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize Line compatibility grid.");
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
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return hasModel_ ? &model_ : nullptr;
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
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }

    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
    bool hasModel_ = true;
};

Editor::VoxelLineResult Line(
    TestEditSession& session,
    Editor::VoxelEditHistory& history,
    Asset::Voxel::VoxelDocument& document,
    const Position a,
    const Position b,
    const std::size_t palette)
{
    return Editor::VoxelLineService::Apply(
        {&session, &document, 0U, a, b, palette, false, &history});
}

void RequireContinuous(
    const std::vector<Position>& positions,
    const std::string_view message)
{
    Require(!positions.empty(), message);
    for (std::size_t index = 1U; index < positions.size(); ++index)
    {
        const Position a = positions[index - 1U];
        const Position b = positions[index];
        Require(std::abs(a.X - b.X) <= 1 && std::abs(a.Y - b.Y) <= 1 &&
            std::abs(a.Z - b.Z) <= 1 && a != b, message);
    }
}

void TestHorizontalVerticalAndPoint()
{
    const auto document = Document({16U, 16U, 16U});
    const auto horizontal = Editor::VoxelLineService::CalculatePositions(
        document, 0U, {1, 2, 3}, {8, 2, 3});
    const auto vertical = Editor::VoxelLineService::CalculatePositions(
        document, 0U, {4, 1, 5}, {4, 9, 5});
    const auto point = Editor::VoxelLineService::CalculatePositions(
        document, 0U, {7, 7, 7}, {7, 7, 7});
    Require(horizontal.size() == 8U && horizontal.front() == Position{1, 2, 3} &&
        horizontal.back() == Position{8, 2, 3},
        "Horizontal Line is incomplete.");
    Require(vertical.size() == 9U && vertical.front() == Position{4, 1, 5} &&
        vertical.back() == Position{4, 9, 5},
        "Vertical Line is incomplete.");
    Require(point == std::vector<Position>{{7, 7, 7}},
        "A point Line did not contain exactly one voxel.");
}

void TestDiagonalContinuityAndSymmetry()
{
    const auto document = Document({32U, 32U, 32U});
    const Position a{2, 3, 4};
    const Position b{25, 17, 29};
    const auto forward = Editor::VoxelLineService::CalculatePositions(
        document, 0U, a, b);
    const auto reverse = Editor::VoxelLineService::CalculatePositions(
        document, 0U, b, a);
    Require(forward == reverse,
        "Line output differs between A-to-B and B-to-A.");
    Require(forward.front() == a && forward.back() == b &&
        forward.size() == 26U,
        "3D Bresenham did not cover the dominant axis deterministically.");
    RequireContinuous(forward, "3D diagonal Line contains a hole.");
}

void TestClipping()
{
    const auto document = Document({8U, 8U, 8U});
    const auto crossing = Editor::VoxelLineService::CalculatePositions(
        document, 0U, {-4, 3, 3}, {12, 3, 3});
    const auto outside = Editor::VoxelLineService::CalculatePositions(
        document, 0U, {-5, -5, -5}, {-1, -1, -1});
    Require(crossing.size() == 8U && crossing.front() == Position{0, 3, 3} &&
        crossing.back() == Position{7, 3, 3} && outside.empty(),
        "Line clipping wrote outside the document or lost inside voxels.");
}

void TestAtomicUndoRedoPaletteAndSave()
{
    auto document = Document({16U, 16U, 16U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    Editor::PaletteService palette;
    palette.SetPalette(document.GetPalette(), "Line test palette");
    Require(palette.SelectColor(37U), "Unable to select Line color.");
    const auto active = palette.ActiveColor();
    Require(active.has_value(), "Line palette has no active color.");
    const auto result = Line(
        session, history, document, {1, 1, 1}, {8, 8, 8}, active->Index);
    Require(result.Code == Editor::VoxelLineResultCode::Applied &&
        result.ChangedVoxelCount == 8U && document.GetVoxelCount() == 8U &&
        document.GetVoxel({5, 5, 5})->PaletteIndex == active->Index &&
        document.GetRevision() == 1U && history.UndoCount() == 1U &&
        history.UndoLabel() == "Create Voxel Line" && session.rebuildCount_ == 1U,
        "Line was not one atomic palette-aware edit.");
    Require(history.Undo(session) && document.GetVoxelCount() == 0U &&
        document.GetRevision() == 2U && history.Redo(session) &&
        document.GetVoxelCount() == 8U && document.GetRevision() == 3U &&
        session.rebuildCount_ == 3U,
        "Line Undo/Redo was not atomic.");
    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded() && !serialized.Bytes.empty(),
        "Line result is incompatible with VOX Save.");
}

void TestDocumentOnlyCanonicalHistory()
{
    auto document = Document({8U, 8U, 8U});
    TestEditSession session(document);
    session.hasModel_ = false;
    Editor::VoxelEditHistory history;
    const auto result = Line(
        session, history, document, {1, 1, 1}, {4, 1, 1}, 13U);
    Require(result.Code == Editor::VoxelLineResultCode::Applied &&
        result.ChangedVoxelCount == 4U && document.GetVoxelCount() == 4U &&
        history.Undo(session) && document.GetVoxelCount() == 0U &&
        history.Redo(session) && document.GetVoxelCount() == 4U &&
        document.GetVoxel({4, 1, 1})->PaletteIndex == 13U,
        "Document-only Line history did not apply, undo and redo canonically.");
}

void TestWorkplaneFaceCancellationAndToolState()
{
    auto document = Document({16U, 16U, 16U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const Position workplanePoint{1, 0, 1};
    const Position adjacentFacePoint{7, 6, 7};
    const auto result = Line(
        session, history, document, workplanePoint, adjacentFacePoint, 9U);
    Require(result.Code == Editor::VoxelLineResultCode::Applied &&
        document.HasVoxel(workplanePoint) && document.HasVoxel(adjacentFacePoint),
        "Workplane or voxel-face Line endpoint was lost.");

    Editor::VoxelLineInteraction interaction;
    Require(interaction.Begin(workplanePoint, 4U) &&
        interaction.Update(adjacentFacePoint) && interaction.IsActive(),
        "Line preview interaction did not track its endpoints.");
    interaction.Cancel();
    Require(!interaction.IsActive() && !interaction.PointA() &&
        !interaction.PointB(),
        "Right-click/Escape Line cancellation did not clear immediately.");

    Editor::VoxelToolState state;
    state.SetActiveTool(Editor::ActiveVoxelTool::Line);
    Require(state.IsLineActive() && state.IsEditingToolActive() &&
        std::string_view(Editor::ActiveVoxelToolName(state.ActiveTool())) == "Line",
        "Line tool state is incorrect.");
}
}

int main()
{
    try
    {
        TestHorizontalVerticalAndPoint();
        TestDiagonalContinuityAndSymmetry();
        TestClipping();
        TestAtomicUndoRedoPaletteAndSave();
        TestDocumentOnlyCanonicalHistory();
        TestWorkplaneFaceCancellationAndToolState();
        std::cout << "Voxel Line tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Line tests failed: " << exception.what() << '\n';
        return 1;
    }
}
