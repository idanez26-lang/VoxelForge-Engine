#include "ViewportInteractionV2/PencilViewportInteractionController.h"

#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;
namespace V2 = Editor::InteractionV2;

constexpr std::uint64_t Generation = 602U;

Asset::Voxel::VoxelDocument MakeDocument(const std::uint32_t side)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{side, side, side}, {{0U, 0U, 0U, 1U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "pencil-interaction-v2-benchmark.vox");
    if (!loaded.Succeeded())
        throw std::runtime_error("Unable to build Pencil V2 benchmark document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U; index < document.GetPalette().size(); ++index)
    {
        const auto color = document.GetPalette()[index];
        if (!model.Palette().Set(index,
                {color.Red, color.Green, color.Blue, color.Alpha}))
            throw std::runtime_error("Unable to initialize benchmark palette.");
    }
    const Asset::Voxel::VoxelSubModel* const source = document.GetModel(0U);
    if (source == nullptr)
        throw std::runtime_error("Benchmark document has no voxel model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    if (!grid.Resize(dimensions.X, dimensions.Y, dimensions.Z))
        throw std::runtime_error("Unable to initialize benchmark voxel grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        if (!grid.Set(static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}))
            throw std::runtime_error("Unable to synchronize benchmark voxel.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class HistorySession final : public Editor::VoxelEditSession
{
public:
    explicit HistorySession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document)) {}

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }
    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    [[nodiscard]] Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedEdits_; }

    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

double Milliseconds(const std::chrono::steady_clock::time_point begin,
    const std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

Editor::PencilCompactRequest MakeRequest(
    const Asset::Voxel::VoxelDocument& document, const std::uint32_t size)
{
    Editor::PencilCompactRequest request;
    request.Dimensions = *document.GetDimensions(0U);
    request.Brush.Shape = Editor::SmartBrushShape::Cube;
    request.Brush.Dimension = Editor::SmartBrushDimension::Volume3D;
    request.Brush.Size = static_cast<std::int32_t>(size);
    const std::int32_t center = static_cast<std::int32_t>(
        request.Dimensions.X / 2U);
    request.Placement = {{center, center, center}, {0, 1, 0}};
    request.Action = Editor::SmartAction::Add;
    request.PaletteIndex = 1U;
    request.DocumentGeneration = Generation;
    request.DocumentRevision = document.GetRevision();
    return request;
}

V2::PencilViewportInputFrame MakeFrame(const std::uint64_t frame,
    const Editor::PencilCompactRequest& request, const Position target)
{
    V2::PencilViewportInputFrame result;
    result.Frame = frame;
    result.PencilToolActive = true;
    result.Interaction = {true, true, false, false, false};
    result.Request = request;
    result.Target = target;
    return result;
}

void RunCase(const std::uint32_t size)
{
    auto document = MakeDocument(512U);
    const Editor::PencilCompactRequest request = MakeRequest(document, size);
    V2::PencilViewportInteractionController controller;

    const auto activationBegin = std::chrono::steady_clock::now();
    controller.SubmitInput(MakeFrame(1U, request, request.Placement.Target));
    controller.Tick(&document);
    const auto activationEnd = std::chrono::steady_clock::now();
    const V2::PencilCompactPresentation first = controller.Presentation();
    if (first.Plans.empty())
        throw std::runtime_error("Pencil V2 activation did not build a plan.");

    const std::uint64_t initialPresentationRevision = first.Revision;
    const std::uint64_t initialPlanBuilds = controller.PlanningMetrics().PlanBuilds;
    const auto sameCellBegin = std::chrono::steady_clock::now();
    for (std::uint64_t frame = 2U; frame < 122U; ++frame)
    {
        controller.SubmitInput(MakeFrame(frame, request,
            request.Placement.Target));
        controller.Tick(&document);
    }
    const auto sameCellEnd = std::chrono::steady_clock::now();
    if (controller.Presentation().Revision != initialPresentationRevision ||
        controller.PlanningMetrics().PlanBuilds != initialPlanBuilds)
    {
        throw std::runtime_error("Same Pencil V2 cell rebuilt its presentation.");
    }

    const auto translationBegin = std::chrono::steady_clock::now();
    controller.SubmitInput(MakeFrame(122U, request, {
        request.Placement.Target.X + 1,
        request.Placement.Target.Y,
        request.Placement.Target.Z}));
    controller.Tick(&document);
    const auto translationEnd = std::chrono::steady_clock::now();

    // Commit remains an explicit, non-interactive cost.  Every benchmark
    // size therefore also builds/executes/undoes/redoes a real operation
    // against a 64^3 document.  For 128^3 and 256^3 the plan is deliberately
    // clipped by that document, bounding the materialized operation at
    // 64^3 cells while still measuring the full 2M/16M procedural traversal.
    auto commitDocument = MakeDocument(64U);
    const Editor::PencilCompactRequest commitRequest =
        MakeRequest(commitDocument, size);
    Editor::SmartToolPlanner planner;
    const auto planBegin = std::chrono::steady_clock::now();
    const Editor::PencilCompactPlanResult planned =
        planner.PlanPencilCompact(commitRequest);
    const auto planEnd = std::chrono::steady_clock::now();
    if (!planned.HasPlan())
        throw std::runtime_error("Pencil V2 bounded commit plan was rejected.");
    const auto buildBegin = std::chrono::steady_clock::now();
    std::optional<Editor::VoxelEditOperation> operation =
        V2::PencilCompactCommitGateway::Build(
            commitDocument, Generation, {&planned.Plan, 1U});
    const auto buildEnd = std::chrono::steady_clock::now();
    if (!operation || operation->Changes.size() > 64U * 64U * 64U)
        throw std::runtime_error("Pencil V2 bounded commit did not clip safely.");
    HistorySession session(commitDocument);
    Editor::VoxelEditHistory history;
    const auto executeBegin = std::chrono::steady_clock::now();
    const bool executed = static_cast<bool>(history.Execute(session,
        std::move(*operation)));
    const auto executeEnd = std::chrono::steady_clock::now();
    const auto undoBegin = std::chrono::steady_clock::now();
    const bool undone = static_cast<bool>(history.Undo(session));
    const auto undoEnd = std::chrono::steady_clock::now();
    const auto redoBegin = std::chrono::steady_clock::now();
    const bool redone = static_cast<bool>(history.Redo(session));
    const auto redoEnd = std::chrono::steady_clock::now();
    if (!executed || !undone || !redone)
        throw std::runtime_error("Pencil V2 bounded commit history failed.");

    const V2::PencilCompactPresentation presentation = controller.Presentation();
    std::cout
        << "brush=" << size
        << " potential_cells=" << presentation.ExactVoxelCount
        << " activation_ms=" << Milliseconds(activationBegin, activationEnd)
        << " same_cell_120_frames_ms=" << Milliseconds(sameCellBegin, sameCellEnd)
        << " translated_cell_ms=" << Milliseconds(translationBegin, translationEnd)
        << " detail=" << static_cast<int>(presentation.Detail)
        << " validity=" << static_cast<int>(presentation.Validity)
        << " plan_builds=" << controller.PlanningMetrics().PlanBuilds
        << " cache_hits=" << controller.PlanningMetrics().PlanCacheHits
        << " preview_builds=" << controller.Metrics().PreviewBuilds
        << " clipped_commit_document_side=64"
        << " commit_plan_ms=" << Milliseconds(planBegin, planEnd)
        << " commit_build_ms=" << Milliseconds(buildBegin, buildEnd)
        << " commit_execute_ms=" << Milliseconds(executeBegin, executeEnd)
        << " undo_ms=" << Milliseconds(undoBegin, undoEnd)
        << " redo_ms=" << Milliseconds(redoBegin, redoEnd)
        << " commit_mesh_rebuilds=" << session.rebuilds_
        << "\n";
}
} // namespace

int main()
{
    try
    {
        for (const std::uint32_t size : {1U, 8U, 32U, 64U, 128U, 256U})
            RunCase(size);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
