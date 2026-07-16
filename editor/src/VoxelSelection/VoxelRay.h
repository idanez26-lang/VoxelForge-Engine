#pragma once

#include "EditorMath.h"

namespace VoxelForge::Editor
{

struct VoxelRay final
{
    Vec3 Origin{};
    Vec3 Direction{};

    [[nodiscard]] bool operator==(const VoxelRay&) const noexcept = default;
};

} // namespace VoxelForge::Editor
