#include "Preview/VoxelPreview.h"
#include "Selection/SelectionService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

VoxelStamp Stamp()
{
    StampValidationResult error{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{909U}, "0123456789abcdef"}, {{}, {0, 0, 0}, {1U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center, .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}}, {}, {{0U, {64U, 128U, 255U, 255U}}},
        {{{0, 0, 0}, 0U}}, DefaultStampResourceLimits(), &error);
    Require(stamp && error.IsValid(), "Stamp smoke fixture invalid.");
    return *stamp;
}

Asset::Voxel::VoxelDocument Document()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({.Dimensions = {8U, 8U, 8U},
        .Voxels = {{.X = 1U, .Y = 0U, .Z = 0U, .ColorIndex = 1U}}});
    const auto document = Asset::Voxel::VoxDocumentLoader{}.Build(source, "live-preview-smoke.vox");
    Require(document.Succeeded() && document.Document, "Document smoke fixture invalid.");
    return std::move(*document.Document);
}

} // namespace

int main()
{
    try
    {
        const VoxelStamp stamp = Stamp();
        Asset::Voxel::VoxelDocument document = Document();
        SelectionService selection;
        selection.SetDocumentGeneration(9U);
        Require(selection.Select({1, 0, 0}), "Smoke selection fixture invalid.");
        const auto selectionBefore = std::vector<Asset::Voxel::VoxelPosition>(
            selection.Voxels().begin(), selection.Voxels().end());
        const std::uint64_t documentRevision = document.GetRevision();
        const std::uint64_t documentVoxelCount = document.GetVoxelCount();
        VoxelEditHistory history;
        VoxelPreviewSession session;

        // Simulate several visible frames and target movement without document edits.
        for (int x = 0; x != 4; ++x)
        {
            const auto preview = StampLivePreviewBuilder::Build({
                .Stamp = &stamp, .Document = &document,
                .TargetPivot = {x * StampFixedPoint::UnitsPerVoxel, 0, 0}});
            Require(preview.IsActive(), "Every live preview frame must remain active.");
            static_cast<void>(session.Activate(preview));
        }
        Require(session.Current() && session.Current()->State == VoxelPreviewState::Valid,
            "Moved preview must remain valid before its overlap frame.");

        const auto overlap = StampLivePreviewBuilder::Build({
            .Stamp = &stamp, .Document = &document,
            .TargetPivot = {StampFixedPoint::UnitsPerVoxel, 0, 0}});
        Require(overlap.IsActive() && overlap.State == VoxelPreviewState::Overlap &&
                    overlap.Voxels.front().OverlapsExisting,
            "The overlap frame must be visible and non-blocking.");
        static_cast<void>(session.Activate(overlap));

        Require(document.GetRevision() == documentRevision &&
                    document.GetVoxelCount() == documentVoxelCount &&
                    std::vector<Asset::Voxel::VoxelPosition>(selection.Voxels().begin(), selection.Voxels().end()) == selectionBefore &&
                    history.UndoCount() == 0U && history.RedoCount() == 0U,
            "Live frames must not mutate document, revision, selection or Undo/Redo history.");
        Require(session.Clear() && session.Current() == nullptr,
            "Clearing the live preview must release its final ghost frame.");
        Require(document.GetRevision() == documentRevision && history.UndoCount() == 0U,
            "Clear must not create an edit operation or mutate the document.");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
