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
    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        const auto result = cache_.Synchronize(*document_, 1U);
        return result.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(result.Message);
    }
    void CompleteVoxelEdit() noexcept override { ++Completed; }

    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
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
    const StampFixedPoint target = {})
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 1U,
        .Transform = {.TargetPivot = target}});
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
    auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() && prepared.Operation.PaletteChange &&
                prepared.Operation.Changes.size() == preview.Voxels.size(),
        "New colors must prepare one composite operation.");
    const auto revision = document.GetRevision();
    Require(history.Execute(session, std::move(prepared.Operation)) &&
                document.GetRevision() == revision + 1U &&
                session.Rebuilds == 1U && history.UndoCount() == 1U,
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
                history.RedoCount() == 1U,
        "Undo must remove only the placement.");
    Require(history.Redo(session) &&
                document.GetVoxelCount() == preview.Voxels.size() &&
                session.Rebuilds == 3U,
        "Redo must restore the placement exactly.");
}

void TestMultiplePlacementsAndOutOfBoundsFailure()
{
    auto document = Document();
    Session session(document);
    VoxelEditHistory history;
    const VoxelStamp stamp = Stamp();
    auto first = Prepare(stamp, document, {});
    Require(first.IsReady() &&
                history.Execute(session, std::move(first.Operation)),
        "First placement failed.");
    auto second = Prepare(stamp, document,
        {2 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    Require(second.IsReady() && !second.Operation.PaletteChange &&
                history.Execute(session, std::move(second.Operation)) &&
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
    const auto noChange =
        PreparePlaceVoxelStampOperation(Plan(stamp, document));
    Require(noChange.IsNoChange() &&
                document.GetRevision() == unchangedRevision &&
                history.UndoCount() == 1U,
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

    auto document = Document();
    const VoxelStamp stamp = Stamp();
    const StampPlacementPlan plan = Plan(stamp, document);
    auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() &&
                document.SetVoxel(plan.Voxels.front().WorldPosition, 1U).Succeeded,
        "Unable to create stale transaction fixture.");
    Session session(document);
    VoxelEditHistory history;
    const auto before = document.GetPaletteSnapshot();
    const auto revision = document.GetRevision();
    Require(!history.Execute(session, std::move(prepared.Operation)) &&
                document.GetPaletteSnapshot() == before &&
                document.GetRevision() == revision &&
                history.UndoCount() == 0U,
        "Stale transaction must fail without further mutation.");
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
    auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() &&
                prepared.Operation.Changes.size() == 4096U,
        "Large stamp must remain one composite operation.");

    Session session(document);
    VoxelEditHistory history;
    const auto revision = document.GetRevision();
    Require(history.Execute(session, std::move(prepared.Operation)) &&
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
        TestOverlapNoChangeAndSharedPaletteAcrossSubModels();
        TestPreviewClearDoesNotMutate();
        TestInvalidAndStalePlan();
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
