#include "Transform/RotateVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

[[nodiscard]] bool PositionLess(
    const Position left, const Position right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

std::vector<Position> Sorted(std::vector<Position> positions)
{
    std::sort(positions.begin(), positions.end(), PositionLess);
    return positions;
}

Asset::Voxel::VoxelDocument MakeDocument(
    const std::vector<Asset::Vox::VoxVoxel>& voxels,
    const Asset::Vox::VoxDimensions dimensions = {16U, 16U, 16U})
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = dimensions;
    model.Voxels = voxels;
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-rotate-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Rotate test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Rotate test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Rotate compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Rotate compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return generation_;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return failRebuild_
            ? Editor::CommandResult::Failure("simulated Rotate rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 61U;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
    bool failRebuild_ = false;
    bool savedState_ = true;
};

Editor::SelectionService MakeSelection(
    const std::uint64_t generation,
    const std::vector<Position>& positions)
{
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(generation);
    static_cast<void>(selection.Apply(positions, Editor::SelectionMode::Replace));
    static_cast<void>(selection.ApplySortedVolume(
        selection.Voxels(), selection.Bounds(), Editor::SelectionMode::Replace));
    return selection;
}

void ApplySelectionTransition(
    Editor::SelectionService& selection,
    const Editor::VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "Rotate history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

Editor::RotateVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Editor::VoxelRotationDirection direction,
    const std::uint64_t generation = 61U)
{
    Require(preview.BeginPreview(document, selection, generation),
        "Rotate preview capture failed.");
    const auto geometry = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        selection.Voxels(), selection.EditableBounds(), direction);
    Require(geometry.Valid(), geometry.Message.empty()
        ? "Rotate geometry failed without a diagnostic."
        : geometry.Message);
    Require(preview.SetExplicitDestinations(
            document, selection, generation, geometry.Destinations),
        "Rotate explicit preview rejected valid geometry.");
    return Editor::RotateVoxelSelectionOperation::Build(
        document, selection, generation, preview, direction);
}

std::vector<Position> FilledRectangle(
    const std::int32_t minimumX, const std::int32_t minimumZ,
    const std::int32_t width, const std::int32_t depth)
{
    std::vector<Position> result;
    result.reserve(static_cast<std::size_t>(width * depth));
    for (std::int32_t z = 0; z < depth; ++z)
        for (std::int32_t x = 0; x < width; ++x)
            result.push_back({minimumX + x, 1, minimumZ + z});
    return result;
}

void RequireFourTurnCycle(
    const std::vector<Position>& source,
    Editor::SelectionBounds bounds,
    const Editor::VoxelRotationDirection direction)
{
    std::vector<Position> cycle = source;
    for (int turn = 0; turn < 4; ++turn)
    {
        const auto geometry =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                cycle, bounds, direction);
        Require(geometry.Valid(),
            "A mixed-parity Rotate cycle produced invalid geometry.");
        cycle = geometry.Destinations;
        bounds = geometry.Bounds;
    }
    Require(Sorted(cycle) == Sorted(source),
        "Four mixed-parity rotations drifted from the source cells.");
}

void TestPivotParityConvention()
{
    using Direction = Editor::VoxelRotationDirection;
    struct Case final
    {
        std::int32_t Width;
        std::int32_t Depth;
        std::int64_t ExpectedCenterDelta2X;
        std::int64_t ExpectedCenterDelta2Z;
    };
    constexpr std::array cases{
        Case{3, 3, 0, 0}, Case{2, 2, 0, 0},
        Case{2, 3, 1, -1}, Case{3, 2, -1, 1},
        Case{2, 5, 1, -1}, Case{5, 2, -1, 1}};
    for (const Case test : cases)
    {
        const std::vector<Position> source =
            FilledRectangle(6, 7, test.Width, test.Depth);
        const Editor::SelectionBounds bounds =
            Editor::SelectionBounds::FromCorners(
                {6, 1, 7},
                {6 + test.Width - 1, 1, 7 + test.Depth - 1});
        const auto clockwise =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                source, bounds, Direction::Clockwise);
        const auto counter =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                source, bounds, Direction::CounterClockwise);
        const auto clockwiseAgain =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                source, bounds, Direction::Clockwise);
        Require(clockwise.Valid() && counter.Valid() &&
            clockwise.SourceCenter2X == bounds.Minimum.X + bounds.Maximum.X &&
            clockwise.SourceCenter2Z == bounds.Minimum.Z + bounds.Maximum.Z &&
            clockwise.DestinationCenter2X - clockwise.SourceCenter2X ==
                test.ExpectedCenterDelta2X &&
            clockwise.DestinationCenter2Z - clockwise.SourceCenter2Z ==
                test.ExpectedCenterDelta2Z &&
            clockwise.GridCorrection2X == test.ExpectedCenterDelta2X &&
            clockwise.GridCorrection2Z == test.ExpectedCenterDelta2Z &&
            clockwise.Bounds.Dimensions() ==
                Asset::Voxel::VoxelDimensions{
                    static_cast<std::uint32_t>(test.Depth), 1U,
                    static_cast<std::uint32_t>(test.Width)},
            "Rotate did not follow the documented doubled-center convention.");
        Require(clockwise.Destinations == clockwiseAgain.Destinations,
            "Repeated preview construction is not deterministic.");
        const auto inverse =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                clockwise.Destinations, clockwise.Bounds,
                Direction::CounterClockwise);
        Require(inverse.Valid() &&
            Sorted(inverse.Destinations) == Sorted(source),
            "Clockwise then counterclockwise did not restore the source.");
        RequireFourTurnCycle(source, bounds, Direction::Clockwise);
        RequireFourTurnCycle(source, bounds, Direction::CounterClockwise);
    }
}

void TestExactCollisionMembership()
{
    using Direction = Editor::VoxelRotationDirection;
    const std::vector<Position> selected{
        {1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {1, 1, 2}};
    auto noCollisionDocument = MakeDocument({
        {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U}, {1U, 1U, 2U, 6U},
        // Occupied but unselected, inside the AABB, and not a destination.
        {2U, 1U, 2U, 9U}});
    auto selection = MakeSelection(61U, selected);
    Editor::TransformPreviewModel preview;
    const auto allowed = Prepare(
        noCollisionDocument, selection, preview, Direction::Clockwise);
    Require(allowed.Ready() && !preview.HasCollisions() &&
        preview.CollisionPositions().empty(),
        "An empty/non-destination AABB cell produced a false collision.");

    auto collisionDocument = MakeDocument({
        {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U}, {1U, 1U, 2U, 6U},
        // Exact external destination of the clockwise preview.
        {2U, 1U, 3U, 9U}});
    auto collisionSelection = MakeSelection(61U, selected);
    Editor::TransformPreviewModel collisionPreview;
    const auto blocked = Prepare(
        collisionDocument, collisionSelection, collisionPreview,
        Direction::Clockwise);
    // Overlap/Merge (decision produit) : la preview signale exactement le
    // recouvrement externe (information), le commit reste pret (fusion).
    Require(blocked.Ready() &&
        collisionPreview.CollisionPositions().size() == 1U &&
        collisionPreview.CollisionPositions().front() == Position{2, 1, 3} &&
        std::find(selected.begin(), selected.end(), Position{2, 1, 3}) ==
            selected.end(),
        "Rotate did not report the exact external overlap as information "
        "while keeping the merge commit ready.");
}

void TestExactGeometryAndCycles()
{
    using Direction = Editor::VoxelRotationDirection;
    const auto point = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        std::array{Position{5, 7, 9}},
        Editor::SelectionBounds::FromCorners({5, 7, 9}, {5, 7, 9}),
        Direction::Clockwise);
    Require(point.Valid() && point.Destinations ==
            std::vector<Position>{{5, 7, 9}},
        "A zero-radius Rotate changed a single voxel or its Y coordinate.");
    const std::vector<Position> lineX{{2, 4, 3}, {3, 4, 3}, {4, 4, 3}};
    const auto clockwise = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        lineX, Editor::SelectionBounds::FromCorners({2, 4, 3}, {4, 4, 3}),
        Direction::Clockwise);
    Require(clockwise.Valid() && clockwise.Pivot2X == 6 &&
        clockwise.Pivot2Z == 6 &&
        Sorted(clockwise.Destinations) ==
            Sorted({{3, 4, 2}, {3, 4, 3}, {3, 4, 4}}) &&
        clockwise.Bounds.Dimensions() ==
            Asset::Voxel::VoxelDimensions{1U, 1U, 3U},
        "Clockwise X line rotation or integer pivot is incorrect.");
    const auto counter = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        lineX, Editor::SelectionBounds::FromCorners({2, 4, 3}, {4, 4, 3}),
        Direction::CounterClockwise);
    Require(counter.Valid() && Sorted(counter.Destinations) ==
            Sorted({{3, 4, 4}, {3, 4, 3}, {3, 4, 2}}),
        "Counterclockwise X line rotation is incorrect.");
    const std::vector<Position> lineZ{{3, 4, 2}, {3, 4, 3}, {3, 4, 4}};
    const auto zClockwise =
        Editor::RotateVoxelSelectionOperation::BuildGeometry(
            lineZ,
            Editor::SelectionBounds::FromCorners({3, 4, 2}, {3, 4, 4}),
            Direction::Clockwise);
    Require(zClockwise.Valid() && Sorted(zClockwise.Destinations) ==
            Sorted({{2, 4, 3}, {3, 4, 3}, {4, 4, 3}}),
        "Clockwise Z line rotation is incorrect.");

    const std::vector<Position> rectangle{
        {2, 1, 5}, {3, 1, 5}, {2, 1, 6},
        {3, 1, 6}, {2, 1, 7}, {3, 1, 7}};
    const auto even = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        rectangle,
        Editor::SelectionBounds::FromCorners({2, 1, 5}, {3, 1, 7}),
        Direction::Clockwise);
    Require(even.Valid() && even.Destinations.size() == rectangle.size() &&
        even.Bounds.Dimensions() ==
            Asset::Voxel::VoxelDimensions{3U, 1U, 2U},
        "Even 2x3 rotation did not swap X/Z dimensions exactly.");
    Require(std::all_of(even.Destinations.begin(), even.Destinations.end(),
        [](const Position position) { return position.Y == 1; }),
        "Rotate changed a source Y coordinate.");

    std::vector<Position> cube;
    for (std::int32_t z = 2; z <= 3; ++z)
        for (std::int32_t y = 4; y <= 5; ++y)
            for (std::int32_t x = 6; x <= 7; ++x)
                cube.push_back({x, y, z});
    const auto cubeTurn = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        cube, Editor::SelectionBounds::FromCorners({6, 4, 2}, {7, 5, 3}),
        Direction::Clockwise);
    Require(cubeTurn.Valid() && Sorted(cubeTurn.Destinations) == Sorted(cube),
        "An even 2x2x2 cube did not rotate bijectively around its center.");

    const std::vector<Position> asymmetric{
        {1, 2, 1}, {2, 2, 1}, {3, 2, 1}, {1, 2, 2}};
    std::vector<Position> cycle = asymmetric;
    Editor::SelectionBounds cycleBounds =
        Editor::SelectionBounds::FromCorners({1, 2, 1}, {3, 2, 2});
    for (int turn = 0; turn < 4; ++turn)
    {
        const auto geometry =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                cycle, cycleBounds, Direction::Clockwise);
        Require(geometry.Valid(), "Clockwise cycle geometry became invalid.");
        cycle = geometry.Destinations;
        cycleBounds = geometry.Bounds;
    }
    Require(Sorted(cycle) == Sorted(asymmetric),
        "Four clockwise rotations did not restore exact positions.");
    for (int turn = 0; turn < 4; ++turn)
    {
        const auto geometry =
            Editor::RotateVoxelSelectionOperation::BuildGeometry(
                cycle, cycleBounds, Direction::CounterClockwise);
        Require(geometry.Valid(),
            "Counterclockwise cycle geometry became invalid.");
        cycle = geometry.Destinations;
        cycleBounds = geometry.Bounds;
    }
    Require(Sorted(cycle) == Sorted(asymmetric),
        "Four counterclockwise rotations did not restore exact positions.");
    const auto oneClockwise =
        Editor::RotateVoxelSelectionOperation::BuildGeometry(
            asymmetric,
            Editor::SelectionBounds::FromCorners({1, 2, 1}, {3, 2, 2}),
            Direction::Clockwise);
    const auto oneCounterClockwise =
        Editor::RotateVoxelSelectionOperation::BuildGeometry(
            asymmetric,
            Editor::SelectionBounds::FromCorners({1, 2, 1}, {3, 2, 2}),
            Direction::CounterClockwise);
    const auto clockwiseRebuilt =
        Editor::RotateVoxelSelectionOperation::BuildGeometry(
            asymmetric,
            Editor::SelectionBounds::FromCorners({1, 2, 1}, {3, 2, 2}),
            Direction::Clockwise);
    Require(oneClockwise.Valid() && oneCounterClockwise.Valid() &&
        Sorted(oneClockwise.Destinations) !=
            Sorted(oneCounterClockwise.Destinations) &&
        oneClockwise.Destinations == clockwiseRebuilt.Destinations,
        "Q/Shift+Q geometry is not distinct and deterministic.");
    const auto inverse = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        oneClockwise.Destinations, oneClockwise.Bounds,
        Direction::CounterClockwise);
    Require(inverse.Valid() && Sorted(inverse.Destinations) == Sorted(asymmetric),
        "Clockwise then counterclockwise was not the identity.");
}

void TestAtomicRotateUndoRedoAndSelection()
{
    auto document = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {4U, 1U, 2U, 5U}});
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(
        session.generation_, {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}});
    Editor::TransformPreviewModel preview;
    const auto revision = document.GetRevision();
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelRotationDirection::Clockwise);
    Require(prepared.Ready() && !document.IsDirty() &&
        history.UndoCount() == 0U && document.GetRevision() == revision,
        "Rotate preview mutated document or history.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        document.GetVoxelCount() == 3U && document.IsDirty() &&
        document.GetVoxel({3, 1, 3})->PaletteIndex == 3U &&
        document.GetVoxel({3, 1, 2})->PaletteIndex == 4U &&
        document.GetVoxel({3, 1, 1})->PaletteIndex == 5U &&
        !document.HasVoxel({2, 1, 2}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({3, 1, 1}, {3, 1, 3}),
        "Atomic Rotate lost colors, count, destination, or selection.");

    const auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && !document.IsDirty() &&
        document.GetVoxel({2, 1, 2})->PaletteIndex == 3U &&
        document.GetVoxel({4, 1, 2})->PaletteIndex == 5U &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({2, 1, 2}, {4, 1, 2}),
        "Undo did not restore Rotate source and selection.");
    const auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && document.GetVoxel({3, 1, 1})->PaletteIndex == 5U &&
        selection.Contains({3, 1, 3}),
        "Redo did not restore Rotate destination and colors.");
}

void TestRefusalsAndRollback()
{
    using Direction = Editor::VoxelRotationDirection;
    auto collisionDocument = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {4U, 1U, 2U, 5U},
        {3U, 1U, 3U, 9U}});
    collisionDocument.MarkSaved();
    auto collisionSelection = MakeSelection(
        61U, {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}});
    Editor::TransformPreviewModel collisionPreview;
    const auto collision = Prepare(collisionDocument, collisionSelection,
        collisionPreview, Direction::Clockwise);
    // Overlap/Merge (decision produit) : recouvrement signale, commit pret,
    // aucune mutation avant Execute.
    Require(collision.Ready() && collisionPreview.HasCollisions() &&
        !collisionDocument.IsDirty() && collisionDocument.GetVoxelCount() == 4U,
        "External Rotate overlap must be reported, accepted and non-mutating.");

    auto edgeDocument = MakeDocument({
        {0U, 1U, 0U, 3U}, {1U, 1U, 0U, 4U}, {2U, 1U, 0U, 5U}});
    edgeDocument.MarkSaved();
    auto edgeSelection = MakeSelection(
        61U, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}});
    Editor::TransformPreviewModel edgePreview;
    const auto outside = Prepare(
        edgeDocument, edgeSelection, edgePreview, Direction::Clockwise);
    Require(outside.Code ==
            Editor::RotateVoxelSelectionResultCode::OutOfBounds &&
        !edgeDocument.IsDirty() && edgeDocument.GetVoxelCount() == 3U,
        "Out-of-bounds Rotate changed the document.");

    auto document = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {4U, 1U, 2U, 5U}});
    document.MarkSaved();
    auto selection = MakeSelection(
        61U, {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}});
    Editor::TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, 61U),
        "Stale Rotate preview capture failed.");
    const auto geometry = Editor::RotateVoxelSelectionOperation::BuildGeometry(
        selection.Voxels(), selection.EditableBounds(), Direction::Clockwise);
    Require(preview.SetExplicitDestinations(
        document, selection, 61U, geometry.Destinations),
        "Stale Rotate preview destinations failed.");
    const auto wrongGeneration = Editor::RotateVoxelSelectionOperation::Build(
        document, selection, 62U, preview, Direction::Clockwise);
    Require(wrongGeneration.Code ==
        Editor::RotateVoxelSelectionResultCode::ModelChanged,
        "Rotate accepted a different document generation.");
    static_cast<void>(selection.Select({2, 1, 2}));
    const auto changedSelection = Editor::RotateVoxelSelectionOperation::Build(
        document, selection, 61U, preview, Direction::Clockwise);
    Require(changedSelection.Code ==
        Editor::RotateVoxelSelectionResultCode::SelectionChanged,
        "Rotate accepted a changed selection.");

    auto staleDocument = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {4U, 1U, 2U, 5U}});
    staleDocument.MarkSaved();
    auto staleSelection = MakeSelection(
        61U, {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}});
    Editor::TransformPreviewModel stalePreview;
    Require(stalePreview.BeginPreview(staleDocument, staleSelection, 61U),
        "Stale Rotate source capture failed.");
    const auto staleGeometry =
        Editor::RotateVoxelSelectionOperation::BuildGeometry(
            staleSelection.Voxels(), staleSelection.EditableBounds(),
            Direction::Clockwise);
    Require(staleGeometry.Valid() && stalePreview.SetExplicitDestinations(
            staleDocument, staleSelection, 61U,
            staleGeometry.Destinations) &&
        staleDocument.SetVoxel({2, 1, 2}, 12U).Changed,
        "Stale Rotate source mutation setup failed.");
    const auto staleSource = Editor::RotateVoxelSelectionOperation::Build(
        staleDocument, staleSelection, 61U, stalePreview,
        Direction::Clockwise);
    Require(staleSource.Code ==
            Editor::RotateVoxelSelectionResultCode::ModelChanged &&
        staleDocument.GetVoxel({2, 1, 2})->PaletteIndex == 12U,
        "Rotate accepted a changed source voxel or color.");

    auto rollbackDocument = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {4U, 1U, 2U, 5U}});
    rollbackDocument.MarkSaved();
    TestSession session(rollbackDocument);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(rollbackDocument);
    auto rollbackSelection = MakeSelection(
        61U, {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}});
    Editor::TransformPreviewModel rollbackPreview;
    auto rollback = Prepare(rollbackDocument, rollbackSelection,
        rollbackPreview, Direction::CounterClockwise);
    Require(rollback.Ready(), "Rollback Rotate was not prepared.");
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, std::move(rollback.Operation));
    Require(!failed && rollbackDocument.GetVoxelCount() == 3U &&
        rollbackDocument.GetVoxel({2, 1, 2})->PaletteIndex == 3U &&
        !rollbackDocument.IsDirty() && history.UndoCount() == 0U,
        "Failed Rotate did not roll back atomically.");
}

void TestLargeRotateSaveAndReopen()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    std::vector<Position> positions;
    voxels.reserve(512U);
    positions.reserve(512U);
    for (std::uint8_t z = 1U; z <= 8U; ++z)
        for (std::uint8_t y = 1U; y <= 8U; ++y)
            for (std::uint8_t x = 1U; x <= 8U; ++x)
            {
                voxels.push_back({x, y, z,
                    static_cast<std::uint8_t>(1U + (x * 3U + z) % 31U)});
                positions.push_back({x, y, z});
            }
    auto document = MakeDocument(voxels, {64U, 64U, 64U});
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(61U, positions);
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelRotationDirection::Clockwise);
    Require(prepared.Ready() && preview.VoxelCount() == 512U,
        "Dense 512-voxel Rotate did not prepare exactly 512 voxels.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetVoxelCount() == 512U && selection.Count() == 512U,
        "Dense Rotate changed its voxel or selection count.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Rotated document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("VoxelForgeRotate-" + std::to_string(stamp) + ".vox");
    struct TemporaryFile final
    {
        std::filesystem::path Path;
        ~TemporaryFile()
        {
            std::error_code error;
            std::filesystem::remove(Path, error);
        }
    } temporary{path};
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        Require(stream.is_open(), "Unable to create Rotate VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to write Rotate VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 512U,
        "Saved and reopened VOX did not retain the rotated selection.");
}
}

int main()
{
    try
    {
        TestExactGeometryAndCycles();
        TestPivotParityConvention();
        TestExactCollisionMembership();
        TestAtomicRotateUndoRedoAndSelection();
        TestRefusalsAndRollback();
        TestLargeRotateSaveAndReopen();
        std::cout << "Voxel Rotate tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Rotate tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
