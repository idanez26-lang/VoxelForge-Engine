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

struct VoxelPencilInputFrame final
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

enum class VoxelPencilInputDecision
{
    None,
    Apply
};

class VoxelPencilInputController final
{
public:
    [[nodiscard]] VoxelPencilInputDecision Update(
        const VoxelPencilInputFrame& frame) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool WaitingForRelease() const noexcept;
    [[nodiscard]] bool CameraRearmRequired() const noexcept;

private:
    bool leftButtonWasDown_ = false;
    bool cameraRearmRequired_ = false;
    std::optional<std::uint64_t> pressedDocumentGeneration_;
};

} // namespace VoxelForge::Editor
