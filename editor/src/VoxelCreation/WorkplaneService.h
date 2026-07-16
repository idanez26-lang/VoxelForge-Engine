#pragma once

#include "VoxelSelection/VoxelRay.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

enum class WorkplaneAxis : std::uint8_t
{
    X,
    Y,
    Z
};

struct WorkplaneDefinition final
{
    WorkplaneAxis Axis = WorkplaneAxis::Y;
    std::int32_t Coordinate = 0;

    [[nodiscard]] bool operator==(
        const WorkplaneDefinition&) const noexcept = default;
};

struct WorkplaneGrid final
{
    WorkplaneDefinition Definition{};
    std::uint32_t ColumnCount = 0U;
    std::uint32_t RowCount = 0U;

    [[nodiscard]] bool operator==(
        const WorkplaneGrid&) const noexcept = default;
};

enum class WorkplaneHitStatus : std::uint8_t
{
    Valid,
    InvalidDocument,
    UnsupportedDefinition,
    Parallel,
    BehindRay,
    OutOfBounds,
    Occupied
};

struct WorkplaneHit final
{
    WorkplaneHitStatus Status = WorkplaneHitStatus::InvalidDocument;
    std::optional<Asset::Voxel::VoxelPosition> Position;
    float Distance = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] bool operator==(
        const WorkplaneHit&) const noexcept = default;
};

class WorkplaneService final
{
public:
    explicit WorkplaneService(
        WorkplaneDefinition definition = {}) noexcept;

    void SetDefinition(WorkplaneDefinition definition) noexcept;
    [[nodiscard]] const WorkplaneDefinition& Definition() const noexcept;

    [[nodiscard]] std::optional<WorkplaneGrid> Grid(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t subModelIndex = 0U) const noexcept;
    [[nodiscard]] WorkplaneHit Intersect(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t subModelIndex,
        const VoxelRay& worldRay,
        Vec3 modelCenter) const noexcept;
    [[nodiscard]] WorkplaneHitStatus ValidateTarget(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t subModelIndex,
        Asset::Voxel::VoxelPosition position) const noexcept;

private:
    [[nodiscard]] bool IsSupportedV1() const noexcept;
    WorkplaneDefinition definition_{};
};

} // namespace VoxelForge::Editor
