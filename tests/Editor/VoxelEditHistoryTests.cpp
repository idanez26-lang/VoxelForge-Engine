#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelHistory/VoxelHistoryInput.h"
#include "Commands/Voxel/VoxelEditTransaction.h"
#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "SmartToolTestSupport.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
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

Asset::Vox::VoxModelMetadata Model(
    const Asset::Vox::VoxDimensions dimensions,
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    return {dimensions, std::move(voxels)};
}

Asset::Voxel::VoxelDocument Document(
    std::vector<Asset::Vox::VoxModelMetadata> models)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = std::move(models);
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    source.HasPackChunk = source.Models.size() > 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-history-memory.vox");
    Require(loaded.Succeeded(), "Unable to build history test document.");
    return std::move(*loaded.Document);
}

Asset::Voxel::VoxelDocument DefaultDocument()
{
    return Document({
        Model({8U, 8U, 8U}, {{0U, 0U, 0U, 3U}}),
        Model({8U, 8U, 8U}, {{1U, 1U, 1U, 9U}})});
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("History test model");
    const auto& palette = document.GetPalette();
    for (std::size_t index = 0U; index < palette.size(); ++index)
    {
        const auto color = palette[index];
        Require(model.Palette().Set(index,
            {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize history compatibility palette.");
    }
    for (std::size_t index = 0U; index < document.GetModelCount(); ++index)
    {
        const Asset::Voxel::VoxelSubModel* source = document.GetModel(index);
        Require(source != nullptr, "Missing test sub-model.");
        const auto dimensions = source->Dimensions();
        Voxel::VoxelGrid grid;
        Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
            "Unable to size history compatibility grid.");
        source->ForEachVoxel([&grid](
            const Asset::Voxel::VoxelPosition position,
            const Asset::Voxel::Voxel voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize history compatibility grid.");
        });
        model.AddGrid(std::move(grid));
    }
    return model;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return generation_;
    }
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
        ++rebuildAttempts_;
        if (failRebuild_)
            return Editor::CommandResult::Failure("simulated history rebuild failure");
        const auto synchronized = cache_.Synchronize(*document_, generation_);
        if (!synchronized.Succeeded)
            return Editor::CommandResult::Failure(synchronized.Message);
        if (synchronized.Rebuilt()) ++rebuilds_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedEdits_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedStateReported_ = saved;
    }
    bool PrimeMesh() { return static_cast<bool>(RebuildActiveVoxelMesh()); }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
    std::uint64_t generation_ = 1U;
    std::size_t rebuildAttempts_ = 0U;
    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;
    bool failRebuild_ = false;
    bool hasModel_ = true;
    bool savedStateReported_ = true;
};

Editor::VoxelChange Change(
    const std::size_t modelIndex,
    const Asset::Voxel::VoxelPosition position,
    const std::optional<std::size_t> before,
    const std::optional<std::size_t> after)
{
    return {
        modelIndex,
        position,
        before.has_value(),
        static_cast<std::uint8_t>(before.value_or(0U)),
        after.has_value(),
        static_cast<std::uint8_t>(after.value_or(0U))};
}

Editor::VoxelEditOperation AddOperation(
    std::string label,
    const Asset::Voxel::VoxelPosition position,
    const std::uint8_t palette = 7U,
    const std::size_t modelIndex = 0U)
{
    return {std::move(label), {Change(modelIndex, position, {}, palette)}};
}

Editor::VoxelEditOperation PaletteOperation(
    const Asset::Voxel::VoxelDocument& document,
    std::string label,
    const std::size_t paletteIndex,
    const Asset::Voxel::VoxelColor color)
{
    Asset::Voxel::VoxelDocumentPaletteChange paletteChange;
    paletteChange.Before = document.GetPaletteSnapshot();
    paletteChange.After = paletteChange.Before;
    paletteChange.After.Colors[paletteIndex] = color;
    paletteChange.After.HasCustomPalette = true;

    Editor::VoxelEditOperation operation;
    operation.Label = std::move(label);
    operation.PaletteChange =
        std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
            std::move(paletteChange));
    return operation;
}

void TestEmptyAndToolIntegration()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Require(session.PrimeMesh(), "Unable to prime history mesh.");
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Require(!history.CanUndo() && !history.CanRedo() &&
        history.UndoCount() == 0U && history.RedoCount() == 0U &&
        history.IsAtSavedState() && !document.IsDirty(),
        "Empty history state is incorrect.");
    const std::uint64_t revision = document.GetRevision();
    const std::size_t builds = session.rebuilds_;

    Editor::VoxelRaycastHit pencilHit;
    pencilHit.Coordinates = {0U, 0U, 0U};
    pencilHit.Face = Editor::VoxelHitFace::PositiveX;
    pencilHit.AdjacentPosition = {1, 0, 0};
    pencilHit.AdjacentWithinBounds = true;
    pencilHit.DocumentRevision = revision;
    Editor::SmartBrushState brushState;
    brushState.PaletteIndex = 7U;
    const auto pencil = Editor::VoxelPencilTool::Apply(
        Editor::TestSupport::MakePencilContext(session, document, 0U,
            brushState, pencilHit.AdjacentPosition,
            Editor::VoxelHitFaceIntegerNormal(pencilHit.Face), &history));
    Require(pencil.Code == Editor::VoxelToolResultCode::Applied &&
        history.CanUndo() && !history.CanRedo() &&
        history.UndoLabel() == "Add Voxel" &&
        document.GetRevision() == revision + 1U &&
        session.rebuilds_ == builds + 1U && document.IsDirty(),
        "Pencil was not recorded as one history operation.");

    Editor::VoxelRaycastHit eraserHit;
    eraserHit.Coordinates = {1U, 0U, 0U};
    eraserHit.Face = Editor::VoxelHitFace::PositiveZ;
    eraserHit.DocumentRevision = document.GetRevision();
    const auto eraser = Editor::VoxelEraserTool::Apply({
        &session, &document, 0U, eraserHit, false, &history});
    Require(eraser.Code == Editor::VoxelEraserResultCode::Applied &&
        eraser.RemovedPaletteIndex == 7U &&
        history.UndoLabel() == "Remove Voxel" &&
        history.UndoCount() == 2U && !document.HasVoxel({1, 0, 0}),
        "Eraser was not recorded with its exact palette index.");

    const auto undoEraser = history.Undo(session);
    Require(undoEraser && undoEraser.Label == "Remove Voxel" &&
        document.GetVoxel({1, 0, 0})->PaletteIndex == 7U &&
        history.RedoLabel() == "Remove Voxel",
        "Undo Eraser did not restore the exact voxel.");
    const auto undoPencil = history.Undo(session);
    Require(undoPencil && !document.HasVoxel({1, 0, 0}) &&
        !document.IsDirty() && session.savedStateReported_ &&
        history.IsAtSavedState(),
        "Undo Pencil did not return to the initial saved state.");
    const auto redoPencil = history.Redo(session);
    const auto redoEraser = history.Redo(session);
    Require(redoPencil && redoEraser && !document.HasVoxel({1, 0, 0}) &&
        document.IsDirty() && !session.savedStateReported_,
        "Redo Pencil/Eraser sequence is incorrect.");
}

void TestOrderingBranchingAndSavedIdentity()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Require(static_cast<bool>(
        history.Execute(session, AddOperation("A", {1, 0, 0}, 4U))),
        "History operation A failed.");
    Require(static_cast<bool>(
        history.Execute(session, AddOperation("B", {2, 0, 0}, 5U))),
        "History operation B failed.");
    history.MarkSavedState(document);
    Require(!document.IsDirty() && history.IsAtSavedState(),
        "MarkSavedState did not define the current history identity.");
    Require(history.Undo(session).Label == "B" && document.IsDirty(),
        "History did not undo in LIFO order.");
    Require(history.Execute(session, AddOperation("C", {3, 0, 0}, 6U)) &&
        !history.CanRedo() && history.UndoLabel() == "C" &&
        document.IsDirty(),
        "A new history branch did not discard Redo.");
    Require(history.Undo(session) && document.IsDirty() &&
        !history.IsAtSavedState(),
        "Discarded saved branch was incorrectly considered clean.");
    history.Clear();
    Require(!history.CanUndo() && !history.CanRedo() &&
        history.EstimatedMemory() == 0U,
        "History Clear did not release all retained operations.");
}

void TestMultiChangeAndSingleRevision()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Editor::VoxelEditOperation operation{"Synthetic Multi Edit", {
        Change(0U, {0, 0, 0}, 3U, {}),
        Change(0U, {1, 1, 1}, {}, 4U),
        Change(0U, {2, 1, 1}, {}, 5U),
        Change(0U, {3, 1, 1}, {}, 6U),
        Change(0U, {4, 1, 1}, {}, 7U),
        Change(0U, {5, 1, 1}, {}, 8U),
        Change(1U, {1, 1, 1}, 9U, 12U),
        Change(1U, {2, 2, 2}, {}, 13U)}};
    const std::uint64_t revision = document.GetRevision();
    const std::size_t builds = session.rebuilds_;
    Require(history.Execute(session, operation) &&
        history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        session.rebuilds_ == builds + 1U &&
        !document.HasVoxel({0, 0, 0}, 0U) &&
        document.GetVoxel({5, 1, 1}, 0U)->PaletteIndex == 8U &&
        document.GetVoxel({1, 1, 1}, 1U)->PaletteIndex == 12U,
        "Multi-change Execute was not one atomic revision/rebuild.");
    const auto bounds = document.GetBounds(0U);
    Require(bounds && bounds->HasValue && bounds->Minimum.X == 1 &&
        bounds->Maximum.X == 5,
        "Multi-change bounds were not recalculated.");
    const std::uint64_t executeRevision = document.GetRevision();
    Require(history.Undo(session) &&
        document.GetRevision() == executeRevision + 1U &&
        document.GetVoxel({0, 0, 0}, 0U)->PaletteIndex == 3U &&
        !document.HasVoxel({5, 1, 1}, 0U) &&
        document.GetVoxel({1, 1, 1}, 1U)->PaletteIndex == 9U,
        "Multi-change Undo was incomplete.");
    Require(history.Redo(session) &&
        document.GetRevision() == executeRevision + 2U &&
        document.GetVoxel({2, 2, 2}, 1U)->PaletteIndex == 13U,
        "Multi-change Redo was incomplete.");
}

void TestPaletteOnlyAndCompositeHistory()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Require(session.PrimeMesh(), "Unable to prime composite history mesh.");
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);

    constexpr std::size_t PaletteIndex = 42U;
    constexpr Asset::Voxel::VoxelColor CompositeColor{
        17U, 93U, 201U, 255U};
    const auto initialPalette = document.GetPaletteSnapshot();
    const std::uint64_t initialRevision = document.GetRevision();
    const std::size_t initialRebuildAttempts = session.rebuildAttempts_;
    const std::size_t initialCompletedEdits = session.completedEdits_;

    auto paletteOnly = PaletteOperation(
        document, "Palette Only", PaletteIndex, CompositeColor);
    const Voxel::VoxelColor compatibilityCompositeColor{
        CompositeColor.Red, CompositeColor.Green,
        CompositeColor.Blue, CompositeColor.Alpha};
    Require(history.Execute(session, std::move(paletteOnly)) &&
        document.GetPaletteColor(PaletteIndex) == CompositeColor &&
        session.model_.Palette().Get(PaletteIndex) != nullptr &&
        *session.model_.Palette().Get(PaletteIndex) ==
            compatibilityCompositeColor &&
        document.GetRevision() == initialRevision + 1U &&
        history.UndoCount() == 1U && history.RedoCount() == 0U &&
        session.rebuildAttempts_ == initialRebuildAttempts + 1U &&
        session.completedEdits_ == initialCompletedEdits + 1U,
        "Palette-only transaction was not one logical edit.");

    Require(history.Undo(session) &&
        document.GetPaletteSnapshot() == initialPalette &&
        session.model_.Palette().Get(PaletteIndex) != nullptr &&
        *session.model_.Palette().Get(PaletteIndex) ==
            Voxel::VoxelColor{
                initialPalette.Colors[PaletteIndex].Red,
                initialPalette.Colors[PaletteIndex].Green,
                initialPalette.Colors[PaletteIndex].Blue,
                initialPalette.Colors[PaletteIndex].Alpha} &&
        document.GetRevision() == initialRevision + 2U &&
        history.UndoCount() == 0U && history.RedoCount() == 1U,
        "Palette-only Undo did not restore the exact snapshot.");
    Require(history.Redo(session) &&
        document.GetPaletteColor(PaletteIndex) == CompositeColor &&
        session.model_.Palette().Get(PaletteIndex) != nullptr &&
        *session.model_.Palette().Get(PaletteIndex) ==
            compatibilityCompositeColor &&
        document.GetRevision() == initialRevision + 3U &&
        history.UndoCount() == 1U && history.RedoCount() == 0U,
        "Palette-only Redo did not restore the exact result.");

    history.Clear();
    history.MarkSavedState(document);
    constexpr Asset::Voxel::VoxelColor ReplacementColor{
        224U, 61U, 37U, 255U};
    Editor::VoxelEditOperation mixed = PaletteOperation(
        document, "Composite Palette And Voxels",
        PaletteIndex, ReplacementColor);
    mixed.Changes = {
        Change(0U, {1, 0, 0}, {}, PaletteIndex),
        Change(0U, {2, 0, 0}, {}, PaletteIndex),
        Change(1U, {1, 1, 1}, 9U, PaletteIndex)};
    const auto paletteBeforeMixed = document.GetPaletteSnapshot();
    const std::uint64_t mixedRevision = document.GetRevision();
    const std::size_t mixedRebuildAttempts = session.rebuildAttempts_;
    const std::size_t mixedCompletedEdits = session.completedEdits_;
    Require(history.Execute(session, mixed) &&
        document.GetPaletteColor(PaletteIndex) == ReplacementColor &&
        document.GetVoxel({1, 0, 0}, 0U)->PaletteIndex == PaletteIndex &&
        document.GetVoxel({2, 0, 0}, 0U)->PaletteIndex == PaletteIndex &&
        document.GetVoxel({1, 1, 1}, 1U)->PaletteIndex == PaletteIndex &&
        document.GetRevision() == mixedRevision + 1U &&
        history.UndoCount() == 1U && history.RedoCount() == 0U &&
        session.rebuildAttempts_ == mixedRebuildAttempts + 1U &&
        session.completedEdits_ == mixedCompletedEdits + 1U,
        "Mixed transaction was not committed atomically.");

    Require(history.Undo(session) &&
        document.GetPaletteSnapshot() == paletteBeforeMixed &&
        !document.HasVoxel({1, 0, 0}, 0U) &&
        !document.HasVoxel({2, 0, 0}, 0U) &&
        document.GetVoxel({1, 1, 1}, 1U)->PaletteIndex == 9U &&
        document.GetRevision() == mixedRevision + 2U &&
        history.UndoCount() == 0U && history.RedoCount() == 1U,
        "Mixed Undo did not restore palette and voxels exactly.");
    Require(history.Redo(session) &&
        document.GetPaletteColor(PaletteIndex) == ReplacementColor &&
        document.GetVoxel({1, 0, 0}, 0U)->PaletteIndex == PaletteIndex &&
        document.GetVoxel({2, 0, 0}, 0U)->PaletteIndex == PaletteIndex &&
        document.GetVoxel({1, 1, 1}, 1U)->PaletteIndex == PaletteIndex &&
        document.GetRevision() == mixedRevision + 3U &&
        history.UndoCount() == 1U && history.RedoCount() == 0U,
        "Mixed Redo did not reproduce the committed result exactly.");
}

void TestCompositeValidationAndRollback()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Require(session.PrimeMesh(), "Unable to prime rollback mesh.");
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);

    const auto initialPalette = document.GetPaletteSnapshot();
    const auto initialBounds = document.GetGlobalBounds();
    const std::uint64_t initialCount = document.GetVoxelCount();
    const std::uint64_t initialRevision = document.GetRevision();
    const std::size_t initialRebuildAttempts = session.rebuildAttempts_;
    const std::size_t initialCompletedEdits = session.completedEdits_;

    auto failing = PaletteOperation(document, "Composite Rollback", 27U,
        {80U, 190U, 120U, 255U});
    failing.Changes = {
        Change(0U, {1, 0, 0}, {}, 27U),
        Change(0U, {2, 0, 0}, {}, 27U)};
    session.failRebuild_ = true;
    const auto failed = history.Execute(session, failing);
    Require(!failed &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetGlobalBounds() == initialBounds &&
        document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision &&
        !document.HasVoxel({1, 0, 0}, 0U) &&
        !document.HasVoxel({2, 0, 0}, 0U) &&
        history.UndoCount() == 0U && history.RedoCount() == 0U &&
        session.rebuildAttempts_ == initialRebuildAttempts + 1U &&
        session.completedEdits_ == initialCompletedEdits,
        "Composite failure left a partial palette, voxel or history state.");

    session.failRebuild_ = false;
    auto stalePalette = PaletteOperation(document, "Stale Palette", 17U,
        {9U, 19U, 29U, 255U});
    auto staleBefore = stalePalette.PaletteChange->Before;
    staleBefore.Colors[17U] = {1U, 1U, 1U, 255U};
    stalePalette.PaletteChange =
        std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
            Asset::Voxel::VoxelDocumentPaletteChange{
                std::move(staleBefore), stalePalette.PaletteChange->After});
    Require(!history.Execute(session, std::move(stalePalette)) &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetRevision() == initialRevision &&
        !history.CanUndo() && !history.CanRedo(),
        "Stale palette before-state was not refused before mutation.");

    Editor::VoxelEditOperation identicalPalette;
    identicalPalette.Label = "Identical Palette";
    identicalPalette.PaletteChange =
        std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
            Asset::Voxel::VoxelDocumentPaletteChange{
                initialPalette, initialPalette});
    Require(!history.Execute(session, std::move(identicalPalette)) &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetRevision() == initialRevision &&
        !history.CanUndo() && !history.CanRedo(),
        "Identical palette before/after states were not refused.");

    auto invalidPalette = PaletteOperation(document, "Invalid Palette", 18U,
        {1U, 2U, 3U, 255U});
    auto invalidSnapshot = invalidPalette.PaletteChange->After;
    invalidSnapshot.Colors[0U] = {1U, 2U, 3U, 4U};
    invalidPalette.PaletteChange =
        std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
            Asset::Voxel::VoxelDocumentPaletteChange{
                invalidPalette.PaletteChange->Before,
                std::move(invalidSnapshot)});
    Require(!history.Execute(session, std::move(invalidPalette)) &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetRevision() == initialRevision &&
        !history.CanUndo() && !history.CanRedo(),
        "Invalid palette snapshot was not refused before mutation.");

    auto incompatibleIndex = PaletteOperation(
        document, "Incompatible Index", 19U, {9U, 8U, 7U, 255U});
    incompatibleIndex.Changes = {Change(0U, {1, 0, 0}, {}, 0U)};
    Require(!history.Execute(session, std::move(incompatibleIndex)) &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetRevision() == initialRevision &&
        !history.CanUndo() && !history.CanRedo(),
        "Invalid voxel palette index changed a composite transaction.");

    auto negative = PaletteOperation(
        document, "Negative Coordinates", 20U, {2U, 4U, 6U, 255U});
    negative.Changes = {Change(0U, {-1, 0, 0}, {}, 20U)};
    Require(!history.Execute(session, std::move(negative)) &&
        document.GetPaletteSnapshot() == initialPalette &&
        document.GetRevision() == initialRevision &&
        !history.CanUndo() && !history.CanRedo(),
        "Negative voxel coordinates changed a composite transaction.");
}

void TestCompositeDuplicatesLargeBatchAndLifecycle()
{
    auto document = Document({
        Model({16U, 16U, 16U}, {{0U, 0U, 0U, 3U}})});
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);

    auto operation = PaletteOperation(
        document, "Large Composite Edit", 101U, {90U, 40U, 210U, 255U});
    auto paletteAfter = operation.PaletteChange->After;
    paletteAfter.Colors[102U] = paletteAfter.Colors[101U];
    operation.PaletteChange =
        std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
            Asset::Voxel::VoxelDocumentPaletteChange{
                operation.PaletteChange->Before, paletteAfter});
    operation.Changes.reserve(512U);
    for (std::int32_t z = 0; z < 8; ++z)
    {
        for (std::int32_t y = 0; y < 8; ++y)
        {
            for (std::int32_t x = 0; x < 8; ++x)
            {
                if (x == 0 && y == 0 && z == 0) continue;
                operation.Changes.push_back(
                    Change(0U, {x, y, z}, {},
                        ((x + y + z) & 1) == 0 ? 101U : 102U));
            }
        }
    }

    const std::uint64_t revision = document.GetRevision();
    Require(history.Execute(session, operation) &&
        document.GetVoxelCount() == 512U &&
        document.GetPaletteColor(101U) == document.GetPaletteColor(102U) &&
        document.GetRevision() == revision + 1U &&
        history.UndoCount() == 1U,
        "Large composite edit or duplicate palette preservation failed.");
    Require(history.Undo(session) &&
        document.GetVoxelCount() == 1U &&
        document.GetRevision() == revision + 2U,
        "Large composite Undo failed.");
    Require(history.Redo(session) &&
        document.GetVoxelCount() == 512U &&
        document.GetRevision() == revision + 3U,
        "Large composite Redo failed.");

    history.Clear();
    Require(!history.CanUndo() && !history.CanRedo() &&
        history.EstimatedMemory() == 0U,
        "Project/history lifecycle retained a composite operation.");
}

void TestLimitsAndMemory()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Editor::VoxelEditHistory history({2U, 1024U * 1024U});
    history.MarkSavedState(document);
    Require(history.Execute(session, AddOperation("A", {1, 0, 0})) &&
        history.Execute(session, AddOperation("B", {2, 0, 0})) &&
        history.Execute(session, AddOperation("C", {3, 0, 0})) &&
        history.UndoCount() == 2U && history.UndoLabel() == "C" &&
        history.EstimatedMemory() > 0U,
        "Command count limit did not evict the oldest operation.");
    Require(history.Undo(session) && history.Undo(session) &&
        document.HasVoxel({1, 0, 0}) && document.IsDirty(),
        "Truncated saved state produced incorrect dirty or stack state.");

    auto limitedDocument = DefaultDocument();
    TestSession limitedSession(limitedDocument);
    const Editor::VoxelEditOperation oversized =
        AddOperation("Operation Larger Than Limit", {1, 0, 0});
    const std::size_t estimated =
        Editor::EstimateVoxelEditOperationMemory(oversized);
    Editor::VoxelEditHistory limited({100U, estimated - 1U});
    const std::uint64_t revision = limitedDocument.GetRevision();
    const auto refused = limited.Execute(limitedSession, oversized);
    Require(refused.Code ==
            Editor::VoxelEditHistoryResultCode::LimitExceeded &&
        !limitedDocument.HasVoxel({1, 0, 0}) &&
        limitedDocument.GetRevision() == revision &&
        !limited.CanUndo() && limited.EstimatedMemory() == 0U,
        "Oversized operation policy did not refuse before mutation.");
}

void TestRefusalsAndRollback()
{
    auto document = DefaultDocument();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    const auto empty = history.Execute(session, {});
    Require(empty.Code == Editor::VoxelEditHistoryResultCode::NoChange &&
        document.GetRevision() == 0U &&
        !history.Undo(session) && !history.Redo(session),
        "Empty history/refusal behavior is incorrect.");

    Editor::VoxelEditOperation unlabeled =
        AddOperation({}, {1, 0, 0}, 11U);
    Require(history.Execute(session, std::move(unlabeled)).Code ==
            Editor::VoxelEditHistoryResultCode::InvalidOperation &&
        document.GetRevision() == 0U && !history.CanUndo(),
        "Unlabelled non-empty operation was not rejected.");

    const auto operation = AddOperation("Rollback", {1, 0, 0}, 11U);
    const auto initialBounds = document.GetBounds();
    const std::uint64_t revision = document.GetRevision();
    const std::uint64_t count = document.GetVoxelCount();
    session.failRebuild_ = true;
    const auto failedExecute = history.Execute(session, operation);
    const Voxel::Voxel* gridVoxel = session.model_.GetGrid(0U)->Get(1U, 0U, 0U);
    Require(!failedExecute && !document.HasVoxel({1, 0, 0}) &&
        gridVoxel && !gridVoxel->IsOccupied() &&
        document.GetRevision() == revision &&
        document.GetVoxelCount() == count &&
        document.GetBounds() == initialBounds && !document.IsDirty() &&
        !history.CanUndo() && !history.CanRedo(),
        "Failed Execute did not roll back document/grid/history.");

    session.failRebuild_ = false;
    Require(static_cast<bool>(history.Execute(session, operation)),
        "Rollback setup Execute failed.");
    const std::uint64_t appliedRevision = document.GetRevision();
    session.failRebuild_ = true;
    Require(!history.Undo(session) && history.CanUndo() &&
        !history.CanRedo() && document.HasVoxel({1, 0, 0}) &&
        document.GetRevision() == appliedRevision,
        "Failed Undo moved stacks or changed the document.");
    session.failRebuild_ = false;
    Require(static_cast<bool>(history.Undo(session)),
        "Rollback setup Undo failed.");
    const std::uint64_t undoneRevision = document.GetRevision();
    session.failRebuild_ = true;
    Require(!history.Redo(session) && !history.CanUndo() &&
        history.CanRedo() && !document.HasVoxel({1, 0, 0}) &&
        document.GetRevision() == undoneRevision,
        "Failed Redo moved stacks or changed the document.");

    session.failRebuild_ = false;
    Editor::VoxelEditOperation mismatch{
        "Mismatch", {Change(0U, {0, 0, 0}, 99U, {})}};
    const std::uint64_t mismatchRevision = document.GetRevision();
    Require(!history.Execute(session, mismatch) &&
        document.GetRevision() == mismatchRevision,
        "Before-state mismatch changed the document.");
}

void TestDocumentOnlyCanonicalTransactions()
{
    auto document = DefaultDocument();
    TestSession session(document);
    session.hasModel_ = false;
    Require(session.PrimeMesh(),
        "Unable to prime the document-only history mesh.");

    Voxel::Voxel empty;
    Require(Editor::ReadEditableVoxel(
            session, session.generation_, 2U, 2U, 2U, empty) &&
        !empty.IsOccupied(),
        "A document-only session could not read an empty canonical cell.");
    Require(Editor::ApplyVoxelEdit(
            session, session.generation_, 2U, 2U, 2U, empty,
            {12U, Voxel::Voxel::OccupiedFlag}) &&
        document.GetVoxel({2, 2, 2}) ==
            std::optional<Asset::Voxel::Voxel>{
                Asset::Voxel::Voxel{12U}},
        "A direct document-only edit did not update the canonical document.");

    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    const Editor::VoxelEditOperation operation =
        AddOperation("Document-only add", {3, 2, 2}, 13U);
    const auto executed = history.Execute(session, operation);
    const bool presentAfterExecute = document.HasVoxel({3, 2, 2});
    const auto undone = history.Undo(session);
    const bool absentAfterUndo = !document.HasVoxel({3, 2, 2});
    const auto redone = history.Redo(session);
    Require(executed && presentAfterExecute && absentAfterUndo &&
        document.GetVoxel({3, 2, 2}) ==
            std::optional<Asset::Voxel::Voxel>{
                Asset::Voxel::Voxel{13U}} &&
        undone && redone && document.HasVoxel({3, 2, 2}),
        "Document-only Execute/Undo/Redo did not preserve canonical state.");

    const Asset::Voxel::VoxelColor replacement{17U, 34U, 51U, 255U};
    const Editor::VoxelEditOperation palette =
        PaletteOperation(document, "Document-only palette", 13U, replacement);
    Require(history.Execute(session, palette) &&
        document.GetPalette()[13U] == replacement &&
        history.Undo(session) && document.GetPalette()[13U] != replacement &&
        history.Redo(session) && document.GetPalette()[13U] == replacement,
        "Document-only palette history still required a compatibility model.");

    const std::uint64_t revision = document.GetRevision();
    session.failRebuild_ = true;
    Require(!history.Execute(
            session, AddOperation("Document-only rollback", {4, 2, 2}, 14U)) &&
        !document.HasVoxel({4, 2, 2}) &&
        document.GetRevision() == revision,
        "A failed document-only rebuild did not roll back canonical state.");
}

// VF-STAB-01B blocage 2, tests 5 et 6 (revue Codex). Les tests d'API unitaires
// ne suffisent pas : il faut prouver que le CHEMIN TRANSACTIONNEL REEL de
// l'editeur — celui qu'empruntent les outils — ne peut pas contourner la garde
// de lecture seule, et que Undo/Redo ne rendent pas le document modifiable.
void TestReadOnlyDocumentRefusesEditorTransactions()
{
    // Un axe au-dela de 256 : le format VOX ne sait pas le reecrire, donc le
    // loader ouvre le document en lecture seule.
    auto readOnly = Document({Model({257U, 1U, 1U}, {{0U, 0U, 0U, 3U}})});
    Require(readOnly.IsReadOnly(),
        "The 257-wide fixture must be loaded read-only.");

    TestSession session(readOnly);
    session.hasModel_ = false;
    Require(session.PrimeMesh(),
        "Unable to prime the read-only history mesh.");

    const auto paletteBefore = readOnly.GetPaletteSnapshot();
    const auto revisionBefore = readOnly.GetRevision();
    const auto voxelCountBefore = readOnly.GetVoxelCount();
    const auto voxelBefore = readOnly.GetVoxel({0, 0, 0});

    Editor::VoxelEditHistory history;
    history.MarkSavedState(readOnly);

    // 5. Transaction editeur reelle sur un document en lecture seule.
    const auto executed =
        history.Execute(session, AddOperation("Read-only add", {1, 0, 0}, 7U));
    Require(!static_cast<bool>(executed),
        "The real editor transaction path must refuse a read-only document.");
    Require(history.UndoCount() == 0U && history.RedoCount() == 0U,
        "A refused transaction must not enter the undo history.");
    Require(readOnly.GetVoxel({1, 0, 0}) ==
            std::optional<Asset::Voxel::Voxel>{},
        "A refused transaction must not create a voxel.");

    // 6. Undo/Redo sur un document en lecture seule : rien a defaire, et
    //    surtout aucun retour a un etat modifiable.
    Require(!static_cast<bool>(history.Undo(session)),
        "Undo must not succeed on a read-only document with no history.");
    Require(!static_cast<bool>(history.Redo(session)),
        "Redo must not succeed on a read-only document with no history.");
    Require(history.UndoCount() == 0U && history.RedoCount() == 0U,
        "Undo/Redo must leave the history of a read-only document empty.");

    // L'historique pilote aussi l'etat « modifie » : il ne doit jamais salir un
    // document non enregistrable (blocage 1, verifie ici sur le vrai chemin).
    readOnly.UpdateDirtyFromHistory(false);
    Require(!readOnly.IsDirty(),
        "History state must never make a read-only document dirty.");

    // Aucun effet partiel : contenu, palette, revision et etat intacts.
    Require(readOnly.GetPaletteSnapshot() == paletteBefore &&
            readOnly.GetRevision() == revisionBefore &&
            readOnly.GetVoxelCount() == voxelCountBefore &&
            readOnly.GetVoxel({0, 0, 0}) == voxelBefore &&
            !readOnly.IsDirty(),
        "A read-only document must be untouched after the editor path, "
        "Undo/Redo and a history dirty update.");
}

// VF-STAB-01C (revue Codex). Le test precedent appelait Undo et Redo sur un
// historique VIDE : les deux sortaient en NothingToUndo / NothingToRedo AVANT
// d'atteindre la moindre transaction. Il ne prouvait donc rien du comportement
// avec des piles peuplees, qui est justement le cas ou la mutation est tentee.
//
// Ce test peuple reellement les piles par l'API de l'historique, puis vise un
// document en lecture seule. Deux preuves rendent le chemin incontestable :
//   - le code de retour est `Failed`, et non NothingToUndo/NothingToRedo : la
//     pile n'etait donc pas vide et l'operation a bien ete tentee ;
//   - le message porte la raison de lecture seule du DOCUMENT, ce qui prouve
//     que le refus vient de ApplyCompositeChanges et non d'un controle
//     anterieur d'ApplyVoxelEditOperation (etat attendu, bornes, generation).
// L'etat de chaque document en lecture seule est prepare pour SATISFAIRE le
// controle d'etat attendu : sans cela, le refus viendrait de ce controle et ne
// dirait rien de la garde lecture seule.
void TestReadOnlyDocumentRefusesUndoRedoWithPopulatedStacks()
{
    const Asset::Voxel::VoxelPosition target{1, 0, 0};
    constexpr std::uint8_t palette = 7U;

    // Un document editable recoit une vraie transaction : la pile Undo se
    // remplit par le chemin de production, jamais a la main.
    auto editable = Document({Model({8U, 8U, 8U}, {})});
    TestSession editableSession(editable);
    editableSession.hasModel_ = false;
    Require(editableSession.PrimeMesh(),
        "Unable to prime the editable history mesh.");

    Editor::VoxelEditHistory history;
    history.MarkSavedState(editable);
    Require(static_cast<bool>(
            history.Execute(editableSession,
                AddOperation("Editable add", target, palette))),
        "The editable transaction that fills the undo stack failed.");
    Require(history.UndoCount() == 1U && history.RedoCount() == 0U,
        "The undo stack was not populated by the real transaction.");
    Require(editable.GetVoxel(target).has_value(),
        "The editable transaction did not create its voxel.");

    // --- Scenario A : Undo avec une pile Undo NON VIDE ---
    //
    // Le document en lecture seule porte deja l'etat que l'annulation attend
    // (le voxel pose), donc le controle d'etat passe et l'operation atteint la
    // mutation, ou la garde lecture seule la refuse.
    auto readOnlyUndo =
        Document({Model({257U, 1U, 1U}, {{1U, 0U, 0U, palette}})});
    Require(readOnlyUndo.IsReadOnly(),
        "The undo fixture must be loaded read-only.");
    TestSession readOnlyUndoSession(readOnlyUndo);
    readOnlyUndoSession.hasModel_ = false;
    Require(readOnlyUndoSession.PrimeMesh(),
        "Unable to prime the read-only undo mesh.");

    const auto paletteBeforeUndo = readOnlyUndo.GetPaletteSnapshot();
    const auto revisionBeforeUndo = readOnlyUndo.GetRevision();
    const auto countBeforeUndo = readOnlyUndo.GetVoxelCount();
    const auto voxelBeforeUndo = readOnlyUndo.GetVoxel(target);

    const auto undone = history.Undo(readOnlyUndoSession);

    Require(undone.Code == Editor::VoxelEditHistoryResultCode::Failed,
        "Undo on a read-only document must fail at the transaction, not "
        "return NothingToUndo: the stack was not empty.");
    Require(undone.Message.find("read-only") != std::string::npos,
        "The undo refusal must come from the document's read-only guard, not "
        "from an earlier expected-state or bounds check.");
    Require(!static_cast<bool>(undone),
        "A refused undo must not report success.");
    // La pile n'est pas consommee et aucune entree Redo n'est fabriquee.
    Require(history.UndoCount() == 1U && history.RedoCount() == 0U,
        "A refused undo must leave both stacks exactly as they were.");
    Require(readOnlyUndo.GetVoxel(target) == voxelBeforeUndo &&
            readOnlyUndo.GetPaletteSnapshot() == paletteBeforeUndo &&
            readOnlyUndo.GetRevision() == revisionBeforeUndo &&
            readOnlyUndo.GetVoxelCount() == countBeforeUndo &&
            !readOnlyUndo.IsDirty(),
        "A refused undo must leave the read-only document untouched.");

    // --- Scenario B : Redo avec une pile Redo NON VIDE ---
    //
    // L'annulation est d'abord effectuee pendant que le document est encore
    // editable, ce qui remplit la pile Redo par le chemin reel.
    Require(static_cast<bool>(history.Undo(editableSession)),
        "The editable undo that fills the redo stack failed.");
    Require(history.UndoCount() == 0U && history.RedoCount() == 1U,
        "The redo stack was not populated by the real history path.");
    Require(!editable.GetVoxel(target).has_value(),
        "The editable undo did not restore the initial state.");

    // Cette fois le retablissement attend une cellule VIDE : le document en
    // lecture seule est prepare en consequence.
    auto readOnlyRedo = Document({Model({257U, 1U, 1U}, {})});
    Require(readOnlyRedo.IsReadOnly(),
        "The redo fixture must be loaded read-only.");
    TestSession readOnlyRedoSession(readOnlyRedo);
    readOnlyRedoSession.hasModel_ = false;
    Require(readOnlyRedoSession.PrimeMesh(),
        "Unable to prime the read-only redo mesh.");

    const auto paletteBeforeRedo = readOnlyRedo.GetPaletteSnapshot();
    const auto revisionBeforeRedo = readOnlyRedo.GetRevision();
    const auto countBeforeRedo = readOnlyRedo.GetVoxelCount();

    const auto redone = history.Redo(readOnlyRedoSession);

    Require(redone.Code == Editor::VoxelEditHistoryResultCode::Failed,
        "Redo on a read-only document must fail at the transaction, not "
        "return NothingToRedo: the stack was not empty.");
    Require(redone.Message.find("read-only") != std::string::npos,
        "The redo refusal must come from the document's read-only guard.");
    Require(!static_cast<bool>(redone),
        "A refused redo must not report success.");
    Require(history.UndoCount() == 0U && history.RedoCount() == 1U,
        "A refused redo must leave both stacks exactly as they were.");
    Require(!readOnlyRedo.GetVoxel(target).has_value() &&
            readOnlyRedo.GetPaletteSnapshot() == paletteBeforeRedo &&
            readOnlyRedo.GetRevision() == revisionBeforeRedo &&
            readOnlyRedo.GetVoxelCount() == countBeforeRedo &&
            !readOnlyRedo.IsDirty(),
        "A refused redo must leave the read-only document untouched.");
}

void TestShortcutInput()
{
    Editor::VoxelHistoryInputController input;
    Editor::VoxelHistoryInputFrame frame;
    frame.ControlDown = true;
    frame.ZDown = true;
    frame.HasDocument = true;
    Require(input.Update(frame) == Editor::VoxelHistoryInputDecision::Undo &&
        input.Update(frame) == Editor::VoxelHistoryInputDecision::None,
        "Ctrl+Z front edge or held-key protection failed.");
    frame.ZDown = false;
    static_cast<void>(input.Update(frame));
    frame.YDown = true;
    Require(input.Update(frame) == Editor::VoxelHistoryInputDecision::Redo,
        "Ctrl+Y was not recognized.");
    frame.YDown = false;
    static_cast<void>(input.Update(frame));
    frame.ZDown = true;
    frame.ShiftDown = true;
    Require(input.Update(frame) == Editor::VoxelHistoryInputDecision::Redo,
        "Ctrl+Shift+Z was not recognized.");

    const auto blocked = [](Editor::VoxelHistoryInputFrame blockedFrame)
    {
        Editor::VoxelHistoryInputController controller;
        return controller.Update(blockedFrame) ==
            Editor::VoxelHistoryInputDecision::None;
    };
    Editor::VoxelHistoryInputFrame base;
    base.ControlDown = true;
    base.ZDown = true;
    base.HasDocument = true;
    auto text = base; text.TextInput = true;
    auto popup = base; popup.PopupOpen = true;
    auto captured = base; captured.KeyboardCaptured = true;
    auto noDocument = base; noDocument.HasDocument = false;
    auto editing = base; editing.EditInProgress = true;
    auto historyBusy = base; historyBusy.HistoryInProgress = true;
    Require(blocked(text) && blocked(popup) && blocked(captured) &&
        blocked(noDocument) && blocked(editing) && blocked(historyBusy),
        "A protected Undo/Redo shortcut context was accepted.");
}
}

int main()
{
    try
    {
        TestEmptyAndToolIntegration();
        TestOrderingBranchingAndSavedIdentity();
        TestMultiChangeAndSingleRevision();
        TestPaletteOnlyAndCompositeHistory();
        TestCompositeValidationAndRollback();
        TestCompositeDuplicatesLargeBatchAndLifecycle();
        TestLimitsAndMemory();
        TestRefusalsAndRollback();
        TestDocumentOnlyCanonicalTransactions();
        TestReadOnlyDocumentRefusesEditorTransactions();
        TestReadOnlyDocumentRefusesUndoRedoWithPopulatedStacks();
        TestShortcutInput();
        std::cout << "Voxel edit history tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel edit history tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
