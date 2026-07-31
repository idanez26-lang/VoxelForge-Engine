#pragma once

#include "EditorMatrix.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor::InteractionV2
{

struct ViewportInputFrame final
{
    std::uint64_t Frame = 0U;
    Vec2 MouseScreen{};
    bool PrimaryPressed = false;
    bool PrimaryHeld = false;
    bool PrimaryReleased = false;
    bool EscapePressed = false;
    bool Control = false;
    bool Shift = false;
    bool ViewportHovered = false;
    bool ViewportFocused = false;
    bool UiCapturesPointer = false;
    bool CameraActive = false;
    bool SelectionToolActive = false;
    bool MoveToolActive = false;
    ViewportRectangle Viewport{};
    Matrix4 ViewProjection = IdentityMatrix();
    Vec3 CameraWorldPosition{};
    Vec3 ModelCenter{};
    float FramebufferScale = 1.0F;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::optional<VoxelRay> PointerRay;
};

} // namespace VoxelForge::Editor::InteractionV2
