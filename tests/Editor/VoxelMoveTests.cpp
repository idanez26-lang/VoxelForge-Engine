#include "Transform/MoveVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
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
    source.Models.push_back({{8U, 8U, 8U}, {
        {1U, 1U, 1U, 3U},
        {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U},
        {6U, 1U, 1U, 9U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-move-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Move test document.");
    return std::move(*loaded.Document);
}

Asset::Voxel::VoxelDocument MakeLargeSparseDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    model.Voxels.reserve(8U * 8U * 8U);
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
        source, "voxel-move-large-memory.vox");
    Require(loaded.Succeeded(), "Unable to build large Move test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Move test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Move compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Move compatibility grid.");
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
            ? Editor::CommandResult::Failure("simulated Move rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 41U;
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
        "Move history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

Editor::MoveVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Asset::Voxel::VoxelPosition delta,
    const std::uint64_t generation = 41U)
{
    Require(preview.BeginPreview(document, selection, generation),
        "Move preview capture failed.");
    if (delta != Asset::Voxel::VoxelPosition{})
        Require(preview.SetDelta(document, selection, generation, delta),
            "Move preview delta failed.");
    return Editor::MoveVoxelSelectionOperation::Build(
        document, selection, generation, preview);
}

void TestAtomicInternalOverlapUndoRedoAndSelection()
{
    auto document = MakeDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Editor::SelectionService selection = MakeSelection(
        session.generation_, {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview, {1, 0, 0});
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 4U &&
        !document.IsDirty() && history.UndoCount() == 0U,
        "Internal overlap was not prepared atomically without mutation.");
    const std::uint64_t revision = document.GetRevision();
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        session.rebuildCount_ == 1U && session.completedCount_ == 1U &&
        !document.HasVoxel({1, 1, 1}) &&
        document.GetVoxel({2, 1, 1})->PaletteIndex == 3U &&
        document.GetVoxel({3, 1, 1})->PaletteIndex == 4U &&
        document.GetVoxel({4, 1, 1})->PaletteIndex == 5U && document.IsDirty(),
        "Atomic Move did not preserve colors and internal overlap.");
    ApplySelectionTransition(selection, applied);
    Require(selection.Voxels().size() == 3U &&
        selection.Contains({4, 1, 1}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({2, 1, 1}, {4, 1, 1}),
        "Move did not retain the destination selection and box.");

    const auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && !document.IsDirty() &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 3U &&
        document.GetVoxel({2, 1, 1})->PaletteIndex == 4U &&
        document.GetVoxel({3, 1, 1})->PaletteIndex == 5U &&
        !document.HasVoxel({4, 1, 1}) && selection.Contains({1, 1, 1}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({1, 1, 1}, {3, 1, 1}),
        "Undo did not restore source voxels, colors, selection and box.");
    const auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && selection.Contains({4, 1, 1}) &&
        document.GetVoxel({4, 1, 1})->PaletteIndex == 5U,
        "Redo did not restore destination voxels and selection.");
}

void TestRefusalsAreNonMutating()
{
    auto document = MakeDocument();
    document.MarkSaved();
    const auto initialCount = document.GetVoxelCount();
    const auto initialRevision = document.GetRevision();

    auto selection = MakeSelection(41U, {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});
    Editor::TransformPreviewModel collisionPreview;
    const auto collision = Prepare(
        document, selection, collisionPreview, {3, 0, 0});
    Require(collision.Code == Editor::MoveVoxelSelectionResultCode::Collision &&
        document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision && !document.IsDirty(),
        "External collision changed the document.");

    Editor::TransformPreviewModel outPreview;
    const auto out = Prepare(document, selection, outPreview, {-2, -2, -2});
    Require(out.Code == Editor::MoveVoxelSelectionResultCode::OutOfBounds &&
        document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision,
        "Out-of-bounds Move changed the document.");

    Editor::TransformPreviewModel zeroPreview;
    const auto zero = Prepare(document, selection, zeroPreview, {});
    Require(zero.Code == Editor::MoveVoxelSelectionResultCode::NoChange &&
        !document.IsDirty(), "Zero Move created a mutation.");

    Editor::TransformPreviewModel staleGeneration;
    Require(staleGeneration.BeginPreview(document, selection, 41U) &&
        staleGeneration.SetDelta(document, selection, 41U, {1, 0, 0}),
        "Stale-generation preview setup failed.");
    const auto generation = Editor::MoveVoxelSelectionOperation::Build(
        document, selection, 42U, staleGeneration);
    Require(generation.Code ==
        Editor::MoveVoxelSelectionResultCode::ModelChanged,
        "A different document generation was accepted.");

    Editor::TransformPreviewModel staleRevision;
    Require(staleRevision.BeginPreview(document, selection, 41U) &&
        staleRevision.SetDelta(document, selection, 41U, {1, 0, 0}) &&
        document.SetVoxel({0, 0, 0}, 8U).Changed,
        "Stale-revision setup failed.");
    const auto changed = Editor::MoveVoxelSelectionOperation::Build(
        document, selection, 41U, staleRevision);
    Require(changed.Code == Editor::MoveVoxelSelectionResultCode::ModelChanged,
        "A changed document or source snapshot was accepted.");
}

void TestCombinedDeltaRollbackAndCancel()
{
    auto document = MakeDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(41U, {{1, 1, 1}});
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview, {2, 3, 4});
    Require(prepared.Ready() && prepared.Operation.SelectionTransition &&
        prepared.Operation.SelectionTransition->After.Bounds ==
            Editor::SelectionBounds::FromCorners({3, 4, 5}, {3, 4, 5}),
        "Combined positive delta produced incorrect destination data.");
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, std::move(prepared.Operation));
    Require(!failed && document.GetVoxel({1, 1, 1})->PaletteIndex == 3U &&
        !document.HasVoxel({3, 4, 5}) && history.UndoCount() == 0U &&
        !document.IsDirty(),
        "Failed atomic Move did not roll back every state.");

    Editor::TransformPreviewModel cancelled;
    Require(cancelled.BeginPreview(document, selection, 41U) &&
        cancelled.SetDelta(document, selection, 41U, {-1, 0, 0}) &&
        cancelled.CancelPreview() && !cancelled.IsActive() &&
        !document.IsDirty() && history.UndoCount() == 0U,
        "Cancelled preview changed the document or history.");
}

void TestLargeSparseMoveAndPersistence()
{
    auto document = MakeLargeSparseDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);

    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(8U * 8U * 8U);
    for (std::int32_t z = 1; z <= 8; ++z)
        for (std::int32_t y = 1; y <= 8; ++y)
            for (std::int32_t x = 1; x <= 8; ++x)
                positions.push_back({x, y, z});
    auto selection = MakeSelection(session.generation_, positions);
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview, {1, 0, 0});
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 576U,
        "Large sparse Move did not build the expected atomic change set.");
    const std::uint64_t revision = document.GetRevision();
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        document.GetVoxelCount() == 512U && !document.HasVoxel({1, 1, 1}) &&
        document.HasVoxel({9, 8, 8}) && selection.Voxels().size() == 512U &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({2, 1, 1}, {9, 8, 8}),
        "Large sparse Move was not applied exactly once.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Moved document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("VoxelForgeMove-" + std::to_string(stamp) + ".vox");
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
        Require(stream.is_open(), "Unable to create temporary Move VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to persist moved VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 512U &&
        !reopened.Document->HasVoxel({1, 1, 1}) &&
        reopened.Document->HasVoxel({9, 8, 8}),
        "Saved and reopened document lost the Move destination.");
}
}

int main()
{
    try
    {
        TestAtomicInternalOverlapUndoRedoAndSelection();
        TestRefusalsAreNonMutating();
        TestCombinedDeltaRollbackAndCancel();
        TestLargeSparseMoveAndPersistence();
        std::cout << "Voxel Move tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Move tests failed: " << exception.what() << '\n';
        return 1;
    }
}
