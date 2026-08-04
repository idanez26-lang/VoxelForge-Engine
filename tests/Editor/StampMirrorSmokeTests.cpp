#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
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
    source.Models.push_back({.Dimensions = {12U, 6U, 12U}});
    const auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "mirror-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Mirror smoke document must build.");
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
            "Unable to initialize mirror smoke palette.");
    }
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Mirror smoke document has no model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize mirror smoke grid.");
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
        return 17U;
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
        const auto synchronized = meshCache_.Synchronize(*document_, 17U);
        return synchronized.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(synchronized.Message);
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
    constexpr std::int32_t unit = StampFixedPoint::UnitsPerVoxel;
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503137ULL}, "stamp-17-smoke"},
        {{0, 0, 0}, {2, 0, 1}, {3U, 1U, 2U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {unit, 0, unit}},
        {},
        {{0U, {10U, 20U, 30U, 255U}},
         {1U, {40U, 50U, 60U, 255U}}},
        {{{0, 0, 0}, 0U}, {{2, 0, 0}, 1U}, {{0, 0, 1}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Mirror smoke Stamp must build.");
    return *stamp;
}

void RunScenario()
{
    auto document = MakeDocument();
    SmokeSession editSession(document);
    VoxelEditHistory history;
    StampPlacementSession placement;

    Require(placement.Begin(
                MakeStamp(), document, 17U, 0U,
                {5 * StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel,
                 5 * StampFixedPoint::UnitsPerVoxel}).Succeeded &&
                placement.CurrentPreview() != nullptr,
        "Preview must begin.");
    Require(placement.SetMirror(
                StampPlacementMirrorMode::X, document, 17U).Succeeded &&
                placement.CycleMirror(document, 17U).Succeeded &&
                placement.CycleMirror(document, 17U).Succeeded &&
                placement.Mirror() == StampPlacementMirrorMode::XZ &&
                placement.RotateClockwise(document, 17U).Succeeded &&
                placement.QuarterRotation() == 1U &&
                placement.CurrentPreview() != nullptr &&
                placement.CurrentPreview()->Transform.MirrorMode ==
                    static_cast<std::uint8_t>(
                        StampPlacementMirrorMode::XZ) &&
                placement.CurrentPreview()->Transform.QuarterTurns == 1U,
        "Mirror and rotation must update the persistent shared preview.");

    const auto plannedVoxels = placement.CurrentPlan()->Voxels;
    const auto placed = placement.PlaceOnce(
        document, 17U, editSession, history);
    Require(static_cast<bool>(placed),
        "Mirrored placement must commit atomically.");
    Require(placement.PlacementOrdinal() == 1U &&
                history.UndoCount() == 1U && editSession.Rebuilds() == 1U &&
                editSession.Completions() == 1U,
        "One click must remain one transaction and one mesh rebuild.");
    for (const StampPlannedVoxel& voxel : plannedVoxels)
    {
        Require(document.GetVoxel(voxel.WorldPosition).has_value(),
            "Placed document cells must exactly equal the mirrored plan.");
    }

    Require(static_cast<bool>(history.Undo(editSession)),
        "Mirrored placement Undo must succeed.");
    for (const StampPlannedVoxel& voxel : plannedVoxels)
    {
        Require(!document.GetVoxel(voxel.WorldPosition).has_value(),
            "Undo must remove the exact mirrored placement.");
    }
    Require(static_cast<bool>(history.Redo(editSession)),
        "Mirrored placement Redo must succeed.");
    for (const StampPlannedVoxel& voxel : plannedVoxels)
    {
        Require(document.GetVoxel(voxel.WorldPosition).has_value(),
            "Redo must restore the exact mirrored placement.");
    }
    Require(placement.Rebuild(document, 17U).Succeeded &&
                placement.CurrentPreview() != nullptr &&
                placement.CurrentPreview()->State ==
                    VoxelPreviewState::Overlap,
        "Preview must persist after mirrored placement and Redo.");
    Require(placement.Cancel() && !placement.IsActive(),
        "Escape/Clear must end mirror placement without mutation.");
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
