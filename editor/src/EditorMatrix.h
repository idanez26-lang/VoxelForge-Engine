#pragma once

#include "EditorMath.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>

namespace VoxelForge::Editor
{

using Matrix4 = std::array<float, 16>;

[[nodiscard]] constexpr Matrix4 IdentityMatrix() noexcept
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] inline Matrix4 MultiplyMatrix(
    const Matrix4& left,
    const Matrix4& right) noexcept
{
    Matrix4 result{};
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            for (std::size_t index = 0U; index < 4U; ++index)
            {
                result[row * 4U + column] +=
                    left[row * 4U + index] * right[index * 4U + column];
            }
        }
    }
    return result;
}

[[nodiscard]] inline bool IsFinite(const Matrix4& matrix) noexcept
{
    return std::all_of(matrix.begin(), matrix.end(),
        [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] inline bool IsFinite(const Vec3 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y) &&
        std::isfinite(value.Z);
}

[[nodiscard]] inline Vec3 TransformPoint(
    const Matrix4& matrix,
    const Vec3 point) noexcept
{
    const float x = matrix[0] * point.X + matrix[1] * point.Y +
        matrix[2] * point.Z + matrix[3];
    const float y = matrix[4] * point.X + matrix[5] * point.Y +
        matrix[6] * point.Z + matrix[7];
    const float z = matrix[8] * point.X + matrix[9] * point.Y +
        matrix[10] * point.Z + matrix[11];
    const float w = matrix[12] * point.X + matrix[13] * point.Y +
        matrix[14] * point.Z + matrix[15];
    if (std::abs(w) <= 1.0e-8F) return {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    return {x / w, y / w, z / w};
}

[[nodiscard]] inline Vec3 TransformVector(
    const Matrix4& matrix,
    const Vec3 vector) noexcept
{
    return {
        matrix[0] * vector.X + matrix[1] * vector.Y + matrix[2] * vector.Z,
        matrix[4] * vector.X + matrix[5] * vector.Y + matrix[6] * vector.Z,
        matrix[8] * vector.X + matrix[9] * vector.Y + matrix[10] * vector.Z};
}

[[nodiscard]] inline std::optional<Matrix4> InvertMatrix(
    const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix)) return std::nullopt;
    std::array<std::array<double, 8>, 4> augmented{};
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
            augmented[row][column] = matrix[row * 4U + column];
        augmented[row][row + 4U] = 1.0;
    }
    for (std::size_t pivot = 0U; pivot < 4U; ++pivot)
    {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1U; row < 4U; ++row)
        {
            if (std::abs(augmented[row][pivot]) >
                std::abs(augmented[best][pivot])) best = row;
        }
        if (std::abs(augmented[best][pivot]) <= 1.0e-10) return std::nullopt;
        if (best != pivot) std::swap(augmented[best], augmented[pivot]);
        const double divisor = augmented[pivot][pivot];
        for (double& value : augmented[pivot]) value /= divisor;
        for (std::size_t row = 0U; row < 4U; ++row)
        {
            if (row == pivot) continue;
            const double factor = augmented[row][pivot];
            for (std::size_t column = 0U; column < 8U; ++column)
                augmented[row][column] -= factor * augmented[pivot][column];
        }
    }
    Matrix4 inverse{};
    for (std::size_t row = 0U; row < 4U; ++row)
        for (std::size_t column = 0U; column < 4U; ++column)
            inverse[row * 4U + column] =
                static_cast<float>(augmented[row][column + 4U]);
    return IsFinite(inverse) ? std::optional<Matrix4>(inverse) : std::nullopt;
}

[[nodiscard]] constexpr Matrix4 TranslationMatrix(const Vec3 translation) noexcept
{
    return {
        1.0F, 0.0F, 0.0F, translation.X,
        0.0F, 1.0F, 0.0F, translation.Y,
        0.0F, 0.0F, 1.0F, translation.Z,
        0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] constexpr Matrix4 UniformScaleMatrix(const float scale) noexcept
{
    return {
        scale, 0.0F, 0.0F, 0.0F,
        0.0F, scale, 0.0F, 0.0F,
        0.0F, 0.0F, scale, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] inline Matrix4 RotationYMatrix(const float radians) noexcept
{
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        cosine, 0.0F, sine, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        -sine, 0.0F, cosine, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
}

} // namespace VoxelForge::Editor
