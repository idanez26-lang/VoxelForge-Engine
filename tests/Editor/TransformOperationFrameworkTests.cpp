#include "Transform/TransformOperationFramework.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <chrono>
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
using Position = Asset::Voxel::VoxelPosition;

constexpr std::uint64_t DocumentGeneration = 71U;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument(const bool obstacle = true)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    model.Voxels = {{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}};
    if (obstacle) model.Voxels.push_back({6U, 1U, 1U, 9U});
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "transform-framework-memory.vox");
    Require(loaded.Succeeded(), "Unable to build framework test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Framework test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size framework compatibility grid.");
    model->ForEachVoxel([&grid](const Position position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize framework compatibility grid.");
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
        return DocumentGeneration;
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
            ? Editor::CommandResult::Failure("simulated framework rebuild failure")
            : Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
    bool failRebuild_ = false;
    bool savedState_ = true;
};

Editor::SelectionService MakeSelection(
    const std::vector<Position>& positions)
{
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(DocumentGeneration);
    static_cast<void>(selection.Apply(
        positions, Editor::SelectionMode::Replace));
    return selection;
}

Editor::TransformOperationBuildResult Build(
    const Asset::Voxel::VoxelDocument& document,
    const Editor::SelectionService& selection,
    const Editor::TransformPreviewModel& preview,
    const Editor::TransformSourcePolicy source,
    const Editor::TransformCollisionPolicy collision,
    const std::uint64_t generation = DocumentGeneration)
{
    return Editor::TransformOperationBuilder::Build(
        document, selection, generation, preview,
        {"Test Transform", "Test Transform",
         {source, collision}, preview.PreviewBounds()});
}

void ApplyTransition(
    Editor::SelectionService& selection,
    const Editor::VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "Framework history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

void TestValidationAndCollisionPolicies()
{
    auto document = MakeDocument();
    document.MarkSaved();
    const std::uint64_t initialRevision = document.GetRevision();
    const std::size_t initialCount = document.GetVoxelCount();
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}});

    Editor::TransformPreviewModel overlap;
    Require(overlap.BeginPreview(document, selection, DocumentGeneration) &&
        overlap.SetDelta(document, selection, DocumentGeneration, {1, 0, 0}),
        "Unable to create source-overlap preview.");
    const auto allowed = Build(document, selection, overlap,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    Require(allowed.Ready() && allowed.Operation.Changes.size() == 3U,
        "A valid source-overlap transform was rejected.");

    Editor::SelectionService volumeSelection;
    volumeSelection.SetDocumentGeneration(DocumentGeneration);
    const std::vector<Position> volumeOrder{{2, 1, 1}, {1, 1, 1}};
    Require(volumeSelection.ApplySortedVolume(
            volumeOrder,
            Editor::SelectionBounds::FromCorners({1, 1, 1}, {2, 1, 1}),
            Editor::SelectionMode::Replace),
        "Unable to preserve volume-selection iteration order.");
    Editor::TransformPreviewModel volumeOrderPreview;
    Require(volumeOrderPreview.BeginPreview(
            document, volumeSelection, DocumentGeneration) &&
        volumeOrderPreview.SetDelta(
            document, volumeSelection, DocumentGeneration, {1, 0, 0}),
        "Unable to create volume-order preview.");
    const auto volumeOrderResult = Build(
        document, volumeSelection, volumeOrderPreview,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    Require(volumeOrderResult.Ready(),
        std::string("Framework incorrectly required a particular source ") +
            "iteration order: " + volumeOrderResult.Message);

    Editor::TransformPreviewModel duplicateDestination;
    Require(duplicateDestination.BeginPreview(
            document, selection, DocumentGeneration),
        "Unable to begin duplicate-destination preview.");
    const std::vector<Position> duplicates{{4, 1, 1}, {4, 1, 1}};
    Require(duplicateDestination.SetExplicitDestinations(
            document, selection, DocumentGeneration, duplicates),
        "Unable to create duplicate destinations.");
    const auto duplicate = Build(document, selection, duplicateDestination,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    Require(duplicate.Code ==
            Editor::TransformOperationBuildCode::InvalidDestinations,
        "Duplicate destinations were accepted.");

    Editor::TransformPreviewModel outOfBounds;
    Require(outOfBounds.BeginPreview(document, selection, DocumentGeneration) &&
        outOfBounds.SetDelta(
            document, selection, DocumentGeneration, {-2, 0, 0}),
        "Unable to create out-of-bounds preview.");
    Require(Build(document, selection, outOfBounds,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::OutOfBounds,
        "An out-of-bounds destination was accepted.");

    auto one = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel externalCollision;
    Require(externalCollision.BeginPreview(
            document, one, DocumentGeneration) &&
        externalCollision.SetDelta(
            document, one, DocumentGeneration, {5, 0, 0}),
        "Unable to create external-collision preview.");
    Require(Build(document, one, externalCollision,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::Collision,
        "An external collision was accepted.");

    Editor::TransformPreviewModel identity;
    Require(identity.BeginPreview(document, one, DocumentGeneration),
        "Unable to begin identity preview.");
    const std::vector<Position> same{{1, 1, 1}};
    Require(identity.SetExplicitDestinations(
            document, one, DocumentGeneration, same),
        "Unable to create identity preview.");
    Require(Build(document, one, identity,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::NoChange,
        "Identity was not recognized as a no-op.");

    Editor::TransformPreviewModel rejectedOverlap;
    Require(rejectedOverlap.BeginPreview(
            document, selection, DocumentGeneration),
        "Unable to begin rejected source-overlap preview.");
    const std::vector<Position> swapped{{2, 1, 1}, {1, 1, 1}};
    Require(rejectedOverlap.SetExplicitDestinations(
            document, selection, DocumentGeneration, swapped),
        "Unable to create rejected source-overlap destinations.");
    Require(Build(document, selection, rejectedOverlap,
            Editor::TransformSourcePolicy::PreserveSource,
            Editor::TransformCollisionPolicy::RejectAnyOccupiedDestination).Code ==
            Editor::TransformOperationBuildCode::Collision,
        "PreserveSource accepted overlap with its source.");

    Require(document.GetRevision() == initialRevision &&
        document.GetVoxelCount() == initialCount && !document.IsDirty(),
        "A validation refusal mutated the document.");
}

void TestDocumentSelectionAndSnapshotValidation()
{
    auto document = MakeDocument(false);
    document.MarkSaved();
    auto selection = MakeSelection({{1, 1, 1}});

    Editor::TransformPreviewModel generationPreview;
    Require(generationPreview.BeginPreview(
            document, selection, DocumentGeneration) &&
        generationPreview.SetDelta(
            document, selection, DocumentGeneration, {2, 0, 0}),
        "Unable to create generation preview.");
    Require(Build(document, selection, generationPreview,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap,
            DocumentGeneration + 1U).Code ==
            Editor::TransformOperationBuildCode::ModelChanged,
        "A different document generation was accepted.");

    auto changedSelection = MakeSelection({{2, 1, 1}});
    Require(Build(document, changedSelection, generationPreview,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::SelectionChanged,
        "A different selection was accepted.");

    Require(document.SetVoxel({1, 1, 1}, 12U).Changed,
        "Unable to modify captured voxel value.");
    Require(Build(document, selection, generationPreview,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::ModelChanged,
        "A changed revision/value was accepted.");

    auto missingDocument = MakeDocument(false);
    missingDocument.MarkSaved();
    auto missingSelection = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel missingPreview;
    Require(missingPreview.BeginPreview(
            missingDocument, missingSelection, DocumentGeneration) &&
        missingPreview.SetDelta(
            missingDocument, missingSelection, DocumentGeneration, {2, 0, 0}) &&
        missingDocument.RemoveVoxel({1, 1, 1}).Changed,
        "Unable to create missing-source case.");
    Require(Build(missingDocument, missingSelection, missingPreview,
            Editor::TransformSourcePolicy::RemoveSource,
            Editor::TransformCollisionPolicy::AllowSourceOverlap).Code ==
            Editor::TransformOperationBuildCode::ModelChanged,
        "A missing captured source was accepted.");
}

void TestAtomicHistoryAndSourcePolicies()
{
    auto document = MakeDocument(false);
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, DocumentGeneration) &&
        preview.SetDelta(document, selection, DocumentGeneration, {2, 0, 0}),
        "Unable to create atomic transform preview.");
    auto prepared = Build(document, selection, preview,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 2U &&
        prepared.Operation.SelectionTransition,
        "RemoveSource did not build one complete operation.");
    const std::uint64_t revision = document.GetRevision();
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplyTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U && document.IsDirty() &&
        !document.HasVoxel({1, 1, 1}) &&
        document.GetVoxel({3, 1, 1})->PaletteIndex == 3U &&
        selection.Contains({3, 1, 1}) && session.rebuildCount_ == 1U,
        "Atomic RemoveSource lost revision, color or destination selection.");

    const auto undone = history.Undo(session);
    ApplyTransition(selection, undone);
    Require(undone && !document.IsDirty() &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 3U &&
        !document.HasVoxel({3, 1, 1}) && selection.Contains({1, 1, 1}),
        "Undo did not restore source data and selection.");
    const auto redone = history.Redo(session);
    ApplyTransition(selection, redone);
    Require(redone && document.GetVoxel({3, 1, 1})->PaletteIndex == 3U &&
        selection.Contains({3, 1, 1}),
        "Redo did not restore destination data and selection.");

    auto copyDocument = MakeDocument(false);
    copyDocument.MarkSaved();
    auto copySelection = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel copyPreview;
    Require(copyPreview.BeginPreview(
            copyDocument, copySelection, DocumentGeneration) &&
        copyPreview.SetDelta(
            copyDocument, copySelection, DocumentGeneration, {2, 0, 0}),
        "Unable to create PreserveSource preview.");
    const auto copy = Build(copyDocument, copySelection, copyPreview,
        Editor::TransformSourcePolicy::PreserveSource,
        Editor::TransformCollisionPolicy::RejectAnyOccupiedDestination);
    Require(copy.Ready() && copy.Operation.Changes.size() == 1U &&
        copy.Operation.Changes.front().Position == Position{3, 1, 1} &&
        copy.Operation.Changes.front().PaletteIndexAfter == 3U,
        "PreserveSource did not retain the source or full voxel value.");

    auto rollbackDocument = MakeDocument(false);
    rollbackDocument.MarkSaved();
    TestSession rollbackSession(rollbackDocument);
    rollbackSession.failRebuild_ = true;
    Editor::VoxelEditHistory rollbackHistory;
    rollbackHistory.MarkSavedState(rollbackDocument);
    auto rollbackSelection = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel rollbackPreview;
    Require(rollbackPreview.BeginPreview(
            rollbackDocument, rollbackSelection, DocumentGeneration) &&
        rollbackPreview.SetDelta(
            rollbackDocument, rollbackSelection,
            DocumentGeneration, {2, 0, 0}),
        "Unable to create rollback preview.");
    auto rollback = Build(rollbackDocument, rollbackSelection, rollbackPreview,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    const std::uint64_t rollbackRevision = rollbackDocument.GetRevision();
    const auto failed = rollbackHistory.Execute(
        rollbackSession, std::move(rollback.Operation));
    Require(!failed && rollbackHistory.UndoCount() == 0U &&
        rollbackDocument.GetRevision() == rollbackRevision &&
        !rollbackDocument.IsDirty() &&
        rollbackDocument.GetVoxel({1, 1, 1})->PaletteIndex == 3U &&
        !rollbackDocument.HasVoxel({3, 1, 1}),
        "Failed atomic application was not rolled back completely.");
}

void TestExpanded4096Destinations()
{
    auto document = MakeDocument(false);
    Require(document.RemoveVoxel({2, 1, 1}).Changed,
        "Unable to isolate the large transform source.");
    document.MarkSaved();
    auto selection = MakeSelection({{1, 1, 1}});
    Editor::TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, DocumentGeneration),
        "Unable to begin expanded transform preview.");
    std::vector<Editor::TransformPreviewDestinationVoxel> destinations;
    destinations.reserve(4096U);
    for (std::int32_t z = 0; z < 16; ++z)
        for (std::int32_t y = 0; y < 16; ++y)
            for (std::int32_t x = 0; x < 16; ++x)
                destinations.push_back({
                    {1, 1, 1}, {x, y, z}, Asset::Voxel::Voxel{3U}});
    Require(preview.SetExplicitVoxelDestinations(
            document, selection, DocumentGeneration, destinations),
        "Unable to create 4096-destination preview.");

    const auto started = std::chrono::steady_clock::now();
    auto prepared = Build(document, selection, preview,
        Editor::TransformSourcePolicy::RemoveSource,
        Editor::TransformCollisionPolicy::AllowSourceOverlap);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 4095U &&
        prepared.Operation.SelectionTransition &&
        prepared.Operation.SelectionTransition->After.Voxels.size() == 4096U,
        "The common framework did not support one-to-many 4096 destinations.");
    Require(elapsed < std::chrono::seconds(5),
        "The 4096-destination validation exceeded its safety budget.");

    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    const std::uint64_t revision = document.GetRevision();
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        document.GetVoxelCount() == 4096U &&
        document.GetVoxel({15, 15, 15})->PaletteIndex == 3U,
        "Large one-to-many transform was not one atomic history entry.");
}
}

int main()
{
    try
    {
        TestValidationAndCollisionPolicies();
        TestDocumentSelectionAndSnapshotValidation();
        TestAtomicHistoryAndSourcePolicies();
        TestExpanded4096Destinations();
        std::cout << "Transform operation framework tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Transform operation framework tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
