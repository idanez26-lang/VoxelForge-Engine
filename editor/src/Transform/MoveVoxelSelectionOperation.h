#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <string>

namespace VoxelForge::Editor
{

enum class MoveVoxelSelectionResultCode : std::uint8_t
{
    Ready,
    NoChange,
    InvalidPreview,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

struct MoveVoxelSelectionResult final
{
    MoveVoxelSelectionResultCode Code = MoveVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == MoveVoxelSelectionResultCode::Ready;
    }
};

class MoveVoxelSelectionOperation final
{
public:
    [[nodiscard]] static MoveVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview);
};

[[nodiscard]] const char* MoveVoxelSelectionResultCodeName(
    MoveVoxelSelectionResultCode code) noexcept;

} // namespace VoxelForge::Editor
