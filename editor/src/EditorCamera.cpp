#include "EditorCamera.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{

void EditorCamera::Update(const float deltaTime, const bool viewportHovered)
{
    if (!viewportHovered)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        yawDegrees_ += io.MouseDelta.x * lookSensitivity_;
        pitchDegrees_ -= io.MouseDelta.y * lookSensitivity_;
        pitchDegrees_ = std::clamp(pitchDegrees_, -89.0F, 89.0F);

        const Vec3 forward = GetForward();
        const Vec3 right = GetRight();
        const Vec3 up{0.0F, 1.0F, 0.0F};

        float speed = movementSpeed_;
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
        {
            speed *= 2.5F;
        }

        const float distance = speed * deltaTime;

        if (ImGui::IsKeyDown(ImGuiKey_W))
        {
            position_ = position_ + (forward * distance);
        }

        if (ImGui::IsKeyDown(ImGuiKey_S))
        {
            position_ = position_ - (forward * distance);
        }

        if (ImGui::IsKeyDown(ImGuiKey_D))
        {
            position_ = position_ + (right * distance);
        }

        if (ImGui::IsKeyDown(ImGuiKey_A))
        {
            position_ = position_ - (right * distance);
        }

        if (ImGui::IsKeyDown(ImGuiKey_E))
        {
            position_ = position_ + (up * distance);
        }

        if (ImGui::IsKeyDown(ImGuiKey_Q))
        {
            position_ = position_ - (up * distance);
        }
    }

    if (io.MouseWheel != 0.0F)
    {
        position_ = position_ + (GetForward() * io.MouseWheel);
    }
}

const Vec3& EditorCamera::GetPosition() const noexcept
{
    return position_;
}

Vec3 EditorCamera::GetForward() const noexcept
{
    const float yaw = DegreesToRadians(yawDegrees_);
    const float pitch = DegreesToRadians(pitchDegrees_);

    return Normalize({
        std::cos(pitch) * std::cos(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::sin(yaw)});
}

Vec3 EditorCamera::GetRight() const noexcept
{
    return Normalize(Cross(GetForward(), {0.0F, 1.0F, 0.0F}));
}

Vec3 EditorCamera::GetUp() const noexcept
{
    return Normalize(Cross(GetRight(), GetForward()));
}

float EditorCamera::GetFieldOfViewDegrees() const noexcept
{
    return fieldOfViewDegrees_;
}

} // namespace VoxelForge::Editor
