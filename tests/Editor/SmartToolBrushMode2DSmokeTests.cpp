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

void Require(const bool condition, const char* const message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument Document()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = {{{16U, 16U, 16U}, {{5U, 5U, 5U, 4U}}}};
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-tool-brush-mode-2d-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create the 2D Brush Mode smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto* const source = document.GetModel(0U);
    Require(source != nullptr, "The 2D Brush Mode smoke document has no grid.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the 2D Brush Mode smoke grid.");
    source->ForEachVoxel([&grid](const Position position, const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed the 2D Brush Mode smoke grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    { return 1U; }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    { return &model_; }
    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    { return document_; }
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    { ++rebuilds; return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override { ++completed; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuilds = 0U;
    std::size_t completed = 0U;
};

SmartToolRequest Request(Asset::Voxel::VoxelDocument& document,
    const SmartToolMode mode, const SmartAction action, const int size,
    const Position target, const std::size_t palette)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = *document.GetDimensions(0U);
    request.BrushRequest.State.Shape = SmartBrushShape::Cube;
    request.BrushRequest.State.Dimension = SmartBrushDimension::Surface2D;
    // Fixed axes are deliberately resolved to the current face/workplane by
    // the Pencil V1 planner.
    request.BrushRequest.State.Orientation = SmartBrushOrientation::Z;
    request.BrushRequest.State.Size = size;
    request.BrushRequest.State.PaletteIndex = palette;
    request.BrushRequest.Placement = {target, {0, 1, 0}};
    request.ReadVoxel = [&document](const Position position)
    {
        const auto voxel = document.GetVoxel(position);
        return SmartToolVoxelState{voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
    };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = 1U;
    request.HasPaletteColors = true;
    request.PaletteColors[palette] = {0.23F, 0.69F, 0.94F, 1.0F};
    return request;
}

void PreviewCommit(SmartToolController& controller, SmartToolSession& session,
    SmartPreviewCache& cache, Session& editSession, VoxelEditHistory& history,
    const SmartToolRequest& request)
{
    const SmartToolResult previewed = controller.ResolvePreview(session, request);
    Require(previewed.HasPlan() &&
            previewed.Plan->BrushState().Dimension == SmartBrushDimension::Surface2D &&
            previewed.Plan->BrushState().Orientation == SmartBrushOrientation::Auto,
        "The 2D Brush Mode preview was not planner-resolved from the face.");
    for (const SmartToolPlanCell& cell : previewed.Plan->Cells())
        Require(cell.WorldPosition.Y == request.BrushRequest.Placement.Target.Y,
            "The 2D Brush Mode preview left the resolved placement plane.");
    const SmartPreviewData& preview = cache.Resolve(previewed.Plan);
    const SmartToolResult committed = controller.ResolveCommit(session);
    Require(preview.GhostVoxels.size() == previewed.Plan->Cells().size() &&
            committed.Plan == previewed.Plan,
        "The 2D Brush Mode commit did not consume its rendered plan.");
    const VoxelToolResult applied = VoxelPencilTool::Apply({committed.Plan,
        {&editSession, editSession.document_, 0U, 1U, &history, 0U, nullptr, nullptr}});
    Require(applied.Code == VoxelToolResultCode::Applied,
        "A 2D Brush Mode click was not one atomic document operation.");
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

        // Cube -> Sphere -> Cylinder -> Paint -> Remove uses one face-resolved
        // 2D plan per click, then Undo/Redo and Esc clear only the session.
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::CubeBrush, SmartAction::Add,
                3, {4, 4, 4}, 5U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::SphereBrush, SmartAction::Add,
                3, {8, 4, 8}, 6U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::CylinderBrush, SmartAction::Add,
                3, {11, 4, 11}, 7U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::CubeBrush, SmartAction::Paint,
                3, {4, 4, 4}, 8U));
        PreviewCommit(controller, session, cache, editSession, history,
            Request(document, SmartToolMode::CubeBrush, SmartAction::Erase,
                3, {4, 4, 4}, 8U));
        Require(history.UndoCount() == 5U && editSession.rebuilds == 5U &&
                history.Undo(editSession) && document.HasVoxel({4, 4, 4}) &&
                history.Redo(editSession) && !document.HasVoxel({4, 4, 4}),
            "The 2D Brush Mode smoke did not preserve Undo/Redo for Remove.");

        // Esc: end only the transient preview/session, not document history.
        session.Clear();
        cache.Clear();
        Require(cache.Resolve(nullptr).GhostVoxels.empty() &&
                !controller.ResolveCommit(session).HasPlan(),
            "Esc did not clear the active 2D Brush Mode preview session.");
        std::cout << "Smart Tool 2D Brush Mode smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
