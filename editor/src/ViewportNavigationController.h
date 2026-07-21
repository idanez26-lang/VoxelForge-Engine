#pragma once

#include "EditorCamera.h"

namespace VoxelForge::Editor
{

// World-space bounds supplied by the viewport host. The controller does not
// depend on scene, selection, renderer, transform, or undo abstractions.
struct ViewportNavigationBounds final
{
    Vec3 Minimum{};
    Vec3 Maximum{};
    bool Valid = false;
};

class ViewportNavigationController final
{
public:
    explicit ViewportNavigationController(EditorCamera& camera) noexcept;

    void Orbit(float horizontalPixels, float verticalPixels) noexcept;
    void Pan(float horizontalPixels, float verticalPixels,
        float viewportHeight) noexcept;
    void Zoom(float wheelDelta) noexcept;
    [[nodiscard]] bool FocusSelection(
        ViewportNavigationBounds bounds) noexcept;
    [[nodiscard]] bool FrameAll(ViewportNavigationBounds bounds) noexcept;
    void Tick(float deltaSeconds) noexcept;
    void CancelFocus() noexcept;

    [[nodiscard]] bool IsFocusing() const noexcept;

private:
    [[nodiscard]] bool Focus(ViewportNavigationBounds bounds) noexcept;
    [[nodiscard]] static bool IsValid(
        ViewportNavigationBounds bounds) noexcept;
    [[nodiscard]] static float Radius(
        ViewportNavigationBounds bounds) noexcept;

    EditorCamera& camera_;
    Vec3 focusStartTarget_{};
    Vec3 focusTarget_{};
    float focusStartDistance_ = 12.0F;
    float focusDistance_ = 12.0F;
    float focusElapsedSeconds_ = 0.0F;
    bool focusing_ = false;
};

} // namespace VoxelForge::Editor
