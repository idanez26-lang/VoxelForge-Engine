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
        Orbit(io.MouseDelta.x, io.MouseDelta.y);
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        Pan(io.MouseDelta.x, io.MouseDelta.y, viewportHeight);
    }

    if (io.MouseWheel != 0.0F)
    {
        Zoom(io.MouseWheel);
    }
}

void EditorCamera::Orbit(
    const float horizontalPixels,
    const float verticalPixels) noexcept
{
    yawDegrees_ += horizontalPixels * orbitSensitivity_;
    pitchDegrees_ -= verticalPixels * orbitSensitivity_;
    pitchDegrees_ = std::clamp(pitchDegrees_, -89.0F, 89.0F);
    view_ = EditorCameraView::Perspective;
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

void EditorCamera::Zoom(const float wheelDelta) noexcept
{
    const float factor = std::pow(0.85F, wheelDelta);
    distance_ = std::clamp(distance_ * factor, 0.1F, 10000.0F);
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

void EditorCamera::SetTargetAndDistance(
    const Vec3 target,
    const float distance) noexcept
{
    if (!std::isfinite(target.X) || !std::isfinite(target.Y) ||
        !std::isfinite(target.Z) || !std::isfinite(distance))
        return;
    target_ = target;
    distance_ = std::clamp(distance, 0.1F, 10000.0F);
}

void EditorCamera::Reset() noexcept
{
    target_ = {};
    yawDegrees_ = -135.0F;
    pitchDegrees_ = -28.0F;
    distance_ = 12.0F;
    view_ = EditorCameraView::Perspective;
}

bool EditorCamera::RestoreState(const EditorCameraState& state) noexcept
{
    const auto finite = [](const Vec3& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) &&
            std::isfinite(value.Z);
    };
    if (!finite(state.Position) || !finite(state.RotationDegrees) ||
        !finite(state.Target) || !std::isfinite(state.Distance) ||
        state.Distance < 0.1F || state.Distance > 10000.0F ||
        state.RotationDegrees.X < -89.0F ||
        state.RotationDegrees.X > 89.0F ||
        std::abs(state.RotationDegrees.Z) > 0.001F ||
        state.View > EditorCameraView::Bottom)
    {
        return false;
    }

    const Vec3 previousTarget = target_;
    const float previousYaw = yawDegrees_;
    const float previousPitch = pitchDegrees_;
    const float previousDistance = distance_;
    const EditorCameraView previousView = view_;
    target_ = state.Target;
    pitchDegrees_ = state.RotationDegrees.X;
    yawDegrees_ = state.RotationDegrees.Y;
    distance_ = state.Distance;
    view_ = state.View;
    const Vec3 restoredPosition = GetPosition();
    constexpr float PositionTolerance = 0.001F;
    if (std::abs(restoredPosition.X - state.Position.X) > PositionTolerance ||
        std::abs(restoredPosition.Y - state.Position.Y) > PositionTolerance ||
        std::abs(restoredPosition.Z - state.Position.Z) > PositionTolerance)
    {
        target_ = previousTarget;
        yawDegrees_ = previousYaw;
        pitchDegrees_ = previousPitch;
        distance_ = previousDistance;
        view_ = previousView;
        return false;
    }
    return true;
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

EditorCameraState EditorCamera::CaptureState() const noexcept
{
    return {
        GetPosition(),
        {pitchDegrees_, yawDegrees_, 0.0F},
        distance_,
        target_,
        view_};
}

VoxelRay EditorCamera::CreateViewportRay(
    const float normalizedX,
    const float normalizedY) const noexcept
{
    const float halfHeight =
        std::tan(DegreesToRadians(fieldOfViewDegrees_) * 0.5F);
    const Vec3 direction = Normalize(
        GetForward() +
        GetRight() * (normalizedX * halfHeight * aspectRatio_) +
        GetUp() * (normalizedY * halfHeight));
    return {GetPosition(), direction};
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
