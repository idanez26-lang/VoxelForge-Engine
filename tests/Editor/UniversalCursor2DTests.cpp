#include "Preview/UniversalCursor2D.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

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
                UniversalCursorPreviewSubject::PencilSingleVoxel),
        "Single Voxel Pencil must render only the universal cursor.");
    Require(ShouldRenderExactPreviewGeometry(
                UniversalCursorPreviewSubject::Geometric),
        "Tool and brush geometry must preserve exact previews.");
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
