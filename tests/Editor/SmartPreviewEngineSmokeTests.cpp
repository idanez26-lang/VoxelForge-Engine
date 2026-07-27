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
#include <string>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;

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
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "smart-preview-engine-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to build the Smart Preview smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Smoke document has no model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to create the smoke compatibility grid.");
    source->ForEachVoxel([&grid](const Asset::Voxel::VoxelPosition position,
                             const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialise the smoke compatibility grid.");
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
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completed; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuilds = 0U;
    std::size_t completed = 0U;
};

SmartToolRequest Request(Asset::Voxel::VoxelDocument& document)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = SmartAction::Add;
    request.BrushRequest.Dimensions = {8U, 8U, 8U};
    request.BrushRequest.State.Shape = SmartBrushShape::Cube;
    request.BrushRequest.State.Size = 1;
    request.BrushRequest.State.PaletteIndex = 5U;
    request.BrushRequest.Placement = {{2, 1, 1}, {1, 0, 0}};
    request.ReadVoxel = [&document](const Asset::Voxel::VoxelPosition position)
    {
        const auto voxel = document.GetVoxel(position);
        return SmartToolVoxelState{voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
    };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = 1U;
    request.HasPaletteColors = true;
    request.PaletteColors[5U] = {0.16F, 0.64F, 0.92F, 1.0F};
    return request;
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

        const SmartToolResult planned = controller.ResolvePreview(session, Request(document));
        Require(planned.HasPlan(), "Smoke planner did not create a plan.");
        SmartPreviewCache cache;
        const SmartPreviewData& preview = cache.Resolve(planned.Plan);
        Require(preview.CanCommit() && preview.GhostVoxels.size() == 1U &&
                preview.AffectedPositions == std::vector<Asset::Voxel::VoxelPosition>{{2, 1, 1}},
            "Smoke preview diverged from the immutable placement plan.");

        const SmartToolResult commitPlan = controller.ResolveCommit(session);
        Require(commitPlan.Plan.get() == planned.Plan.get(),
            "Smoke commit did not consume the preview plan instance.");
        const VoxelToolResult applied = VoxelPencilTool::Apply({commitPlan.Plan,
            {&editSession, &document, 0U, 1U, &history, 0U, nullptr, nullptr}});
        Require(applied.Code == VoxelToolResultCode::Applied &&
                document.GetVoxel({2, 1, 1})->PaletteIndex == 5U &&
                history.UndoCount() == 1U && editSession.rebuilds == 1U,
            "Smoke commit did not apply the exact previewed plan atomically.");
        for (const SmartToolPlanCell& cell : commitPlan.Plan->Cells())
        {
            const auto voxel = document.GetVoxel(cell.WorldPosition);
            Require(voxel.has_value() == cell.After.Exists &&
                    (!voxel || voxel->PaletteIndex == cell.After.PaletteIndex),
                "Smoke committed a cell that differs from the preview plan.");
        }
        Require(history.Undo(editSession) && !document.HasVoxel({2, 1, 1}) &&
                history.Redo(editSession) && document.GetVoxel({2, 1, 1})->PaletteIndex == 5U &&
                editSession.rebuilds == 3U,
            "Smoke Undo/Redo did not restore the committed Smart Preview plan.");

        session.Clear();
        cache.Clear();
        Require(cache.Resolve(nullptr).GhostVoxels.empty() &&
                !controller.ResolveCommit(session).HasPlan(),
            "Smoke Clear did not remove the preview and commit plan.");
        std::cout << "Smart Preview Engine smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
