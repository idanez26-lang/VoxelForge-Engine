#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(const Position position) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Y)) << 11U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Z)) << 22U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, PositionHash>;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument Document(std::vector<Asset::Vox::VoxVoxel> voxels,
    const Asset::Vox::VoxDimensions dimensions = {8U, 8U, 8U})
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({dimensions, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, "preview.vox");
    Require(loaded.Succeeded(), "Unable to create preview document.");
    return std::move(*loaded.Document);
}

SmartToolPlanPtr Plan(const Asset::Voxel::VoxelDocument& document,
    const SmartAction action, const Position target, const std::uint8_t palette,
    const SmartToolMode mode = SmartToolMode::SingleVoxel, const int size = 1,
    const Position normal = {0, 1, 0})
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = *document.GetDimensions();
    request.BrushRequest.State = {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
        SmartBrushOrientation::Auto, size, palette, SmartBrushMode::Add};
    request.BrushRequest.Placement = {target, normal};
    request.SourceIdentity = 17U;
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = 9U;
    request.HasPaletteColors = true;
    for (std::size_t index = 0U; index < document.GetPalette().size(); ++index)
    {
        const auto& color = document.GetPalette()[index];
        request.PaletteColors[index] = {color.Red / 255.0F, color.Green / 255.0F,
            color.Blue / 255.0F, color.Alpha / 255.0F};
    }
    request.ReadVoxel = [&document](const Position position)
    {
        const auto voxel = document.GetVoxel(position);
        return voxel ? SmartToolVoxelState{true, voxel->PaletteIndex} : SmartToolVoxelState{};
    };
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Unable to create exact preview plan.");
    return result.Plan;
}

Asset::Voxel::VoxelDocument ApplyPlan(
    const Asset::Voxel::VoxelDocument& source, const SmartToolPlan& plan)
{
    Asset::Voxel::VoxelDocument result = source;
    std::vector<Asset::Voxel::VoxelDocumentChange> changes;
    changes.reserve(plan.Cells().size());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange()) continue;
        changes.push_back({0U, cell.WorldPosition, cell.Before.Exists,
            cell.Before.PaletteIndex, cell.After.Exists, cell.After.PaletteIndex});
    }
    if (!changes.empty())
    {
        const auto applied = result.ApplyVoxelChanges(changes);
        Require(applied.Succeeded, "Unable to apply the immutable plan to test document.");
    }
    return result;
}

void RequireExactFinalState(const SmartToolExactPreviewMesh& preview,
    const Asset::Voxel::VoxelDocument& source, const SmartToolPlan& plan)
{
    const Asset::Voxel::VoxelDocument committed = ApplyPlan(source, plan);
    const Mesh::MeshBuildResult expected = Mesh::VoxelMeshBuilder::Build(committed);
    Require(preview.Active && preview.Succeeded() && expected.Succeeded && expected.Mesh,
        "Exact preview or expected committed mesh was not available.");
    Require(preview.Mesh.Vertices() == expected.Mesh->Vertices() &&
            preview.Mesh.Indices() == expected.Mesh->Indices(),
        "Preview mesh diverged from the mesh after committing the same immutable plan.");
    for (std::size_t index = 0U; index < Voxel::VoxelPalette::Size(); ++index)
    {
        const Voxel::VoxelColor* const actual = preview.Palette.Get(index);
        const auto& expectedColor = committed.GetPalette()[index];
        Require(actual != nullptr && actual->Red == expectedColor.Red &&
                actual->Green == expectedColor.Green && actual->Blue == expectedColor.Blue &&
                actual->Alpha == expectedColor.Alpha,
            "Preview palette diverged from the committed document palette.");
    }
}

void RequireTargetInSymmetricPlanBounds(const SmartToolPlan& plan,
    const Position target)
{
    const bool includesTarget = std::any_of(plan.Cells().begin(), plan.Cells().end(),
        [target](const SmartToolPlanCell& cell)
        {
            return cell.WorldPosition == target && cell.FinalVoxel();
        });
    Require(includesTarget,
        "The immutable plan did not retain the requested target cell.");

    const SmartToolPlanBounds& bounds = plan.Bounds();
    Require(bounds.HasValue,
        "The immutable plan did not provide cell bounds for an exact preview.");
    const std::array<int, 3> minimum{bounds.Minimum.X, bounds.Minimum.Y,
        bounds.Minimum.Z};
    const std::array<int, 3> maximum{bounds.Maximum.X, bounds.Maximum.Y,
        bounds.Maximum.Z};
    const std::array<int, 3> center{target.X, target.Y, target.Z};
    for (std::size_t axis = 0U; axis < center.size(); ++axis)
    {
        Require(center[axis] - minimum[axis] == maximum[axis] - center[axis],
            "The immutable plan bounds were not symmetric around the target cell.");
    }
}

void RequireMeshCenter(const Mesh::MeshData& mesh,
    const std::array<float, 3>& expectedCenter)
{
    Require(!mesh.Empty(), "The exact preview mesh was unexpectedly empty.");
    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const Mesh::MeshVertex& vertex : mesh.Vertices())
    {
        for (std::size_t axis = 0U; axis < expectedCenter.size(); ++axis)
        {
            minimum[axis] = std::min(minimum[axis], vertex.Position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.Position[axis]);
        }
    }
    constexpr float Tolerance = 0.0001F;
    for (std::size_t axis = 0U; axis < expectedCenter.size(); ++axis)
    {
        const float actualCenter = (minimum[axis] + maximum[axis]) * 0.5F;
        Require(std::abs(actualCenter - expectedCenter[axis]) <= Tolerance,
            "Exact preview mesh center was shifted from the planned cell center.");
    }
}

void TestCreatePaintAndRemoveFinalMeshes()
{
    Asset::Voxel::VoxelDocument empty = Document({});
    const SmartToolPlanPtr create = Plan(empty, SmartAction::Add, {2, 2, 2}, 7U);
    const SmartToolExactPreviewMesh created =
        SmartToolExactPreviewComposer::Compose(empty, *create);
    RequireExactFinalState(created, empty, *create);

    Asset::Voxel::VoxelDocument paintedDocument = Document({{2U, 2U, 2U, 3U}});
    const SmartToolPlanPtr paint = Plan(paintedDocument, SmartAction::Paint, {2, 2, 2}, 7U);
    const SmartToolExactPreviewMesh painted =
        SmartToolExactPreviewComposer::Compose(paintedDocument, *paint);
    RequireExactFinalState(painted, paintedDocument, *paint);

    const SmartToolPlanPtr remove = Plan(paintedDocument, SmartAction::Erase, {2, 2, 2}, 7U);
    const SmartToolExactPreviewMesh removed =
        SmartToolExactPreviewComposer::Compose(paintedDocument, *remove);
    RequireExactFinalState(removed, paintedDocument, *remove);
    Require(removed.Active && removed.Empty(),
        "Remove-last-voxel must remain an active empty exact preview.");
}

void TestCacheIdentityAndNoChange()
{
    Asset::Voxel::VoxelDocument document = Document({});
    const SmartToolPlanPtr firstPlan = Plan(document, SmartAction::Add, {1, 1, 1}, 8U);
    SmartToolExactPreviewCache cache;
    const SmartToolExactPreviewMesh* first = &cache.Resolve(document, 42U, firstPlan);
    const SmartToolExactPreviewMesh* repeated = &cache.Resolve(document, 42U, firstPlan);
    Require(first == repeated && cache.BuildCount() == 1U,
        "Exact preview cache rebuilt an unchanged document/plan pair.");
    static_cast<void>(cache.Resolve(document, 43U, firstPlan));
    Require(cache.BuildCount() == 2U,
        "Exact preview cache ignored document identity changes.");

    const SmartToolPlanPtr noChange = Plan(document, SmartAction::Erase, {1, 1, 1}, 8U);
    const SmartToolExactPreviewMesh& unchanged = cache.Resolve(document, 43U, noChange);
    RequireExactFinalState(unchanged, document, *noChange);
    static_cast<void>(document.SetVoxel({7, 7, 7}, 4U));
    static_cast<void>(cache.Resolve(document, 43U, noChange));
    Require(cache.BuildCount() == 4U,
        "Exact preview cache ignored a source document revision change.");
    cache.Clear();
    static_cast<void>(cache.Resolve(document, 43U, noChange));
    Require(cache.BuildCount() == 5U,
        "Exact preview cache Clear did not force a fresh composition.");
}

void TestAllSupportedBrushModesUseFinalStateMesh()
{
    Asset::Voxel::VoxelDocument document = Document({});
    for (const SmartToolMode mode : {SmartToolMode::SingleVoxel,
             SmartToolMode::CubeBrush, SmartToolMode::SphereBrush,
             SmartToolMode::CylinderBrush})
    {
        const int size = mode == SmartToolMode::SingleVoxel ? 1 : 3;
        const SmartToolPlanPtr plan = Plan(document, SmartAction::Add, {3, 3, 3},
            11U, mode, size);
        const SmartToolExactPreviewMesh preview =
            SmartToolExactPreviewComposer::Compose(document, *plan);
        Require(preview.Active && preview.Succeeded() && !preview.Empty() &&
                preview.Mesh.IndexCount() > 0U,
            "A supported Smart Tool mode did not produce an exact final mesh.");
    }
}

void TestExactPreviewCellAlignment()
{
    constexpr Position Target{8, 8, 8};
    Asset::Voxel::VoxelDocument document = Document({}, {16U, 16U, 16U});

    // A zero normal models an already resolved cell target. It lets this test
    // prove the exact-grid contract without reproducing surface-anchor logic
    // that belongs exclusively to the brush engine/planner.
    constexpr Position ResolvedCellNormal{};
    const std::array<float, 3> targetCenter{
        static_cast<float>(Target.X) + 0.5F,
        static_cast<float>(Target.Y) + 0.5F,
        static_cast<float>(Target.Z) + 0.5F};

    const auto assertAligned = [&](const SmartToolMode mode, const int size)
    {
        const SmartToolPlanPtr plan = Plan(document, SmartAction::Add, Target,
            11U, mode, size, ResolvedCellNormal);
        RequireTargetInSymmetricPlanBounds(*plan, Target);
        const SmartToolExactPreviewMesh preview =
            SmartToolExactPreviewComposer::Compose(document, *plan);
        RequireMeshCenter(preview.Mesh, targetCenter);
        RequireExactFinalState(preview, document, *plan);
    };

    assertAligned(SmartToolMode::SingleVoxel, 1);
    for (const SmartToolMode mode : {SmartToolMode::CubeBrush,
             SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush})
    {
        assertAligned(mode, 3);
        assertAligned(mode, 7);
    }
}

void TestSurfacePlacementStillUsesTheImmutablePlan()
{
    Asset::Voxel::VoxelDocument document = Document({}, {16U, 16U, 16U});
    constexpr Position Target{8, 8, 8};
    for (const Position normal : {Position{0, 1, 0}, Position{1, 0, 0}})
    {
        const SmartToolPlanPtr plan = Plan(document, SmartAction::Add, Target,
            11U, SmartToolMode::CubeBrush, 3, normal);
        const SmartToolExactPreviewMesh preview =
            SmartToolExactPreviewComposer::Compose(document, *plan);
        const SmartToolPlanBounds& bounds = plan->Bounds();
        Require(bounds.HasValue,
            "A surface placement plan did not provide immutable bounds.");
        RequireMeshCenter(preview.Mesh, {
            (static_cast<float>(bounds.Minimum.X + bounds.Maximum.X + 1) * 0.5F),
            (static_cast<float>(bounds.Minimum.Y + bounds.Maximum.Y + 1) * 0.5F),
            (static_cast<float>(bounds.Minimum.Z + bounds.Maximum.Z + 1) * 0.5F)});
        RequireExactFinalState(preview, document, *plan);
    }
}
}

int main()
{
    try
    {
        TestCreatePaintAndRemoveFinalMeshes();
        TestCacheIdentityAndNoChange();
        TestAllSupportedBrushModesUseFinalStateMesh();
        TestExactPreviewCellAlignment();
        TestSurfacePlacementStillUsesTheImmutablePlan();
        std::cout << "Smart Tool Exact Preview Composer tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
