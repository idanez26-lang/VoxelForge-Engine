#include "EditorCamera.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{

namespace
{
using Matrix = std::array<float, 16>;

Matrix Multiply(const Matrix& left, const Matrix& right) noexcept
{
    Matrix result{};
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int index = 0; index < 4; ++index)
            {
                result[column * 4 + row] +=
                    left[index * 4 + row] * right[column * 4 + index];
            }
        }
    }
    return result;
}
}

void EditorCamera::Update(
    const bool viewportHovered,
    const float viewportHeight)
{
    if (!viewportHovered)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        yawDegrees_ += io.MouseDelta.x * orbitSensitivity_;
        pitchDegrees_ -= io.MouseDelta.y * orbitSensitivity_;
        pitchDegrees_ = std::clamp(pitchDegrees_, -89.0F, 89.0F);
        view_ = EditorCameraView::Perspective;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        Pan(io.MouseDelta.x, io.MouseDelta.y, viewportHeight);
    }

    if (io.MouseWheel != 0.0F)
    {
        const float factor = std::pow(0.85F, io.MouseWheel);
        distance_ = std::clamp(distance_ * factor, 0.1F, 10000.0F);
    }
}

void EditorCamera::Pan(
    const float horizontalPixels,
    const float verticalPixels,
    const float viewportHeight) noexcept
{
    const float safeHeight = std::max(viewportHeight, 1.0F);
    const float worldUnitsPerPixel =
        (2.0F * distance_ *
         std::tan(DegreesToRadians(fieldOfViewDegrees_) * 0.5F)) /
        safeHeight;
    target_ = target_ - (GetRight() * horizontalPixels * worldUnitsPerPixel);
    target_ = target_ + (GetUp() * verticalPixels * worldUnitsPerPixel);
}

void EditorCamera::Frame(
    const float width,
    const float height,
    const float depth) noexcept
{
    target_ = {};
    const float radius = std::max(
        0.5F * std::sqrt(
            width * width + height * height + depth * depth),
        0.5F);
    const float halfFov = DegreesToRadians(fieldOfViewDegrees_) * 0.5F;
    distance_ = std::max(1.0F, (radius / std::tan(halfFov)) * 1.40F);
}

void EditorCamera::Reset() noexcept
{
    target_ = {};
    yawDegrees_ = -135.0F;
    pitchDegrees_ = -28.0F;
    distance_ = 12.0F;
    view_ = EditorCameraView::Perspective;
}

void EditorCamera::SetView(const EditorCameraView view) noexcept
{
    view_ = view;
    switch (view)
    {
    case EditorCameraView::Front:
        yawDegrees_ = -90.0F;
        pitchDegrees_ = 0.0F;
        break;
    case EditorCameraView::Back:
        yawDegrees_ = 90.0F;
        pitchDegrees_ = 0.0F;
        break;
    case EditorCameraView::Left:
        yawDegrees_ = 0.0F;
        pitchDegrees_ = 0.0F;
        break;
    case EditorCameraView::Right:
        yawDegrees_ = 180.0F;
        pitchDegrees_ = 0.0F;
        break;
    case EditorCameraView::Top:
        yawDegrees_ = -90.0F;
        pitchDegrees_ = -89.0F;
        break;
    case EditorCameraView::Bottom:
        yawDegrees_ = -90.0F;
        pitchDegrees_ = 89.0F;
        break;
    case EditorCameraView::Perspective:
        yawDegrees_ = -135.0F;
        pitchDegrees_ = -28.0F;
        break;
    }
}

void EditorCamera::SetAspectRatio(const float aspectRatio) noexcept
{
    aspectRatio_ = std::max(aspectRatio, 0.01F);
}

Vec3 EditorCamera::GetPosition() const noexcept
{
    return target_ - (GetForward() * distance_);
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

const Vec3& EditorCamera::GetTarget() const noexcept
{
    return target_;
}

float EditorCamera::GetDistance() const noexcept
{
    return distance_;
}

float EditorCamera::GetFieldOfViewDegrees() const noexcept
{
    return fieldOfViewDegrees_;
}

EditorCameraView EditorCamera::GetView() const noexcept
{
    return view_;
}

std::array<float, 16> EditorCamera::GetViewProjection() const noexcept
{
    const Vec3 eye = GetPosition();
    const Vec3 right = GetRight();
    const Vec3 up = GetUp();
    const Vec3 forward = GetForward();

    const Matrix view{
        right.X, up.X, forward.X, 0.0F,
        right.Y, up.Y, forward.Y, 0.0F,
        right.Z, up.Z, forward.Z, 0.0F,
        -Dot(right, eye), -Dot(up, eye), -Dot(forward, eye), 1.0F};

    constexpr float nearPlane = 0.05F;
    const float farPlane = std::max(10000.0F, distance_ * 2.0F);
    const float yScale =
        1.0F / std::tan(DegreesToRadians(fieldOfViewDegrees_) * 0.5F);
    const float xScale = yScale / aspectRatio_;
    const float depthScale = farPlane / (farPlane - nearPlane);
    const Matrix projection{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 1.0F,
        0.0F, 0.0F, -nearPlane * depthScale, 0.0F};

    const Matrix columnMajor = Multiply(projection, view);
    Matrix rowMajor{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            rowMajor[row * 4 + column] = columnMajor[column * 4 + row];
        }
    }
    return rowMajor;
}

} // namespace VoxelForge::Editor
