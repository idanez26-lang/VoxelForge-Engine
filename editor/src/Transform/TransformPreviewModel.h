#pragma once

#include "Selection/SelectionService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace VoxelForge::Editor
{

enum class TransformPreviewVoxelState : std::uint8_t
{
    Valid,
    Collision,
    OutOfBounds
};

enum class TransformPreviewCollisionPolicy : std::uint8_t
{
    IgnoreSource,
    IncludeSource
};

struct TransformPreviewVoxel final
{
    Asset::Voxel::VoxelPosition SourcePosition{};
    Asset::Voxel::VoxelPosition PreviewPosition{};
    Asset::Voxel::Voxel Value{};
    TransformPreviewVoxelState State = TransformPreviewVoxelState::Valid;

    [[nodiscard]] bool operator==(
        const TransformPreviewVoxel&) const noexcept = default;
};

struct TransformPreviewRenderPlan final
{
    bool DrawIndividualVoxels = false;
    bool DrawIndividualCollisions = false;
    std::size_t SourceVoxelCount = 0U;
    std::size_t DestinationVoxelCount = 0U;
    std::size_t CollisionVoxelCount = 0U;
    std::size_t OutOfBoundsVoxelCount = 0U;
};

class TransformPreviewRenderPolicy final
{
public:
    static constexpr std::size_t IndividualVoxelLimit = 512U;
    static constexpr std::size_t IndividualCollisionLimit = 256U;

    [[nodiscard]] static TransformPreviewRenderPlan Build(
        std::size_t voxelCount,
        std::size_t collisionCount,
        std::size_t outOfBoundsCount) noexcept;
};

struct TransformPreviewBufferMetrics final
{
    std::size_t CapturedCapacity = 0U;
    std::size_t SourcePositionCapacity = 0U;
    std::size_t CollisionCapacity = 0U;
    std::size_t OutOfBoundsCapacity = 0U;
    std::uint64_t RebuildCount = 0U;
};

struct TransformPreviewRenderData final
{
    std::uint64_t Revision = 0U;
    bool DrawSourceGhost = true;
    std::span<const TransformPreviewVoxel> Voxels;
    std::span<const Asset::Voxel::VoxelColor> Palette;
    SelectionBounds SourceBounds{};
    SelectionBounds PreviewBounds{};
    SelectionBounds CollisionBounds{};
    SelectionBounds OutOfBoundsBounds{};
    TransformPreviewRenderPlan Plan{};
};

// Read-only hand-off for a future atomic transform operation. It deliberately
// exposes no Apply method and owns none of the referenced document data.
struct TransformPreviewOperationData final
{
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t ModelIndex = 0U;
    Asset::Voxel::VoxelPosition Delta{};
    SelectionBounds SourceBounds{};
    SelectionBounds PreviewBounds{};
    std::span<const TransformPreviewVoxel> Voxels;
    std::span<const Asset::Voxel::VoxelPosition> SourcePositions;
    std::span<const Asset::Voxel::VoxelPosition> CollisionPositions;
    std::span<const Asset::Voxel::VoxelPosition> OutOfBoundsPositions;
};

class TransformPreviewModel final
{
public:
    [[nodiscard]] bool BeginPreview(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::size_t modelIndex = 0U,
        TransformPreviewCollisionPolicy collisionPolicy =
            TransformPreviewCollisionPolicy::IgnoreSource);
    [[nodiscard]] bool SetDelta(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        Asset::Voxel::VoxelPosition delta);
    [[nodiscard]] bool IsValidFor(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration) const noexcept;
    [[nodiscard]] bool CancelPreview() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool HasCollisions() const noexcept;
    [[nodiscard]] bool HasOutOfBounds() const noexcept;
    [[nodiscard]] std::size_t VoxelCount() const noexcept;
    [[nodiscard]] std::size_t CollisionCount() const noexcept;
    [[nodiscard]] std::size_t OutOfBoundsCount() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept;
    [[nodiscard]] std::size_t ModelIndex() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition Delta() const noexcept;
    [[nodiscard]] const SelectionBounds& SourceBounds() const noexcept;
    [[nodiscard]] const SelectionBounds& PreviewBounds() const noexcept;
    [[nodiscard]] std::span<const TransformPreviewVoxel> Voxels() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        SourcePositions() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        CollisionPositions() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        OutOfBoundsPositions() const noexcept;
    [[nodiscard]] TransformPreviewRenderData RenderData() const noexcept;
    [[nodiscard]] TransformPreviewOperationData OperationData() const noexcept;
    [[nodiscard]] TransformPreviewBufferMetrics Metrics() const noexcept;

private:
    [[nodiscard]] bool Rebuild(
        const Asset::Voxel::VoxelDocument& document);
    void ClearState() noexcept;

    std::vector<TransformPreviewVoxel> voxels_;
    std::vector<Asset::Voxel::VoxelPosition> sourcePositions_;
    std::vector<Asset::Voxel::VoxelPosition> collisionPositions_;
    std::vector<Asset::Voxel::VoxelPosition> outOfBoundsPositions_;
    std::array<Asset::Voxel::VoxelColor, 256U> palette_{};
    SelectionBounds sourceBounds_{};
    SelectionBounds previewBounds_{};
    SelectionBounds collisionBounds_{};
    SelectionBounds outOfBoundsBounds_{};
    Asset::Voxel::VoxelDimensions dimensions_{};
    Asset::Voxel::VoxelPosition delta_{};
    std::filesystem::path sourcePath_;
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    std::uint64_t renderRevision_ = 0U;
    std::uint64_t rebuildCount_ = 0U;
    std::size_t modelIndex_ = 0U;
    TransformPreviewCollisionPolicy collisionPolicy_ =
        TransformPreviewCollisionPolicy::IgnoreSource;
    bool active_ = false;
};

} // namespace VoxelForge::Editor
