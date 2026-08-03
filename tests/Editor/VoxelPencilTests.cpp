#include "VoxelTools/VoxelPencilInput.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "PencilPreviewTestSupport.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartToolController.h"
#include "VoxelTools/VoxelBrush.h"
#include "VoxelTools/VoxelToolState.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <array>
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

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Vox::VoxModelMetadata Model(
    const Asset::Vox::VoxDimensions dimensions,
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    return {dimensions, std::move(voxels)};
}

Asset::Voxel::VoxelDocument Document(
    std::vector<Asset::Vox::VoxModelMetadata> models)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = std::move(models);
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    source.HasPackChunk = source.Models.size() > 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-pencil-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Pencil test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("Pencil test model");
    for (std::size_t index = 0U; index < document.GetModelCount(); ++index)
    {
        const Asset::Voxel::VoxelSubModel* source = document.GetModel(index);
        Require(source != nullptr, "Missing source sub-model.");
        const auto dimensions = source->Dimensions();
        Voxel::VoxelGrid grid;
        Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
            "Unable to size compatibility grid.");
        source->ForEachVoxel([&grid](
            const Asset::Voxel::VoxelPosition position,
            const Asset::Voxel::Voxel voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to initialize compatibility grid.");
        });
        model.AddGrid(std::move(grid));
    }
    return model;
}

Editor::VoxelRaycastHit Hit(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const Editor::VoxelHitFace face,
    const std::size_t modelIndex = 0U)
{
    Editor::VoxelRaycastHit result;
    result.Coordinates = {x, y, z};
    result.Face = face;
    result.SubModelIndex = modelIndex;
    const auto normal = Editor::VoxelHitFaceIntegerNormal(face);
    result.AdjacentPosition = {
        static_cast<std::int32_t>(x) + normal.X,
        static_cast<std::int32_t>(y) + normal.Y,
        static_cast<std::int32_t>(z) + normal.Z};
    result.AdjacentWithinBounds = true;
    return result;
}

class TestEditSession final : public Editor::VoxelEditSession
{
public:
    explicit TestEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return generation_;
    }

    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return hasModel_ ? &model_ : nullptr;
    }

    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }

    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildAttempts_;
        if (throwOnRebuild_) throw std::runtime_error("simulated rebuild throw");
        if (failRebuild_)
            return Editor::CommandResult::Failure("simulated rebuild failure");
        const auto synchronized = cache_.Synchronize(*document_, generation_);
        if (!synchronized.Succeeded)
            return Editor::CommandResult::Failure(synchronized.Message);
        if (synchronized.Rebuilt())
        {
            if (gpuBufferActive_) ++releasedGpuBuffers_;
            gpuBufferActive_ = true;
            ++gpuUploads_;
        }
        return Editor::CommandResult::Success();
    }

    void CompleteVoxelEdit() noexcept override
    {
        ++completedEdits_;
    }

    bool PrimeMesh()
    {
        return static_cast<bool>(RebuildActiveVoxelMesh());
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
    std::uint64_t generation_ = 1U;
    std::size_t rebuildAttempts_ = 0U;
    std::size_t gpuUploads_ = 0U;
    std::size_t releasedGpuBuffers_ = 0U;
    std::size_t completedEdits_ = 0U;
    bool hasModel_ = true;
    bool failRebuild_ = false;
    bool throwOnRebuild_ = false;
    bool gpuBufferActive_ = false;
};

struct TestPencilContext final
{
    TestEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::optional<Editor::VoxelRaycastHit> Hit;
    Editor::SmartBrushState State{};
    Editor::VoxelEditHistory* History = nullptr;
    std::optional<Asset::Voxel::VoxelPosition> WorkplaneTarget;

    [[nodiscard]] operator Editor::VoxelPencilContext() const
    {
        Require(EditSession != nullptr && Document != nullptr,
            "Cannot build a test Smart Tool plan without a document and session.");
        const auto dimensions = Document->GetDimensions(SubModelIndex);
        if (!dimensions)
        {
            return {nullptr, {EditSession, Document, SubModelIndex,
                EditSession->VoxelModelGeneration(), History,
                std::optional<std::size_t>{State.PaletteIndex}, nullptr, nullptr}};
        }
        const bool erasing = State.Mode == Editor::SmartBrushMode::Erase;
        const auto target = WorkplaneTarget ? WorkplaneTarget
            : Hit ? std::optional<Asset::Voxel::VoxelPosition>{erasing
                ? Asset::Voxel::VoxelPosition{static_cast<std::int32_t>(Hit->Coordinates.X),
                    static_cast<std::int32_t>(Hit->Coordinates.Y),
                    static_cast<std::int32_t>(Hit->Coordinates.Z)}
                : Hit->AdjacentPosition} : std::nullopt;
        Require(target.has_value(), "Missing test placement target.");
        if (Hit && Hit->Face == Editor::VoxelHitFace::None)
        {
            return {nullptr, {EditSession, Document, SubModelIndex,
                EditSession->VoxelModelGeneration(), History,
                std::optional<std::size_t>{State.PaletteIndex}, nullptr, nullptr}};
        }
        const auto normal = Hit ? Editor::VoxelHitFaceIntegerNormal(Hit->Face)
            : Asset::Voxel::VoxelPosition{0, 1, 0};
        Editor::SmartToolRequest request;
        request.Geometry = Editor::SmartGeometry::Pencil;
        request.Action = erasing ? Editor::SmartAction::Erase : Editor::SmartAction::Add;
        request.BrushRequest = {*dimensions, State, {*target, normal}, {}};
        request.ReadVoxel =
            [document = Document, subModelIndex = SubModelIndex](
                const Asset::Voxel::VoxelPosition position)
            {
                const auto voxel = document->GetVoxel(position, subModelIndex);
                return Editor::SmartToolVoxelState{
                    voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
            };
        request.SourceIdentity = reinterpret_cast<std::uintptr_t>(Document);
        request.SourceRevision = Document->GetRevision();
        request.SourceGeneration = EditSession->VoxelModelGeneration();
        request.SourceSubModelIndex = SubModelIndex;
        Editor::SmartToolController controller;
        Editor::SmartToolSession session;
        const Editor::SmartToolPlanPtr plan = controller.ResolvePreview(
            session, request).Plan;
        return {plan, {EditSession, Document, SubModelIndex,
            request.SourceGeneration, History,
            std::optional<std::size_t>{State.PaletteIndex}, nullptr, nullptr}};
    }
};

TestPencilContext Context(
    TestEditSession& session,
    Asset::Voxel::VoxelDocument& document,
    const Editor::VoxelRaycastHit hit,
    const std::size_t paletteIndex = 1U,
    const std::size_t modelIndex = 0U)
{
    TestPencilContext context;
    context.EditSession = &session;
    context.Document = &document;
    context.SubModelIndex = modelIndex;
    context.Hit = hit;
    context.State.PaletteIndex = paletteIndex;
    return context;
}

void TestToolApplicationAndSynchronization()
{
    auto document = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 7U}})});
    TestEditSession session(document);
    Require(session.PrimeMesh(), "Initial mesh build failed.");
    const std::size_t initialVertices = session.cache_.Mesh()->VertexCount();
    const std::size_t initialIndices = session.cache_.Mesh()->IndexCount();
    const std::uint64_t revision = document.GetRevision();
    const std::uint64_t count = document.GetVoxelCount();
    const std::size_t builds = session.cache_.BuildCount();
    const std::size_t uploads = session.gpuUploads_;

    const Editor::VoxelToolResult result = Editor::VoxelPencilTool::Apply(
        Context(session, document,
            Hit(1U, 1U, 1U, Editor::VoxelHitFace::NegativeX), 1U));
    const auto added = document.GetVoxel({0, 1, 1});
    const auto bounds = document.GetBounds();
    Require(result.Code == Editor::VoxelToolResultCode::Applied &&
        result.Changed && result.Position == Asset::Voxel::VoxelPosition{0, 1, 1} &&
        result.RevisionBefore == revision &&
        result.RevisionAfter == revision + 1U && result.Error.empty(),
        "Pencil returned an incorrect structured success result.");
    Require(added && added->PaletteIndex == 1U && document.IsDirty() &&
        document.GetRevision() == revision + 1U &&
        document.GetVoxelCount() == count + 1U && bounds &&
        bounds->Minimum.X == 0 &&
        session.model_.GetGrid(0U)->Get(0U, 1U, 1U)->ColorIndex == 1U,
        "Pencil did not update document, bounds, color, and compatibility grid.");
    Require(session.cache_.BuildCount() == builds + 1U &&
        session.gpuUploads_ == uploads + 1U &&
        session.releasedGpuBuffers_ == 1U &&
        session.completedEdits_ == 1U &&
        (session.cache_.Mesh()->VertexCount() != initialVertices ||
         session.cache_.Mesh()->IndexCount() != initialIndices),
        "Pencil did not rebuild and replace the synchronized mesh exactly once.");
}

void TestToolRefusals()
{
    auto document = Document({Model(
        {3U, 3U, 3U},
        {{0U, 1U, 1U, 2U}, {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}})});
    TestEditSession session(document);
    const auto initialCount = document.GetVoxelCount();
    const auto initialRevision = document.GetRevision();
    const auto validHit = Hit(
        1U, 1U, 1U, Editor::VoxelHitFace::NegativeY);

    Editor::VoxelPencilContext noDocument =
        Context(session, document, validHit);
    noDocument.Execution.Document = nullptr;
    Require(Editor::VoxelPencilTool::Apply(noDocument).Code ==
        Editor::VoxelToolResultCode::NoDocument,
        "Missing document was not refused.");
    Editor::VoxelPencilContext noHit = Context(session, document, validHit);
    noHit.Plan.reset();
    Require(Editor::VoxelPencilTool::Apply(noHit).Code ==
        Editor::VoxelToolResultCode::Failed,
        "A missing immutable plan was not refused.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document, Hit(1U, 1U, 1U, Editor::VoxelHitFace::None))).Code ==
            Editor::VoxelToolResultCode::Failed,
        "An invalid request without a plan was not refused.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document, Hit(0U, 1U, 1U,
            Editor::VoxelHitFace::NegativeX))).Code ==
            Editor::VoxelToolResultCode::TargetOutOfBounds,
        "Out-of-bounds target was not refused.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document, Hit(1U, 1U, 1U,
            Editor::VoxelHitFace::PositiveX))).Code ==
            Editor::VoxelToolResultCode::TargetOccupied,
        "Occupied target was not refused.");
    const Editor::VoxelPencilContext zeroPalette =
        Context(session, document, validHit, 0U);
    const Editor::VoxelPencilContext oversizedPalette =
        Context(session, document, validHit, 256U);
    Require(zeroPalette.Plan == nullptr && oversizedPalette.Plan == nullptr &&
        Editor::VoxelPencilTool::Apply(zeroPalette).Code ==
            Editor::VoxelToolResultCode::Failed &&
        Editor::VoxelPencilTool::Apply(oversizedPalette).Code ==
            Editor::VoxelToolResultCode::Failed,
        "The Planner did not refuse invalid palette requests before commit.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::NegativeY, 1U),
        1U, 1U)).Code == Editor::VoxelToolResultCode::Failed,
        "An unavailable sub-model plan was not refused.");
    Editor::VoxelPencilContext blocked = Context(session, document, validHit);
    blocked.Plan.reset();
    Require(Editor::VoxelPencilTool::Apply(blocked).Code ==
        Editor::VoxelToolResultCode::Failed,
        "An absent plan was not refused before execution.");
    Editor::VoxelPencilContext inconsistent =
        Context(session, document, validHit);
    ++inconsistent.Execution.SourceGeneration;
    Require(Editor::VoxelPencilTool::Apply(inconsistent).Code ==
        Editor::VoxelToolResultCode::Failed,
        "Inconsistent hit adjacency was not refused.");
    Require(document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision && !document.IsDirty() &&
        session.rebuildAttempts_ == 0U,
        "A refused Pencil operation changed state or rebuilt the mesh.");
}

void TestRollbackAndMultiModel()
{
    auto rollbackDocument = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 1U}})});
    TestEditSession rollbackSession(rollbackDocument);
    rollbackSession.failRebuild_ = true;
    const auto beforeBounds = rollbackDocument.GetBounds();
    const Editor::VoxelToolResult failed = Editor::VoxelPencilTool::Apply(
        Context(rollbackSession, rollbackDocument,
            Hit(1U, 1U, 1U, Editor::VoxelHitFace::NegativeX)));
    Require(failed.Code == Editor::VoxelToolResultCode::Failed &&
        !failed.Changed && !rollbackDocument.HasVoxel({0, 1, 1}) &&
        rollbackDocument.GetVoxelCount() == 1U &&
        rollbackDocument.GetRevision() == 0U &&
        !rollbackDocument.IsDirty() &&
        rollbackDocument.GetBounds() == beforeBounds &&
        !rollbackSession.model_.GetGrid(0U)->Get(0U, 1U, 1U)->IsOccupied(),
        "Failed rebuild did not roll back document and compatibility grid exactly.");

    auto multiDocument = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 1U}}),
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 2U}})});
    TestEditSession multiSession(multiDocument);
    const Editor::VoxelToolResult result = Editor::VoxelPencilTool::Apply(
        Context(multiSession, multiDocument,
            Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveY, 1U),
            9U, 1U));
    Require(result.Code == Editor::VoxelToolResultCode::Applied &&
        multiDocument.GetVoxel({1, 2, 1}, 1U)->PaletteIndex == 9U &&
        !multiDocument.HasVoxel({1, 2, 1}, 0U) &&
        multiSession.model_.GetGrid(1U)->Get(1U, 2U, 1U)->ColorIndex == 9U,
        "Pencil edited the wrong sub-model.");
}

void TestDocumentOnlyCanonicalEdits()
{
    auto directDocument = Document({Model(
        {4U, 4U, 4U}, {{1U, 1U, 1U, 4U}})});
    TestEditSession directSession(directDocument);
    directSession.hasModel_ = false;
    const Editor::VoxelToolResult direct = Editor::VoxelPencilTool::Apply(
        Context(directSession, directDocument,
            Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveX), 7U));
    Require(direct.Code == Editor::VoxelToolResultCode::Applied &&
        directDocument.GetVoxel({2, 1, 1})->PaletteIndex == 7U &&
        directSession.completedEdits_ == 1U,
        "Pencil did not edit the canonical document without a compatibility model.");

    auto historyDocument = Document({Model(
        {4U, 4U, 4U}, {{1U, 1U, 1U, 2U}})});
    TestEditSession historySession(historyDocument);
    historySession.hasModel_ = false;
    Editor::VoxelEditHistory history;
    TestPencilContext context = Context(historySession, historyDocument,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveY), 9U);
    context.History = &history;
    Require(Editor::VoxelPencilTool::Apply(context).Code ==
            Editor::VoxelToolResultCode::Applied &&
        historyDocument.GetVoxel({1, 2, 1})->PaletteIndex == 9U &&
        history.UndoCount() == 1U && history.Undo(historySession) &&
        !historyDocument.HasVoxel({1, 2, 1}) && history.Redo(historySession) &&
        historyDocument.GetVoxel({1, 2, 1})->PaletteIndex == 9U,
        "Document-only Pencil history did not apply, undo and redo canonically.");
}

void TestPreview()
{
    auto document = Document({Model(
        {3U, 3U, 3U},
        {{0U, 1U, 1U, 2U}, {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}})});
    const auto validHit = Hit(
        1U, 1U, 1U, Editor::VoxelHitFace::PositiveY);
    const auto valid = Editor::EvaluatePencilPlanForTest(
        &document, 0U, validHit, true);
    const auto occupied = Editor::EvaluatePencilPlanForTest(
        &document, 0U,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveX), true);
    const auto outside = Editor::EvaluatePencilPlanForTest(
        &document, 0U,
        Hit(0U, 1U, 1U, Editor::VoxelHitFace::NegativeX), true);
    Require(valid.IsVisible() && valid.IsValid() &&
        valid.Position == Asset::Voxel::VoxelPosition{1, 2, 1} &&
        occupied.Status == Editor::VoxelPlacementPreviewStatus::Occupied &&
        outside.Status == Editor::VoxelPlacementPreviewStatus::OutOfBounds &&
        !Editor::EvaluatePencilPlanForTest(
            &document, 0U, std::nullopt, true).IsVisible() &&
        !Editor::EvaluatePencilPlanForTest(
            &document, 0U, validHit, false).IsVisible() &&
        !Editor::EvaluatePencilPlanForTest(
            nullptr, 0U, validHit, true).IsVisible(),
        "Placement preview statuses are incorrect.");
    const auto changed = document.SetVoxel({1, 2, 1}, 5U);
    Require(changed && Editor::EvaluatePencilPlanForTest(
        &document, 0U, validHit, true).Status ==
            Editor::VoxelPlacementPreviewStatus::Occupied,
        "Placement preview did not follow document revision changes.");
}

void TestBrushGenerationAndPreview()
{
    const Asset::Voxel::VoxelPosition anchor{10, 10, 10};
    const auto requireUniqueAndSymmetric = [&anchor](
        const std::vector<Asset::Voxel::VoxelPosition>& positions,
        const int size)
    {
        const int centerOffset = size % 2 == 0 ? 1 : 0;
        for (std::size_t first = 0U; first < positions.size(); ++first)
        {
            for (std::size_t second = first + 1U;
                 second < positions.size(); ++second)
            {
                Require(positions[first] != positions[second],
                    "Brush generation produced duplicate positions.");
            }
            const Asset::Voxel::VoxelPosition reflected{
                2 * anchor.X + centerOffset - positions[first].X,
                2 * anchor.Y + centerOffset - positions[first].Y,
                2 * anchor.Z + centerOffset - positions[first].Z};
            bool foundReflection = false;
            for (const Asset::Voxel::VoxelPosition candidate : positions)
            {
                if (candidate == reflected)
                {
                    foundReflection = true;
                    break;
                }
            }
            Require(foundReflection,
                "Brush generation is not symmetric around its doubled centre.");
        }
    };
    for (const int size : {1, 2, 3})
    {
        const auto cube = Editor::GenerateVoxelBrush(
            anchor, Editor::VoxelBrushShape::Cube, size);
        const std::size_t expected = static_cast<std::size_t>(size * size * size);
        Require(cube.size() == expected &&
            Editor::EstimateVoxelBrushVoxelCount(
                Editor::VoxelBrushShape::Cube, size) == expected,
            "Cube brush generation or estimate is incorrect.");
        requireUniqueAndSymmetric(cube, size);
    }
    const auto evenCube = Editor::GenerateVoxelBrush(
        anchor, Editor::VoxelBrushShape::Cube, 4);
    Require(std::find(evenCube.begin(), evenCube.end(),
                Asset::Voxel::VoxelPosition{9, 9, 9}) != evenCube.end() &&
        std::find(evenCube.begin(), evenCube.end(), anchor) != evenCube.end() &&
        std::find(evenCube.begin(), evenCube.end(),
                Asset::Voxel::VoxelPosition{12, 12, 12}) != evenCube.end(),
        "Even brush anchoring does not use the documented lower/positive pair.");
    constexpr std::array<std::size_t, 5U> sphereCounts{1U, 8U, 19U, 32U, 81U};
    for (int size = 1; size <= 5; ++size)
    {
        const auto sphere = Editor::GenerateVoxelBrush(
            anchor, Editor::VoxelBrushShape::Sphere, size);
        Require(sphere.size() == sphereCounts[static_cast<std::size_t>(size - 1)] &&
            Editor::EstimateVoxelBrushVoxelCount(
                Editor::VoxelBrushShape::Sphere, size) == sphere.size(),
            "Sphere brush generation or non-allocating estimate is incorrect.");
        requireUniqueAndSymmetric(sphere, size);
    }
    Require(!Editor::IsVoxelBrushSizeValid(0) &&
        !Editor::IsVoxelBrushSizeValid(Editor::MaximumVoxelBrushSize + 1) &&
        Editor::IsVoxelBrushSizeValid(Editor::MaximumVoxelBrushSize) &&
        Editor::EstimateVoxelBrushVoxelCount(
            Editor::VoxelBrushShape::Cube,
            Editor::MaximumVoxelBrushSize) == 4096U &&
        !Editor::UsesAggregateBrushPreview(256U) &&
        Editor::UsesAggregateBrushPreview(257U) &&
        Editor::UsesAggregateBrushPreview(4096U) &&
        Editor::SelectVoxelBrushPreviewRenderMode(
            Editor::VoxelBrushShape::Cube, 4096U) ==
            Editor::VoxelBrushPreviewRenderMode::AggregateBox &&
        Editor::SelectVoxelBrushPreviewRenderMode(
            Editor::VoxelBrushShape::Sphere, 4096U) ==
            Editor::VoxelBrushPreviewRenderMode::AggregateSphere,
        "Brush size limits are incorrect.");

    auto document = Document({Model(
        {8U, 8U, 8U}, {{0U, 0U, 0U, 1U}})});
    const auto valid = Editor::EvaluatePencilPlanForTest(
        &document, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{2, 2, 2},
        Editor::VoxelBrushShape::Cube, 2);
    const auto outside = Editor::EvaluatePencilPlanForTest(
        &document, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{20, 2, 2},
        Editor::VoxelBrushShape::Cube, 4);
    Require(valid.IsValid() && valid.Positions.size() == 8U &&
        outside.Status == Editor::VoxelPlacementPreviewStatus::OutOfBounds &&
        outside.Positions.empty(),
        "Multi-voxel preview did not expose the generated brush plan.");
    Require(static_cast<bool>(document.SetVoxel({3, 3, 3}, 2U)),
        "Unable to set up occupied brush preview test.");
    const std::uint64_t occupiedPreviewRevision = document.GetRevision();
    const std::uint64_t occupiedPreviewCount = document.GetVoxelCount();
    const auto occupied = Editor::EvaluatePencilPlanForTest(
        &document, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{2, 2, 2},
        Editor::VoxelBrushShape::Cube, 2);
    Require(occupied.IsValid() && occupied.Positions.size() == 8U &&
        occupied.AddablePositions.size() == 7U &&
        occupied.OccupiedPositions.size() == 1U &&
        document.GetRevision() == occupiedPreviewRevision &&
        document.GetVoxelCount() == occupiedPreviewCount,
        "Partially occupied multi-voxel preview did not preserve addable cells.");
}

void TestAtomicBrushHistory()
{
    auto document = Document({Model(
        {8U, 8U, 8U}, {{0U, 0U, 0U, 1U}})});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const std::uint64_t revision = document.GetRevision();
    const std::uint64_t count = document.GetVoxelCount();
    TestPencilContext context = Context(
        session, document, Hit(0U, 0U, 0U, Editor::VoxelHitFace::PositiveX));
    context.WorkplaneTarget = {2, 2, 2};
    context.State.Shape = Editor::SmartBrushShape::Cube;
    context.State.Size = 2;
    context.History = &history;
    const Editor::VoxelToolResult applied = Editor::VoxelPencilTool::Apply(context);
    Require(applied.Code == Editor::VoxelToolResultCode::Applied &&
        applied.RevisionBefore == revision &&
        applied.RevisionAfter == revision + 1U &&
        document.GetVoxelCount() == count + 8U && history.UndoCount() == 1U &&
        history.RedoCount() == 0U && session.completedEdits_ == 1U,
        "A brush was not applied as one atomic history transaction.");
    Require(history.Undo(session) && document.GetVoxelCount() == count &&
        history.UndoCount() == 0U && history.RedoCount() == 1U &&
        history.Redo(session) && document.GetVoxelCount() == count + 8U &&
        history.UndoCount() == 1U && history.RedoCount() == 0U,
        "A brush did not undo and redo as one operation.");

    auto overlapDocument = Document({Model(
        {8U, 8U, 8U}, {{0U, 0U, 0U, 1U}, {3U, 3U, 3U, 2U}})});
    TestEditSession overlapSession(overlapDocument);
    Editor::VoxelEditHistory overlapHistory;
    const std::uint64_t overlapRevision = overlapDocument.GetRevision();
    const std::uint64_t overlapCount = overlapDocument.GetVoxelCount();
    TestPencilContext occupied = Context(overlapSession, overlapDocument,
        Hit(0U, 0U, 0U, Editor::VoxelHitFace::PositiveX));
    occupied.WorkplaneTarget = {2, 2, 2};
    occupied.State.Shape = Editor::SmartBrushShape::Cube;
    occupied.State.Size = 2;
    occupied.History = &overlapHistory;
    Require(Editor::VoxelPencilTool::Apply(occupied).Code ==
            Editor::VoxelToolResultCode::Applied &&
        overlapDocument.GetRevision() == overlapRevision + 1U &&
        overlapDocument.GetVoxelCount() == overlapCount + 7U &&
        overlapDocument.GetVoxel({3, 3, 3})->PaletteIndex == 2U &&
        overlapHistory.UndoCount() == 1U &&
        overlapHistory.Undo(overlapSession) &&
        overlapDocument.GetVoxelCount() == overlapCount &&
        overlapDocument.GetVoxel({3, 3, 3})->PaletteIndex == 2U,
        "A partially occupied brush did not preserve existing voxels on undo.");

    std::vector<Asset::Vox::VoxVoxel> occupiedVoxels;
    for (std::uint8_t z = 2U; z <= 3U; ++z)
    {
        for (std::uint8_t y = 2U; y <= 3U; ++y)
        {
            for (std::uint8_t x = 2U; x <= 3U; ++x)
                occupiedVoxels.push_back({x, y, z, 3U});
        }
    }
    auto fullyOccupiedDocument = Document({Model({8U, 8U, 8U}, occupiedVoxels)});
    TestEditSession fullyOccupiedSession(fullyOccupiedDocument);
    Editor::VoxelEditHistory fullyOccupiedHistory;
    const std::uint64_t fullyOccupiedRevision = fullyOccupiedDocument.GetRevision();
    const std::uint64_t fullyOccupiedCount = fullyOccupiedDocument.GetVoxelCount();
    TestPencilContext fullyOccupied = Context(
        fullyOccupiedSession, fullyOccupiedDocument,
        Hit(2U, 2U, 2U, Editor::VoxelHitFace::PositiveX));
    fullyOccupied.WorkplaneTarget = {2, 2, 2};
    fullyOccupied.State.Shape = Editor::SmartBrushShape::Cube;
    fullyOccupied.State.Size = 2;
    fullyOccupied.History = &fullyOccupiedHistory;
    Require(Editor::VoxelPencilTool::Apply(fullyOccupied).Code ==
            Editor::VoxelToolResultCode::TargetOccupied &&
        fullyOccupiedDocument.GetRevision() == fullyOccupiedRevision &&
        fullyOccupiedDocument.GetVoxelCount() == fullyOccupiedCount &&
        fullyOccupiedHistory.UndoCount() == 0U &&
        fullyOccupiedSession.completedEdits_ == 0U,
        "A fully occupied brush was not a no-op.");

    TestPencilContext outside = fullyOccupied;
    outside.WorkplaneTarget = {20, 2, 2};
    outside.State.Size = 4;
    Require(Editor::VoxelPencilTool::Apply(outside).Code ==
            Editor::VoxelToolResultCode::TargetOutOfBounds &&
        fullyOccupiedDocument.GetRevision() == fullyOccupiedRevision &&
        fullyOccupiedDocument.GetVoxelCount() == fullyOccupiedCount &&
        fullyOccupiedHistory.UndoCount() == 0U &&
        fullyOccupiedSession.completedEdits_ == 0U,
        "An out-of-bounds brush target partially mutated the document.");
}

void TestSurfaceAnchoredBrushes()
{
    auto workplaneDocument = Document({Model(
        {8U, 8U, 8U}, {{0U, 0U, 0U, 1U}})});
    const auto workplaneCube = Editor::EvaluatePencilPlanForTest(
        &workplaneDocument, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{3, 0, 3},
        Editor::VoxelBrushShape::Cube, 3);
    const auto workplaneSphere = Editor::EvaluatePencilPlanForTest(
        &workplaneDocument, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{6, 0, 6},
        Editor::VoxelBrushShape::Sphere, 3);
    Require(workplaneCube.IsValid() && workplaneCube.Positions.size() == 27U &&
        workplaneSphere.IsValid() &&
        workplaneSphere.Positions.size() == 19U,
        "Size 3 workplane brushes were not valid at Y=0.");
    for (const Asset::Voxel::VoxelPosition position : workplaneCube.Positions)
        Require(position.Y >= 0,
            "A Size 3 Cube brush extended behind the Y=0 workplane.");
    for (const Asset::Voxel::VoxelPosition position : workplaneSphere.Positions)
        Require(position.Y >= 0,
            "A Size 3 Sphere brush extended behind the Y=0 workplane.");

    TestEditSession workplaneSession(workplaneDocument);
    TestPencilContext workplaneContext = Context(
        workplaneSession, workplaneDocument,
        Hit(0U, 0U, 0U, Editor::VoxelHitFace::PositiveY));
    workplaneContext.WorkplaneTarget = {3, 0, 3};
    workplaneContext.State.Shape = Editor::SmartBrushShape::Cube;
    workplaneContext.State.Size = 3;
    const std::uint64_t workplaneRevision = workplaneDocument.GetRevision();
    Require(Editor::VoxelPencilTool::Apply(workplaneContext).Code ==
            Editor::VoxelToolResultCode::Applied &&
        workplaneDocument.GetRevision() == workplaneRevision + 1U &&
        workplaneDocument.GetVoxelCount() == 28U,
        "A valid Size 3 workplane brush did not apply atomically.");

    auto faceDocument = Document({Model(
        {8U, 8U, 8U}, {{3U, 3U, 3U, 1U}, {4U, 3U, 3U, 6U}})});
    TestEditSession faceSession(faceDocument);
    const auto faceHit = Hit(3U, 3U, 3U, Editor::VoxelHitFace::PositiveX);
    const auto facePreview = Editor::EvaluatePencilPlanForTest(
        &faceDocument, 0U, faceHit, true, std::nullopt,
        Editor::VoxelBrushShape::Cube, 3);
    Require(facePreview.IsValid() && facePreview.Positions.size() == 27U &&
        facePreview.AddablePositions.size() == 26U &&
        facePreview.OccupiedPositions.size() == 1U &&
        std::find(facePreview.Positions.begin(), facePreview.Positions.end(),
            Asset::Voxel::VoxelPosition{3, 3, 3}) == facePreview.Positions.end(),
        "A Size 3 face brush still overlaps its source voxel.");
    TestPencilContext faceContext = Context(
        faceSession, faceDocument, faceHit);
    faceContext.State.Shape = Editor::SmartBrushShape::Cube;
    faceContext.State.Size = 3;
    Require(Editor::VoxelPencilTool::Apply(faceContext).Code ==
            Editor::VoxelToolResultCode::Applied &&
        faceDocument.GetVoxelCount() == 28U &&
        faceDocument.GetVoxel({4, 3, 3})->PaletteIndex == 6U,
        "A face brush did not preserve an overlapping existing voxel.");

    auto borderDocument = Document({Model(
        {8U, 8U, 8U}, {{6U, 3U, 3U, 1U}})});
    TestEditSession borderSession(borderDocument);
    const auto borderHit = Hit(6U, 3U, 3U, Editor::VoxelHitFace::PositiveX);
    const auto borderPreview = Editor::EvaluatePencilPlanForTest(
        &borderDocument, 0U, borderHit, true, std::nullopt,
        Editor::VoxelBrushShape::Cube, 3);
    const std::uint64_t borderRevision = borderDocument.GetRevision();
    const std::uint64_t borderCount = borderDocument.GetVoxelCount();
    Editor::VoxelEditHistory borderHistory;
    TestPencilContext borderContext = Context(
        borderSession, borderDocument, borderHit);
    borderContext.State.Shape = Editor::SmartBrushShape::Cube;
    borderContext.State.Size = 3;
    borderContext.History = &borderHistory;
    Require(borderPreview.IsValid() && borderPreview.Statistics.Total == 27U &&
        borderPreview.Statistics.New == 9U &&
        borderPreview.Statistics.Clipped == 18U &&
        Editor::VoxelPencilTool::Apply(borderContext).Code ==
            Editor::VoxelToolResultCode::Applied &&
        borderDocument.GetRevision() == borderRevision + 1U &&
        borderDocument.GetVoxelCount() == borderCount + 9U &&
        borderHistory.UndoCount() == 1U && borderHistory.Undo(borderSession) &&
        borderDocument.GetVoxelCount() == borderCount &&
        borderDocument.HasVoxel({6, 3, 3}),
        "A boundary face brush did not clip and undo atomically.");
}

void TestSmartEraseBrushHistoryAndPreview()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    for (std::uint8_t z = 2U; z <= 3U; ++z)
        for (std::uint8_t y = 2U; y <= 3U; ++y)
            for (std::uint8_t x = 2U; x <= 3U; ++x)
                voxels.push_back({x, y, z,
                    static_cast<std::uint8_t>(x + y + z)});
    auto document = Document({Model({8U, 8U, 8U}, voxels)});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    auto hit = Hit(2U, 2U, 2U, Editor::VoxelHitFace::PositiveX);
    hit.DocumentRevision = document.GetRevision();
    Editor::SmartBrushState state;
    state.Mode = Editor::SmartBrushMode::Erase;
    state.Size = 2;
    const auto preview = Editor::EvaluatePencilPlanForTest(
        &document, 0U, hit, true, std::nullopt, state);
    TestPencilContext context = Context(session, document, hit);
    context.State = state;
    context.History = &history;
    const std::uint64_t revision = document.GetRevision();
    Require(preview.IsValid() && preview.OccupiedPositions.size() == 8U &&
        preview.AddablePositions.empty() && preview.Statistics.Total == 8U &&
        preview.Statistics.Existing == 8U && preview.Statistics.New == 0U &&
        Editor::VoxelPencilTool::Apply(context).Code ==
            Editor::VoxelToolResultCode::Applied &&
        document.GetVoxelCount() == 0U && document.GetRevision() == revision + 1U &&
        history.UndoCount() == 1U && history.UndoLabel() == "Remove Brush",
        "Smart Erase did not apply the exact preview as one history operation.");
    Require(history.Undo(session) && document.GetVoxelCount() == 8U &&
        document.GetVoxel({2, 2, 2})->PaletteIndex == 6U &&
        document.GetVoxel({3, 3, 3})->PaletteIndex == 9U &&
        history.Redo(session) && document.GetVoxelCount() == 0U,
        "Smart Erase Undo/Redo did not restore every palette index.");

    auto emptyHit = Hit(0U, 0U, 0U, Editor::VoxelHitFace::PositiveX);
    emptyHit.DocumentRevision = document.GetRevision();
    const auto emptyPreview = Editor::EvaluatePencilPlanForTest(
        &document, 0U, emptyHit, true, std::nullopt, state);
    context.Hit = emptyHit;
    context.State.Size = 1;
    Require(emptyPreview.Status == Editor::VoxelPlacementPreviewStatus::Occupied &&
        emptyPreview.OccupiedPositions.empty() &&
        Editor::VoxelPencilTool::Apply(context).Code ==
            Editor::VoxelToolResultCode::TargetEmpty && history.UndoCount() == 1U,
        "Smart Erase empty preview did not remain a no-op.");

    context.Hit.reset();
    context.WorkplaneTarget = {2, 2, 2};
    Editor::VoxelPencilContext noPlan = context;
    noPlan.Plan.reset();
    Require(Editor::VoxelPencilTool::Apply(noPlan).Code ==
            Editor::VoxelToolResultCode::Failed && history.UndoCount() == 1U,
        "Smart Erase accepted an absent plan after its preview context changed.");
}

Editor::VoxelPencilInputFrame AllowedInput()
{
    Editor::VoxelPencilInputFrame frame;
    frame.LeftButtonDown = true;
    frame.ToolActive = true;
    frame.HasDocument = true;
    frame.ViewportHovered = true;
    frame.ViewportFocused = true;
    frame.DocumentGeneration = 4U;
    return frame;
}

void TestInputController()
{
    Editor::VoxelPencilInputController input;
    auto frame = AllowedInput();
    Require(input.Update(frame) == Editor::VoxelPencilInputDecision::Apply &&
        input.Update(frame) == Editor::VoxelPencilInputDecision::None,
        "A held Pencil click repeated.");
    frame.LeftButtonDown = false;
    Require(input.Update(frame) == Editor::VoxelPencilInputDecision::None,
        "Pencil release produced an operation.");
    frame.LeftButtonDown = true;
    Require(input.Update(frame) == Editor::VoxelPencilInputDecision::Apply,
        "Pencil did not rearm after release.");

    const auto refused = [](auto mutate)
    {
        Editor::VoxelPencilInputController controller;
        auto candidate = AllowedInput();
        mutate(candidate);
        return controller.Update(candidate) ==
            Editor::VoxelPencilInputDecision::None;
    };
    Require(refused([](auto& value) { value.ToolActive = false; }) &&
        refused([](auto& value) { value.HasDocument = false; }) &&
        refused([](auto& value) { value.ViewportHovered = false; }) &&
        refused([](auto& value) { value.ViewportFocused = false; }) &&
        refused([](auto& value) { value.InterfaceCapturedMouse = true; }) &&
        refused([](auto& value) { value.PopupOpen = true; }) &&
        refused([](auto& value) { value.DragDropActive = true; }) &&
        refused([](auto& value) {
            value.CameraInteraction = Editor::VoxelCameraInteraction::Orbit;
        }) &&
        refused([](auto& value) {
            value.CameraInteraction = Editor::VoxelCameraInteraction::Pan;
        }) &&
        refused([](auto& value) {
            value.CameraInteraction = Editor::VoxelCameraInteraction::Zoom;
        }) &&
        refused([](auto& value) { value.EditInProgress = true; }),
        "Pencil input authorization accepted a blocked context.");

    Editor::VoxelPencilInputController camera;
    auto neutral = AllowedInput();
    neutral.LeftButtonDown = false;
    neutral.CameraInteraction = Editor::VoxelCameraInteraction::Orbit;
    static_cast<void>(camera.Update(neutral));
    auto immediate = AllowedInput();
    Require(camera.Update(immediate) == Editor::VoxelPencilInputDecision::None,
        "Click immediately after camera interaction was not blocked.");
    immediate.LeftButtonDown = false;
    static_cast<void>(camera.Update(immediate));
    immediate.LeftButtonDown = true;
    Require(camera.Update(immediate) == Editor::VoxelPencilInputDecision::Apply,
        "Pencil did not rearm after a neutral camera release.");

    Editor::VoxelPencilInputController documentChange;
    auto pressed = AllowedInput();
    Require(documentChange.Update(pressed) ==
        Editor::VoxelPencilInputDecision::Apply,
        "Initial document click failed.");
    pressed.DocumentGeneration = 5U;
    Require(documentChange.Update(pressed) ==
        Editor::VoxelPencilInputDecision::None,
        "Document replacement repeated a held click.");
    pressed.HasDocument = false;
    Require(documentChange.Update(pressed) ==
        Editor::VoxelPencilInputDecision::None,
        "Document close repeated a held click.");
    documentChange.Reset();
    Require(!documentChange.WaitingForRelease() &&
        !documentChange.CameraRearmRequired(),
        "Pencil input did not reset on project close.");
}

void TestToolState()
{
    Editor::VoxelToolState state;
    Require(state.IsPencilActive(),
        "Pencil should be the default editing tool.");
    state.SetActiveTool(Editor::ActiveVoxelTool::None);
    Require(!state.IsPencilActive(), "Pencil tool did not deactivate.");
    state.Reset();
    Require(state.IsPencilActive(),
        "Pencil tool state did not reset.");
}

void TestPlanStalenessRejectsWithoutHistory()
{
    auto document = Document({Model({4U, 4U, 4U}, {{1U, 1U, 1U, 1U}})});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    TestPencilContext freshRequest = Context(session, document,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveX));
    freshRequest.History = &history;
    Editor::VoxelPencilContext fresh = freshRequest;

    Editor::VoxelPencilContext staleGeneration = fresh;
    ++staleGeneration.Execution.SourceGeneration;
    Require(Editor::VoxelPencilTool::Apply(staleGeneration).Code ==
            Editor::VoxelToolResultCode::Failed && history.UndoCount() == 0U,
        "A stale generation created a history entry.");

    Editor::VoxelPencilContext staleSubModel = fresh;
    staleSubModel.Execution.SubModelIndex = 1U;
    Require(Editor::VoxelPencilTool::Apply(staleSubModel).Code ==
            Editor::VoxelToolResultCode::Failed && history.UndoCount() == 0U,
        "A stale sub-model created a history entry.");

    auto otherDocument = Document({Model({4U, 4U, 4U}, {})});
    Editor::VoxelPencilContext staleIdentity = fresh;
    staleIdentity.Execution.Document = &otherDocument;
    Require(Editor::VoxelPencilTool::Apply(staleIdentity).Code ==
            Editor::VoxelToolResultCode::Failed && history.UndoCount() == 0U,
        "A stale document identity created a history entry.");

    Require(static_cast<bool>(document.SetVoxel({0, 0, 0}, 2U)),
        "Unable to advance the test document revision.");
    Require(Editor::VoxelPencilTool::Apply(fresh).Code ==
            Editor::VoxelToolResultCode::Failed && history.UndoCount() == 0U,
        "A stale revision created a history entry.");
}
}

int main()
{
    try
    {
        TestToolApplicationAndSynchronization();
        TestToolRefusals();
        TestRollbackAndMultiModel();
        TestDocumentOnlyCanonicalEdits();
        TestPreview();
        TestBrushGenerationAndPreview();
        TestAtomicBrushHistory();
        TestSurfaceAnchoredBrushes();
        TestSmartEraseBrushHistoryAndPreview();
        TestInputController();
        TestToolState();
        TestPlanStalenessRejectsWithoutHistory();
        std::cout << "Voxel Pencil tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Pencil tests failed: " << exception.what() << '\n';
        return 1;
    }
}
