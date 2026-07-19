#include "Transform/DuplicateVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

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

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{16U, 16U, 16U}, {
        {1U, 1U, 1U, 3U},
        {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U},
        {14U, 1U, 1U, 9U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-duplicate-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Duplicate test document.");
    return std::move(*loaded.Document);
}

Asset::Voxel::VoxelDocument MakeLargeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    model.Voxels.reserve(512U);
    for (std::uint32_t z = 1U; z <= 8U; ++z)
        for (std::uint32_t y = 1U; y <= 8U; ++y)
            for (std::uint32_t x = 1U; x <= 8U; ++x)
                model.Voxels.push_back({
                    static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    static_cast<std::uint8_t>(1U + (x + y + z) % 31U)});
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-duplicate-large-memory.vox");
    Require(loaded.Succeeded(), "Unable to build large Duplicate document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Duplicate model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Duplicate compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Duplicate compatibility grid.");
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
            ? Editor::CommandResult::Failure("simulated Duplicate rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 51U;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
    bool failRebuild_ = false;
    bool savedState_ = true;
};

Editor::SelectionService MakeSelection(
    const std::uint64_t generation,
    const std::vector<Asset::Voxel::VoxelPosition>& positions)
{
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(generation);
    static_cast<void>(selection.Apply(positions, Editor::SelectionMode::Replace));
    return selection;
}

void ApplySelectionTransition(
    Editor::SelectionService& selection,
    const Editor::VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "Duplicate history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

Editor::DuplicateVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Asset::Voxel::VoxelPosition delta,
    const std::uint64_t generation = 51U)
{
    Require(preview.BeginPreview(document, selection, generation, 0U,
        Editor::TransformPreviewCollisionPolicy::IncludeSource),
        "Duplicate preview capture failed.");
    if (delta != Asset::Voxel::VoxelPosition{})
        Require(preview.SetDelta(document, selection, generation, delta),
            "Duplicate preview delta failed.");
    return Editor::DuplicateVoxelSelectionOperation::Build(
        document, selection, generation, preview);
}

void TestAtomicDuplicateUndoRedoAndRepeat()
{
    auto document = MakeDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(
        session.generation_, {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview, {4, 0, 0});
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 3U &&
        !document.IsDirty() && history.UndoCount() == 0U,
        "Duplicate mutated the document before application.");
    const std::uint64_t revision = document.GetRevision();
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        session.rebuildCount_ == 1U && document.IsDirty() &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 3U &&
        document.GetVoxel({2, 1, 1})->PaletteIndex == 4U &&
        document.GetVoxel({3, 1, 1})->PaletteIndex == 5U &&
        document.GetVoxel({5, 1, 1})->PaletteIndex == 3U &&
        document.GetVoxel({6, 1, 1})->PaletteIndex == 4U &&
        document.GetVoxel({7, 1, 1})->PaletteIndex == 5U &&
        selection.Voxels().size() == 3U && selection.Contains({7, 1, 1}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({5, 1, 1}, {7, 1, 1}),
        "Atomic Duplicate did not preserve source, colors, and selection.");

    const auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && !document.HasVoxel({5, 1, 1}) &&
        document.HasVoxel({1, 1, 1}) && selection.Contains({1, 1, 1}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({1, 1, 1}, {3, 1, 1}),
        "Undo removed source voxels or failed to restore source selection.");
    const auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && document.GetVoxel({7, 1, 1})->PaletteIndex == 5U &&
        selection.Contains({7, 1, 1}),
        "Redo did not recreate the copy and destination selection.");

    Editor::TransformPreviewModel repeatedPreview;
    auto repeated = Prepare(document, selection, repeatedPreview, {4, 0, 0});
    Require(repeated.Ready(), "A selected copy cannot be duplicated again.");
    const auto repeatedResult = history.Execute(
        session, std::move(repeated.Operation));
    ApplySelectionTransition(selection, repeatedResult);
    Require(repeatedResult && document.HasVoxel({9, 1, 1}) &&
        document.HasVoxel({11, 1, 1}) && selection.Contains({11, 1, 1}) &&
        history.UndoCount() == 2U,
        "Consecutive duplication did not select the newest copy.");
}

void TestRefusalsAndRollback()
{
    auto document = MakeDocument();
    document.MarkSaved();
    const auto initialCount = document.GetVoxelCount();
    const auto initialRevision = document.GetRevision();
    auto selection = MakeSelection(51U, {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});

    Editor::TransformPreviewModel overlapPreview;
    const auto overlap = Prepare(document, selection, overlapPreview, {1, 0, 0});
    Require(overlap.Code ==
            Editor::DuplicateVoxelSelectionResultCode::Collision &&
        overlapPreview.HasCollisions() &&
        document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision,
        "Duplicate accepted overlap with its still-existing source.");

    Editor::TransformPreviewModel collisionPreview;
    const auto collision = Prepare(
        document, selection, collisionPreview, {11, 0, 0});
    Require(collision.Code ==
            Editor::DuplicateVoxelSelectionResultCode::Collision &&
        document.GetVoxelCount() == initialCount && !document.IsDirty(),
        "External collision changed the document.");

    Editor::TransformPreviewModel outsidePreview;
    const auto outside = Prepare(
        document, selection, outsidePreview, {-2, -2, -2});
    Require(outside.Code ==
            Editor::DuplicateVoxelSelectionResultCode::OutOfBounds &&
        document.GetRevision() == initialRevision,
        "Out-of-bounds Duplicate changed the document.");

    Editor::TransformPreviewModel zeroPreview;
    const auto zero = Prepare(document, selection, zeroPreview, {});
    Require(zero.Code == Editor::DuplicateVoxelSelectionResultCode::NoChange,
        "Zero Duplicate prepared an operation.");

    Editor::TransformPreviewModel staleGeneration;
    Require(staleGeneration.BeginPreview(document, selection, 51U, 0U,
            Editor::TransformPreviewCollisionPolicy::IncludeSource) &&
        staleGeneration.SetDelta(document, selection, 51U, {4, 0, 0}),
        "Stale generation setup failed.");
    const auto wrongGeneration = Editor::DuplicateVoxelSelectionOperation::Build(
        document, selection, 52U, staleGeneration);
    Require(wrongGeneration.Code ==
        Editor::DuplicateVoxelSelectionResultCode::ModelChanged,
        "Duplicate accepted a different document generation.");

    Editor::TransformPreviewModel changedSelectionPreview;
    Require(changedSelectionPreview.BeginPreview(document, selection, 51U, 0U,
            Editor::TransformPreviewCollisionPolicy::IncludeSource) &&
        changedSelectionPreview.SetDelta(
            document, selection, 51U, {4, 0, 0}) &&
        selection.Apply(
            std::array{Asset::Voxel::VoxelPosition{14, 1, 1}},
            Editor::SelectionMode::Replace),
        "Changed selection setup failed.");
    const auto changedSelection =
        Editor::DuplicateVoxelSelectionOperation::Build(
            document, selection, 51U, changedSelectionPreview);
    Require(changedSelection.Code ==
        Editor::DuplicateVoxelSelectionResultCode::SelectionChanged,
        "Duplicate accepted a selection changed during preview.");
    selection = MakeSelection(
        51U, {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});

    Editor::TransformPreviewModel staleRevision;
    Require(staleRevision.BeginPreview(document, selection, 51U, 0U,
            Editor::TransformPreviewCollisionPolicy::IncludeSource) &&
        staleRevision.SetDelta(document, selection, 51U, {4, 0, 0}) &&
        document.SetVoxel({1, 1, 1}, 8U).Changed,
        "Stale source setup failed.");
    const auto stale = Editor::DuplicateVoxelSelectionOperation::Build(
        document, selection, 51U, staleRevision);
    Require(stale.Code == Editor::DuplicateVoxelSelectionResultCode::ModelChanged,
        "Duplicate accepted a changed source voxel or color.");

    auto rollbackDocument = MakeDocument();
    rollbackDocument.MarkSaved();
    TestSession session(rollbackDocument);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(rollbackDocument);
    auto rollbackSelection = MakeSelection(51U, {{14, 1, 1}});
    Editor::TransformPreviewModel rollbackPreview;
    auto rollback = Prepare(
        rollbackDocument, rollbackSelection, rollbackPreview, {-2, 3, 4});
    Require(rollback.Ready(), "Combined negative Duplicate was not prepared.");
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, std::move(rollback.Operation));
    Require(!failed && rollbackDocument.HasVoxel({14, 1, 1}) &&
        !rollbackDocument.HasVoxel({12, 4, 5}) &&
        history.UndoCount() == 0U && !rollbackDocument.IsDirty(),
        "Failed Duplicate did not roll back atomically.");
}

void TestLargeDuplicateSaveAndReopen()
{
    auto document = MakeLargeDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(512U);
    for (std::int32_t z = 1; z <= 8; ++z)
        for (std::int32_t y = 1; y <= 8; ++y)
            for (std::int32_t x = 1; x <= 8; ++x)
                positions.push_back({x, y, z});
    auto selection = MakeSelection(51U, positions);
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview, {16, 0, 0});
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 512U,
        "Dense 512-voxel Duplicate did not prepare exactly 512 writes.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetVoxelCount() == 1024U && document.HasVoxel({1, 1, 1}) &&
        document.HasVoxel({17, 1, 1}) && document.HasVoxel({24, 8, 8}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({17, 1, 1}, {24, 8, 8}),
        "Dense Duplicate lost source, destination, or selection.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Duplicated document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("VoxelForgeDuplicate-" + std::to_string(stamp) + ".vox");
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
        Require(stream.is_open(), "Unable to create Duplicate VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to write Duplicate VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 1024U &&
        reopened.Document->HasVoxel({1, 1, 1}) &&
        reopened.Document->HasVoxel({17, 1, 1}) &&
        reopened.Document->GetVoxel({24, 8, 8})->PaletteIndex ==
            document.GetVoxel({8, 8, 8})->PaletteIndex,
        "Saved and reopened VOX did not retain original and copy.");
}
}

int main()
{
    try
    {
        TestAtomicDuplicateUndoRedoAndRepeat();
        TestRefusalsAndRollback();
        TestLargeDuplicateSaveAndReopen();
        std::cout << "Voxel Duplicate tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Duplicate tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
