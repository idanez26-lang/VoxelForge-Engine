#include "Preview/UniversalCursor2D.h"
#include "Preview/FacePlanGhostSurface.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const char* const message)
{
    if (!condition) throw std::runtime_error(message);
}

float Distance(const Vec2 left, const Vec2 right)
{
    const float x = right.X - left.X;
    const float y = right.Y - left.Y;
    return std::sqrt(x * x + y * y);
}

Vec2 Edge(const UniversalCursor2DGeometry& cursor, const std::size_t index)
{
    const Vec2 current = cursor.Corners[index];
    const Vec2 next = cursor.Corners[
        (index + 1U) % cursor.Corners.size()];
    return {next.X - current.X, next.Y - current.Y};
}

float PolygonArea(const UniversalCursor2DGeometry& cursor)
{
    float twiceArea = 0.0F;
    for (std::size_t index = 0U; index < cursor.Corners.size(); ++index)
    {
        const Vec2 current = cursor.Corners[index];
        const Vec2 next = cursor.Corners[
            (index + 1U) % cursor.Corners.size()];
        twiceArea += current.X * next.Y - current.Y * next.X;
    }
    return std::abs(twiceArea) * 0.5F;
}

void TestProjectsFullOneVoxelFace()
{
    const ViewportRectangle viewport{100.0F, 50.0F, 800.0F, 600.0F};
    const UniversalCursor2DGeometry cursor = ProjectUniversalCursor2D(
        {{0.0F, 0.0F, 0.25F}, {0.0F, 0.0F, 1.0F}},
        viewport, IdentityMatrix());
    Require(cursor.Visible, "A valid voxel face must be visible.");
    const std::array<Vec2, 4U> expected{{
        {300.0F, 500.0F}, {700.0F, 500.0F},
        {700.0F, 200.0F}, {300.0F, 200.0F}}};
    for (std::size_t index = 0U; index < expected.size(); ++index)
        Require(Distance(cursor.Corners[index], expected[index]) < 0.001F,
            "Cursor corners must cover the complete projected 1x1 face.");
}

void TestPerspectiveDepthChangesProjectedSize()
{
    const ViewportRectangle viewport{0.0F, 0.0F, 1280.0F, 720.0F};
    Matrix4 perspectiveLike = IdentityMatrix();
    perspectiveLike[14] = 0.25F;
    const UniversalCursor2DGeometry nearCursor = ProjectUniversalCursor2D(
        {{0.0F, 0.0F, 0.2F}, {0.0F, 0.0F, 1.0F}},
        viewport, perspectiveLike);
    const UniversalCursor2DGeometry farCursor = ProjectUniversalCursor2D(
        {{0.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 1.0F}},
        viewport, perspectiveLike);
    Require(nearCursor.Visible && farCursor.Visible,
        "Both depth samples must project.");
    Require(Distance(nearCursor.Corners[0], nearCursor.Corners[1]) >
            Distance(farCursor.Corners[0], farCursor.Corners[1]),
        "A real voxel face must shrink with perspective depth.");
}

void TestProjectedFaceOrientationIsPreserved()
{
    const ViewportRectangle viewport{0.0F, 0.0F, 800.0F, 600.0F};
    Matrix4 oblique = IdentityMatrix();
    oblique[1] = 0.35F;
    oblique[2] = -0.25F;
    oblique[4] = 0.15F;
    oblique[6] = 0.30F;
    const auto cursorZ = ProjectUniversalCursor2D(
        {{}, {0.0F, 0.0F, 1.0F}}, viewport, oblique);
    const auto cursorX = ProjectUniversalCursor2D(
        {{}, {1.0F, 0.0F, 0.0F}}, viewport, oblique);
    const auto cursorY = ProjectUniversalCursor2D(
        {{}, {0.0F, 1.0F, 0.0F}}, viewport, oblique);
    Require(cursorX.Visible && cursorY.Visible && cursorZ.Visible &&
            PolygonArea(cursorX) > 1.0F && PolygonArea(cursorY) > 1.0F &&
            PolygonArea(cursorZ) > 1.0F,
        "X, Y and Z faces must project as real visible quadrilaterals.");
    Require(Distance(Edge(cursorX, 0U), Edge(cursorY, 0U)) > 1.0F &&
            Distance(Edge(cursorY, 1U), Edge(cursorZ, 1U)) > 1.0F,
        "X, Y and Z face orientations must remain distinct.");
}

void TestInvalidTargetIsHidden()
{
    Require(!ProjectUniversalCursor2D(
        {{}, {}}, {0.0F, 0.0F, 640.0F, 480.0F},
        IdentityMatrix()).Visible,
        "A target without a face normal must not draw a marker.");
    Require(!ProjectUniversalCursor2D(
        {{}, {0.0F, 1.0F, 0.0F}}, {},
        IdentityMatrix()).Visible,
        "A cursor cannot be projected without a viewport.");
}

void TestExactPreviewPresentationPolicy()
{
    Require(!ShouldRenderExactPreviewGeometry(
                UniversalCursorPreviewSubject::PencilSingleVoxel, false) &&
            ShouldRenderExactPreviewGeometry(
                UniversalCursorPreviewSubject::PencilSingleVoxel, true),
        "Single Voxel must be cursor-only while idle and exact during a stroke.");
    Require(ShouldRenderExactPreviewGeometry(
                UniversalCursorPreviewSubject::PencilBrush, false),
        "Pencil brushes must preserve their exact preview.");
    Require(ShouldRenderExactPreviewGeometry(
                UniversalCursorPreviewSubject::Geometric, false),
        "Tool and brush geometry must preserve exact previews.");
    Require(ShouldRetainExactPreviewOnMissingFrame(
                UniversalCursorPreviewSubject::PencilBrush, true) &&
            ShouldRetainExactPreviewOnMissingFrame(
                UniversalCursorPreviewSubject::PencilSingleVoxel, true) &&
            !ShouldRetainExactPreviewOnMissingFrame(
                UniversalCursorPreviewSubject::PencilBrush, false) &&
            !ShouldRetainExactPreviewOnMissingFrame(
                UniversalCursorPreviewSubject::Geometric, true),
        "Only an active Pencil stroke may retain a transiently missing preview.");
    Require(ShouldResolvePreviewForPresentation(false) &&
            !ShouldResolvePreviewForPresentation(true),
        "Presentation must reuse the accepted plan instead of replanning an active stroke.");
    Require(ShouldPresentFaceAddAsPlanGhosts(true, true, true),
        "An active Face Add must use its exact accepted plan ghosts.");
    Require(!ShouldPresentFaceAddAsPlanGhosts(true, true, false) &&
            !ShouldPresentFaceAddAsPlanGhosts(true, false, true) &&
            !ShouldPresentFaceAddAsPlanGhosts(false, true, true),
        "Plan-ghost presentation must remain exclusive to an active Face Add.");
}

void TestLockedFaceAnchorPolicy()
{
    const UniversalCursor2DTarget hovered{
        {1.0F, 2.0F, 3.0F}, {1.0F, 0.0F, 0.0F}};
    const UniversalCursor2DTarget planned{
        {7.0F, 8.0F, 9.0F}, {0.0F, 1.0F, 0.0F}};
    Require(SelectUniversalCursor2DTarget(hovered, planned,
                UniversalCursorAnchorPolicy::PreferHoveredTarget) == hovered,
        "Ordinary cursor targeting must continue to follow the hovered face.");
    Require(SelectUniversalCursor2DTarget(hovered, planned,
                UniversalCursorAnchorPolicy::PreferPlannedTarget) == planned,
        "An active stroke must share the accepted preview plan anchor.");
    Require(SelectUniversalCursor2DTarget(hovered, std::nullopt,
                UniversalCursorAnchorPolicy::PreferPlannedTarget) == hovered,
        "A missing planned anchor must safely fall back to the hovered face.");
}

void TestVoxelFaceTargetMatchesVisiblePreviewFace()
{
    using Position = VoxelForge::Asset::Voxel::VoxelPosition;
    constexpr Position seed{3, 3, 3};
    constexpr Vec3 modelCenter{1.0F, 1.0F, 1.0F};
    const std::array<Position, 6U> normals{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (const Position normal : normals)
    {
        const UniversalCursor2DTarget sourceFace =
            MakeVoxelFaceCursor2DTarget(seed, normal, modelCenter);
        constexpr int depth = 3;
        const Position outerVoxel{
            seed.X + normal.X * depth,
            seed.Y + normal.Y * depth,
            seed.Z + normal.Z * depth};
        const UniversalCursor2DTarget addPreviewOuterFace =
            MakeVoxelFaceCursor2DTarget(outerVoxel, normal, modelCenter);
        const Vec3 expectedDelta{
            static_cast<float>(normal.X * depth),
            static_cast<float>(normal.Y * depth),
            static_cast<float>(normal.Z * depth)};
        Require(addPreviewOuterFace.SurfaceWorldPosition -
                    sourceFace.SurfaceWorldPosition == expectedDelta &&
                sourceFace.FaceNormal == addPreviewOuterFace.FaceNormal,
            "Face Add cursor must move to the visible outer face of its planned depth.");
    }

    const UniversalCursor2DTarget paintEraseFace =
        MakeVoxelFaceCursor2DTarget(seed, {0, 1, 0}, modelCenter);
    Require(paintEraseFace.SurfaceWorldPosition == Vec3{2.5F, 3.0F, 2.5F},
        "Face Paint/Erase cursor must remain on the clicked exposed seed face.");

    const std::array<Position, 4U> presented{{
        {-50, 4, -80}, {100, 4, 200}, {-50, 5, -80}, {100, 5, 200}}};
    const auto outermost = MakeOutermostVoxelFaceCursor2DTarget(
        presented, seed, {0, 1, 0}, modelCenter);
    Require(outermost &&
            outermost->SurfaceWorldPosition.X == 2.5F &&
            outermost->SurfaceWorldPosition.Y == 5.0F &&
            outermost->SurfaceWorldPosition.Z == 2.5F &&
            outermost->FaceNormal == Vec3{0.0F, 1.0F, 0.0F},
        "Face Add cursor must keep the locked seed's tangential coordinates "
        "and use only the presented outward depth.");
    Require(!MakeOutermostVoxelFaceCursor2DTarget(
                std::span<const Position>{}, seed, {0, 1, 0}, modelCenter),
        "An empty Face Add presentation must not synthesize a cursor anchor.");

    const std::array<Position, 2U> shallowFace{{
        {-100, 4, 200}, {100, 4, -200}}};
    const std::array<Position, 2U> deepFace{{
        {500, 9, -700}, {-500, 9, 700}}};
    const auto shallowCursor = MakeOutermostVoxelFaceCursor2DTarget(
        shallowFace, seed, {0, 1, 0}, modelCenter);
    const auto deepCursor = MakeOutermostVoxelFaceCursor2DTarget(
        deepFace, seed, {0, 1, 0}, modelCenter);
    Require(shallowCursor && deepCursor &&
            shallowCursor->SurfaceWorldPosition.X ==
                deepCursor->SurfaceWorldPosition.X &&
            shallowCursor->SurfaceWorldPosition.Z ==
                deepCursor->SurfaceWorldPosition.Z &&
            shallowCursor->SurfaceWorldPosition.Y !=
                deepCursor->SurfaceWorldPosition.Y,
        "Face depth changes must alter only the locked normal coordinate; "
        "tangential cursor bounds must remain camera-independent.");
}

void TestFacePlanGhostSurfaceCullsSharedFaces()
{
    using Position = VoxelForge::Asset::Voxel::VoxelPosition;
    const auto ghost = [](const Position position,
        const GhostVoxelState state = GhostVoxelState::Added)
    {
        return GhostVoxel{
            position, state, {0.2F, 0.4F, 0.8F, 1.0F}, 0.5F};
    };

    const std::array<GhostVoxel, 1U> single{{ghost({2, 3, 4})}};
    const FacePlanGhostSurface singleSurface =
        BuildFacePlanGhostSurface(single);
    Require(singleSurface.Cells.size() == 1U &&
            singleSurface.ExposedFaceCount == 6U &&
            singleSurface.Cells.front().ExposedFaceMask == 0x3FU,
        "One Face ghost voxel must expose all six faces.");

    const std::array<GhostVoxel, 2U> pair{{
        ghost({2, 3, 4}), ghost({3, 3, 4}, GhostVoxelState::Painted)}};
    const FacePlanGhostSurface pairSurface = BuildFacePlanGhostSurface(pair);
    Require(pairSurface.Cells.size() == 2U &&
            pairSurface.ExposedFaceCount == 10U &&
            (pairSurface.Cells[0].ExposedFaceMask &
                FacePlanGhostSideBit(FacePlanGhostSide::PositiveX)) == 0U &&
            (pairSurface.Cells[1].ExposedFaceMask &
                FacePlanGhostSideBit(FacePlanGhostSide::NegativeX)) == 0U &&
            pair[pairSurface.Cells[1].GhostIndex].State ==
                GhostVoxelState::Painted,
        "Adjacent Face ghosts must cull only their shared faces and retain "
        "the source ghost used for colour/state.");

    std::array<GhostVoxel, 8U> block{};
    std::size_t index = 0U;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                block[index++] = ghost({x, y, z});
    const FacePlanGhostSurface blockSurface =
        BuildFacePlanGhostSurface(block);
    Require(blockSurface.Cells.size() == 8U &&
            blockSurface.ExposedFaceCount == 24U,
        "A dense 2x2x2 Face ghost must emit its exact 24-quad envelope, "
        "not 48 per-voxel faces.");

    const std::array<GhostVoxel, 2U> duplicate{{
        ghost({7, 8, 9}), ghost({7, 8, 9}, GhostVoxelState::Erased)}};
    const FacePlanGhostSurface duplicateSurface =
        BuildFacePlanGhostSurface(duplicate);
    Require(duplicateSurface.Cells.size() == 1U &&
            duplicateSurface.ExposedFaceCount == 6U &&
            duplicateSurface.Cells.front().GhostIndex == 0U,
        "Duplicate Face ghost coordinates must remain stable and must not "
        "duplicate surface geometry.");

    // Match the size of the creation used during manual validation. A full
    // boundary shell contains 4624 cells; nine interior cells bring the
    // fixture to 4633 while preserving exposed surfaces in every direction.
    std::vector<GhostVoxel> largeStamp;
    largeStamp.reserve(4633U);
    std::size_t interiorAdded = 0U;
    for (int x = 0; x < 18; ++x)
    {
        for (int y = 0; y < 24; ++y)
        {
            for (int z = 0; z < 49; ++z)
            {
                const bool boundary = x == 0 || x == 17 ||
                    y == 0 || y == 23 || z == 0 || z == 48;
                if (boundary || interiorAdded < 9U)
                {
                    largeStamp.push_back(ghost({x, y, z}));
                    if (!boundary) ++interiorAdded;
                }
            }
        }
    }
    const FacePlanGhostSurface largeSurface =
        BuildFacePlanGhostSurface(largeStamp);
    std::uint8_t largeSurfaceSides = 0U;
    for (const FacePlanGhostSurfaceCell& cell : largeSurface.Cells)
        largeSurfaceSides = static_cast<std::uint8_t>(
            largeSurfaceSides | cell.ExposedFaceMask);
    Require(largeStamp.size() == 4633U &&
            largeSurface.ExposedFaceCount != 0U &&
            largeSurface.ExposedFaceCount < largeStamp.size() * 6U &&
            largeSurfaceSides == 0x3FU,
        "A 4633-cell Stamp preview must retain exposed geometry on all six "
        "sides while culling its hidden internal faces.");

    const FacePlanGhostVoxelBounds left =
        MakeFacePlanGhostVoxelBounds({10, 20, 30});
    const FacePlanGhostVoxelBounds right =
        MakeFacePlanGhostVoxelBounds({11, 20, 30});
    Require(left.Maximum[0] == right.Minimum[0] &&
            left.Minimum[1] == right.Minimum[1] &&
            left.Maximum[1] == right.Maximum[1] &&
            left.Minimum[2] == right.Minimum[2] &&
            left.Maximum[2] == right.Maximum[2],
        "Adjacent Face ghost cells must share one exact edge plane with "
        "identical tangential bounds and no overlap.");

    constexpr std::array<FacePlanGhostSide, 6U> sides{{
        FacePlanGhostSide::NegativeX, FacePlanGhostSide::PositiveX,
        FacePlanGhostSide::NegativeY, FacePlanGhostSide::PositiveY,
        FacePlanGhostSide::NegativeZ, FacePlanGhostSide::PositiveZ}};
    constexpr std::array<float, 3U> point{{4.0F, 5.0F, 6.0F}};
    for (const FacePlanGhostSide side : sides)
    {
        const auto normal = FacePlanGhostSideNormal(side);
        const auto offset = OffsetFacePlanGhostPointOutward(point, side);
        for (std::size_t axis = 0U; axis < 3U; ++axis)
        {
            const float expected =
                point[axis] + normal[axis] * FacePlanGhostOutwardOffset;
            Require(std::abs(offset[axis] - expected) < 0.000001F,
                "Each exposed Face quad coordinate must move exactly by the "
                "configured epsilon along its outward normal.");
            if (normal[axis] == 0.0F)
                Require(offset[axis] == point[axis],
                    "The Face anti-z-fighting offset must never alter a "
                    "tangential coordinate.");
        }
    }

    const auto leftSharedEdge = OffsetFacePlanGhostPointOutward(
        {left.Maximum[0], left.Maximum[1], left.Minimum[2]},
        FacePlanGhostSide::PositiveY);
    const auto rightSharedEdge = OffsetFacePlanGhostPointOutward(
        {right.Minimum[0], right.Maximum[1], right.Minimum[2]},
        FacePlanGhostSide::PositiveY);
    Require(leftSharedEdge == rightSharedEdge,
        "Coplanar neighboring Face quads must retain one identical shared "
        "edge after the normal-only offset.");
}
}

int main()
{
    try
    {
        TestProjectsFullOneVoxelFace();
        TestPerspectiveDepthChangesProjectedSize();
        TestProjectedFaceOrientationIsPreserved();
        TestInvalidTargetIsHidden();
        TestExactPreviewPresentationPolicy();
        TestLockedFaceAnchorPolicy();
        TestVoxelFaceTargetMatchesVisiblePreviewFace();
        TestFacePlanGhostSurfaceCullsSharedFaces();
        std::cout << "Universal Cursor 2D tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Universal Cursor 2D tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
