#include "Transform/MirrorVoxelSelectionOperation.h"
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
        source, "voxel-mirror-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Mirror test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Mirror test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Mirror compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Mirror compatibility grid.");
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
            ? Editor::CommandResult::Failure("simulated Mirror rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 71U;
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
        "Mirror history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

Editor::MirrorVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Editor::VoxelMirrorAxis axis,
    const std::uint64_t generation = 71U)
{
    Require(preview.BeginPreview(document, selection, generation),
        "Mirror preview capture failed.");
    const auto geometry = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        selection.Voxels(), selection.EditableBounds(), axis);
    Require(geometry.Valid(), geometry.Message.empty()
        ? "Mirror geometry failed without a diagnostic."
        : geometry.Message);
    Require(preview.SetExplicitDestinations(
            document, selection, generation, geometry.Destinations),
        "Mirror explicit preview rejected valid geometry.");
    return Editor::MirrorVoxelSelectionOperation::Build(
        document, selection, generation, preview, axis);
}

void TestExactGeometry()
{
    using Axis = Editor::VoxelMirrorAxis;
    const std::vector<Position> source{
        {2, 4, 3}, {3, 4, 3}, {4, 4, 3}, {2, 4, 4}};
    const auto bounds =
        Editor::SelectionBounds::FromCorners({2, 4, 3}, {4, 4, 4});
    const auto mirrorX = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        source, bounds, Axis::X);
    const auto mirrorZ = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        source, bounds, Axis::Z);
    Require(mirrorX.Valid() && mirrorZ.Valid() &&
        Sorted(mirrorX.Destinations) ==
            Sorted({{4, 4, 3}, {3, 4, 3}, {2, 4, 3}, {4, 4, 4}}) &&
        Sorted(mirrorZ.Destinations) ==
            Sorted({{2, 4, 4}, {3, 4, 4}, {4, 4, 4}, {2, 4, 3}}),
        "Mirror X/Z geometry is incorrect.");
    Require(mirrorX.Bounds.Dimensions() == bounds.Dimensions() &&
        mirrorZ.Bounds.Dimensions() == bounds.Dimensions() &&
        std::all_of(mirrorX.Destinations.begin(), mirrorX.Destinations.end(),
            [](const Position position) { return position.Y == 4; }),
        "Mirror changed dimensions or Y coordinates.");
    const std::vector<Position> uniqueMirrorX = Sorted(mirrorX.Destinations);
    Require(uniqueMirrorX.size() == source.size() &&
        std::adjacent_find(uniqueMirrorX.begin(), uniqueMirrorX.end()) ==
            uniqueMirrorX.end(),
        "Mirror produced a duplicate destination.");

    const auto xTwice = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        mirrorX.Destinations, mirrorX.Bounds, Axis::X);
    const auto zTwice = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        mirrorZ.Destinations, mirrorZ.Bounds, Axis::Z);
    Require(xTwice.Valid() && zTwice.Valid() &&
        Sorted(xTwice.Destinations) == Sorted(source) &&
        Sorted(zTwice.Destinations) == Sorted(source),
        "Mirror X or Z is not exactly involutive.");

    const auto xThenZ = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        mirrorX.Destinations, mirrorX.Bounds, Axis::Z);
    const auto zThenX = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        mirrorZ.Destinations, mirrorZ.Bounds, Axis::X);
    Require(xThenZ.Valid() && zThenX.Valid() &&
        Sorted(xThenZ.Destinations) == Sorted(zThenX.Destinations),
        "Orthogonal Mirror operations are not deterministic.");

    const std::array<Position, 2U> even{{{7, 2, 5}, {8, 2, 5}}};
    const auto evenMirror = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        even, Editor::SelectionBounds::FromCorners({7, 2, 5}, {8, 2, 5}),
        Axis::X);
    const std::array<Position, 3U> odd{{
        {7, 2, 5}, {8, 2, 5}, {9, 2, 5}}};
    const auto oddMirror = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        odd, Editor::SelectionBounds::FromCorners({7, 2, 5}, {9, 2, 5}),
        Axis::X);
    Require(evenMirror.Valid() && oddMirror.Valid() &&
        evenMirror.Destinations == std::vector<Position>{{8, 2, 5}, {7, 2, 5}} &&
        oddMirror.Destinations ==
            std::vector<Position>{{9, 2, 5}, {8, 2, 5}, {7, 2, 5}},
        "Even or odd Mirror required a half-step correction.");
}

void TestAtomicMirrorUndoRedoAndIdentity()
{
    auto document = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U},
        {4U, 1U, 2U, 5U}, {2U, 1U, 3U, 11U}});
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(session.generation_,
        {{2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {2, 1, 3}});
    Editor::TransformPreviewModel preview;
    const auto revision = document.GetRevision();
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelMirrorAxis::X);
    Require(prepared.Ready() && !document.IsDirty() &&
        history.UndoCount() == 0U && document.GetRevision() == revision,
        "Mirror preview mutated document or history.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        document.GetVoxelCount() == 4U && document.IsDirty() &&
        document.GetVoxel({4, 1, 3})->PaletteIndex == 11U &&
        !document.HasVoxel({2, 1, 3}) &&
        selection.Count() == 4U &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({2, 1, 2}, {4, 1, 3}),
        "Atomic Mirror lost colors, count, destination, or selection.");

    const auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && !document.IsDirty() &&
        document.GetVoxel({2, 1, 3})->PaletteIndex == 11U &&
        !document.HasVoxel({4, 1, 3}),
        "Undo did not restore Mirror source and colors.");
    const auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && document.GetVoxel({4, 1, 3})->PaletteIndex == 11U &&
        selection.Contains({4, 1, 3}),
        "Redo did not restore Mirror destination and selection.");

    auto symmetric = MakeDocument({
        {2U, 1U, 2U, 7U}, {3U, 1U, 2U, 7U}});
    symmetric.MarkSaved();
    auto symmetricSelection = MakeSelection(
        71U, {{2, 1, 2}, {3, 1, 2}});
    Editor::TransformPreviewModel identityPreview;
    const auto identity = Prepare(symmetric, symmetricSelection,
        identityPreview, Editor::VoxelMirrorAxis::X);
    Require(identity.Code == Editor::MirrorVoxelSelectionResultCode::NoChange &&
        !symmetric.IsDirty() && symmetric.GetRevision() == 0U,
        "A symmetric Mirror created history, dirty state, or a revision.");
}

void TestRefusalsAndRollback()
{
    using Axis = Editor::VoxelMirrorAxis;
    auto collisionDocument = MakeDocument({
        {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {1U, 1U, 2U, 5U}, {2U, 1U, 2U, 9U}});
    collisionDocument.MarkSaved();
    auto collisionSelection = MakeSelection(
        71U, {{1, 1, 1}, {2, 1, 1}, {1, 1, 2}});
    Editor::TransformPreviewModel collisionPreview;
    const auto collision = Prepare(collisionDocument, collisionSelection,
        collisionPreview, Axis::X);
    Require(collision.Code == Editor::MirrorVoxelSelectionResultCode::Collision &&
        collisionPreview.CollisionPositions().size() == 1U &&
        collisionPreview.CollisionPositions().front() == Position{2, 1, 2} &&
        !collisionDocument.IsDirty(),
        "External Mirror collision was not rejected exactly.");

    auto document = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {2U, 1U, 3U, 5U}});
    document.MarkSaved();
    auto selection = MakeSelection(
        71U, {{2, 1, 2}, {3, 1, 2}, {2, 1, 3}});
    Editor::TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, 71U),
        "Stale Mirror preview capture failed.");
    const auto geometry = Editor::MirrorVoxelSelectionOperation::BuildGeometry(
        selection.Voxels(), selection.EditableBounds(), Axis::X);
    Require(preview.SetExplicitDestinations(
        document, selection, 71U, geometry.Destinations),
        "Stale Mirror preview destinations failed.");
    Require(Editor::MirrorVoxelSelectionOperation::Build(
        document, selection, 72U, preview, Axis::X).Code ==
            Editor::MirrorVoxelSelectionResultCode::ModelChanged,
        "Mirror accepted a different document generation.");
    static_cast<void>(selection.Select({2, 1, 2}));
    Require(Editor::MirrorVoxelSelectionOperation::Build(
        document, selection, 71U, preview, Axis::X).Code ==
            Editor::MirrorVoxelSelectionResultCode::SelectionChanged,
        "Mirror accepted a changed selection.");

    auto stale = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {2U, 1U, 3U, 5U}});
    stale.MarkSaved();
    auto staleSelection = MakeSelection(
        71U, {{2, 1, 2}, {3, 1, 2}, {2, 1, 3}});
    Editor::TransformPreviewModel stalePreview;
    Require(stalePreview.BeginPreview(stale, staleSelection, 71U),
        "Source-change Mirror capture failed.");
    const auto staleGeometry =
        Editor::MirrorVoxelSelectionOperation::BuildGeometry(
            staleSelection.Voxels(), staleSelection.EditableBounds(), Axis::X);
    Require(stalePreview.SetExplicitDestinations(
            stale, staleSelection, 71U, staleGeometry.Destinations) &&
        stale.SetVoxel({2, 1, 2}, 12U).Changed,
        "Source-change Mirror setup failed.");
    Require(Editor::MirrorVoxelSelectionOperation::Build(
        stale, staleSelection, 71U, stalePreview, Axis::X).Code ==
            Editor::MirrorVoxelSelectionResultCode::ModelChanged,
        "Mirror accepted a changed source value.");

    auto outsideDocument = MakeDocument({{0U, 1U, 0U, 3U}});
    outsideDocument.MarkSaved();
    auto outsideSelection = MakeSelection(71U, {{0, 1, 0}});
    Editor::TransformPreviewModel outsidePreview;
    Require(outsidePreview.BeginPreview(
            outsideDocument, outsideSelection, 71U) &&
        outsidePreview.SetExplicitDestinations(
            outsideDocument, outsideSelection, 71U,
            std::array{Position{-1, 1, 0}}),
        "Out-of-bounds Mirror setup failed.");
    Require(Editor::MirrorVoxelSelectionOperation::Build(
        outsideDocument, outsideSelection, 71U, outsidePreview, Axis::X).Code ==
            Editor::MirrorVoxelSelectionResultCode::OutOfBounds,
        "Out-of-bounds Mirror was not rejected before mutation.");

    auto rollbackDocument = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}, {2U, 1U, 3U, 5U}});
    rollbackDocument.MarkSaved();
    TestSession session(rollbackDocument);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(rollbackDocument);
    auto rollbackSelection = MakeSelection(
        71U, {{2, 1, 2}, {3, 1, 2}, {2, 1, 3}});
    Editor::TransformPreviewModel rollbackPreview;
    auto rollback = Prepare(rollbackDocument, rollbackSelection,
        rollbackPreview, Axis::X);
    Require(rollback.Ready(), "Rollback Mirror was not prepared.");
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, std::move(rollback.Operation));
    Require(!failed && rollbackDocument.GetVoxelCount() == 3U &&
        rollbackDocument.GetVoxel({2, 1, 3})->PaletteIndex == 5U &&
        !rollbackDocument.IsDirty() && history.UndoCount() == 0U,
        "Failed Mirror did not roll back atomically.");
}

void TestLargeMirrorSaveAndReopen()
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
    auto selection = MakeSelection(71U, positions);
    Editor::TransformPreviewModel preview;
    const auto started = std::chrono::steady_clock::now();
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelMirrorAxis::X);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Require(prepared.Ready() && preview.VoxelCount() == 512U &&
        elapsed < std::chrono::seconds(1),
        "Dense 512-voxel Mirror was incomplete or unreasonably slow.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetVoxelCount() == 512U && selection.Count() == 512U &&
        session.rebuildCount_ == 1U,
        "Dense Mirror changed count or rebuilt more than once.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Mirrored document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("VoxelForgeMirror-" + std::to_string(stamp) + ".vox");
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
        Require(stream.is_open(), "Unable to create Mirror VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to write Mirror VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 512U,
        "Saved and reopened VOX did not retain the mirrored selection.");
}
}

int main()
{
    try
    {
        TestExactGeometry();
        TestAtomicMirrorUndoRedoAndIdentity();
        TestRefusalsAndRollback();
        TestLargeMirrorSaveAndReopen();
        std::cout << "Voxel Mirror tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Mirror tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
