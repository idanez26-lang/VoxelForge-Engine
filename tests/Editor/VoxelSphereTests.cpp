#include "Palette/PaletteService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelSphereService.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
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
        source, "voxel-sphere-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Sphere test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto dimensions = *document.GetDimensions(0U);
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Sphere compatibility grid.");
    if (const auto* source = document.GetModel(0U))
        source->ForEachVoxel([&grid](const auto position, const auto voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize Sphere compatibility grid.");
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

Editor::VoxelSphereResult Sphere(
    TestEditSession& session,
    Editor::VoxelEditHistory& history,
    Asset::Voxel::VoxelDocument& document,
    const Position center,
    const Position radiusPoint,
    const std::size_t palette)
{
    return Editor::VoxelSphereService::Apply(
        {&session, &document, 0U, center, radiusPoint,
         palette, false, &history});
}

void TestRadiusZeroAndSmallSphere()
{
    const auto document = Document({16U, 16U, 16U});
    const auto point = Editor::VoxelSphereService::CalculatePositions(
        document, 0U, {7, 7, 7}, {7, 7, 7});
    const auto radiusTwo = Editor::VoxelSphereService::CalculatePositions(
        document, 0U, {7, 7, 7}, {9, 7, 7});
    Require(point == std::vector<Position>{{7, 7, 7}},
        "A radius-zero Sphere did not contain exactly one voxel.");
    Require(radiusTwo.size() == 33U &&
        std::find(radiusTwo.begin(), radiusTwo.end(), Position{9, 7, 7}) !=
            radiusTwo.end(),
        "The solid radius-two Sphere is incomplete.");
}

void TestLargeSphereClippingContinuityAndLimits()
{
    const auto document = Document({16U, 16U, 16U});
    const auto positions = Editor::VoxelSphereService::CalculatePositions(
        document, 0U, {0, 0, 0}, {12, 0, 0});
    Require(!positions.empty() &&
        std::find(positions.begin(), positions.end(), Position{12, 0, 0}) !=
            positions.end(),
        "A large clipped Sphere lost an in-bounds boundary voxel.");
    for (const Position position : positions)
        Require(position.X >= 0 && position.X < 16 && position.Y >= 0 &&
            position.Y < 16 && position.Z >= 0 && position.Z < 16,
            "Sphere clipping generated an out-of-bounds voxel.");

    const auto encode = [](const Position position)
    {
        return (static_cast<std::uint64_t>(position.X) << 32U) |
            (static_cast<std::uint64_t>(position.Y) << 16U) |
            static_cast<std::uint64_t>(position.Z);
    };
    std::unordered_set<std::uint64_t> remaining;
    for (const Position position : positions) remaining.insert(encode(position));
    std::queue<Position> queue;
    queue.push(positions.front());
    remaining.erase(encode(positions.front()));
    constexpr Position neighbors[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    while (!queue.empty())
    {
        const Position current = queue.front();
        queue.pop();
        for (const Position offset : neighbors)
        {
            const Position next{current.X + offset.X, current.Y + offset.Y,
                current.Z + offset.Z};
            if (remaining.erase(encode(next)) != 0U) queue.push(next);
        }
    }
    Require(remaining.empty(), "The solid Sphere is not six-neighbor continuous.");
    Require(Editor::VoxelSphereService::CalculatePositions(
        document, 0U, {-20, -20, -20}, {-20, -20, -20}).empty(),
        "An out-of-document radius-zero Sphere was not rejected.");
}

void TestAtomicUndoRedoPaletteSaveAndOccupiedPreservation()
{
    auto document = Document({16U, 16U, 16U}, {{8U, 8U, 8U, 91U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    Editor::PaletteService palette;
    palette.SetPalette(document.GetPalette(), "Sphere test palette");
    Require(palette.SelectColor(42U), "Unable to select Sphere color.");
    const auto active = palette.ActiveColor();
    Require(active.has_value(), "Sphere palette has no active color.");
    const auto result = Sphere(
        session, history, document, {8, 8, 8}, {10, 8, 8}, active->Index);
    Require(result.Code == Editor::VoxelSphereResultCode::Applied &&
        result.ChangedVoxelCount == 32U && document.GetVoxelCount() == 33U &&
        document.GetVoxel({8, 8, 8})->PaletteIndex == 91U &&
        document.GetVoxel({10, 8, 8})->PaletteIndex == active->Index &&
        document.GetRevision() == 1U && history.UndoCount() == 1U &&
        history.UndoLabel() == "Create Voxel Sphere" &&
        session.rebuildCount_ == 1U,
        "Sphere was not one atomic palette-aware non-destructive edit.");
    Require(history.Undo(session) && document.GetVoxelCount() == 1U &&
        document.GetRevision() == 2U && history.Redo(session) &&
        document.GetVoxelCount() == 33U && document.GetRevision() == 3U &&
        session.rebuildCount_ == 3U,
        "Sphere Undo/Redo was not atomic.");
    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded() && !serialized.Bytes.empty(),
        "Sphere result is incompatible with VOX Save.");
}

void TestWorkplaneFaceCancellationAndToolState()
{
    auto document = Document({16U, 16U, 16U});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const Position workplaneCenter{5, 0, 5};
    const Position faceRadiusPoint{5, 3, 5};
    const auto result = Sphere(
        session, history, document, workplaneCenter, faceRadiusPoint, 17U);
    Require(result.Code == Editor::VoxelSphereResultCode::Applied &&
        document.HasVoxel(workplaneCenter) && document.HasVoxel(faceRadiusPoint),
        "Workplane center or voxel-face radius target was lost.");

    Editor::VoxelSphereInteraction interaction;
    Require(interaction.Begin(workplaneCenter, 4U) &&
        interaction.Update(faceRadiusPoint) && interaction.IsActive(),
        "Sphere preview interaction did not track center and radius.");
    interaction.Cancel();
    Require(!interaction.IsActive() && !interaction.Center() &&
        !interaction.RadiusPoint(),
        "Right-click/Escape Sphere cancellation did not clear immediately.");

    Editor::VoxelToolState state;
    state.SetActiveTool(Editor::ActiveVoxelTool::Sphere);
    Require(state.IsSphereActive() && state.IsEditingToolActive() &&
        std::string_view(Editor::ActiveVoxelToolName(state.ActiveTool())) ==
            "Sphere",
        "Sphere tool state is incorrect.");
}
}

int main()
{
    try
    {
        TestRadiusZeroAndSmallSphere();
        TestLargeSphereClippingContinuityAndLimits();
        TestAtomicUndoRedoPaletteSaveAndOccupiedPreservation();
        TestWorkplaneFaceCancellationAndToolState();
        std::cout << "Voxel Sphere tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Sphere tests failed: " << exception.what() << '\n';
        return 1;
    }
}
