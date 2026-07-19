#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace VoxelForge::Editor
{

enum class TransformSourcePolicy : std::uint8_t
{
    RemoveSource,
    PreserveSource
};

enum class TransformCollisionPolicy : std::uint8_t
{
    AllowSourceOverlap,
    RejectAnyOccupiedDestination
};

enum class TransformOperationBuildCode : std::uint8_t
{
    Ready,
    NoChange,
    InvalidPreview,
    InvalidDestinations,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

struct TransformOperationPolicy final
{
    TransformSourcePolicy Source = TransformSourcePolicy::RemoveSource;
    TransformCollisionPolicy Collision =
        TransformCollisionPolicy::AllowSourceOverlap;
};

struct TransformOperationRequest final
{
    std::string_view Name;
    std::string Label;
    TransformOperationPolicy Policy{};
    SelectionBounds DestinationBounds{};
};

struct TransformOperationBuildResult final
{
    TransformOperationBuildCode Code = TransformOperationBuildCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == TransformOperationBuildCode::Ready;
    }
};

// Validates an immutable transform preview and builds one atomic history
// operation. Geometry remains the responsibility of each concrete tool.
class TransformOperationBuilder final
{
public:
    [[nodiscard]] static TransformOperationBuildResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        TransformOperationRequest request);
};

[[nodiscard]] const char* TransformOperationBuildCodeName(
    TransformOperationBuildCode code) noexcept;

} // namespace VoxelForge::Editor
