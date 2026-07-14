#pragma once

#include "EditorMath.h"

namespace VoxelForge::Editor
{

class EditorCamera final
{
public:
    void Update(float deltaTime, bool viewportHovered);

    [[nodiscard]] const Vec3& GetPosition() const noexcept;
    [[nodiscard]] Vec3 GetForward() const noexcept;
    [[nodiscard]] Vec3 GetRight() const noexcept;
    [[nodiscard]] Vec3 GetUp() const noexcept;

    [[nodiscard]] float GetFieldOfViewDegrees() const noexcept;

private:
    Vec3 position_{8.0F, 6.0F, 10.0F};
    float yawDegrees_ = -140.0F;
    float pitchDegrees_ = -22.0F;
    float fieldOfViewDegrees_ = 60.0F;
    float movementSpeed_ = 6.0F;
    float lookSensitivity_ = 0.18F;
};

} // namespace VoxelForge::Editor
