#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
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

Asset::Voxel::VoxelDocument CreateDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {16U, 4U, 4U}});
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-placement-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create the smoke-test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CreateCompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U; index < document.GetPalette().size(); ++index)
    {
        const auto color = document.GetPalette()[index];
        Require(model.Palette().Set(index,
                    {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize the compatibility palette.");
    }
    const Asset::Voxel::VoxelSubModel* const source = document.GetModel(0U);
    Require(source != nullptr, "Smoke-test document has no model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize the compatibility grid.");
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeSession final : public VoxelEditSession
{
public:
    explicit SmokeSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CreateCompatibilityModel(document))
    {
    }

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 1U;
    }

    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }

    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }

    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        const auto result = meshCache_.Synchronize(*document_, 1U);
        return result.Succeeded ? CommandResult::Success()
                                : CommandResult::Failure(result.Message);
    }

    void CompleteVoxelEdit() noexcept override
    {
        ++completeCount_;
    }

    [[nodiscard]] std::size_t RebuildCount() const noexcept { return rebuildCount_; }
    [[nodiscard]] std::size_t CompleteCount() const noexcept { return completeCount_; }

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache meshCache_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completeCount_ = 0U;
};

VoxelStamp CreateStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503133ULL}, "stamp-placement-smoke-v1"},
        {{0, 0, 0}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {0, 0, 0}},
        {},
        {{0U, {11U, 22U, 33U, 255U}}, {1U, {44U, 55U, 66U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(), "Unable to create the stamp fixture.");
    return *stamp;
}

VoxelPreviewData BuildPreview(const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document, const StampFixedPoint target)
{
    const VoxelPreviewData preview = StampLivePreviewBuilder::Build(
        {.Stamp = &stamp, .Document = &document, .TargetPivot = target});
    Require(preview.IsActive(), "Unable to build the live stamp preview.");
    return preview;
}

void RunStampPlacementSmokeScenario()
{
    Asset::Voxel::VoxelDocument document = CreateDocument();
    SmokeSession documentSession(document);
    VoxelEditHistory history;
    VoxelPreviewSession previewSession;
    const VoxelStamp stamp = CreateStamp();
    const std::uint64_t initialRevision = document.GetRevision();

    VoxelPreviewData preview = BuildPreview(stamp, document, {});
    Require(preview.State == VoxelPreviewState::Valid &&
            previewSession.Activate(preview) && previewSession.Current() != nullptr,
        "Opening the preview session must leave a valid active ghost.");

    PlaceVoxelStampPreparation first = PreparePlaceVoxelStampOperation(
        {.Stamp = &stamp, .Preview = previewSession.Current(), .Document = &document});
    Require(first.IsReady() && first.Operation.PaletteChange &&
            first.Operation.Changes.size() == preview.Voxels.size(),
        "The first placement must prepare one palette-aware composite operation.");
    Require(history.Execute(documentSession, std::move(first.Operation)) &&
            document.GetRevision() == initialRevision + 1U &&
            documentSession.RebuildCount() == 1U && documentSession.CompleteCount() == 1U &&
            history.UndoCount() == 1U && history.RedoCount() == 0U,
        "The first placement must commit exactly one revision, rebuild, and history entry.");
    for (const VoxelPreviewVoxel& voxel : preview.Voxels)
    {
        const auto committed = document.GetVoxel(voxel.Position);
        Require(committed && document.GetPalette()[committed->PaletteIndex] == voxel.Color,
            "The committed palette color must exactly equal the preview color.");
    }
    preview = BuildPreview(stamp, document, {});
    Require(preview.State == VoxelPreviewState::Overlap && previewSession.Activate(preview),
        "The first placement must retain an overlap preview for the next placement.");

    const StampFixedPoint secondTarget{
        4 * StampFixedPoint::UnitsPerVoxel, 0, 0};
    preview = BuildPreview(stamp, document, secondTarget);
    Require(preview.State == VoxelPreviewState::Valid && previewSession.Activate(preview),
        "Moving the preview must produce a second valid ghost without mutation.");
    PlaceVoxelStampPreparation second = PreparePlaceVoxelStampOperation(
        {.Stamp = &stamp, .Preview = previewSession.Current(), .Document = &document});
    Require(second.IsReady() && !second.Operation.PaletteChange &&
            history.Execute(documentSession, std::move(second.Operation)) &&
            document.GetRevision() == initialRevision + 2U &&
            documentSession.RebuildCount() == 2U && history.UndoCount() == 2U,
        "The moved placement must reuse palette entries as one second operation.");
    preview = BuildPreview(stamp, document, secondTarget);
    Require(preview.State == VoxelPreviewState::Overlap && previewSession.Activate(preview),
        "The second placement must keep its ghost preview active.");

    Require(history.Undo(documentSession) && history.UndoCount() == 1U &&
            history.RedoCount() == 1U && documentSession.RebuildCount() == 3U,
        "Undo must remove only the latest placement with one rebuild.");
    preview = BuildPreview(stamp, document, secondTarget);
    Require(preview.State == VoxelPreviewState::Valid && previewSession.Activate(preview),
        "Undo must refresh the moved preview to valid state.");

    Require(history.Redo(documentSession) && history.UndoCount() == 2U &&
            history.RedoCount() == 0U && documentSession.RebuildCount() == 4U,
        "Redo must restore the latest placement with one rebuild.");
    preview = BuildPreview(stamp, document, secondTarget);
    Require(preview.State == VoxelPreviewState::Overlap && previewSession.Activate(preview),
        "Redo must refresh the moved preview to overlap state.");

    const std::uint64_t revisionBeforeClear = document.GetRevision();
    Require(previewSession.Clear() && previewSession.Current() == nullptr &&
            document.GetRevision() == revisionBeforeClear,
        "Clearing the preview (Esc) must not mutate document or history.");
}
} // namespace

int main()
{
    try
    {
        RunStampPlacementSmokeScenario();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
