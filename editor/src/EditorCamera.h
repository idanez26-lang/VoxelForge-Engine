#pragma once

#include "EditorMath.h"
#include "VoxelSelection/VoxelRay.h"

#include <array>
#include <cstdint>

namespace VoxelForge::Editor
{

enum class EditorCameraView : std::uint8_t
{
    // X points right, Y points up and Z points forward. Front observes the
    // origin from +Z; Back from -Z; Left from -X; Right from +X; Top from +Y;
    // Bottom from -Y. Perspective uses the default three-quarter view.
    Perspective,
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom
};

struct EditorCameraState final
{
    Vec3 Position{};
    Vec3 RotationDegrees{};
    float Distance = 0.0F;
    Vec3 Target{};
    EditorCameraView View = EditorCameraView::Perspective;

    [[nodiscard]] bool operator==(
        const EditorCameraState&) const noexcept = default;
};

class EditorCamera final
{
public:
    void Update(bool viewportHovered, float viewportHeight);
    void Orbit(float horizontalPixels, float verticalPixels) noexcept;
    void Pan(float horizontalPixels, float verticalPixels, float viewportHeight) noexcept;
    void Zoom(float wheelDelta) noexcept;
    void Frame(float width, float height, float depth) noexcept;
    void Reset() noexcept;
    void SetView(EditorCameraView view) noexcept;
    void SetAspectRatio(float aspectRatio) noexcept;

    [[nodiscard]] Vec3 GetPosition() const noexcept;
    [[nodiscard]] Vec3 GetForward() const noexcept;
    [[nodiscard]] Vec3 GetRight() const noexcept;
    [[nodiscard]] Vec3 GetUp() const noexcept;
    [[nodiscard]] const Vec3& GetTarget() const noexcept;
    [[nodiscard]] float GetDistance() const noexcept;
    [[nodiscard]] float GetFieldOfViewDegrees() const noexcept;
    [[nodiscard]] EditorCameraView GetView() const noexcept;
    [[nodiscard]] EditorCameraState CaptureState() const noexcept;
    [[nodiscard]] VoxelRay CreateViewportRay(
        float normalizedX,
        float normalizedY) const noexcept;
    [[nodiscard]] std::array<float, 16> GetViewProjection() const noexcept;

private:
    Vec3 target_{};
    float yawDegrees_ = -135.0F;
    float pitchDegrees_ = -28.0F;
    float distance_ = 12.0F;
    float aspectRatio_ = 1.0F;
    float fieldOfViewDegrees_ = 45.0F;
    float orbitSensitivity_ = 0.25F;
    EditorCameraView view_ = EditorCameraView::Perspective;
};

} // namespace VoxelForge::Editor
