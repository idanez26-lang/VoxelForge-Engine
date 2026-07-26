#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const char* const message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {16U, 8U, 8U}});
    const auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "planning-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Planning smoke document must build.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibilityModel(
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
    Require(source != nullptr, "Planning smoke document has no model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize compatibility grid.");
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeSession final : public VoxelEditSession
{
public:
    explicit SmokeSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 14U;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds_;
        const auto result = meshCache_.Synchronize(*document_, 14U);
        return result.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(result.Message);
    }
    void CompleteVoxelEdit() noexcept override { ++completions_; }

    std::size_t Rebuilds() const noexcept { return rebuilds_; }
    std::size_t Completions() const noexcept { return completions_; }

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache meshCache_;
    std::size_t rebuilds_ = 0U;
    std::size_t completions_ = 0U;
};

VoxelStamp MakeStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503134ULL}, "stamp-14-smoke"},
        {{0, 0, 0}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}},
        {},
        {{0U, {10U, 20U, 30U, 255U}},
         {1U, {40U, 50U, 60U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Planning smoke Stamp must build.");
    return *stamp;
}

void RunScenario()
{
    auto document = MakeDocument();
    SmokeSession editSession(document);
    VoxelEditHistory history;
    StampPlacementSession placement;
    const VoxelStamp stamp = MakeStamp();

    const auto begun = placement.Begin(stamp, document, 14U, 0U, {});
    Require(begun.Succeeded && placement.CurrentPlan() != nullptr &&
                placement.CurrentPlan()->CanCommit &&
                placement.CurrentPreview() != nullptr &&
                placement.CurrentPreview()->IsActive(),
        "Document -> Session -> Planner -> Preview must succeed.");

    // Force staleness and model the Workspace rule: rebuild, do not commit.
    Require(document.SetVoxel({8, 0, 0}, 1U).Succeeded,
        "Unable to mutate stale smoke fixture.");
    const std::size_t undoBeforeStaleClick = history.UndoCount();
    Require(!placement.IsCurrent(document, 14U),
        "External edit must stale the current plan.");
    const auto refreshed = placement.Rebuild(document, 14U);
    Require(refreshed.Succeeded && placement.IsCurrent(document, 14U) &&
                history.UndoCount() == undoBeforeStaleClick,
        "Stale click must refresh without placement.");

    auto first =
        PreparePlaceVoxelStampOperation(*placement.CurrentPlan());
    Require(first.IsReady() &&
                history.Execute(editSession, std::move(first.Operation)),
        "Second click must commit the refreshed plan.");
    placement.MarkPlacementCommitted();
    Require(placement.PlacementOrdinal() == 1U &&
                editSession.Rebuilds() == 1U &&
                editSession.Completions() == 1U &&
                history.UndoCount() == 1U,
        "One placement must create one transaction and rebuild.");

    Require(placement.Rebuild(document, 14U).Succeeded &&
                placement.CurrentPreview() != nullptr &&
                placement.CurrentPreview()->State ==
                    VoxelPreviewState::Overlap,
        "Preview must persist and refresh after placement.");
    Require(history.Undo(editSession) &&
                placement.Rebuild(document, 14U).Succeeded &&
                placement.CurrentPreview()->State ==
                    VoxelPreviewState::Valid,
        "Undo must restore a valid persistent preview.");
    Require(history.Redo(editSession) &&
                placement.Rebuild(document, 14U).Succeeded &&
                placement.CurrentPreview()->State ==
                    VoxelPreviewState::Overlap,
        "Redo must restore overlap and persistent preview.");
    Require(placement.Cancel() && !placement.IsActive() &&
                placement.CurrentPlan() == nullptr &&
                placement.CurrentPreview() == nullptr,
        "Clear must release session resources without document mutation.");
}

} // namespace

int main()
{
    try
    {
        RunScenario();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
