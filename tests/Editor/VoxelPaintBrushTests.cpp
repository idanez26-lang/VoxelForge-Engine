#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPaintBrushTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
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

Asset::Voxel::VoxelDocument Document(
    const Asset::Vox::VoxDimensions dimensions,
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({dimensions, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "voxel-paint-brush-memory.vox");
    Require(loaded.Succeeded(), "Unable to build Paint Brush test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Paint Brush test document has no model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size Paint Brush compatibility grid.");
    source->ForEachVoxel([&grid](
        const Asset::Voxel::VoxelPosition position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize Paint Brush compatibility grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
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
        return &model_;
    }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override
    {
        ++completedCount_;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 1U;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
};

Editor::VoxelRaycastHit Hit(
    const Asset::Voxel::VoxelPosition position,
    const Editor::VoxelHitFace face,
    const std::uint64_t revision)
{
    Editor::VoxelRaycastHit hit;
    hit.Coordinates = {
        static_cast<std::uint32_t>(position.X),
        static_cast<std::uint32_t>(position.Y),
        static_cast<std::uint32_t>(position.Z)};
    hit.Face = face;
    hit.SubModelIndex = 0U;
    hit.DocumentRevision = revision;
    return hit;
}

Editor::SmartBrushState State(
    const std::size_t paletteIndex,
    const Editor::SmartBrushShape shape = Editor::SmartBrushShape::Cube,
    const Editor::SmartBrushDimension dimension =
        Editor::SmartBrushDimension::Volume3D,
    const Editor::SmartBrushOrientation orientation =
        Editor::SmartBrushOrientation::Auto,
    const int size = 1)
{
    Editor::SmartBrushState state;
    state.Mode = Editor::SmartBrushMode::Paint;
    state.PaletteIndex = paletteIndex;
    state.Shape = shape;
    state.Dimension = dimension;
    state.Orientation = orientation;
    state.Size = size;
    return state;
}

Editor::VoxelPaintBrushContext Context(
    TestEditSession& session,
    Asset::Voxel::VoxelDocument& document,
    const Asset::Voxel::VoxelPosition position,
    const Editor::SmartBrushState& state,
    Editor::VoxelEditHistory* history = nullptr,
    const Editor::VoxelHitFace face = Editor::VoxelHitFace::PositiveY)
{
    return {&session, &document, 0U,
        Hit(position, face, document.GetRevision()), state, false, history};
}

std::vector<Asset::Vox::VoxVoxel> FilledCube(
    const std::uint32_t side, const std::uint8_t color)
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(side) * side * side);
    for (std::uint32_t z = 0U; z < side; ++z)
        for (std::uint32_t y = 0U; y < side; ++y)
            for (std::uint32_t x = 0U; x < side; ++x)
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z), color});
    return voxels;
}

void TestGeometryAndStatistics()
{
    auto document = Document({20U, 20U, 20U}, FilledCube(20U, 1U));
    TestEditSession session(document);
    const Asset::Voxel::VoxelPosition target{10, 10, 10};

    const auto cube3D = Editor::VoxelPaintBrushTool::Evaluate(Context(
        session, document, target, State(9U)));
    Require(cube3D.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        cube3D.Statistics.Total == 1U && cube3D.Statistics.Painted == 1U &&
        cube3D.Statistics.Ignored == 0U &&
        cube3D.Statistics.Clipped == 0U &&
        cube3D.Statistics.IsConsistent() &&
        cube3D.PaintablePositions.front() == target,
        "Size-one 3D Cube Paint was not centred on the clicked voxel.");

    const auto sphere = Editor::VoxelPaintBrushTool::Evaluate(Context(
        session, document, target,
        State(9U, Editor::SmartBrushShape::Sphere,
            Editor::SmartBrushDimension::Volume3D,
            Editor::SmartBrushOrientation::Auto, 3)));
    Require(sphere.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        sphere.Statistics.Total == 19U && sphere.Statistics.Painted == 19U &&
        sphere.Statistics.IsConsistent(),
        "3D sphere Paint geometry or statistics are incorrect.");

    const auto surface = [&](const Editor::SmartBrushOrientation orientation,
                             const Editor::VoxelHitFace face)
    {
        return Editor::VoxelPaintBrushTool::Evaluate(Context(
            session, document, target,
            State(9U, Editor::SmartBrushShape::Cube,
                Editor::SmartBrushDimension::Surface2D, orientation, 3),
            nullptr, face));
    };
    const auto automatic = surface(Editor::SmartBrushOrientation::Auto,
        Editor::VoxelHitFace::PositiveX);
    const auto x = surface(Editor::SmartBrushOrientation::X,
        Editor::VoxelHitFace::PositiveY);
    const auto y = surface(Editor::SmartBrushOrientation::Y,
        Editor::VoxelHitFace::PositiveY);
    const auto z = surface(Editor::SmartBrushOrientation::Z,
        Editor::VoxelHitFace::PositiveY);
    const auto allOnAxis = [&](const Editor::VoxelPaintBrushEvaluation& value,
                               const char axis)
    {
        for (const auto position : value.Positions)
        {
            if ((axis == 'X' && position.X != target.X) ||
                (axis == 'Y' && position.Y != target.Y) ||
                (axis == 'Z' && position.Z != target.Z))
                return false;
        }
        return value.Statistics.Total == 9U &&
            value.Statistics.Painted == 9U && value.Statistics.IsConsistent();
    };
    Require(allOnAxis(automatic, 'X') && allOnAxis(x, 'X') &&
        allOnAxis(y, 'Y') && allOnAxis(z, 'Z'),
        "2D Auto/X/Y/Z Paint orientation did not follow SmartBrush geometry.");

    const auto surfaceSphere = Editor::VoxelPaintBrushTool::Evaluate(Context(
        session, document, target,
        State(9U, Editor::SmartBrushShape::Sphere,
            Editor::SmartBrushDimension::Surface2D,
            Editor::SmartBrushOrientation::Y, 4),
        nullptr, Editor::VoxelHitFace::PositiveX));
    Require(surfaceSphere.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        surfaceSphere.Statistics.Total == 12U &&
        surfaceSphere.Statistics.Painted == 12U &&
        surfaceSphere.Statistics.IsConsistent() &&
        std::all_of(surfaceSphere.Positions.begin(),
            surfaceSphere.Positions.end(),
            [&target](const Asset::Voxel::VoxelPosition position)
            {
                return position.Y == target.Y;
            }),
        "2D Sphere Paint did not use the shared surface geometry.");

    const auto large = Editor::VoxelPaintBrushTool::Evaluate(Context(
        session, document, target,
        State(9U, Editor::SmartBrushShape::Cube,
            Editor::SmartBrushDimension::Volume3D,
            Editor::SmartBrushOrientation::Auto, 16)));
    Require(large.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        large.Statistics.Total == 4096U && large.Statistics.Painted == 4096U &&
        large.Statistics.IsConsistent() && large.RenderPlan.Mode ==
            Editor::SmartBrushRenderMode::AggregateBox,
        "Size-sixteen Paint did not preserve the shared Smart Brush plan.");
}

void TestClippingAndNoCreation()
{
    auto document = Document({3U, 3U, 3U}, {
        {0U, 0U, 0U, 1U}, {1U, 0U, 0U, 9U}});
    TestEditSession session(document);
    const auto clipped = Editor::VoxelPaintBrushTool::Evaluate(Context(
        session, document, {0, 0, 0},
        State(9U, Editor::SmartBrushShape::Cube,
            Editor::SmartBrushDimension::Volume3D,
            Editor::SmartBrushOrientation::Auto, 3)));
    Require(clipped.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        clipped.Statistics.Total == 27U && clipped.Statistics.Clipped > 0U &&
        clipped.Statistics.Painted == 1U && clipped.Statistics.Ignored > 0U &&
        clipped.Statistics.IsConsistent(),
        "Paint clipping and ignored-empty statistics are inconsistent.");

    Editor::VoxelEditHistory history;
    const std::uint64_t count = document.GetVoxelCount();
    const auto result = Editor::VoxelPaintBrushTool::Apply(Context(
        session, document, {0, 0, 0},
        State(9U, Editor::SmartBrushShape::Cube,
            Editor::SmartBrushDimension::Volume3D,
            Editor::SmartBrushOrientation::Auto, 3), &history));
    Require(result.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        document.GetVoxelCount() == count &&
        document.GetVoxel({0, 0, 0})->PaletteIndex == 9U &&
        document.GetVoxel({1, 0, 0})->PaletteIndex == 9U &&
        !document.GetVoxel({0, 1, 0}) && history.UndoCount() == 1U,
        "Paint created, removed, or unexpectedly changed voxels.");
}

void TestNoOpAndAtomicHistory()
{
    auto document = Document({4U, 4U, 4U}, {
        {1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U}, {3U, 1U, 1U, 4U}});
    TestEditSession session(document);
    Editor::VoxelEditHistory history;
    const std::uint64_t revision = document.GetRevision();
    const auto applied = Editor::VoxelPaintBrushTool::Apply(Context(
        session, document, {2, 1, 1}, State(8U), &history));
    Require(applied.Code == Editor::VoxelPaintBrushResultCode::Applied &&
        applied.Statistics.Total == 1U && applied.Statistics.Painted == 1U &&
        document.GetVoxel({2, 1, 1})->PaletteIndex == 8U &&
        document.GetVoxelCount() == 3U && document.GetRevision() == revision + 1U &&
        history.UndoCount() == 1U && history.UndoLabel() == "Paint Brush" &&
        session.rebuildCount_ == 1U,
        "Paint did not produce one atomic edit, revision, and rebuild.");
    Require(history.Undo(session) && document.GetVoxel({2, 1, 1})->PaletteIndex == 4U &&
        document.GetRevision() == revision + 2U && history.RedoCount() == 1U &&
        history.Redo(session) && document.GetVoxel({2, 1, 1})->PaletteIndex == 8U &&
        document.GetRevision() == revision + 3U && session.rebuildCount_ == 3U,
        "Paint Undo/Redo was not atomic.");

    const std::uint64_t noChangeRevision = document.GetRevision();
    const std::size_t rebuilds = session.rebuildCount_;
    const auto same = Editor::VoxelPaintBrushTool::Apply(Context(
        session, document, {2, 1, 1}, State(8U), &history));
    const auto empty = Editor::VoxelPaintBrushTool::Apply(Context(
        session, document, {0, 0, 0}, State(8U), &history));
    Require(same.Code == Editor::VoxelPaintBrushResultCode::NoChange &&
        empty.Code == Editor::VoxelPaintBrushResultCode::NoChange &&
        noChangeRevision == document.GetRevision() &&
        history.UndoCount() == 1U && session.rebuildCount_ == rebuilds,
        "Same-color or empty Paint created history or changed the document.");
}
}

int main()
{
    try
    {
        TestGeometryAndStatistics();
        TestClippingAndNoCreation();
        TestNoOpAndAtomicHistory();
        std::cout << "Voxel Paint Brush tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel Paint Brush tests failed: " << exception.what() << '\n';
        return 1;
    }
}
