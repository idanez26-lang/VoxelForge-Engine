#include "ViewportRenderer.h"

#include <imgui.h>

#include <array>
#include <cmath>

namespace VoxelForge::Editor
{

namespace
{

constexpr float NearPlane = 0.05F;

} // namespace

void ViewportRenderer::Draw(
    ImDrawList& drawList,
    const ImVec2& origin,
    const ImVec2& size,
    const EditorCamera& camera,
    const Vec3& cubePosition) const
{
    drawList.AddRectFilled(
        origin,
        ImVec2(origin.x + size.x, origin.y + size.y),
        IM_COL32(19, 21, 26, 255));

    drawList.PushClipRect(
        origin,
        ImVec2(origin.x + size.x, origin.y + size.y),
        true);

    DrawGrid(drawList, origin, size, camera);
    DrawAxes(drawList, origin, size, camera);
    DrawCube(drawList, origin, size, camera, cubePosition);

    drawList.PopClipRect();
}

bool ViewportRenderer::Project(
    const Vec3& worldPosition,
    const ImVec2& origin,
    const ImVec2& size,
    const EditorCamera& camera,
    Vec2& screenPosition) const
{
    const Vec3 relative = worldPosition - camera.GetPosition();
    const Vec3 right = camera.GetRight();
    const Vec3 up = camera.GetUp();
    const Vec3 forward = camera.GetForward();

    const float cameraX = Dot(relative, right);
    const float cameraY = Dot(relative, up);
    const float cameraZ = Dot(relative, forward);

    if (cameraZ <= NearPlane)
    {
        return false;
    }

    const float aspect = size.x / (size.y > 1.0F ? size.y : 1.0F);
    const float focal =
        1.0F /
        std::tan(
            DegreesToRadians(camera.GetFieldOfViewDegrees()) * 0.5F);

    const float normalizedX = (cameraX * focal / aspect) / cameraZ;
    const float normalizedY = (cameraY * focal) / cameraZ;

    screenPosition.X =
        origin.x + ((normalizedX * 0.5F) + 0.5F) * size.x;
    screenPosition.Y =
        origin.y + ((-normalizedY * 0.5F) + 0.5F) * size.y;

    return true;
}

void ViewportRenderer::DrawGrid(
    ImDrawList& drawList,
    const ImVec2& origin,
    const ImVec2& size,
    const EditorCamera& camera) const
{
    constexpr int gridExtent = 20;

    for (int value = -gridExtent; value <= gridExtent; ++value)
    {
        const bool majorLine = (value % 5) == 0;
        const ImU32 color = majorLine
            ? IM_COL32(76, 80, 90, 180)
            : IM_COL32(48, 52, 60, 125);

        Vec2 start{};
        Vec2 end{};

        if (Project(
                {static_cast<float>(value), 0.0F, -gridExtent},
                origin,
                size,
                camera,
                start) &&
            Project(
                {static_cast<float>(value), 0.0F, gridExtent},
                origin,
                size,
                camera,
                end))
        {
            drawList.AddLine(
                ImVec2(start.X, start.Y),
                ImVec2(end.X, end.Y),
                color,
                majorLine ? 1.4F : 1.0F);
        }

        if (Project(
                {-gridExtent, 0.0F, static_cast<float>(value)},
                origin,
                size,
                camera,
                start) &&
            Project(
                {gridExtent, 0.0F, static_cast<float>(value)},
                origin,
                size,
                camera,
                end))
        {
            drawList.AddLine(
                ImVec2(start.X, start.Y),
                ImVec2(end.X, end.Y),
                color,
                majorLine ? 1.4F : 1.0F);
        }
    }
}

void ViewportRenderer::DrawAxes(
    ImDrawList& drawList,
    const ImVec2& origin,
    const ImVec2& size,
    const EditorCamera& camera) const
{
    Vec2 center{};
    Vec2 xAxis{};
    Vec2 yAxis{};
    Vec2 zAxis{};

    if (!Project({0.0F, 0.0F, 0.0F}, origin, size, camera, center))
    {
        return;
    }

    if (Project({4.0F, 0.0F, 0.0F}, origin, size, camera, xAxis))
    {
        drawList.AddLine(
            ImVec2(center.X, center.Y),
            ImVec2(xAxis.X, xAxis.Y),
            IM_COL32(220, 70, 70, 255),
            2.0F);
    }

    if (Project({0.0F, 4.0F, 0.0F}, origin, size, camera, yAxis))
    {
        drawList.AddLine(
            ImVec2(center.X, center.Y),
            ImVec2(yAxis.X, yAxis.Y),
            IM_COL32(80, 210, 100, 255),
            2.0F);
    }

    if (Project({0.0F, 0.0F, 4.0F}, origin, size, camera, zAxis))
    {
        drawList.AddLine(
            ImVec2(center.X, center.Y),
            ImVec2(zAxis.X, zAxis.Y),
            IM_COL32(70, 130, 230, 255),
            2.0F);
    }
}

void ViewportRenderer::DrawCube(
    ImDrawList& drawList,
    const ImVec2& origin,
    const ImVec2& size,
    const EditorCamera& camera,
    const Vec3& cubePosition) const
{
    constexpr std::array<Vec3, 8> localVertices{{
        {-1.0F, -1.0F, -1.0F},
        { 1.0F, -1.0F, -1.0F},
        { 1.0F,  1.0F, -1.0F},
        {-1.0F,  1.0F, -1.0F},
        {-1.0F, -1.0F,  1.0F},
        { 1.0F, -1.0F,  1.0F},
        { 1.0F,  1.0F,  1.0F},
        {-1.0F,  1.0F,  1.0F}
    }};

    constexpr std::array<std::array<int, 2>, 12> edges{{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}}
    }};

    std::array<Vec2, 8> projected{};
    std::array<bool, 8> visible{};

    for (std::size_t index = 0; index < localVertices.size(); ++index)
    {
        visible[index] = Project(
            localVertices[index] + cubePosition,
            origin,
            size,
            camera,
            projected[index]);
    }

    for (const auto& edge : edges)
    {
        const int first = edge[0];
        const int second = edge[1];

        if (!visible[first] || !visible[second])
        {
            continue;
        }

        drawList.AddLine(
            ImVec2(projected[first].X, projected[first].Y),
            ImVec2(projected[second].X, projected[second].Y),
            IM_COL32(240, 174, 65, 255),
            2.2F);
    }
}

} // namespace VoxelForge::Editor
