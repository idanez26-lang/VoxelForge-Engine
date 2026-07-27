#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolStroke.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    for (std::uint8_t z = 2U; z <= 5U; ++z)
        for (std::uint8_t x = 2U; x <= 5U; ++x)
            voxels.push_back({x, 3U, z, 2U});
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{12U, 12U, 12U}, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-tool-face-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Face smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Face smoke document has no sub-model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to allocate Face smoke compatibility grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed Face smoke compatibility grid.");
    });
    Voxel::VoxelModel model;
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeSession final : public VoxelEditSession
{
public:
    explicit SmokeSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}
    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    { return 1U; }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    { return &model_; }
    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    { return document_; }
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    { ++rebuilds_; return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override { ++completions_; }

    std::size_t rebuilds_ = 0U;
    std::size_t completions_ = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

SmartToolRequest Request(const SmartToolStroke& stroke, const SmartAction action,
    const Position target, const Position normal, const int size,
    const std::uint8_t palette, const int depth = 1)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Face;
    request.Action = action;
    request.BrushRequest.Dimensions = {12U, 12U, 12U};
    request.BrushRequest.State = {SmartBrushShape::Cube,
        SmartBrushDimension::Surface2D, SmartBrushOrientation::Auto,
        size, palette, SmartBrushMode::Add};
    request.BrushRequest.Placement = {target, normal};
    request.FaceSeed = {action == SmartAction::Add
            ? Position{target.X - normal.X, target.Y - normal.Y,
                target.Z - normal.Z}
            : target, normal};
    request.FaceDepth = depth;
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.VirtualRevision = stroke.Revision();
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    request.SourceSubModelIndex = stroke.Context().SubModelIndex;
    // Face depth is a replacement plan, not a sampled stroke union. Both
    // planning passes therefore read the same immutable document snapshot.
    request.ReadVoxel = [&stroke](const Position position)
    { return stroke.Context().ReadSourceVoxel(position); };
    request.ReadFaceSupportVoxel = [&stroke](const Position position)
    { return stroke.Context().ReadSourceVoxel(position); };
    return request;
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument();
    SmokeSession editSession(document);
    VoxelEditHistory history;
    const Position normal{0, 1, 0};
    const Position firstTarget{3, 4, 3};
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), editSession.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, SmartAction::Add, firstTarget, normal),
        "Unable to begin Face stroke.");

    SmartToolController controller;
    SmartToolSession planningSession;
    auto planAndReplace = [&](const Position target, const int depth)
    {
        const SmartToolResult preview = controller.ResolvePreview(planningSession,
            Request(stroke, SmartAction::Add, target, normal, 2, 7U, depth));
        if (!preview.HasPlan() || preview.Code != SmartBrushResultCode::Valid)
            throw std::runtime_error("Face smoke preview did not produce a valid plan: " +
                preview.Error);
        const SmartToolResult commit = controller.ResolveCommit(planningSession);
        Require(commit.Plan != nullptr && commit.Plan->PlanId() == preview.Plan->PlanId(),
            "Face commit did not retain the rendered immutable plan.");
        Require(stroke.ReplaceWithPlan(*preview.Plan) &&
                stroke.PreviewChanges(*preview.Plan) == stroke.Changes(),
            "Face preview changes diverged from the replacement commit plan.");
    };
    planAndReplace(firstTarget, 5);
    // Returning toward the press point replaces (rather than unions with) the
    // prior depth. MouseUp must therefore commit exactly two layers.
    planAndReplace(firstTarget, 2);
    const std::vector<Asset::Voxel::VoxelDocumentChange> changes = stroke.Changes();
    Require(changes.size() == 32U && document.GetVoxelCount() == 16U,
        "Face depth shrink retained stale layers from the earlier larger preview.");
    for (const Asset::Voxel::VoxelDocumentChange& change : changes)
        Require(change.Position.Y == 4 || change.Position.Y == 5,
            "Face depth shrink committed a layer outside the final two-layer plan.");

    const Asset::Voxel::VoxelDocument beforeCommit = document;
    const VoxelToolResult applied = VoxelPencilTool::ApplyChanges(
        {&editSession, &document, 0U, editSession.VoxelModelGeneration(), &history,
            std::nullopt, nullptr, nullptr}, SmartAction::Add, {4, 4, 3}, changes);
    Require(applied.Code == VoxelToolResultCode::Applied && history.UndoCount() == 1U &&
            document.GetVoxelCount() == 48U && editSession.rebuilds_ == 1U,
        "Face stroke was not committed as one atomic edit transaction.");
    const SmartToolExactPreviewMesh exact = SmartToolExactPreviewComposer::Compose(
        beforeCommit, changes);
    const Mesh::MeshBuildResult committed = Mesh::VoxelMeshBuilder::Build(document);
    Require(exact.Succeeded() && committed.Succeeded && committed.Mesh &&
            exact.Mesh.Vertices() == committed.Mesh->Vertices() &&
            exact.Mesh.Indices() == committed.Mesh->Indices(),
        "Face exact preview mesh did not match the committed document mesh.");
    Require(history.Undo(editSession) && document.GetVoxelCount() == 16U &&
            history.Redo(editSession) && document.GetVoxelCount() == 48U,
        "Face atomic transaction Undo/Redo did not restore the exact result.");

    const auto commitSingleFace = [&](const SmartAction action,
        const Position seed, const std::uint8_t palette)
    {
        SmartToolStroke single;
        Require(single.Begin({reinterpret_cast<std::uintptr_t>(&document),
                document.GetRevision(), editSession.VoxelModelGeneration(), 0U,
                [&document](const Position position)
                {
                    const auto voxel = document.GetVoxel(position);
                    return SmartToolVoxelState{voxel.has_value(),
                        voxel ? voxel->PaletteIndex : 0U};
                }}, action, seed, normal), "Unable to begin single Face action.");
        SmartToolController actionController;
        SmartToolSession actionSession;
        const SmartToolResult plan = actionController.ResolvePreview(actionSession,
            Request(single, action, seed, normal, 1, palette));
        Require(plan.HasPlan() && single.Accumulate(*plan.Plan),
            "Face action did not use the planner-owned plan.");
        const VoxelToolResult result = VoxelPencilTool::ApplyChanges(
            {&editSession, &document, 0U, editSession.VoxelModelGeneration(), &history,
                std::nullopt, nullptr, nullptr}, action, seed, single.Changes());
        Require(result.Code == VoxelToolResultCode::Applied,
            "Face action was not committed through VoxelEditHistory.");
    };
    const std::uint64_t countBeforePaint = document.GetVoxelCount();
    commitSingleFace(SmartAction::Paint, {3, 5, 3}, 10U);
    Require(document.GetVoxelCount() == countBeforePaint && history.UndoCount() == 2U,
        "Face Paint did not preserve occupancy or atomic history.");
    commitSingleFace(SmartAction::Erase, {4, 5, 3}, 0U);
    Require(document.GetVoxelCount() + 16U == countBeforePaint && history.UndoCount() == 3U,
        "Face Remove did not remove the full visible surface atomically.");

    const std::uint64_t revisionBeforeCancel = document.GetRevision();
    const std::size_t undoBeforeCancel = history.UndoCount();
    SmartToolStroke pending;
    Require(pending.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), editSession.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
                }}, SmartAction::Paint, {3, 4, 3}, normal),
        "Unable to begin cancellable Face action.");
    SmartToolController cancelController;
    SmartToolSession cancelSession;
    const SmartToolResult pendingPlan = cancelController.ResolvePreview(cancelSession,
        Request(pending, SmartAction::Paint, {3, 4, 3}, normal, 1, 11U));
    Require(pendingPlan.HasPlan() && pending.Accumulate(*pendingPlan.Plan),
        "Unable to prepare cancellable Face plan.");
    pending.Cancel();
    cancelSession.Clear();
    Require(!pending.IsActive() && !pending.HasChanges() &&
            document.GetRevision() == revisionBeforeCancel &&
            history.UndoCount() == undoBeforeCancel,
        "Face Esc/Clear cancellation mutated document state or history.");
}
}

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool Face smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
