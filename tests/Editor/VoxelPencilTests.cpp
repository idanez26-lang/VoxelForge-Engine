#include "VoxelTools/VoxelPencilInput.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"
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

Editor::VoxelPencilContext Context(
    TestEditSession& session,
    Asset::Voxel::VoxelDocument& document,
    const Editor::VoxelRaycastHit hit,
    const std::size_t paletteIndex = 1U,
    const std::size_t modelIndex = 0U)
{
    return {&session, &document, modelIndex, hit, paletteIndex, false};
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
    noDocument.Document = nullptr;
    Require(Editor::VoxelPencilTool::Apply(noDocument).Code ==
        Editor::VoxelToolResultCode::NoDocument,
        "Missing document was not refused.");
    Editor::VoxelPencilContext noHit = Context(session, document, validHit);
    noHit.Hit.reset();
    Require(Editor::VoxelPencilTool::Apply(noHit).Code ==
        Editor::VoxelToolResultCode::NoHit,
        "Missing hit was not refused.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document, Hit(1U, 1U, 1U, Editor::VoxelHitFace::None))).Code ==
            Editor::VoxelToolResultCode::NoHit,
        "Face None was not refused.");
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
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document, validHit, 0U)).Code ==
            Editor::VoxelToolResultCode::InvalidPaletteIndex &&
        Editor::VoxelPencilTool::Apply(Context(
            session, document, validHit, 256U)).Code ==
            Editor::VoxelToolResultCode::InvalidPaletteIndex,
        "Invalid palette indices were not refused.");
    Require(Editor::VoxelPencilTool::Apply(Context(
        session, document,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::NegativeY, 1U),
        1U, 1U)).Code == Editor::VoxelToolResultCode::InvalidModel,
        "Invalid sub-model was not refused.");
    Editor::VoxelPencilContext blocked = Context(session, document, validHit);
    blocked.Blocked = true;
    Require(Editor::VoxelPencilTool::Apply(blocked).Code ==
        Editor::VoxelToolResultCode::Blocked,
        "Blocked edit was not refused.");
    Editor::VoxelPencilContext inconsistent =
        Context(session, document, validHit);
    inconsistent.Hit->AdjacentPosition = {2, 2, 2};
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

void TestPreview()
{
    auto document = Document({Model(
        {3U, 3U, 3U},
        {{0U, 1U, 1U, 2U}, {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}})});
    const auto validHit = Hit(
        1U, 1U, 1U, Editor::VoxelHitFace::PositiveY);
    const auto valid = Editor::EvaluateVoxelPencilPreview(
        &document, 0U, validHit, true);
    const auto occupied = Editor::EvaluateVoxelPencilPreview(
        &document, 0U,
        Hit(1U, 1U, 1U, Editor::VoxelHitFace::PositiveX), true);
    const auto outside = Editor::EvaluateVoxelPencilPreview(
        &document, 0U,
        Hit(0U, 1U, 1U, Editor::VoxelHitFace::NegativeX), true);
    Require(valid.IsVisible() && valid.IsValid() &&
        valid.Position == Asset::Voxel::VoxelPosition{1, 2, 1} &&
        occupied.Status == Editor::VoxelPlacementPreviewStatus::Occupied &&
        outside.Status == Editor::VoxelPlacementPreviewStatus::OutOfBounds &&
        !Editor::EvaluateVoxelPencilPreview(
            &document, 0U, std::nullopt, true).IsVisible() &&
        !Editor::EvaluateVoxelPencilPreview(
            &document, 0U, validHit, false).IsVisible() &&
        !Editor::EvaluateVoxelPencilPreview(
            nullptr, 0U, validHit, true).IsVisible(),
        "Placement preview statuses are incorrect.");
    const auto changed = document.SetVoxel({1, 2, 1}, 5U);
    Require(changed && Editor::EvaluateVoxelPencilPreview(
        &document, 0U, validHit, true).Status ==
            Editor::VoxelPlacementPreviewStatus::Occupied,
        "Placement preview did not follow document revision changes.");
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
    Require(state.IsPencilActive() &&
        state.ActivePaletteIndex() == Editor::VoxelToolState::DefaultPaletteIndex &&
        !state.SetActivePaletteIndex(0U) &&
        !state.SetActivePaletteIndex(256U) &&
        state.SetActivePaletteIndex(255U) &&
        state.ActivePaletteIndex() == 255U,
        "Centralized Pencil state or palette validation is incorrect.");
    state.SetActiveTool(Editor::ActiveVoxelTool::None);
    Require(!state.IsPencilActive(), "Pencil tool did not deactivate.");
    state.Reset();
    Require(state.IsPencilActive() && state.ActivePaletteIndex() == 1U,
        "Pencil tool state did not reset.");
}
}

int main()
{
    try
    {
        TestToolApplicationAndSynchronization();
        TestToolRefusals();
        TestRollbackAndMultiModel();
        TestPreview();
        TestInputController();
        TestToolState();
        std::cout << "Voxel Pencil tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Pencil tests failed: " << exception.what() << '\n';
        return 1;
    }
}
