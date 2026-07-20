#pragma once

#include <array>

namespace VoxelForge::Editor
{

// Central visual policy for editor gizmos. Mission 28 applies the Rotate
// values only; Move keeps its validated appearance unchanged.
struct GizmoStyle final
{
    static constexpr std::array<float, 4U> RotateXColor{
        0.910F, 0.357F, 0.357F, 1.0F};
    static constexpr std::array<float, 4U> RotateYColor{
        0.447F, 0.784F, 0.353F, 1.0F};
    static constexpr std::array<float, 4U> RotateZColor{
        0.310F, 0.561F, 0.918F, 1.0F};

    static constexpr float RotateIdleThicknessPixels = 1.65F;
    static constexpr float RotateHoverThicknessPixels = 1.90F;
    static constexpr float RotateDraggingThicknessPixels = 1.85F;
    static constexpr float RotateIdleIntensity = 0.72F;
    static constexpr float RotateHoverIntensity = 0.98F;
    static constexpr float RotateDraggingIntensity = 1.0F;
    static constexpr float RotateInactiveDraggingIntensity = 0.50F;
    static constexpr float RotateOccludedIntensity = 0.50F;
    static constexpr float RotateRadiusMultiplier = 1.30F;
    static constexpr float RotateCenterDiameterPixels = 2.50F;
    static constexpr float RotateCenterIdleIntensity = 0.35F;
    static constexpr float RotateCenterDraggingIntensity = 0.50F;
    static constexpr float RotateMaximumChordPixels = 18.0F;
    static constexpr float RotateNearPlaneDistance = 0.05F;
    static constexpr float PickingTolerancePixels = 8.0F;
};

} // namespace VoxelForge::Editor
