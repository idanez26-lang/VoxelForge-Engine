#pragma once

#include "EditorMath.h"

#include <array>

namespace VoxelForge::Editor
{

class EditorCamera final
{
public:
    void Update(bool viewportHovered);
    void Frame(float width, float height, float depth) noexcept;
    void SetAspectRatio(float aspectRatio) noexcept;

    [[nodiscard]] Vec3 GetPosition() const noexcept;
    [[nodiscard]] Vec3 GetForward() const noexcept;
    [[nodiscard]] Vec3 GetRight() const noexcept;
    [[nodiscard]] Vec3 GetUp() const noexcept;
    [[nodiscard]] const Vec3& GetTarget() const noexcept;
    [[nodiscard]] float GetDistance() const noexcept;
    [[nodiscard]] float GetFieldOfViewDegrees() const noexcept;
    [[nodiscard]] std::array<float, 16> GetViewProjection() const noexcept;

private:
    Vec3 target_{};
    float yawDegrees_ = -135.0F;
    float pitchDegrees_ = -28.0F;
    float distance_ = 12.0F;
    float aspectRatio_ = 1.0F;
    float fieldOfViewDegrees_ = 45.0F;
    float orbitSensitivity_ = 0.25F;
};

} // namespace VoxelForge::Editor
