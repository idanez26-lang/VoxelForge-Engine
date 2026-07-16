#pragma once

#include "EditorMatrix.h"

namespace VoxelForge::Editor
{

struct VoxelModelTransform final
{
    Matrix4 ModelMatrix = IdentityMatrix();
    Matrix4 InverseModelMatrix = IdentityMatrix();
};

[[nodiscard]] inline VoxelModelTransform CenteredVoxelModelTransform(
    const Vec3 modelCenter) noexcept
{
    return {
        TranslationMatrix(modelCenter * -1.0F),
        TranslationMatrix(modelCenter)};
}

} // namespace VoxelForge::Editor
