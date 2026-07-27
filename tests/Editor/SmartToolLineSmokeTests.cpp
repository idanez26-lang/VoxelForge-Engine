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

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;
void Require(const bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U; source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{12U, 12U, 12U}, {{2U, 3U, 2U, 2U}, {3U, 3U, 2U, 2U},
        {4U, 3U, 2U, 2U}, {5U, 3U, 2U, 2U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, "smart-line-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Smart Line smoke document.");
    return std::move(*loaded.Document);
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document) : document_(&document)
    {
        Voxel::VoxelGrid grid; Require(grid.Resize(12U, 12U, 12U), "Grid allocation failed.");
        model_.AddGrid(std::move(grid));
    }
    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override { return document_; }
    CommandResult RebuildActiveVoxelMesh() override { ++rebuilds; return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override { ++completions; }
    std::size_t rebuilds = 0U, completions = 0U;
private: Asset::Voxel::VoxelDocument* document_; Voxel::VoxelModel model_;
};

SmartToolRequest Request(const SmartToolStroke& stroke, SmartAction action,
    Position a, Position b, std::uint8_t palette)
{
    SmartToolRequest request; request.Geometry = SmartGeometry::Line;
    request.Mode = SmartToolMode::SingleVoxel; request.Action = action;
    request.LineStart = a; request.BrushRequest = {{12U, 12U, 12U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, 1, palette, SmartBrushMode::Add}, {b, {0, 1, 0}}, {}};
    request.ReadVoxel = [&stroke](Position position) { return stroke.Context().ReadSourceVoxel(position); };
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    request.SourceSubModelIndex = stroke.Context().SubModelIndex;
    return request;
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument(); Session session(document);
    VoxelEditHistory history; const Position a{2, 4, 2}, b{6, 4, 2};
    const auto begin = [&](SmartToolStroke& stroke, SmartAction action)
    {
        return stroke.Begin({reinterpret_cast<std::uintptr_t>(&document), document.GetRevision(),
            1U, 0U, [&document](Position p) { const auto v = document.GetVoxel(p);
                return SmartToolVoxelState{v.has_value(), v ? v->PaletteIndex : 0U}; }}, action, a, {0, 1, 0});
    };
    const auto plan = [&](SmartToolStroke& stroke, SmartAction action, std::uint8_t color)
    {
        SmartToolController controller; SmartToolSession planning;
        const SmartToolResult preview = controller.ResolvePreview(planning, Request(stroke, action, a, b, color));
        Require(preview.HasPlan() && controller.ResolveCommit(planning).Plan == preview.Plan,
            "Line preview and commit plan differ.");
        Require(stroke.ReplaceWithPlan(*preview.Plan) && stroke.PreviewChanges(*preview.Plan) == stroke.Changes(),
            "Line replacement preview diverges from commit changes.");
    };
    SmartToolStroke add; Require(begin(add, SmartAction::Add), "Line Add stroke failed."); plan(add, SmartAction::Add, 7U);
    const auto before = document;
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history, std::nullopt,
        nullptr, nullptr}, SmartAction::Add, b, add.Changes()).Code == VoxelToolResultCode::Applied &&
        document.GetVoxelCount() == 9U && history.UndoCount() == 1U, "Line Add was not one transaction.");
    const auto preview = SmartToolExactPreviewComposer::Compose(before, add.Changes());
    const auto mesh = Mesh::VoxelMeshBuilder::Build(document);
    Require(preview.Succeeded() && mesh.Succeeded &&
        preview.Mesh.Vertices() == mesh.Mesh->Vertices() &&
        preview.Mesh.Indices() == mesh.Mesh->Indices(),
        "Line exact preview differs from commit mesh.");
    Require(history.Undo(session) && history.Redo(session), "Line Undo/Redo failed.");
    SmartToolStroke paint; Require(begin(paint, SmartAction::Paint), "Line Paint stroke failed."); plan(paint, SmartAction::Paint, 10U);
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history, std::nullopt, nullptr, nullptr}, SmartAction::Paint, b, paint.Changes()).Code == VoxelToolResultCode::Applied, "Line Paint failed.");
    SmartToolStroke erase; Require(begin(erase, SmartAction::Erase), "Line Remove stroke failed."); plan(erase, SmartAction::Erase, 0U);
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history, std::nullopt, nullptr, nullptr}, SmartAction::Erase, b, erase.Changes()).Code == VoxelToolResultCode::Applied, "Line Remove failed.");
    const auto revision = document.GetRevision(); SmartToolStroke cancelled; Require(begin(cancelled, SmartAction::Add), "Line cancel stroke failed."); plan(cancelled, SmartAction::Add, 8U); cancelled.Cancel();
    Require(!cancelled.HasChanges() && document.GetRevision() == revision &&
        history.UndoCount() == 3U,
        "An invalid Line endpoint before release must not mutate document or history.");
}
}
int main() { try { RunSmoke(); std::cout << "Smart Tool Line smoke passed.\n"; return 0; } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
