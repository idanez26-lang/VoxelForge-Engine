#pragma once

#include <array>

namespace VoxelForge::Editor
{

// Central visual policy shared by editor gizmos.
struct GizmoStyle final
{
    static constexpr std::array<float, 4U> AxisColorX{
        0.910F, 0.357F, 0.357F, 1.0F};
    static constexpr std::array<float, 4U> AxisColorY{
        0.447F, 0.784F, 0.353F, 1.0F};
    static constexpr std::array<float, 4U> AxisColorZ{
        0.310F, 0.561F, 0.918F, 1.0F};

    static constexpr float IdleIntensity = 0.72F;
    static constexpr float HoverIntensity = 0.98F;
    static constexpr float DraggingIntensity = 1.0F;
    static constexpr float InactiveDraggingIntensity = 0.50F;
    static constexpr float OccludedIntensity = 0.50F;

    static constexpr float MoveAxisIdleThicknessPixels = 1.75F;
    static constexpr float MoveAxisHoverThicknessPixels = 2.00F;
    static constexpr float MoveAxisDraggingThicknessPixels = 1.95F;
    static constexpr float MoveArrowLengthRatio = 0.18F;
    static constexpr float MoveArrowWidthRatio = 0.55F;
    static constexpr float MoveCenterDiameterPixels = 3.0F;
    static constexpr float MoveCenterIdleIntensity = 0.40F;
    static constexpr float MoveCenterDraggingIntensity = 0.55F;

    static constexpr float RotateIdleThicknessPixels = 1.65F;
    static constexpr float RotateHoverThicknessPixels = 1.90F;
    static constexpr float RotateDraggingThicknessPixels = 1.85F;
    static constexpr float RotateRadiusMultiplier = 1.30F;
    static constexpr float RotateCenterDiameterPixels = 2.50F;
    static constexpr float RotateCenterIdleIntensity = 0.35F;
    static constexpr float RotateCenterDraggingIntensity = 0.50F;
    static constexpr float RotateMaximumChordPixels = 18.0F;
    static constexpr float RotateNearPlaneDistance = 0.05F;
    static constexpr float PickingTolerancePixels = 8.0F;

    // Compatibility names keep the validated Rotate API stable while both
    // gizmos consume the same visual palette and intensity hierarchy.
    static constexpr auto RotateXColor = AxisColorX;
    static constexpr auto RotateYColor = AxisColorY;
    static constexpr auto RotateZColor = AxisColorZ;
    static constexpr float RotateIdleIntensity = IdleIntensity;
    static constexpr float RotateHoverIntensity = HoverIntensity;
    static constexpr float RotateDraggingIntensity = DraggingIntensity;
    static constexpr float RotateInactiveDraggingIntensity =
        InactiveDraggingIntensity;
    static constexpr float RotateOccludedIntensity = OccludedIntensity;
};

} // namespace VoxelForge::Editor
