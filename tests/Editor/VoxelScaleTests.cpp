#include "Transform/ScaleVoxelSelectionOperation.h"
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
#include <tuple>
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
        source, "voxel-scale-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Scale test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Scale test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Scale compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Scale compatibility grid.");
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
            ? Editor::CommandResult::Failure("simulated Scale rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 91U;
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
        "Scale history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

Editor::ScaleVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Editor::VoxelScaleMode mode,
    const std::uint64_t generation = 91U)
{
    Require(preview.BeginPreview(document, selection, generation, 0U,
            Editor::TransformPreviewCollisionPolicy::IgnoreSource),
        "Scale preview capture failed.");
    const auto geometry = Editor::ScaleVoxelSelectionOperation::BuildGeometry(
        preview.SourceVoxels(), selection.EditableBounds(), mode);
    Require(geometry.Valid(), geometry.Message.empty()
        ? "Scale geometry failed without a diagnostic." : geometry.Message);
    Require(preview.SetExplicitVoxelDestinations(
            document, selection, generation, geometry.Destinations),
        "Scale explicit preview rejected valid geometry.");
    return Editor::ScaleVoxelSelectionOperation::Build(
        document, selection, generation, preview, mode);
}

void TestExactGeometry()
{
    using Mode = Editor::VoxelScaleMode;
    const Editor::TransformPreviewVoxel voxel{
        {2, 3, 4}, {2, 3, 4}, {7U},
        Editor::TransformPreviewVoxelState::Valid};
    const auto bounds =
        Editor::SelectionBounds::FromCorners({2, 3, 4}, {2, 3, 4});
    for (const auto [mode, count, dimensions] : std::array{
            std::tuple{Mode::X, 2U, Asset::Voxel::VoxelDimensions{2U, 1U, 1U}},
            std::tuple{Mode::Y, 2U, Asset::Voxel::VoxelDimensions{1U, 2U, 1U}},
            std::tuple{Mode::Z, 2U, Asset::Voxel::VoxelDimensions{1U, 1U, 2U}},
            std::tuple{Mode::Uniform, 8U,
                Asset::Voxel::VoxelDimensions{2U, 2U, 2U}}})
    {
        const auto geometry = Editor::ScaleVoxelSelectionOperation::BuildGeometry(
            std::array{voxel}, bounds, mode);
        Require(geometry.Valid() && geometry.Destinations.size() == count &&
            geometry.Bounds.Dimensions() == dimensions &&
            geometry.Bounds.Minimum == bounds.Minimum &&
            std::all_of(geometry.Destinations.begin(), geometry.Destinations.end(),
                [](const auto& destination)
                {
                    return destination.Value.PaletteIndex == 7U;
                }), "Single-voxel Scale geometry is not exact.");
    }

    const std::array<Editor::TransformPreviewVoxel, 3U> line{{
        {{4, 5, 6}, {4, 5, 6}, {3U}, Editor::TransformPreviewVoxelState::Valid},
        {{5, 5, 6}, {5, 5, 6}, {9U}, Editor::TransformPreviewVoxelState::Valid},
        {{6, 5, 6}, {6, 5, 6}, {12U}, Editor::TransformPreviewVoxelState::Valid}}};
    const auto lineBounds =
        Editor::SelectionBounds::FromCorners({4, 5, 6}, {6, 5, 6});
    const auto x = Editor::ScaleVoxelSelectionOperation::BuildGeometry(
        line, lineBounds, Mode::X);
    const auto uniform = Editor::ScaleVoxelSelectionOperation::BuildGeometry(
        line, lineBounds, Mode::Uniform);
    Require(x.Valid() && x.Destinations.size() == 6U &&
        x.Bounds == Editor::SelectionBounds::FromCorners({4, 5, 6}, {9, 5, 6}) &&
        uniform.Valid() && uniform.Destinations.size() == 24U &&
        uniform.Bounds ==
            Editor::SelectionBounds::FromCorners({4, 5, 6}, {9, 6, 7}),
        "Scale did not double line dimensions from the minimum anchor.");
    std::vector<Position> unique;
    for (const auto& destination : uniform.Destinations)
        unique.push_back(destination.DestinationPosition);
    std::sort(unique.begin(), unique.end(), PositionLess);
    Require(std::adjacent_find(unique.begin(), unique.end()) == unique.end(),
        "Uniform Scale generated duplicate destinations.");
}

void TestAtomicScaleUndoRedo()
{
    auto document = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 11U}});
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(
        session.generation_, {{2, 1, 2}, {3, 1, 2}});
    Editor::TransformPreviewModel preview;
    const auto revision = document.GetRevision();
    auto prepared = Prepare(
        document, selection, preview, Editor::VoxelScaleMode::Uniform);
    Require(prepared.Ready() && !document.IsDirty() &&
        document.GetRevision() == revision && history.UndoCount() == 0U &&
        preview.SourceVoxels().size() == 2U && preview.VoxelCount() == 16U,
        "Scale preview mutated the document or did not expand one-to-many.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U && document.IsDirty() &&
        document.GetVoxelCount() == 16U && selection.Count() == 16U &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({2, 1, 2}, {5, 2, 3}) &&
        document.GetVoxel({2, 2, 3})->PaletteIndex == 3U &&
        document.GetVoxel({5, 2, 3})->PaletteIndex == 11U &&
        session.rebuildCount_ == 1U,
        "Atomic Scale lost geometry, colors, selection, or rebuild count.");

    const auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && !document.IsDirty() && document.GetVoxelCount() == 2U &&
        selection.Count() == 2U &&
        document.GetVoxel({3, 1, 2})->PaletteIndex == 11U,
        "Undo did not restore the exact Scale source.");
    const auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && document.GetVoxelCount() == 16U &&
        selection.Count() == 16U && session.rebuildCount_ == 3U,
        "Redo did not restore the exact Scale destination.");
}

void TestRefusalsAndRollback()
{
    auto collision = MakeDocument({
        {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}, {4U, 1U, 1U, 9U}});
    collision.MarkSaved();
    auto collisionSelection = MakeSelection(91U, {{1, 1, 1}, {2, 1, 1}});
    Editor::TransformPreviewModel collisionPreview;
    const auto collisionResult = Prepare(collision, collisionSelection,
        collisionPreview, Editor::VoxelScaleMode::X);
    Require(collisionResult.Code ==
            Editor::ScaleVoxelSelectionResultCode::Collision &&
        collisionPreview.CollisionPositions().size() == 1U &&
        collisionPreview.CollisionPositions().front() == Position{4, 1, 1} &&
        !collision.IsDirty(),
        "Scale external collision was not rejected before mutation.");

    auto outside = MakeDocument({
        {7U, 1U, 1U, 3U}, {8U, 1U, 1U, 4U}}, {10U, 10U, 10U});
    outside.MarkSaved();
    auto outsideSelection = MakeSelection(91U, {{7, 1, 1}, {8, 1, 1}});
    Editor::TransformPreviewModel outsidePreview;
    const auto outsideResult = Prepare(
        outside, outsideSelection, outsidePreview, Editor::VoxelScaleMode::X);
    Require(outsideResult.Code ==
            Editor::ScaleVoxelSelectionResultCode::OutOfBounds &&
        outsidePreview.HasOutOfBounds() && !outside.IsDirty(),
        "Scale out-of-bounds destination was not rejected.");

    auto stale = MakeDocument({{2U, 1U, 2U, 3U}});
    stale.MarkSaved();
    auto staleSelection = MakeSelection(91U, {{2, 1, 2}});
    Editor::TransformPreviewModel stalePreview;
    Require(stalePreview.BeginPreview(stale, staleSelection, 91U),
        "Stale Scale preview capture failed.");
    const auto staleGeometry = Editor::ScaleVoxelSelectionOperation::BuildGeometry(
        stalePreview.SourceVoxels(), staleSelection.EditableBounds(),
        Editor::VoxelScaleMode::Uniform);
    Require(stalePreview.SetExplicitVoxelDestinations(
            stale, staleSelection, 91U, staleGeometry.Destinations) &&
        stale.SetVoxel({2, 1, 2}, 8U).Changed,
        "Stale Scale setup failed.");
    Require(Editor::ScaleVoxelSelectionOperation::Build(
        stale, staleSelection, 91U, stalePreview,
        Editor::VoxelScaleMode::Uniform).Code ==
            Editor::ScaleVoxelSelectionResultCode::ModelChanged,
        "Scale accepted a changed document source.");

    auto rollbackDocument = MakeDocument({
        {2U, 1U, 2U, 3U}, {3U, 1U, 2U, 4U}});
    rollbackDocument.MarkSaved();
    TestSession session(rollbackDocument);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(rollbackDocument);
    auto rollbackSelection = MakeSelection(91U, {{2, 1, 2}, {3, 1, 2}});
    Editor::TransformPreviewModel rollbackPreview;
    auto rollback = Prepare(rollbackDocument, rollbackSelection,
        rollbackPreview, Editor::VoxelScaleMode::X);
    Require(rollback.Ready(), "Rollback Scale was not prepared.");
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, std::move(rollback.Operation));
    Require(!failed && rollbackDocument.GetVoxelCount() == 2U &&
        rollbackDocument.GetVoxel({2, 1, 2})->PaletteIndex == 3U &&
        rollbackDocument.GetVoxel({3, 1, 2})->PaletteIndex == 4U &&
        !rollbackDocument.IsDirty() && history.UndoCount() == 0U,
        "Failed Scale did not roll back atomically.");
}

void TestLargeScaleSaveAndReopen()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    std::vector<Position> positions;
    voxels.reserve(512U);
    positions.reserve(512U);
    for (std::uint8_t z = 1U; z <= 8U; ++z)
        for (std::uint8_t y = 1U; y <= 8U; ++y)
            for (std::uint8_t x = 1U; x <= 8U; ++x)
            {
                const auto color = static_cast<std::uint8_t>(
                    1U + (x * 3U + y * 5U + z) % 31U);
                voxels.push_back({x, y, z, color});
                positions.push_back({x, y, z});
            }
    auto document = MakeDocument(voxels, {64U, 64U, 64U});
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(91U, positions);
    Editor::TransformPreviewModel preview;
    const auto started = std::chrono::steady_clock::now();
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelScaleMode::Uniform);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Require(prepared.Ready() && preview.SourceVoxels().size() == 512U &&
        preview.VoxelCount() == 4096U && elapsed < std::chrono::seconds(2),
        "Dense 512-to-4096 Scale was incomplete or unreasonably slow.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetVoxelCount() == 4096U && selection.Count() == 4096U &&
        session.rebuildCount_ == 1U,
        "Dense Scale changed count or rebuilt more than once.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Scaled document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("VoxelForgeScale-" + std::to_string(stamp) + ".vox");
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
        Require(stream.is_open(), "Unable to create Scale VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to write Scale VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 4096U &&
        reopened.Document->GetVoxel({1, 1, 1})->PaletteIndex ==
            document.GetVoxel({1, 1, 1})->PaletteIndex,
        "Saved and reopened VOX did not retain the scaled geometry.");
}
}

int main()
{
    try
    {
        TestExactGeometry();
        TestAtomicScaleUndoRedo();
        TestRefusalsAndRollback();
        TestLargeScaleSaveAndReopen();
        std::cout << "Voxel Scale tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Scale tests failed: " << exception.what() << '\n';
        return 1;
    }
}
