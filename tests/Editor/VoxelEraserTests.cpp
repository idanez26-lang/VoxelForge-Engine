#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelPencilInput.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
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
        source, "voxel-eraser-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Eraser test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("Eraser test model");
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
    const Editor::VoxelHitFace face = Editor::VoxelHitFace::PositiveZ,
    const std::size_t modelIndex = 0U)
{
    Editor::VoxelRaycastHit result;
    result.Coordinates = {x, y, z};
    result.Face = face;
    result.SubModelIndex = modelIndex;
    result.DocumentRevision = 0U;
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
            gpuBufferActive_ = cache_.Mesh() != nullptr &&
                !cache_.Mesh()->Empty();
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

Editor::VoxelEraserContext Context(
    TestEditSession& session,
    Asset::Voxel::VoxelDocument& document,
    const Editor::VoxelRaycastHit hit,
    const std::size_t modelIndex = 0U)
{
    return {&session, &document, modelIndex, hit, false};
}

void TestValidRemovalAndRendering()
{
    auto document = Document({Model(
        {3U, 3U, 3U},
        {{0U, 1U, 1U, 7U}, {1U, 1U, 1U, 8U}, {2U, 1U, 1U, 9U}})});
    TestEditSession session(document);
    Require(session.PrimeMesh(), "Initial Eraser mesh build failed.");
    const std::size_t initialVertices = session.cache_.Mesh()->VertexCount();
    const std::size_t initialIndices = session.cache_.Mesh()->IndexCount();
    const std::size_t builds = session.cache_.BuildCount();
    const std::size_t uploads = session.gpuUploads_;
    const std::uint64_t revision = document.GetRevision();

    const Editor::VoxelEraserResult result = Editor::VoxelEraserTool::Apply(
        Context(session, document, Hit(0U, 1U, 1U)));
    const auto bounds = document.GetBounds();
    Require(result.Code == Editor::VoxelEraserResultCode::Applied &&
        result.Changed && result.Position == Asset::Voxel::VoxelPosition{0, 1, 1} &&
        result.RemovedPaletteIndex == 7U && result.SubModelIndex == 0U &&
        result.RevisionBefore == revision &&
        result.RevisionAfter == revision + 1U && result.Error.empty(),
        "Eraser returned an incorrect structured result.");
    Require(!document.HasVoxel({0, 1, 1}) && document.GetVoxelCount() == 2U &&
        document.IsDirty() && document.GetRevision() == revision + 1U &&
        bounds && bounds->HasValue && bounds->Minimum.X == 1 &&
        !session.model_.GetGrid(0U)->Get(0U, 1U, 1U)->IsOccupied(),
        "Eraser did not update document, bounds, and compatibility grid.");
    Require(session.cache_.BuildCount() == builds + 1U &&
        session.gpuUploads_ == uploads + 1U &&
        session.releasedGpuBuffers_ == 1U && session.gpuBufferActive_ &&
        session.completedEdits_ == 1U &&
        (session.cache_.Mesh()->VertexCount() != initialVertices ||
         session.cache_.Mesh()->IndexCount() != initialIndices),
        "Eraser did not replace the synchronized mesh exactly once.");
}

void TestInteriorAndLastVoxel()
{
    auto interior = Document({Model(
        {3U, 3U, 3U},
        {{0U, 1U, 1U, 1U}, {1U, 1U, 1U, 2U}, {2U, 1U, 1U, 3U}})});
    TestEditSession interiorSession(interior);
    const auto beforeBounds = interior.GetBounds();
    Require(Editor::VoxelEraserTool::Apply(Context(
        interiorSession, interior, Hit(1U, 1U, 1U))).Code ==
            Editor::VoxelEraserResultCode::Applied &&
        interior.GetBounds() == beforeBounds,
        "Removing an interior voxel changed the bounds.");

    auto last = Document({Model({3U, 3U, 3U}, {{1U, 1U, 1U, 12U}})});
    TestEditSession lastSession(last);
    Require(lastSession.PrimeMesh(), "Last-voxel mesh setup failed.");
    const std::size_t builds = lastSession.cache_.BuildCount();
    const auto result = Editor::VoxelEraserTool::Apply(
        Context(lastSession, last, Hit(1U, 1U, 1U)));
    const auto bounds = last.GetBounds();
    Require(result.Code == Editor::VoxelEraserResultCode::Applied &&
        result.RemovedPaletteIndex == 12U && last.GetVoxelCount() == 0U &&
        bounds && !bounds->HasValue && lastSession.cache_.BuildCount() ==
            builds + 1U && lastSession.cache_.Mesh() != nullptr &&
        lastSession.cache_.Mesh()->Empty() && !lastSession.gpuBufferActive_ &&
        lastSession.releasedGpuBuffers_ == 1U,
        "Removing the last voxel did not produce an empty stable mesh.");
}

void TestRefusalsAndRollback()
{
    auto document = Document({Model(
        {3U, 3U, 3U}, {{1U, 1U, 1U, 5U}})});
    TestEditSession session(document);
    const auto initialBounds = document.GetBounds();
    const auto initialCount = document.GetVoxelCount();
    const auto initialRevision = document.GetRevision();
    const auto validHit = Hit(1U, 1U, 1U);

    auto noDocument = Context(session, document, validHit);
    noDocument.Document = nullptr;
    Require(Editor::VoxelEraserTool::Apply(noDocument).Code ==
        Editor::VoxelEraserResultCode::NoDocument,
        "Missing document was not refused.");
    auto noHit = Context(session, document, validHit);
    noHit.Hit.reset();
    Require(Editor::VoxelEraserTool::Apply(noHit).Code ==
        Editor::VoxelEraserResultCode::NoHit,
        "Missing hit was not refused.");
    Require(Editor::VoxelEraserTool::Apply(Context(
        session, document,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::None))).Code ==
            Editor::VoxelEraserResultCode::NoHit,
        "Face None was not refused.");
    Require(Editor::VoxelEraserTool::Apply(Context(
        session, document, Hit(0U, 0U, 0U))).Code ==
            Editor::VoxelEraserResultCode::TargetMissing,
        "Missing target was not refused.");
    Require(Editor::VoxelEraserTool::Apply(Context(
        session, document, Hit(1U, 1U, 1U,
            Editor::VoxelHitFace::PositiveZ, 1U), 1U)).Code ==
            Editor::VoxelEraserResultCode::InvalidModel,
        "Invalid sub-model was not refused.");
    auto blocked = Context(session, document, validHit);
    blocked.Blocked = true;
    Require(Editor::VoxelEraserTool::Apply(blocked).Code ==
        Editor::VoxelEraserResultCode::Blocked,
        "Blocked Eraser was not refused.");
    Require(document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision && !document.IsDirty() &&
        document.GetBounds() == initialBounds && session.rebuildAttempts_ == 0U,
        "A refused Eraser operation changed or rebuilt the document.");

    auto staleDocument = Document({Model(
        {3U, 3U, 3U}, {{1U, 1U, 1U, 5U}})});
    TestEditSession staleSession(staleDocument);
    const auto paletteChange = staleDocument.SetPaletteColor(
        1U, {1U, 2U, 3U, 255U});
    Require(paletteChange && Editor::VoxelEraserTool::Apply(Context(
        staleSession, staleDocument, validHit)).Code ==
            Editor::VoxelEraserResultCode::TargetMissing &&
        staleDocument.HasVoxel({1, 1, 1}),
        "A hit from an older document revision was not refused.");

    session.failRebuild_ = true;
    const auto failed = Editor::VoxelEraserTool::Apply(
        Context(session, document, validHit));
    Require(failed.Code == Editor::VoxelEraserResultCode::Failed &&
        !failed.Changed && document.HasVoxel({1, 1, 1}) &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 5U &&
        document.GetVoxelCount() == initialCount &&
        document.GetRevision() == initialRevision && !document.IsDirty() &&
        document.GetBounds() == initialBounds &&
        session.model_.GetGrid(0U)->Get(1U, 1U, 1U)->IsOccupied(),
        "Failed Eraser rebuild did not roll back every document field.");
}

void TestMultiModelAndGridDivergence()
{
    auto document = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 1U}}),
        Model({3U, 3U, 3U}, {{2U, 1U, 1U, 22U}})});
    TestEditSession session(document);
    const auto result = Editor::VoxelEraserTool::Apply(Context(
        session, document,
        Hit(2U, 1U, 1U, Editor::VoxelHitFace::PositiveX, 1U), 1U));
    Require(result.Code == Editor::VoxelEraserResultCode::Applied &&
        result.RemovedPaletteIndex == 22U && result.SubModelIndex == 1U &&
        !document.HasVoxel({2, 1, 1}, 1U) &&
        document.HasVoxel({1, 1, 1}, 0U),
        "Eraser modified the wrong sub-model.");

    auto diverged = Document({Model(
        {3U, 3U, 3U}, {{1U, 1U, 1U, 9U}})});
    TestEditSession divergedSession(diverged);
    Require(divergedSession.model_.GetGrid(0U)->Set(
        1U, 1U, 1U, Voxel::Voxel{}), "Unable to diverge test grid.");
    Require(Editor::VoxelEraserTool::Apply(Context(
        divergedSession, diverged, Hit(1U, 1U, 1U))).Code ==
            Editor::VoxelEraserResultCode::Failed &&
        diverged.HasVoxel({1, 1, 1}) && diverged.GetRevision() == 0U,
        "Document/grid divergence was not rejected atomically.");
}

Editor::VoxelToolInputFrame AllowedInput()
{
    Editor::VoxelToolInputFrame frame;
    frame.LeftButtonDown = true;
    frame.ToolActive = true;
    frame.HasDocument = true;
    frame.ViewportHovered = true;
    frame.ViewportFocused = true;
    frame.DocumentGeneration = 8U;
    return frame;
}

void TestSharedInputAndShortcuts()
{
    Editor::VoxelToolInputController input;
    auto frame = AllowedInput();
    Require(input.Update(frame) == Editor::VoxelToolInputDecision::Apply &&
        input.Update(frame) == Editor::VoxelToolInputDecision::None,
        "Held Eraser click repeated.");
    frame.LeftButtonDown = false;
    static_cast<void>(input.Update(frame));
    frame.LeftButtonDown = true;
    Require(input.Update(frame) == Editor::VoxelToolInputDecision::Apply,
        "Eraser did not rearm after release.");

    const auto refused = [](auto mutate)
    {
        Editor::VoxelToolInputController controller;
        auto candidate = AllowedInput();
        mutate(candidate);
        return controller.Update(candidate) == Editor::VoxelToolInputDecision::None;
    };
    Require(refused([](auto& value) { value.ToolActive = false; }) &&
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
        "Shared input accepted a blocked Eraser context.");

    Editor::VoxelToolInputController camera;
    auto cameraFrame = AllowedInput();
    cameraFrame.LeftButtonDown = false;
    cameraFrame.CameraInteraction = Editor::VoxelCameraInteraction::Orbit;
    static_cast<void>(camera.Update(cameraFrame));
    auto immediate = AllowedInput();
    Require(camera.Update(immediate) == Editor::VoxelToolInputDecision::None,
        "Post-camera Eraser click was not neutralized.");
    immediate.LeftButtonDown = false;
    static_cast<void>(camera.Update(immediate));
    immediate.LeftButtonDown = true;
    Require(camera.Update(immediate) == Editor::VoxelToolInputDecision::Apply,
        "Eraser did not rearm after camera release.");

    Editor::VoxelToolInputController changed;
    auto changedFrame = AllowedInput();
    static_cast<void>(changed.Update(changedFrame));
    changedFrame.DocumentGeneration = 9U;
    Require(changed.Update(changedFrame) == Editor::VoxelToolInputDecision::None,
        "Document replacement repeated a held Eraser click.");
    changedFrame.HasDocument = false;
    Require(changed.Update(changedFrame) == Editor::VoxelToolInputDecision::None,
        "Document close repeated a held Eraser click.");
    changed.Reset();
    Require(!changed.WaitingForRelease(),
        "Shared input did not reset on project close.");

    Require(Editor::ResolveVoxelToolShortcut(
        Editor::ActiveVoxelTool::Pencil, false, true, true) ==
            Editor::ActiveVoxelTool::Eraser &&
        Editor::ResolveVoxelToolShortcut(
            Editor::ActiveVoxelTool::Eraser, false, true, true) ==
            Editor::ActiveVoxelTool::None &&
        Editor::ResolveVoxelToolShortcut(
            Editor::ActiveVoxelTool::Pencil, false, true, false) ==
            Editor::ActiveVoxelTool::Pencil,
        "Eraser shortcut or text-input protection is incorrect.");
}

void TestPreviewAndToolState()
{
    auto document = Document({Model(
        {3U, 3U, 3U}, {{1U, 1U, 1U, 4U}})});
    const auto hit = Hit(1U, 1U, 1U);
    const auto valid = Editor::EvaluateVoxelEraserPreview(
        &document, 0U, hit, true);
    Require(valid.IsVisible() && valid.IsValid() &&
        valid.Position == Asset::Voxel::VoxelPosition{1, 1, 1} &&
        valid.Tool == Editor::VoxelPreviewTool::Eraser &&
        !Editor::EvaluateVoxelEraserPreview(
            &document, 0U, std::nullopt, true).IsVisible() &&
        !Editor::EvaluateVoxelEraserPreview(
            &document, 0U, hit, false).IsVisible() &&
        !Editor::EvaluateVoxelEraserPreview(
            nullptr, 0U, hit, true).IsVisible() &&
        Editor::EvaluateVoxelEraserPreview(
            &document, 0U, hit, true, true).Status ==
                Editor::VoxelPlacementPreviewStatus::Blocked,
        "Eraser preview activation and blocked states are incorrect.");
    const auto removed = document.RemoveVoxel({1, 1, 1});
    Require(removed && Editor::EvaluateVoxelEraserPreview(
        &document, 0U, hit, true).Status ==
            Editor::VoxelPlacementPreviewStatus::TargetMissing &&
        !Editor::EvaluateVoxelEraserPreview(
            &document, 0U, hit, true).IsVisible(),
        "Eraser preview did not clear after target removal/revision change.");

    Editor::VoxelToolState state;
    state.SetActiveTool(Editor::ActiveVoxelTool::Eraser);
    Require(state.IsEraserActive() && state.IsEditingToolActive() &&
        !state.IsPencilActive() &&
        Editor::ActiveVoxelToolName(state.ActiveTool()) ==
            std::string_view("Eraser"),
        "Eraser tool state is incorrect.");
}
}

int main()
{
    try
    {
        TestValidRemovalAndRendering();
        TestInteriorAndLastVoxel();
        TestRefusalsAndRollback();
        TestMultiModelAndGridDivergence();
        TestSharedInputAndShortcuts();
        TestPreviewAndToolState();
        std::cout << "Voxel Eraser tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Eraser tests failed: " << exception.what() << '\n';
        return 1;
    }
}
