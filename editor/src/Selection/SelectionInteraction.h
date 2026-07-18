#pragma once

#include "SelectionService.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

enum class SelectionInteractionMode : std::uint8_t
{
    Idle,
    Creating
};

struct SelectionPointerRelease final
{
    bool WasDrag = false;
    std::optional<SelectionBounds> Bounds;
    SelectionMode Operation = SelectionMode::Replace;
};

class SelectionInteraction final
{
public:
    static constexpr float DragThresholdPixels = 4.0F;

    [[nodiscard]] static bool ExceedsDragThreshold(
        float deltaX, float deltaY) noexcept;
    [[nodiscard]] bool PointerDown(
        std::optional<Asset::Voxel::VoxelPosition> target,
        std::uint64_t documentGeneration,
        SelectionMode operation,
        float screenX,
        float screenY) noexcept;
    [[nodiscard]] bool PointerMove(
        float screenX,
        float screenY,
        std::optional<Asset::Voxel::VoxelPosition> target,
        std::optional<Asset::Voxel::VoxelDimensions> dimensions =
            std::nullopt) noexcept;
    [[nodiscard]] SelectionPointerRelease PointerUp() noexcept;
    [[nodiscard]] std::optional<SelectionBounds> Cancel() noexcept;
    [[nodiscard]] bool ValidateDocumentGeneration(
        std::uint64_t documentGeneration) noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsDragRecognized() const noexcept;
    [[nodiscard]] SelectionInteractionMode Mode() const noexcept;
    [[nodiscard]] const SelectionBounds& CurrentBounds() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;

private:
    [[nodiscard]] bool BeginCreating(
        Asset::Voxel::VoxelPosition anchor,
        std::uint64_t documentGeneration,
        SelectionMode mode) noexcept;
    [[nodiscard]] bool Update(Asset::Voxel::VoxelPosition target) noexcept;
    [[nodiscard]] bool RecognizeDrag(float deltaX, float deltaY) noexcept;
    [[nodiscard]] bool ClampCurrentTo(
        Asset::Voxel::VoxelDimensions dimensions) noexcept;
    [[nodiscard]] std::optional<SelectionBounds> Commit() noexcept;
    void Reset() noexcept;

    SelectionInteractionMode mode_ = SelectionInteractionMode::Idle;
    SelectionMode operation_ = SelectionMode::Replace;
    SelectionBounds currentBounds_{};
    Asset::Voxel::VoxelPosition anchor_{};
    std::uint64_t documentGeneration_ = 0U;
    bool dragRecognized_ = false;
    float pointerStartX_ = 0.0F;
    float pointerStartY_ = 0.0F;
};

} // namespace VoxelForge::Editor
