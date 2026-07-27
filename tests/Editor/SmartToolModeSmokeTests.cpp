#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartPreviewEngine.h"
#include "SmartTools/SmartToolController.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument Document()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = {{{8U, 8U, 8U}, {{1U, 1U, 1U, 4U}}}};
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-tool-mode-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document, "Unable to create smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Missing smoke source model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to create smoke grid.");
    source->ForEachVoxel([&grid](const Position position, const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to populate smoke grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override { return document_; }
    CommandResult RebuildActiveVoxelMesh() override { ++rebuilds; return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override { ++completed; }
    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    std::size_t rebuilds = 0U;
    std::size_t completed = 0U;
};

SmartToolRequest Request(Asset::Voxel::VoxelDocument& document,
    const SmartToolMode mode, const SmartAction action, const int size,
    const Position target, const std::size_t palette = 7U)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = *document.GetDimensions(0U);
    request.BrushRequest.State.Shape = SmartBrushShape::Sphere;
    request.BrushRequest.State.Dimension = SmartBrushDimension::Surface2D;
    request.BrushRequest.State.Size = size;
    request.BrushRequest.State.PaletteIndex = palette;
    request.BrushRequest.Placement = {target, {0, 0, 0}};
    request.ReadVoxel = [&document](const Position position)
    {
        const auto voxel = document.GetVoxel(position);
        return SmartToolVoxelState{voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
    };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = 1U;
    request.HasPaletteColors = true;
    request.PaletteColors[palette] = {0.18F, 0.72F, 0.42F, 1.0F};
    return request;
}

void PreviewCommit(SmartToolController& controller, SmartToolSession& session,
    SmartPreviewCache& cache, Session& editSession, VoxelEditHistory& history,
    const SmartToolRequest& request)
{
    const SmartToolResult previewed = controller.ResolvePreview(session, request);
    Require(previewed.HasPlan(), "Planner did not produce a SMART-04 plan.");
    const SmartPreviewData& preview = cache.Resolve(previewed.Plan);
    Require(preview.GhostVoxels.size() == previewed.Plan->Cells().size() &&
            preview.AffectedPositions == previewed.Plan->AffectedPositions(),
        "Preview does not display the exact SMART-04 plan.");
    const SmartToolResult committed = controller.ResolveCommit(session);
    Require(committed.Plan.get() == previewed.Plan.get(),
        "Commit attempted to replace the previewed SMART-04 plan.");
    const VoxelToolResult applied = VoxelPencilTool::Apply({committed.Plan,
        {&editSession, editSession.document_, 0U, 1U, &history, 0U, nullptr, nullptr}});
    Require(applied.Code == VoxelToolResultCode::Applied,
        "SMART-04 commit was not one atomic document operation.");
    for (const SmartToolPlanCell& cell : committed.Plan->Cells())
    {
        const auto voxel = editSession.document_->GetVoxel(cell.WorldPosition);
        Require(voxel.has_value() == cell.After.Exists &&
                (!voxel || voxel->PaletteIndex == cell.After.PaletteIndex),
            "Committed document state differs from the displayed Smart Tool plan.");
    }
}
}

int main()
{
    try
    {
        auto document = Document();
        Session editSession(document);
        VoxelEditHistory history;
        SmartToolController controller;
        SmartToolSession session;
        SmartPreviewCache cache;

        // Single -> Cube -> Paint -> Remove: one exact plan and one history
        // transaction for each click.
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::SingleVoxel, SmartAction::Add,
                9, {2, 1, 1}, 5U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::CubeBrush, SmartAction::Add,
                3, {4, 4, 4}, 6U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::SingleVoxel, SmartAction::Paint,
                3, {2, 1, 1}, 7U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::SingleVoxel, SmartAction::Erase,
                5, {2, 1, 1}, 7U));
        Require(history.UndoCount() == 4U && editSession.rebuilds == 4U,
            "SMART-04 did not preserve one transaction per click.");
        Require(history.Undo(editSession) && document.HasVoxel({2, 1, 1}) &&
                history.Redo(editSession) && !document.HasVoxel({2, 1, 1}),
            "SMART-04 Undo/Redo did not restore the exact remove transaction.");

        session.Clear();
        cache.Clear();
        Require(cache.Resolve(nullptr).GhostVoxels.empty() &&
                !controller.ResolveCommit(session).HasPlan(),
            "SMART-04 Clear did not remove the active preview and plan.");
        std::cout << "Smart Tool mode smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
