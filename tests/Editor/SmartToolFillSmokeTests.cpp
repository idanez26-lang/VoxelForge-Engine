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

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const char* const message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{12U, 12U, 12U}, {
        {2U, 3U, 2U, 2U}, {3U, 3U, 2U, 2U},
        {2U, 3U, 3U, 2U}, {3U, 3U, 3U, 2U}}});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "fill-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Fill smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Fill smoke document has no sub-model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to allocate Fill smoke grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed Fill smoke grid.");
    });
    Voxel::VoxelModel model;
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
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override {}

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

SmartToolRequest Request(const SmartToolStroke& stroke,
    const SmartFillMode mode, const SmartAction action,
    const std::uint8_t palette)
{
    const Position seed{2, 3, 2};
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Fill;
    request.FillMode = mode;
    request.Action = action;
    request.BrushRequest = {{12U, 12U, 12U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, 1, palette, SmartBrushMode::Paint},
        {seed, {0, 1, 0}}, {}};
    if (mode == SmartFillMode::Plane)
        request.FaceSeed = SmartToolFaceSeed{seed, {0, 1, 0}};
    request.ReadVoxel = [&stroke](const Position position)
    {
        return stroke.Context().ReadSourceVoxel(position);
    };
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    return request;
}

void Execute(Asset::Voxel::VoxelDocument& document, Session& session,
    VoxelEditHistory& history, const SmartFillMode mode,
    const SmartAction action, const std::uint8_t palette)
{
    const Position seed{2, 3, 2};
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
                document.GetRevision(), 1U, 0U,
                [&document](const Position position)
                {
                    const auto voxel = document.GetVoxel(position);
                    return SmartToolVoxelState{voxel.has_value(),
                        voxel ? voxel->PaletteIndex : 0U};
                }}, action, seed, {0, 1, 0}),
        "Unable to begin atomic Fill click.");
    SmartToolController controller;
    SmartToolSession planning;
    const SmartToolResult preview = controller.ResolvePreview(
        planning, Request(stroke, mode, action, palette));
    Require(preview.HasPlan() &&
            controller.ResolveCommit(planning).Plan == preview.Plan &&
            stroke.ReplaceWithPlan(*preview.Plan),
        "Fill Preview/Commit plan diverged.");

    const Asset::Voxel::VoxelDocument before = document;
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U,
                &history, std::nullopt, nullptr, nullptr},
            action, seed, stroke.Changes()).Code ==
            VoxelToolResultCode::Applied,
        "Fill atomic click was not applied.");
    const SmartToolExactPreviewMesh exact =
        SmartToolExactPreviewComposer::Compose(before, stroke.Changes());
    const auto committed = Mesh::VoxelMeshBuilder::Build(document);
    Require(exact.Succeeded() && committed.Succeeded &&
            exact.Mesh.Vertices() == committed.Mesh->Vertices() &&
            exact.Mesh.Indices() == committed.Mesh->Indices(),
        "Fill exact preview mesh differs from commit mesh.");
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument();
    Session session(document);
    VoxelEditHistory history;

    Execute(document, session, history, SmartFillMode::Connected,
        SmartAction::Add, 9U);
    Require(history.UndoCount() == 1U &&
            history.Undo(session) && history.Redo(session),
        "Connected Fill Undo/Redo failed.");
    Execute(document, session, history, SmartFillMode::Plane,
        SmartAction::Add, 7U);
    Require(document.GetVoxel({2, 4, 2}).has_value() &&
            document.GetVoxel({3, 4, 3}).has_value(),
        "Plane Fill Create used the wrong adjacent layer.");
}
}

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool Fill smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
