#include "ViewportNavigationController.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
constexpr float FocusDurationSeconds = 0.25F;

bool IsFinite(const Vec3 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y) &&
        std::isfinite(value.Z);
}

float SmoothStep(const float value) noexcept
{
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

Vec3 Lerp(const Vec3 from, const Vec3 to, const float amount) noexcept
{
    return from + (to - from) * amount;
}
}

ViewportNavigationController::ViewportNavigationController(
    EditorCamera& camera) noexcept : camera_(camera)
{
}

void ViewportNavigationController::Orbit(
    const float horizontalPixels,
    const float verticalPixels) noexcept
{
    CancelFocus();
    camera_.Orbit(horizontalPixels, verticalPixels);
}

void ViewportNavigationController::Pan(
    const float horizontalPixels,
    const float verticalPixels,
    const float viewportHeight) noexcept
{
    CancelFocus();
    camera_.Pan(horizontalPixels, verticalPixels, viewportHeight);
}

void ViewportNavigationController::Zoom(const float wheelDelta) noexcept
{
    CancelFocus();
    camera_.Zoom(wheelDelta);
}

bool ViewportNavigationController::FocusSelection(
    const ViewportNavigationBounds bounds) noexcept
{
    return Focus(bounds);
}

bool ViewportNavigationController::FrameAll(
    const ViewportNavigationBounds bounds) noexcept
{
    if (!Focus(bounds)) return false;
    // Framing the complete scene is also used by loading and recovery flows.
    // It must be immediately usable, unlike the deliberate selection focus.
    Tick(FocusDurationSeconds);
    return true;
}

void ViewportNavigationController::Tick(const float deltaSeconds) noexcept
{
    if (!focusing_) return;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) return;

    focusElapsedSeconds_ = std::min(
        focusElapsedSeconds_ + deltaSeconds, FocusDurationSeconds);
    const float amount = SmoothStep(focusElapsedSeconds_ / FocusDurationSeconds);
    camera_.SetTargetAndDistance(
        Lerp(focusStartTarget_, focusTarget_, amount),
        focusStartDistance_ + (focusDistance_ - focusStartDistance_) * amount);
    focusing_ = focusElapsedSeconds_ < FocusDurationSeconds;
}

void ViewportNavigationController::CancelFocus() noexcept
{
    focusing_ = false;
}

bool ViewportNavigationController::IsFocusing() const noexcept
{
    return focusing_;
}

bool ViewportNavigationController::Focus(
    const ViewportNavigationBounds bounds) noexcept
{
    if (!IsValid(bounds))
        return false;

    focusTarget_ = (bounds.Minimum + bounds.Maximum) * 0.5F;
    const float radius = Radius(bounds);
    const float halfFov = DegreesToRadians(camera_.GetFieldOfViewDegrees()) *
        0.5F;
    focusDistance_ = std::max(
        1.0F, (radius / std::tan(halfFov)) * 1.40F);
    focusStartTarget_ = camera_.GetTarget();
    focusStartDistance_ = camera_.GetDistance();
    focusElapsedSeconds_ = 0.0F;
    focusing_ = true;
    return true;
}

bool ViewportNavigationController::IsValid(
    const ViewportNavigationBounds bounds) noexcept
{
    return bounds.Valid && IsFinite(bounds.Minimum) && IsFinite(bounds.Maximum) &&
        bounds.Minimum.X <= bounds.Maximum.X &&
        bounds.Minimum.Y <= bounds.Maximum.Y &&
        bounds.Minimum.Z <= bounds.Maximum.Z;
}

float ViewportNavigationController::Radius(
    const ViewportNavigationBounds bounds) noexcept
{
    return std::max(0.5F * Length(bounds.Maximum - bounds.Minimum), 0.5F);
}

} // namespace VoxelForge::Editor
