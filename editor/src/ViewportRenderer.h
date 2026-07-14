#pragma once

#include "EditorCamera.h"
#include "EditorMath.h"

#include <array>

struct ImDrawList;
struct ImVec2;

namespace VoxelForge::Editor
{

class ViewportRenderer final
{
public:
    void Draw(
        ImDrawList& drawList,
        const ImVec2& origin,
        const ImVec2& size,
        const EditorCamera& camera,
        const Vec3& cubePosition) const;

private:
    [[nodiscard]] bool Project(
        const Vec3& worldPosition,
        const ImVec2& origin,
        const ImVec2& size,
        const EditorCamera& camera,
        Vec2& screenPosition) const;

    void DrawGrid(
        ImDrawList& drawList,
        const ImVec2& origin,
        const ImVec2& size,
        const EditorCamera& camera) const;

    void DrawCube(
        ImDrawList& drawList,
        const ImVec2& origin,
        const ImVec2& size,
        const EditorCamera& camera,
        const Vec3& cubePosition) const;

    void DrawAxes(
        ImDrawList& drawList,
        const ImVec2& origin,
        const ImVec2& size,
        const EditorCamera& camera) const;
};

} // namespace VoxelForge::Editor
