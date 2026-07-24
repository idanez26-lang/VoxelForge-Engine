#include "SmartTools/SmartBrushPreviewResolver.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPaintBrushTool.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;
using namespace Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument Document(std::vector<Asset::Vox::VoxVoxel> voxels)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{4U, 4U, 4U}, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "professional-ghost-preview-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Ghost Preview test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Ghost Preview test document has no model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the Ghost Preview compatibility grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize the Ghost Preview compatibility grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class TestEditSession final : public VoxelEditSession
{
public:
    explicit TestEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document))
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override { return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override {}

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

VoxelRaycastHit Hit(const Position position,
    const VoxelHitFace face, const std::uint64_t revision)
{
    VoxelRaycastHit hit;
    hit.Coordinates = {static_cast<std::uint32_t>(position.X),
        static_cast<std::uint32_t>(position.Y),
        static_cast<std::uint32_t>(position.Z)};
    hit.Face = face;
    hit.SubModelIndex = 0U;
    hit.DocumentRevision = revision;
    const Position normal = VoxelHitFaceIntegerNormal(face);
    hit.AdjacentPosition = {position.X + normal.X, position.Y + normal.Y,
        position.Z + normal.Z};
    return hit;
}

std::vector<Position> OccupiedPositions(const Asset::Voxel::VoxelDocument& document)
{
    std::vector<Position> positions;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Missing model while inspecting applied coordinates.");
    model->ForEachVoxel([&positions](const Position position, const auto)
    {
        positions.push_back(position);
    });
    return positions;
}

std::vector<Position> Difference(
    const std::vector<Position>& left, const std::vector<Position>& right)
{
    std::vector<Position> result;
    for (const Position position : left)
        if (std::find(right.begin(), right.end(), position) == right.end())
            result.push_back(position);
    return result;
}

SmartBrushPreviewResult Resolve(const Asset::Voxel::VoxelDocument& document,
    SmartBrushState state, const Position target = {1, 1, 1},
    const float alpha = 0.5F)
{
    return SmartBrushPreviewResolver::Resolve({&document, 0U, state,
        {target, {0, 0, 0}}, {0.15F, 0.45F, 0.90F, 1.0F}, alpha});
}

const GhostVoxel& OnlyGhost(const SmartBrushPreviewResult& preview)
{
    Require(preview.GhostVoxels.size() == 1U,
        "Expected exactly one Ghost Preview voxel.");
    return preview.GhostVoxels.front();
}

void RequireUnique(const std::vector<GhostVoxel>& ghosts)
{
    for (std::size_t first = 0U; first < ghosts.size(); ++first)
        for (std::size_t second = first + 1U;
             second < ghosts.size(); ++second)
            Require(ghosts[first].Position != ghosts[second].Position,
                "Ghost Preview duplicated a Smart Brush coordinate.");
}

void TestStatesColorsAndAffectedCoordinates()
{
    const auto document = Document({{1U, 1U, 1U, 3U}});
    SmartBrushState state;
    state.Size = 1;
    state.PaletteIndex = 7U;

    state.Mode = SmartBrushMode::Add;
    const auto add = Resolve(document, state);
    Require(OnlyGhost(add).State == GhostVoxelState::Ignored &&
        OnlyGhost(add).Color == GhostPreviewStyle::Ignored &&
        add.AffectedPositions.empty() && add.Statistics.IsConsistent(),
        "Add preview did not mark an existing voxel as ignored.");

    const auto emptyDocument = Document({});
    const auto added = Resolve(emptyDocument, state);
    Require(OnlyGhost(added).State == GhostVoxelState::Added &&
        OnlyGhost(added).Color == GhostPreviewStyle::Added &&
        added.AffectedPositions == std::vector<Position>{{1, 1, 1}},
        "Add preview did not mark an empty voxel as added.");
    const auto clampedHigh = Resolve(emptyDocument, state, {1, 1, 1}, 3.0F);
    const auto clampedLow = Resolve(emptyDocument, state, {1, 1, 1}, -1.0F);
    Require(OnlyGhost(clampedHigh).Alpha == 1.0F &&
        OnlyGhost(clampedLow).Alpha == 0.0F,
        "Ghost Preview alpha was not clamped to [0, 1].");

    state.Mode = SmartBrushMode::Erase;
    const auto erase = Resolve(document, state);
    Require(OnlyGhost(erase).State == GhostVoxelState::Erased &&
        OnlyGhost(erase).Color == GhostPreviewStyle::Erased &&
        erase.AffectedPositions == std::vector<Position>{{1, 1, 1}} &&
        erase.Statistics.IsConsistent(),
        "Erase preview did not mark the existing voxel as affected.");

    state.Mode = SmartBrushMode::Paint;
    const auto paint = Resolve(document, state, {1, 1, 1}, 0.35F);
    Require(OnlyGhost(paint).State == GhostVoxelState::Painted &&
        OnlyGhost(paint).Color == std::array<float, 4>{0.15F, 0.45F, 0.90F, 1.0F} &&
        OnlyGhost(paint).Alpha == 0.35F &&
        paint.AffectedPositions == std::vector<Position>{{1, 1, 1}} &&
        paint.Statistics.IsConsistent(),
        "Paint preview did not use the active palette color and alpha.");

    state.PaletteIndex = 3U;
    const auto sameColor = Resolve(document, state);
    Require(OnlyGhost(sameColor).State == GhostVoxelState::Ignored &&
        sameColor.AffectedPositions.empty() && sameColor.Statistics.IsConsistent(),
        "Paint preview did not ignore a voxel with the active palette color.");
}

void TestEngineParityClippingAndInvalids()
{
    const auto document = Document({{1U, 1U, 1U, 3U}});
    SmartBrushState state;
    state.Size = 3;
    state.Mode = SmartBrushMode::Add;
    const SmartBrushPreviewResult preview = Resolve(document, state, {0, 0, 0});
    const SmartBrushResult engine = SmartBrushEngine::Resolve({{4U, 4U, 4U},
        state, {{0, 0, 0}, {0, 0, 0}}, [&document](const Position position)
        {
            return document.HasVoxel(position, 0U);
        }});
    Require(preview.AffectedPositions == engine.AddablePositions &&
        preview.Statistics.Total == engine.Statistics.Total &&
        preview.Statistics.Clipped == engine.Statistics.Clipped &&
        preview.Statistics.IsConsistent(),
        "Ghost Preview affected coordinates diverge from SmartBrushEngine.");
    Require(static_cast<std::size_t>(std::count_if(preview.GhostVoxels.begin(),
                preview.GhostVoxels.end(), [](const GhostVoxel& ghost)
                {
                    return ghost.State == GhostVoxelState::Clipped;
                })) == engine.ClippedPositions.size(),
        "Clipped Smart Brush coordinates were not preserved for Ghost Preview.");
    for (const GhostVoxel& ghost : preview.GhostVoxels)
        if (ghost.State == GhostVoxelState::Clipped)
            Require(ghost.Color == GhostPreviewStyle::Clipped,
                "Clipped Ghost Preview voxels do not use the dark red color.");
    RequireUnique(preview.GhostVoxels);

    state.Shape = SmartBrushShape::Cylinder;
    const SmartBrushPreviewResult invalid = Resolve(document, state);
    Require(invalid.Code == SmartBrushResultCode::Unsupported &&
        !invalid.GhostVoxels.empty() &&
        invalid.GhostVoxels.back().State == GhostVoxelState::Invalid &&
        invalid.GhostVoxels.back().Color == GhostPreviewStyle::Invalid,
        "An invalid Smart Brush request has no representable invalid ghost.");
}

void TestCacheAndAppliedOperationParity()
{
    SmartBrushState state;
    state.Size = 1;
    state.PaletteIndex = 7U;
    const std::array<float, 4> activeColor{0.20F, 0.50F, 0.80F, 1.0F};

    auto addDocument = Document({{1U, 1U, 1U, 3U}});
    SmartBrushPreviewCache cache;
    const SmartBrushPreviewCacheKey addKey{&addDocument, 0U,
        addDocument.GetRevision(), 9U, state, 0U, {{2, 1, 1}, {0, 1, 0}},
        activeColor, GhostPreviewStyle::DefaultAlpha};
    const SmartBrushPreviewRequest addRequest{&addDocument, 0U, state,
        {{2, 1, 1}, {0, 1, 0}}, activeColor, GhostPreviewStyle::DefaultAlpha};
    const auto& addPreview = cache.Resolve(addKey, addRequest);
    const auto* const cachedPreviewAddress = &addPreview;
    const auto* const repeatedPreviewAddress = &cache.Resolve(addKey, addRequest);
    Require(cache.ResolutionCount() == 1U &&
        repeatedPreviewAddress == cachedPreviewAddress,
        "Ghost cache recalculated or copied an unchanged preview request.");
    SmartBrushPreviewCacheKey revisedAddKey = addKey;
    ++revisedAddKey.DocumentRevision;
    static_cast<void>(cache.Resolve(revisedAddKey, addRequest));
    Require(cache.ResolutionCount() == 2U,
        "Ghost cache did not invalidate after a document revision change.");
    const auto requireCacheInvalidation = [&cache, &addKey, &addRequest](
        const SmartBrushPreviewCacheKey& changed, const std::string_view name)
    {
        static_cast<void>(cache.Resolve(addKey, addRequest));
        const std::size_t resolutionCount = cache.ResolutionCount();
        static_cast<void>(cache.Resolve(changed, addRequest));
        Require(cache.ResolutionCount() == resolutionCount + 1U,
            std::string("Ghost cache did not invalidate for ") + std::string(name));
    };
    SmartBrushPreviewCacheKey changed = addKey;
    changed.State.Size = 2;
    requireCacheInvalidation(changed, "brush size");
    changed = addKey;
    changed.State.Shape = SmartBrushShape::Sphere;
    requireCacheInvalidation(changed, "brush shape");
    changed = addKey;
    changed.GeometryKey = 1U;
    requireCacheInvalidation(changed, "Smart Geometry");
    changed = addKey;
    changed.State.Mode = SmartBrushMode::Erase;
    requireCacheInvalidation(changed, "brush action");
    changed = addKey;
    changed.State.PaletteIndex = 8U;
    requireCacheInvalidation(changed, "palette index");
    changed = addKey;
    changed.ActivePaletteColor[0] = 0.6F;
    requireCacheInvalidation(changed, "palette color");
    changed = addKey;
    changed.Placement.Target.X = 3;
    requireCacheInvalidation(changed, "position");
    changed = addKey;
    changed.State.Orientation = SmartBrushOrientation::X;
    requireCacheInvalidation(changed, "orientation");
    const std::vector<Position> addBefore = OccupiedPositions(addDocument);
    TestEditSession addSession(addDocument);
    VoxelPencilContext addContext{&addSession, &addDocument, 0U, std::nullopt,
        state, false, nullptr, Position{2, 1, 1}};
    Require(VoxelPencilTool::Apply(addContext).Code == VoxelToolResultCode::Applied &&
        Difference(OccupiedPositions(addDocument), addBefore) ==
            addPreview.AffectedPositions,
        "Applied Add coordinates diverge from the Ghost Preview.");

    auto eraseDocument = Document({{1U, 1U, 1U, 3U}});
    state.Mode = SmartBrushMode::Erase;
    const auto erasePreview = SmartBrushPreviewResolver::Resolve({&eraseDocument,
        0U, state, {{1, 1, 1}, {0, 1, 0}}, activeColor,
        GhostPreviewStyle::DefaultAlpha});
    const std::vector<Position> eraseBefore = OccupiedPositions(eraseDocument);
    TestEditSession eraseSession(eraseDocument);
    VoxelPencilContext eraseContext{&eraseSession, &eraseDocument, 0U,
        Hit({1, 1, 1}, VoxelHitFace::PositiveY, eraseDocument.GetRevision()),
        state, false, nullptr, std::nullopt};
    Require(VoxelPencilTool::Apply(eraseContext).Code == VoxelToolResultCode::Applied &&
        Difference(eraseBefore, OccupiedPositions(eraseDocument)) ==
            erasePreview.AffectedPositions,
        "Applied Erase coordinates diverge from the Ghost Preview.");

    auto paintDocument = Document({{1U, 1U, 1U, 3U}});
    state.Mode = SmartBrushMode::Paint;
    const auto paintPreview = SmartBrushPreviewResolver::Resolve({&paintDocument,
        0U, state, {{1, 1, 1}, {}}, activeColor,
        GhostPreviewStyle::DefaultAlpha});
    TestEditSession paintSession(paintDocument);
    VoxelEditHistory history;
    VoxelPaintBrushContext paintContext{&paintSession, &paintDocument, 0U,
        Hit({1, 1, 1}, VoxelHitFace::PositiveY, paintDocument.GetRevision()),
        state, false, &history};
    Require(VoxelPaintBrushTool::Apply(paintContext).Code ==
            VoxelPaintBrushResultCode::Applied &&
        paintPreview.AffectedPositions == std::vector<Position>{{1, 1, 1}} &&
        paintDocument.GetVoxel({1, 1, 1})->PaletteIndex == state.PaletteIndex,
        "Applied Paint coordinates diverge from the Ghost Preview.");
}
}

int main()
{
    try
    {
        TestStatesColorsAndAffectedCoordinates();
        TestEngineParityClippingAndInvalids();
        TestCacheAndAppliedOperationParity();
        std::cout << "Professional Ghost Preview smoke tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
