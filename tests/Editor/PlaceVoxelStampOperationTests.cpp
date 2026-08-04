#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument Document(
    const std::uint32_t width = 16U,
    const bool additionalModel = false,
    const std::uint32_t height = 4U,
    const std::uint32_t depth = 4U)
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {width, height, depth}});
    if (additionalModel)
    {
        source.Models.push_back({.Dimensions = {4U, 4U, 4U},
            .Voxels = {
                {.X = 0U, .Y = 0U, .Z = 0U, .ColorIndex = 7U},
                {.X = 1U, .Y = 0U, .Z = 0U, .ColorIndex = 8U}}});
        source.Palette[7U] = {11U, 22U, 33U, 255U};
        source.Palette[8U] = {44U, 55U, 66U, 255U};
        source.HasCustomPalette = true;
    }
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-placement-test.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create placement document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U;
         index < document.GetPalette().size(); ++index)
    {
        const auto color = document.GetPalette()[index];
        Require(model.Palette().Set(index,
            {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize compatibility palette.");
    }
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Placement document has no model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize grid.");
    source->ForEachVoxel(
        [&grid](const Asset::Voxel::VoxelPosition position,
                const Asset::Voxel::Voxel voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize compatibility voxel.");
        });
    model.AddGrid(std::move(grid));
    return model;
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document))
    {
    }
    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        if (FailRebuild)
        {
            return CommandResult::Failure(
                "Simulated Stamp mesh rebuild failure.");
        }
        const auto result = cache_.Synchronize(*document_, Generation);
        return result.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(result.Message);
    }
    void CompleteVoxelEdit() noexcept override { ++Completed; }

    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
    std::uint64_t Generation = 1U;
    bool FailRebuild = false;
    std::size_t Rebuilds = 0U;
    std::size_t Completed = 0U;
};

VoxelStamp Stamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{13U}, "stamp-13"},
        {{}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}},
        {},
        {{0U, {11U, 22U, 33U, 255U}},
         {1U, {44U, 55U, 66U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(), "Invalid stamp fixture.");
    return *stamp;
}

StampPlacementPlan Plan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampFixedPoint target = {},
    const StampCollisionPolicy collisionPolicy =
        StampCollisionPolicy::Overwrite,
    const std::uint64_t documentGeneration = 1U)
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = documentGeneration,
        .Transform = {.TargetPivot = target},
        .CollisionPolicy = collisionPolicy});
}

PlaceVoxelStampPreparation Prepare(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampFixedPoint target)
{
    const StampPlacementPlan plan = Plan(stamp, document, target);
    Require(plan.WorldBounds.Valid, "Unable to build placement plan.");
    return PreparePlaceVoxelStampOperation(plan);
}

void TestPreviewPlacementUndoRedo()
{
    auto document = Document();
    Session session(document);
    VoxelEditHistory history;
    const VoxelStamp stamp = Stamp();
    const StampPlacementPlan plan = Plan(stamp, document);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    const auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() && prepared.Operation.PaletteChange &&
                prepared.Operation.Changes.size() == preview.Voxels.size(),
        "New colors must prepare one composite operation.");
    const auto paletteBefore = document.GetPaletteSnapshot();
    const auto revision = document.GetRevision();
    Require(ExecutePlaceVoxelStampOperation(plan, session, history) &&
                document.GetRevision() == revision + 1U &&
                session.Rebuilds == 1U && session.Completed == 1U &&
                history.UndoCount() == 1U && history.RedoCount() == 0U &&
                document.GetPaletteSnapshot() ==
                    plan.PaletteMapping.FinalDocumentPalette,
        "Placement must execute as one revision and rebuild.");
    for (std::size_t index = 0U; index < preview.Voxels.size(); ++index)
    {
        const auto voxel =
            document.GetVoxel(preview.Voxels[index].Position);
        Require(voxel &&
                    document.GetPalette()[voxel->PaletteIndex] ==
                        preview.Voxels[index].Color,
            "Committed color must exactly match preview and plan.");
    }
    Require(history.Undo(session) && document.GetVoxelCount() == 0U &&
                document.GetPaletteSnapshot() == paletteBefore &&
                history.UndoCount() == 0U && history.RedoCount() == 1U,
        "Undo must remove only the placement.");
    Require(history.Redo(session) &&
                document.GetVoxelCount() == preview.Voxels.size() &&
                document.GetPaletteSnapshot() ==
                    plan.PaletteMapping.FinalDocumentPalette &&
                session.Rebuilds == 3U && session.Completed == 3U &&
                history.UndoCount() == 1U && history.RedoCount() == 0U,
        "Redo must restore the placement exactly.");
    for (const auto& planned : plan.Voxels)
    {
        const auto voxel = document.GetVoxel(planned.WorldPosition);
        Require(voxel && *voxel == planned.FinalVoxel,
            "Redo must use the exact stored Stamp result.");
    }
}

void TestMultiplePlacementsAndOutOfBoundsFailure()
{
    auto document = Document();
    Session session(document);
    VoxelEditHistory history;
    const VoxelStamp stamp = Stamp();
    const auto firstPlan = Plan(stamp, document);
    Require(static_cast<bool>(ExecutePlaceVoxelStampOperation(
                firstPlan, session, history)),
        "First placement failed.");
    const auto secondPlan = Plan(stamp, document,
        {2 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    const auto second = PreparePlaceVoxelStampOperation(secondPlan);
    Require(second.IsReady() && !second.Operation.PaletteChange &&
                ExecutePlaceVoxelStampOperation(
                    secondPlan, session, history) &&
                history.UndoCount() == 2U && session.Rebuilds == 2U,
        "Second placement must reuse colors in a separate operation.");

    const auto invalidPlan = Plan(stamp, document,
        {-StampFixedPoint::UnitsPerVoxel, 0, 0});
    const auto revision = document.GetRevision();
    const auto invalid = PreparePlaceVoxelStampOperation(invalidPlan);
    Require(!invalidPlan.CanCommit &&
                invalid.Status ==
                    PlaceVoxelStampPreparationStatus::InvalidPreview &&
                document.GetRevision() == revision,
        "Out-of-bounds plan must fail before mutation.");
}

void TestCollisionPoliciesExecuteAtomically()
{
    const VoxelStamp stamp = Stamp();

    auto overwriteDocument = Document();
    Require(overwriteDocument.SetVoxel({0, 0, 0}, 1U).Succeeded,
        "Unable to create overwrite fixture.");
    const auto overwritePlan = Plan(stamp, overwriteDocument);
    Session overwriteSession(overwriteDocument);
    VoxelEditHistory overwriteHistory;
    Require(ExecutePlaceVoxelStampOperation(
                overwritePlan, overwriteSession, overwriteHistory) &&
                overwriteDocument.GetVoxel({0, 0, 0}) ==
                    overwritePlan.Voxels[0U].FinalVoxel &&
                overwriteDocument.GetVoxel({1, 0, 0}) ==
                    overwritePlan.Voxels[1U].FinalVoxel &&
                overwriteHistory.UndoCount() == 1U,
        "Overwrite must replace and add in one operation.");
    Require(overwriteHistory.Undo(overwriteSession) &&
                overwriteDocument.GetVoxel({0, 0, 0}) ==
                    Asset::Voxel::Voxel{1U} &&
                !overwriteDocument.GetVoxel({1, 0, 0}),
        "Overwrite Undo must restore the occupied destination exactly.");

    auto skipDocument = Document();
    Require(skipDocument.SetVoxel({0, 0, 0}, 1U).Succeeded,
        "Unable to create SkipOccupied fixture.");
    const auto skipPlan = Plan(stamp, skipDocument, {},
        StampCollisionPolicy::SkipOccupied);
    Require(skipPlan.CanCommit && skipPlan.Statistics.SkippedVoxelCount == 1U,
        "SkipOccupied plan must identify the occupied destination.");
    Session skipSession(skipDocument);
    VoxelEditHistory skipHistory;
    Require(ExecutePlaceVoxelStampOperation(
                skipPlan, skipSession, skipHistory) &&
                skipDocument.GetVoxel({0, 0, 0}) ==
                    Asset::Voxel::Voxel{1U} &&
                skipDocument.GetVoxel({1, 0, 0}) ==
                    skipPlan.Voxels[1U].FinalVoxel &&
                skipHistory.UndoCount() == 1U,
        "SkipOccupied must preserve occupied cells and add free cells.");
    Require(skipHistory.Undo(skipSession) &&
                skipDocument.GetVoxel({0, 0, 0}) ==
                    Asset::Voxel::Voxel{1U} &&
                !skipDocument.GetVoxel({1, 0, 0}),
        "SkipOccupied Undo must preserve the pre-existing voxel.");

    auto rejectDocument = Document();
    Require(rejectDocument.SetVoxel({0, 0, 0}, 1U).Succeeded,
        "Unable to create Reject fixture.");
    const auto rejectPlan = Plan(stamp, rejectDocument, {},
        StampCollisionPolicy::Reject);
    const auto rejectPalette = rejectDocument.GetPaletteSnapshot();
    const auto rejectRevision = rejectDocument.GetRevision();
    const auto rejectVoxelCount = rejectDocument.GetVoxelCount();
    Session rejectSession(rejectDocument);
    VoxelEditHistory rejectHistory;
    const auto rejected = ExecutePlaceVoxelStampOperation(
        rejectPlan, rejectSession, rejectHistory);
    Require(rejected.Code == VoxelEditHistoryResultCode::InvalidOperation &&
                rejectDocument.GetPaletteSnapshot() == rejectPalette &&
                rejectDocument.GetRevision() == rejectRevision &&
                rejectDocument.GetVoxelCount() == rejectVoxelCount &&
                rejectHistory.UndoCount() == 0U &&
                rejectSession.Rebuilds == 0U,
        "Reject must refuse the whole placement before mutation.");
}

void TestOverlapNoChangeAndSharedPaletteAcrossSubModels()
{
    auto document = Document(16U, true);
    const VoxelStamp stamp = Stamp();
    auto sharedPalette = PreparePlaceVoxelStampOperation(
        Plan(stamp, document));
    Require(sharedPalette.IsReady() &&
                !sharedPalette.Operation.PaletteChange &&
                sharedPalette.Operation.Changes.front().PaletteIndexAfter ==
                    7U,
        "Colors occupied in another sub-model must be reused.");

    Session session(document);
    VoxelEditHistory history;
    Require(static_cast<bool>(
                history.Execute(session, std::move(sharedPalette.Operation))),
        "Shared-palette placement failed.");
    const auto unchangedRevision = document.GetRevision();
    const auto noChangePlan = Plan(stamp, document);
    const auto noChange =
        PreparePlaceVoxelStampOperation(noChangePlan);
    const auto noChangeResult = ExecutePlaceVoxelStampOperation(
        noChangePlan, session, history);
    Require(noChange.IsNoChange() &&
                noChangeResult.Code == VoxelEditHistoryResultCode::NoChange &&
                document.GetRevision() == unchangedRevision &&
                history.UndoCount() == 1U && session.Rebuilds == 1U,
        "Identical overlap must create no history operation.");

    Require(document.SetVoxel({0, 0, 0}, 1U).Succeeded,
        "Unable to create replacement fixture.");
    const auto replacement =
        PreparePlaceVoxelStampOperation(Plan(stamp, document));
    Require(replacement.IsReady() &&
                replacement.Operation.Changes.size() == 1U &&
                replacement.Operation.Changes.front().ExistedBefore &&
                replacement.Operation.Changes.front().PaletteIndexBefore ==
                    1U,
        "Differing overlap must retain exact Before state.");
}

void TestPreviewClearDoesNotMutate()
{
    auto document = Document();
    const VoxelStamp stamp = Stamp();
    const auto preview =
        StampLivePreviewBuilder::Build(Plan(stamp, document));
    VoxelPreviewSession session;
    const auto revision = document.GetRevision();
    Require(session.Activate(preview) && session.Current() != nullptr &&
                session.Clear() && session.Current() == nullptr &&
                document.GetRevision() == revision,
        "ESC-equivalent clear must not mutate the document.");
}

void TestInvalidAndStalePlan()
{
    const StampPlacementPlan invalid{};
    Require(PreparePlaceVoxelStampOperation(invalid).Status ==
                PlaceVoxelStampPreparationStatus::InvalidInput,
        "Empty plan must fail safely.");

    const VoxelStamp stamp = Stamp();
    auto invalidDocument = Document();
    Session invalidSession(invalidDocument);
    VoxelEditHistory invalidHistory;
    const auto invalidResult = ExecutePlaceVoxelStampOperation(
        invalid, invalidSession, invalidHistory);
    Require(invalidResult.Code ==
                VoxelEditHistoryResultCode::InvalidOperation &&
                invalidDocument.GetVoxelCount() == 0U &&
                invalidHistory.UndoCount() == 0U,
        "Empty plan execution must fail without mutation.");

    const auto missingDocumentPlan = Plan(stamp, invalidDocument);
    invalidSession.document_ = nullptr;
    const auto missingDocumentResult = ExecutePlaceVoxelStampOperation(
        missingDocumentPlan, invalidSession, invalidHistory);
    Require(missingDocumentResult.Code ==
                VoxelEditHistoryResultCode::InvalidOperation &&
                invalidDocument.GetVoxelCount() == 0U &&
                invalidHistory.UndoCount() == 0U,
        "Placement without an active document must fail safely.");

    auto generationDocument = Document();
    const auto generationPlan = Plan(stamp, generationDocument);
    Session generationSession(generationDocument);
    generationSession.Generation = 2U;
    VoxelEditHistory generationHistory;
    const auto generationResult = ExecutePlaceVoxelStampOperation(
        generationPlan, generationSession, generationHistory);
    Require(generationResult.Code ==
                VoxelEditHistoryResultCode::InvalidOperation &&
                generationDocument.GetVoxelCount() == 0U &&
                generationHistory.UndoCount() == 0U &&
                generationSession.Rebuilds == 0U,
        "A stale document generation must fail before preparation.");

    auto revisionDocument = Document();
    const auto revisionPlan = Plan(stamp, revisionDocument);
    Require(revisionDocument.SetVoxel({8, 0, 0}, 1U).Succeeded,
        "Unable to advance the document revision.");
    const auto revisionPalette = revisionDocument.GetPaletteSnapshot();
    const auto revision = revisionDocument.GetRevision();
    Session revisionSession(revisionDocument);
    VoxelEditHistory revisionHistory;
    const auto revisionResult = ExecutePlaceVoxelStampOperation(
        revisionPlan, revisionSession, revisionHistory);
    Require(revisionResult.Code ==
                VoxelEditHistoryResultCode::InvalidOperation &&
                revisionDocument.GetPaletteSnapshot() == revisionPalette &&
                revisionDocument.GetRevision() == revision &&
                revisionDocument.GetVoxelCount() == 1U &&
                revisionHistory.UndoCount() == 0U &&
                revisionSession.Rebuilds == 0U,
        "A stale document revision must fail without mutation.");

    auto plannedDocument = Document();
    const auto identityPlan = Plan(stamp, plannedDocument);
    auto activeDocument = Document();
    Session identitySession(activeDocument);
    VoxelEditHistory identityHistory;
    const auto identityResult = ExecutePlaceVoxelStampOperation(
        identityPlan, identitySession, identityHistory);
    Require(identityResult.Code ==
                VoxelEditHistoryResultCode::InvalidOperation &&
                activeDocument.GetVoxelCount() == 0U &&
                identityHistory.UndoCount() == 0U &&
                identitySession.Rebuilds == 0U,
        "A plan from another document instance must fail before mutation.");

    auto preconditionDocument = Document();
    const auto preconditionPlan = Plan(stamp, preconditionDocument);
    auto prepared = PreparePlaceVoxelStampOperation(preconditionPlan);
    Require(prepared.IsReady() && preconditionDocument.SetVoxel(
                preconditionPlan.Voxels.front().WorldPosition, 1U).Succeeded,
        "Unable to create stale transaction fixture.");
    Session preconditionSession(preconditionDocument);
    VoxelEditHistory preconditionHistory;
    const auto before = preconditionDocument.GetPaletteSnapshot();
    const auto preconditionRevision = preconditionDocument.GetRevision();
    Require(!preconditionHistory.Execute(
                preconditionSession, std::move(prepared.Operation)) &&
                preconditionDocument.GetPaletteSnapshot() == before &&
                preconditionDocument.GetRevision() == preconditionRevision &&
                preconditionHistory.UndoCount() == 0U,
        "Stored operation preconditions must prevent partial mutation.");
}

void TestRollbackOnRebuildFailure()
{
    auto document = Document();
    const VoxelStamp stamp = Stamp();
    const auto plan = Plan(stamp, document);
    const auto paletteBefore = document.GetPaletteSnapshot();
    const auto revisionBefore = document.GetRevision();
    const bool dirtyBefore = document.IsDirty();
    Session session(document);
    session.FailRebuild = true;
    VoxelEditHistory history;

    const auto result = ExecutePlaceVoxelStampOperation(
        plan, session, history);
    Require(result.Code == VoxelEditHistoryResultCode::Failed &&
                document.GetPaletteSnapshot() == paletteBefore &&
                document.GetVoxelCount() == 0U &&
                document.GetRevision() == revisionBefore &&
                document.IsDirty() == dirtyBefore &&
                history.UndoCount() == 0U && history.RedoCount() == 0U &&
                session.Rebuilds == 1U && session.Completed == 0U,
        "Rebuild failure must roll back document, palette and history.");
    const auto* grid = session.model_.GetGrid(0U);
    Require(grid != nullptr && grid->Get(0U, 0U, 0U) &&
                !grid->Get(0U, 0U, 0U)->IsOccupied() &&
                grid->Get(1U, 0U, 0U) &&
                !grid->Get(1U, 0U, 0U)->IsOccupied(),
        "Rebuild failure must roll back the compatibility grid.");
}

void TestHistoryMemoryLimitRefusesBeforeMutation()
{
    auto document = Document();
    const VoxelStamp stamp = Stamp();
    const auto plan = Plan(stamp, document);
    const auto paletteBefore = document.GetPaletteSnapshot();
    const auto revisionBefore = document.GetRevision();
    const bool dirtyBefore = document.IsDirty();
    Session session(document);
    VoxelEditHistory history({
        .MaximumCommandCount = 100U,
        .MaximumEstimatedMemory = 1U});

    const auto result = ExecutePlaceVoxelStampOperation(
        plan, session, history);
    Require(result.Code == VoxelEditHistoryResultCode::LimitExceeded &&
                document.GetPaletteSnapshot() == paletteBefore &&
                document.GetVoxelCount() == 0U &&
                document.GetRevision() == revisionBefore &&
                document.IsDirty() == dirtyBefore &&
                history.UndoCount() == 0U && history.RedoCount() == 0U &&
                session.Rebuilds == 0U && session.Completed == 0U,
        "History memory refusal must happen before any Stamp mutation.");
}

void TestFullPaletteFailsWithoutMutation()
{
    auto document = Document(300U);
    for (std::size_t index = 1U; index < 256U; ++index)
    {
        Require(document.SetVoxel(
            {static_cast<std::int32_t>(index - 1U), 1, 0}, index).Succeeded,
            "Unable to fill palette fixture.");
    }
    const VoxelStamp stamp = Stamp();
    const auto plan = Plan(stamp, document,
        {256 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    const auto revision = document.GetRevision();
    const auto result = PreparePlaceVoxelStampOperation(plan);
    Require(result.Status ==
                PlaceVoxelStampPreparationStatus::PaletteMappingFailed &&
                result.PaletteStatus ==
                    PaletteMappingStatus::PaletteCapacityExceeded &&
                document.GetRevision() == revision,
        "Full palette must fail before mutation.");
}

void TestLargeStampUsesOneOperationAndOneRebuild()
{
    auto document = Document(64U, false, 64U, 1U);
    std::vector<StampVoxel> voxels;
    voxels.reserve(64U * 64U);
    for (std::int32_t y = 0; y < 64; ++y)
    {
        for (std::int32_t x = 0; x < 64; ++x)
        {
            voxels.push_back({{x, y, 0}, 0U});
        }
    }
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x4c41524745535441ULL}, "stamp-13-large"},
        {{0, 0, 0}, {63, 63, 0}, {64U, 64U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}},
        {}, {{0U, {3U, 5U, 7U, 255U}}}, std::move(voxels),
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(), "Invalid large stamp fixture.");

    const auto plan = Plan(*stamp, document);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    Require(preview.IsActive() && preview.Voxels.size() == 4096U,
        "Large preview must retain every planned voxel.");
    const auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() &&
                prepared.Operation.Changes.size() == 4096U,
        "Large stamp must remain one composite operation.");

    Session session(document);
    VoxelEditHistory history;
    const auto revision = document.GetRevision();
    Require(ExecutePlaceVoxelStampOperation(plan, session, history) &&
                document.GetRevision() == revision + 1U &&
                session.Rebuilds == 1U && history.UndoCount() == 1U,
        "Large stamp must commit and rebuild exactly once.");
}

} // namespace

int main()
{
    try
    {
        TestPreviewPlacementUndoRedo();
        TestMultiplePlacementsAndOutOfBoundsFailure();
        TestCollisionPoliciesExecuteAtomically();
        TestOverlapNoChangeAndSharedPaletteAcrossSubModels();
        TestPreviewClearDoesNotMutate();
        TestInvalidAndStalePlan();
        TestRollbackOnRebuildFailure();
        TestHistoryMemoryLimitRefusesBeforeMutation();
        TestFullPaletteFailsWithoutMutation();
        TestLargeStampUsesOneOperationAndOneRebuild();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
