#pragma once

#include "EditorMath.h"

#include <cstdint>

namespace VoxelForge::Editor
{

enum class TransformPivotMode : std::uint8_t
{
    Center,
    Bottom,
    Top
};

struct TransformPivot final
{
    TransformPivotMode Mode = TransformPivotMode::Center;
    Vec3 WorldPosition{};

    [[nodiscard]] bool operator==(
        const TransformPivot&) const noexcept = default;
};

} // namespace VoxelForge::Editor
