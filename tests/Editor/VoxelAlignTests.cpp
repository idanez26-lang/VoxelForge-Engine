#include "Transform/AlignVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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

Asset::Voxel::VoxelDocument MakeDocument(const bool withObstacle = false)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {8U, 8U, 8U};
    model.Voxels = {{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U}};
    if (withObstacle) model.Voxels.push_back({6U, 1U, 1U, 9U});
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-align-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Align test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Align test model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Align compatibility grid.");
    model->ForEachVoxel([&grid](const auto position, const auto voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Align compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return generation_;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        savedState_ = saved;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 71U;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
    bool savedState_ = true;
};

Editor::SelectionService MakeSelection(const std::uint64_t generation)
{
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(generation);
    const std::array<Asset::Voxel::VoxelPosition, 3U> positions{{
        {1, 1, 1}, {2, 1, 1}, {3, 1, 1}}};
    Require(selection.ApplySortedVolume(positions,
        Editor::SelectionBounds::FromCorners({1, 1, 1}, {3, 1, 1}),
        Editor::SelectionMode::Replace),
        "Unable to select Align voxels.");
    return selection;
}

Editor::MoveVoxelSelectionResult Prepare(
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    Editor::TransformPreviewModel& preview,
    const Editor::VoxelAlignDirection direction,
    const std::uint64_t generation = 71U)
{
    const auto dimensions = document.GetDimensions(0U);
    const auto delta = dimensions
        ? Editor::AlignVoxelSelectionOperation::CalculateDelta(
            selection.EditableBounds(), *dimensions, direction)
        : std::nullopt;
    Require(delta.has_value(), "Align delta calculation failed.");
    Require(preview.BeginPreview(document, selection, generation, 0U,
        Editor::TransformPreviewCollisionPolicy::IgnoreSource),
        "Align preview capture failed.");
    if (*delta != Asset::Voxel::VoxelPosition{})
        Require(preview.SetDelta(document, selection, generation, *delta),
            "Align preview delta failed.");
    return Editor::AlignVoxelSelectionOperation::Build(
        document, selection, generation, preview);
}

void ApplySelectionTransition(Editor::SelectionService& selection,
    const Editor::VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "Align history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

void TestSixDirectionDeltas()
{
    const auto bounds = Editor::SelectionBounds::FromCorners(
        {2, 3, 4}, {5, 7, 9});
    const Asset::Voxel::VoxelDimensions dimensions{16U, 16U, 16U};
    Require(Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Left) ==
            Asset::Voxel::VoxelPosition{-2, 0, 0} &&
        Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Right) ==
            Asset::Voxel::VoxelPosition{10, 0, 0} &&
        Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Bottom) ==
            Asset::Voxel::VoxelPosition{0, -3, 0} &&
        Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Top) ==
            Asset::Voxel::VoxelPosition{0, 8, 0} &&
        Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Front) ==
            Asset::Voxel::VoxelPosition{0, 0, -4} &&
        Editor::AlignVoxelSelectionOperation::CalculateDelta(
                bounds, dimensions, Editor::VoxelAlignDirection::Back) ==
            Asset::Voxel::VoxelPosition{0, 0, 6},
        "One or more Align deltas are incorrect.");
    Require(!Editor::AlignVoxelSelectionOperation::CalculateDelta(
                {}, dimensions, Editor::VoxelAlignDirection::Left) &&
        !Editor::AlignVoxelSelectionOperation::CalculateDelta(
            bounds, {}, Editor::VoxelAlignDirection::Left) &&
        !Editor::AlignVoxelSelectionOperation::CalculateDelta(
            Editor::SelectionBounds::FromCorners({-1, 0, 0}, {0, 0, 0}),
            dimensions, Editor::VoxelAlignDirection::Left),
        "Align accepted invalid bounds or dimensions.");
}

void TestDirectionsPreviewAndRefusals()
{
    constexpr Editor::VoxelAlignDirection directions[] = {
        Editor::VoxelAlignDirection::Left,
        Editor::VoxelAlignDirection::Right,
        Editor::VoxelAlignDirection::Bottom,
        Editor::VoxelAlignDirection::Top,
        Editor::VoxelAlignDirection::Front,
        Editor::VoxelAlignDirection::Back};
    for (const auto direction : directions)
    {
        auto document = MakeDocument();
        document.MarkSaved();
        auto selection = MakeSelection(71U);
        Editor::TransformPreviewModel preview;
        const std::uint64_t revision = document.GetRevision();
        const auto prepared = Prepare(document, selection, preview, direction);
        Require((prepared.Ready() ||
                    prepared.Code ==
                        Editor::MoveVoxelSelectionResultCode::NoChange) &&
            document.GetRevision() == revision && !document.IsDirty(),
            "Align preview mutated the document or rejected a direction.");
    }

    auto collisionDocument = MakeDocument(true);
    collisionDocument.MarkSaved();
    auto collisionSelection = MakeSelection(71U);
    Editor::TransformPreviewModel collisionPreview;
    const auto collision = Prepare(collisionDocument, collisionSelection,
        collisionPreview, Editor::VoxelAlignDirection::Right);
    // Overlap/Merge (decision produit) : Align est un Move contraint et herite
    // de la fusion — recouvrement signale, commit pret, aucune mutation.
    Require(collision.Ready() && collisionPreview.HasCollisions() &&
        !collisionDocument.IsDirty(),
        "Align did not reuse Move overlap merge (reported, ready, non-mutating).");

    auto outDocument = MakeDocument();
    outDocument.MarkSaved();
    auto outSelection = MakeSelection(71U);
    Editor::TransformPreviewModel outPreview;
    Require(outPreview.BeginPreview(outDocument, outSelection, 71U) &&
        outPreview.SetDelta(outDocument, outSelection, 71U, {-2, 0, 0}),
        "Unable to build out-of-bounds Align validation preview.");
    const auto outside = Editor::AlignVoxelSelectionOperation::Build(
        outDocument, outSelection, 71U, outPreview);
    Require(outside.Code ==
            Editor::MoveVoxelSelectionResultCode::OutOfBounds &&
        !outDocument.IsDirty(),
        "Align bypassed Transform Framework out-of-bounds rejection.");
}

void TestAtomicApplyUndoRedoAndPersistence()
{
    auto document = MakeDocument();
    document.MarkSaved();
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    auto selection = MakeSelection(session.generation_);
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelAlignDirection::Right, session.generation_);
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 6U,
        "Align did not prepare one atomic Move operation.");
    const std::uint64_t revision = document.GetRevision();
    auto applied = history.Execute(session, std::move(prepared.Operation));
    ApplySelectionTransition(selection, applied);
    Require(applied && history.UndoCount() == 1U &&
        document.GetRevision() == revision + 1U &&
        session.rebuildCount_ == 1U && session.completedCount_ == 1U &&
        !document.HasVoxel({1, 1, 1}) &&
        document.GetVoxel({5, 1, 1})->PaletteIndex == 3U &&
        document.GetVoxel({6, 1, 1})->PaletteIndex == 4U &&
        document.GetVoxel({7, 1, 1})->PaletteIndex == 5U &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({5, 1, 1}, {7, 1, 1}),
        "Align was not applied atomically with one revision/rebuild.");

    auto undone = history.Undo(session);
    ApplySelectionTransition(selection, undone);
    Require(undone && document.HasVoxel({1, 1, 1}) &&
        !document.HasVoxel({7, 1, 1}) &&
        selection.EditableBounds() ==
            Editor::SelectionBounds::FromCorners({1, 1, 1}, {3, 1, 1}),
        "Align Undo did not restore source voxels and selection.");
    auto redone = history.Redo(session);
    ApplySelectionTransition(selection, redone);
    Require(redone && document.HasVoxel({7, 1, 1}) &&
        !document.HasVoxel({1, 1, 1}),
        "Align Redo did not restore the destination.");

    const auto serialized = Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "Aligned document could not be serialized.");
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("VoxelForgeAlign-" + std::to_string(stamp) + ".vox");
    struct TemporaryFile final
    {
        std::filesystem::path Path;
        ~TemporaryFile()
        {
            std::error_code error;
            std::filesystem::remove(Path, error);
        }
    } temporary{path};
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        Require(stream.is_open(), "Unable to create temporary Align VOX file.");
        stream.write(reinterpret_cast<const char*>(serialized.Bytes.data()),
            static_cast<std::streamsize>(serialized.Bytes.size()));
        Require(stream.good(), "Unable to persist aligned VOX bytes.");
    }
    const auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(reopened.Succeeded() && reopened.Document &&
        reopened.Document->GetVoxelCount() == 3U &&
        reopened.Document->HasVoxel({5, 1, 1}) &&
        reopened.Document->HasVoxel({7, 1, 1}),
        "Save/reopen lost the aligned selection.");
}

void TestLargeSelection()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(512U);
    for (std::uint32_t z = 1U; z <= 8U; ++z)
        for (std::uint32_t y = 1U; y <= 8U; ++y)
            for (std::uint32_t x = 1U; x <= 8U; ++x)
            {
                model.Voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z), 12U});
                positions.push_back({static_cast<std::int32_t>(x),
                    static_cast<std::int32_t>(y),
                    static_cast<std::int32_t>(z)});
            }
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-align-large-memory.vox");
    Require(loaded.Succeeded(), "Unable to build large Align document.");
    auto document = std::move(*loaded.Document);
    TestSession session(document);
    Editor::VoxelEditHistory history;
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(session.generation_);
    Require(selection.ApplySortedVolume(positions,
        Editor::SelectionBounds::FromCorners({1, 1, 1}, {8, 8, 8}),
        Editor::SelectionMode::Replace),
        "Unable to select large Align volume.");
    Editor::TransformPreviewModel preview;
    auto prepared = Prepare(document, selection, preview,
        Editor::VoxelAlignDirection::Top, session.generation_);
    Require(prepared.Ready() && prepared.Operation.Changes.size() == 1024U,
        "Large Align did not reuse the linear atomic Move pipeline.");
    const auto applied = history.Execute(session, std::move(prepared.Operation));
    Require(applied && document.GetVoxelCount() == 512U &&
        document.HasVoxel({1, 63, 1}) && document.HasVoxel({8, 56, 8}) &&
        history.UndoCount() == 1U,
        "Large Align was not applied as one operation.");
}
}

int main()
{
    try
    {
        TestSixDirectionDeltas();
        TestDirectionsPreviewAndRefusals();
        TestAtomicApplyUndoRedoAndPersistence();
        TestLargeSelection();
        std::cout << "Voxel Align tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Align tests failed: " << exception.what() << '\n';
        return 1;
    }
}
