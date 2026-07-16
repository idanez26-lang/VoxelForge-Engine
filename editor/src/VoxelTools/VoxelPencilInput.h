#pragma once

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

enum class VoxelCameraInteraction
{
    None,
    Orbit,
    Pan,
    Zoom
};

struct VoxelToolInputFrame final
{
    bool LeftButtonDown = false;
    bool ToolActive = false;
    bool HasDocument = false;
    bool ViewportHovered = false;
    bool ViewportFocused = false;
    bool InterfaceCapturedMouse = false;
    bool PopupOpen = false;
    bool DragDropActive = false;
    VoxelCameraInteraction CameraInteraction =
        VoxelCameraInteraction::None;
    bool EditInProgress = false;
    std::uint64_t DocumentGeneration = 0U;
};

enum class VoxelToolInputDecision
{
    None,
    Apply
};

class VoxelToolInputController final
{
public:
    [[nodiscard]] VoxelToolInputDecision Update(
        const VoxelToolInputFrame& frame) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool WaitingForRelease() const noexcept;
    [[nodiscard]] bool CameraRearmRequired() const noexcept;

private:
    bool leftButtonWasDown_ = false;
    bool cameraRearmRequired_ = false;
    std::optional<std::uint64_t> pressedDocumentGeneration_;
};

// Compatibility aliases keep the v1 Pencil API stable while the same input
// gate is shared by every single-click voxel tool.
using VoxelPencilInputFrame = VoxelToolInputFrame;
using VoxelPencilInputDecision = VoxelToolInputDecision;
using VoxelPencilInputController = VoxelToolInputController;

} // namespace VoxelForge::Editor
