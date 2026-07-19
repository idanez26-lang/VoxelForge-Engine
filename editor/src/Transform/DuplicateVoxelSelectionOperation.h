#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <string>

namespace VoxelForge::Editor
{

enum class DuplicateVoxelSelectionResultCode : std::uint8_t
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

struct DuplicateVoxelSelectionResult final
{
    DuplicateVoxelSelectionResultCode Code =
        DuplicateVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == DuplicateVoxelSelectionResultCode::Ready;
    }
};

class DuplicateVoxelSelectionOperation final
{
public:
    [[nodiscard]] static DuplicateVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview);
};

[[nodiscard]] const char* DuplicateVoxelSelectionResultCodeName(
    DuplicateVoxelSelectionResultCode code) noexcept;

} // namespace VoxelForge::Editor
