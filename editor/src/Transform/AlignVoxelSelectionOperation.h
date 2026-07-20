#pragma once

#include "MoveVoxelSelectionOperation.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

enum class VoxelAlignDirection : std::uint8_t
{
    Left,
    Right,
    Bottom,
    Top,
    Front,
    Back
};

// Align only determines a translation. Preview validation, collision checks,
// atomic mutation, history and selection transitions remain the Move pipeline's
// responsibility.
class AlignVoxelSelectionOperation final
{
public:
    [[nodiscard]] static std::optional<Asset::Voxel::VoxelPosition>
        CalculateDelta(
            SelectionBounds selectionBounds,
            Asset::Voxel::VoxelDimensions documentDimensions,
            VoxelAlignDirection direction) noexcept;

    [[nodiscard]] static MoveVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview);
};

[[nodiscard]] const char* VoxelAlignDirectionName(
    VoxelAlignDirection direction) noexcept;

} // namespace VoxelForge::Editor
