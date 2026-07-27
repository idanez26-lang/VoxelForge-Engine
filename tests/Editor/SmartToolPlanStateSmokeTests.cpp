#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartBrushPreviewResolver.h"
#include "SmartTools/SmartToolController.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
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

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = {{{8U, 8U, 8U}, {{1U, 1U, 1U, 4U}}}};
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "smart-tool-plan-state-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to build Smart Tool smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Smart Tool smoke document has no model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Smart Tool smoke grid.");
    source->ForEachVoxel([&grid](const Asset::Voxel::VoxelPosition position,
                             const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Smart Tool smoke grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds_;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedEdits_; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;
};

SmartToolRequest MakeRequest(Asset::Voxel::VoxelDocument& document)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = SmartAction::Add;
    request.BrushRequest.Dimensions = {8U, 8U, 8U};
    request.BrushRequest.State.Shape = SmartBrushShape::Cube;
    request.BrushRequest.State.Size = 1;
    request.BrushRequest.State.PaletteIndex = 9U;
    request.BrushRequest.Placement = {{2, 1, 1}, {1, 0, 0}};
    request.ReadVoxel = [&document](const Asset::Voxel::VoxelPosition position)
    {
        const auto voxel = document.GetVoxel(position);
        return SmartToolVoxelState{
            voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
    };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = 1U;
    request.SourceSubModelIndex = 0U;
    return request;
}

void TestCompleteSmoke()
{
    auto document = MakeDocument();
    Session editSession(document);
    VoxelEditHistory history;
    SmartToolController controller;
    SmartToolSession session;

    const SmartToolResult previewResult = controller.ResolvePreview(
        session, MakeRequest(document));
    Require(previewResult.HasPlan() && previewResult.Plan->HasChanges(),
        "Smoke preview did not create an actionable immutable plan.");
    const SmartBrushPreviewResult preview = SmartBrushPreviewResolver::Resolve(
        *previewResult.Plan, {0.9F, 0.3F, 0.1F, 1.0F});
    Require(preview.IsAvailable() && preview.AffectedPositions.size() == 1U &&
            preview.AffectedPositions.front() == Asset::Voxel::VoxelPosition{2, 1, 1},
        "Smoke preview does not describe the planned document change.");

    const SmartToolResult commitPlan = controller.ResolveCommit(session);
    Require(commitPlan.Plan.get() == previewResult.Plan.get(),
        "Smoke commit path did not consume the preview plan.");
    const auto revision = document.GetRevision();
    const VoxelToolResult applied = VoxelPencilTool::Apply({commitPlan.Plan,
        // Execution palette deliberately differs: the immutable plan owns the
        // final palette and the click must not reinterpret current UI state.
        {&editSession, &document, 0U, 1U, &history, 0U, nullptr, nullptr}});
    Require(applied.Code == VoxelToolResultCode::Applied &&
            document.GetRevision() == revision + 1U &&
            document.GetVoxel({2, 1, 1})->PaletteIndex == 9U &&
            history.UndoCount() == 1U && editSession.rebuilds_ == 1U,
        "Smoke commit did not apply the planned change atomically.");

    Require(history.Undo(editSession) && !document.HasVoxel({2, 1, 1}) &&
            history.Redo(editSession) && document.GetVoxel({2, 1, 1})->PaletteIndex == 9U &&
            editSession.rebuilds_ == 3U,
        "Smoke Undo/Redo did not restore the exact committed plan.");

    session.Clear();
    const SmartToolResult cleared = controller.ResolveCommit(session);
    Require(!cleared.HasPlan() && cleared.Code == SmartBrushResultCode::InvalidRequest,
        "Smoke Clear did not end the active plan session.");
}
}

int main()
{
    try
    {
        TestCompleteSmoke();
        std::cout << "Smart Tool plan state smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
