#pragma once

#include <cmath>

namespace VoxelForge::Editor
{

struct Vec2
{
    float X = 0.0F;
    float Y = 0.0F;
};

struct Vec3
{
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;

    [[nodiscard]] bool operator==(const Vec3&) const noexcept = default;
};

[[nodiscard]] inline Vec3 operator+(const Vec3& left, const Vec3& right)
{
    return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

[[nodiscard]] inline Vec3 operator-(const Vec3& left, const Vec3& right)
{
    return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
}

[[nodiscard]] inline Vec3 operator*(const Vec3& value, const float scalar)
{
    return {value.X * scalar, value.Y * scalar, value.Z * scalar};
}

[[nodiscard]] inline float Dot(const Vec3& left, const Vec3& right)
{
    return (left.X * right.X) + (left.Y * right.Y) + (left.Z * right.Z);
}

[[nodiscard]] inline Vec3 Cross(const Vec3& left, const Vec3& right)
{
    return {
        (left.Y * right.Z) - (left.Z * right.Y),
        (left.Z * right.X) - (left.X * right.Z),
        (left.X * right.Y) - (left.Y * right.X)};
}

[[nodiscard]] inline float Length(const Vec3& value)
{
    return std::sqrt(Dot(value, value));
}

[[nodiscard]] inline Vec3 Normalize(const Vec3& value)
{
    const float length = Length(value);
    if (length <= 0.00001F)
    {
        return {};
    }

    const float inverse = 1.0F / length;
    return value * inverse;
}

[[nodiscard]] inline float DegreesToRadians(const float degrees)
{
    return degrees * 0.01745329251994329577F;
}

} // namespace VoxelForge::Editor
