#include "TransformPreviewModel.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace VoxelForge::Editor
{
namespace
{
[[nodiscard]] bool PositionLess(
    const Asset::Voxel::VoxelPosition left,
    const Asset::Voxel::VoxelPosition right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] SelectionBounds BoundsOf(
    const std::span<const Asset::Voxel::VoxelPosition> positions) noexcept
{
    if (positions.empty()) return {};
    Asset::Voxel::VoxelPosition minimum = positions.front();
    Asset::Voxel::VoxelPosition maximum = positions.front();
    for (const auto position : positions.subspan(1U))
    {
        minimum.X = std::min(minimum.X, position.X);
        minimum.Y = std::min(minimum.Y, position.Y);
        minimum.Z = std::min(minimum.Z, position.Z);
        maximum.X = std::max(maximum.X, position.X);
        maximum.Y = std::max(maximum.Y, position.Y);
        maximum.Z = std::max(maximum.Z, position.Z);
    }
    return SelectionBounds::FromCorners(minimum, maximum);
}

[[nodiscard]] Asset::Voxel::VoxelPosition AddSafely(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelPosition delta,
    bool& representable) noexcept
{
    const auto add = [&representable](
        const std::int32_t value,
        const std::int32_t offset) noexcept
    {
        const std::int64_t result =
            static_cast<std::int64_t>(value) + offset;
        if (result < std::numeric_limits<std::int32_t>::min() ||
            result > std::numeric_limits<std::int32_t>::max())
            representable = false;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(result,
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max()));
    };
    return {
        add(position.X, delta.X),
        add(position.Y, delta.Y),
        add(position.Z, delta.Z)};
}

[[nodiscard]] bool InBounds(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        position.X < static_cast<std::int32_t>(dimensions.X) &&
        position.Y < static_cast<std::int32_t>(dimensions.Y) &&
        position.Z < static_cast<std::int32_t>(dimensions.Z);
}
}

TransformPreviewRenderPlan TransformPreviewRenderPolicy::Build(
    const std::size_t voxelCount,
    const std::size_t collisionCount,
    const std::size_t outOfBoundsCount) noexcept
{
    return {
        voxelCount <= IndividualVoxelLimit,
        collisionCount + outOfBoundsCount <= IndividualCollisionLimit,
        voxelCount,
        voxelCount,
        collisionCount,
        outOfBoundsCount};
}

bool TransformPreviewModel::BeginPreview(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const std::size_t modelIndex)
{
    ClearState();
    const auto dimensions = document.GetDimensions(modelIndex);
    if (documentGeneration == 0U || selection.Empty() ||
        selection.DocumentGeneration() != documentGeneration || !dimensions)
        return false;

    const auto selected = selection.Voxels();
    // Capturing is intentionally atomic: one stale/missing selected voxel
    // rejects the whole preview instead of silently moving only a subset.
    voxels_.reserve(selected.size());
    sourcePositions_.reserve(selected.size());
    collisionPositions_.reserve(selected.size());
    outOfBoundsPositions_.reserve(selected.size());
    for (const auto position : selected)
    {
        const auto voxel = document.GetVoxel(position, modelIndex);
        if (!voxel)
        {
            ClearState();
            return false;
        }
        sourcePositions_.push_back(position);
        voxels_.push_back({position, position, *voxel,
            TransformPreviewVoxelState::Valid});
    }
    if (voxels_.empty()) return false;

    sourceBounds_ = BoundsOf(sourcePositions_);
    previewBounds_ = sourceBounds_;
    dimensions_ = *dimensions;
    palette_ = document.GetPalette();
    sourcePath_ = document.SourcePath().lexically_normal();
    documentGeneration_ = documentGeneration;
    documentRevision_ = document.GetRevision();
    modelIndex_ = modelIndex;
    active_ = true;
    ++renderRevision_;
    ++rebuildCount_;
    return true;
}

bool TransformPreviewModel::SetDelta(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const Asset::Voxel::VoxelPosition delta)
{
    if (!IsValidFor(document, selection, documentGeneration) ||
        delta == delta_)
        return false;
    delta_ = delta;
    return Rebuild(document);
}

bool TransformPreviewModel::IsValidFor(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration) const noexcept
{
    if (!active_ || documentGeneration == 0U ||
        documentGeneration_ != documentGeneration ||
        selection.DocumentGeneration() != documentGeneration ||
        documentRevision_ != document.GetRevision() ||
        modelIndex_ >= document.GetModelCount() ||
        sourcePath_ != document.SourcePath().lexically_normal() ||
        document.GetDimensions(modelIndex_) !=
            std::optional<Asset::Voxel::VoxelDimensions>(dimensions_))
        return false;
    const auto selectionPositions = selection.Voxels();
    if (selectionPositions.size() != sourcePositions_.size() ||
        !std::equal(selectionPositions.begin(), selectionPositions.end(),
            sourcePositions_.begin()))
        return false;
    for (std::size_t index = 0U; index < voxels_.size(); ++index)
    {
        const auto voxel = document.GetVoxel(
            sourcePositions_[index], modelIndex_);
        if (!voxel || *voxel != voxels_[index].Value) return false;
    }
    return true;
}

bool TransformPreviewModel::CancelPreview() noexcept
{
    if (!active_) return false;
    ClearState();
    return true;
}

void TransformPreviewModel::Reset() noexcept
{
    ClearState();
}

bool TransformPreviewModel::Rebuild(
    const Asset::Voxel::VoxelDocument& document)
{
    collisionPositions_.clear();
    outOfBoundsPositions_.clear();
    for (std::size_t index = 0U; index < voxels_.size(); ++index)
    {
        bool representable = true;
        const Asset::Voxel::VoxelPosition destination = AddSafely(
            sourcePositions_[index], delta_, representable);
        TransformPreviewVoxel& voxel = voxels_[index];
        voxel.PreviewPosition = destination;
        if (!representable || !InBounds(destination, dimensions_))
        {
            voxel.State = TransformPreviewVoxelState::OutOfBounds;
            outOfBoundsPositions_.push_back(destination);
            continue;
        }
        const bool belongsToSource = std::binary_search(
            sourcePositions_.begin(), sourcePositions_.end(),
            destination, PositionLess);
        if (!belongsToSource && document.HasVoxel(destination, modelIndex_))
        {
            voxel.State = TransformPreviewVoxelState::Collision;
            collisionPositions_.push_back(destination);
        }
        else
        {
            voxel.State = TransformPreviewVoxelState::Valid;
        }
    }

    if (voxels_.empty())
    {
        previewBounds_ = {};
    }
    else
    {
        Asset::Voxel::VoxelPosition minimum = voxels_.front().PreviewPosition;
        Asset::Voxel::VoxelPosition maximum = minimum;
        for (const auto& voxel :
             std::span<TransformPreviewVoxel>(voxels_).subspan(1U))
        {
            const auto position = voxel.PreviewPosition;
            minimum.X = std::min(minimum.X, position.X);
            minimum.Y = std::min(minimum.Y, position.Y);
            minimum.Z = std::min(minimum.Z, position.Z);
            maximum.X = std::max(maximum.X, position.X);
            maximum.Y = std::max(maximum.Y, position.Y);
            maximum.Z = std::max(maximum.Z, position.Z);
        }
        previewBounds_ = SelectionBounds::FromCorners(minimum, maximum);
    }
    collisionBounds_ = BoundsOf(collisionPositions_);
    outOfBoundsBounds_ = BoundsOf(outOfBoundsPositions_);
    ++renderRevision_;
    ++rebuildCount_;
    return true;
}

void TransformPreviewModel::ClearState() noexcept
{
    const bool hadState = active_ || !voxels_.empty() ||
        !sourcePositions_.empty() || !collisionPositions_.empty() ||
        !outOfBoundsPositions_.empty();
    voxels_.clear();
    sourcePositions_.clear();
    collisionPositions_.clear();
    outOfBoundsPositions_.clear();
    sourceBounds_ = {};
    previewBounds_ = {};
    collisionBounds_ = {};
    outOfBoundsBounds_ = {};
    dimensions_ = {};
    delta_ = {};
    sourcePath_.clear();
    documentGeneration_ = 0U;
    documentRevision_ = 0U;
    modelIndex_ = 0U;
    active_ = false;
    if (hadState) ++renderRevision_;
}

bool TransformPreviewModel::IsActive() const noexcept { return active_; }
bool TransformPreviewModel::HasCollisions() const noexcept
{
    return !collisionPositions_.empty();
}
bool TransformPreviewModel::HasOutOfBounds() const noexcept
{
    return !outOfBoundsPositions_.empty();
}
std::size_t TransformPreviewModel::VoxelCount() const noexcept
{
    return voxels_.size();
}
std::size_t TransformPreviewModel::CollisionCount() const noexcept
{
    return collisionPositions_.size();
}
std::size_t TransformPreviewModel::OutOfBoundsCount() const noexcept
{
    return outOfBoundsPositions_.size();
}
std::uint64_t TransformPreviewModel::DocumentGeneration() const noexcept
{
    return documentGeneration_;
}
std::uint64_t TransformPreviewModel::DocumentRevision() const noexcept
{
    return documentRevision_;
}
std::size_t TransformPreviewModel::ModelIndex() const noexcept
{
    return modelIndex_;
}
Asset::Voxel::VoxelPosition TransformPreviewModel::Delta() const noexcept
{
    return delta_;
}
const SelectionBounds& TransformPreviewModel::SourceBounds() const noexcept
{
    return sourceBounds_;
}
const SelectionBounds& TransformPreviewModel::PreviewBounds() const noexcept
{
    return previewBounds_;
}
std::span<const TransformPreviewVoxel>
TransformPreviewModel::Voxels() const noexcept
{
    return voxels_;
}
std::span<const Asset::Voxel::VoxelPosition>
TransformPreviewModel::SourcePositions() const noexcept
{
    return sourcePositions_;
}
std::span<const Asset::Voxel::VoxelPosition>
TransformPreviewModel::CollisionPositions() const noexcept
{
    return collisionPositions_;
}
std::span<const Asset::Voxel::VoxelPosition>
TransformPreviewModel::OutOfBoundsPositions() const noexcept
{
    return outOfBoundsPositions_;
}

TransformPreviewRenderData TransformPreviewModel::RenderData() const noexcept
{
    return {
        renderRevision_, voxels_, palette_, sourceBounds_, previewBounds_,
        collisionBounds_, outOfBoundsBounds_,
        TransformPreviewRenderPolicy::Build(
            voxels_.size(), collisionPositions_.size(),
            outOfBoundsPositions_.size())};
}

TransformPreviewOperationData TransformPreviewModel::OperationData()
    const noexcept
{
    return {
        documentGeneration_, documentRevision_, modelIndex_, delta_,
        sourceBounds_, previewBounds_, voxels_, sourcePositions_,
        collisionPositions_, outOfBoundsPositions_};
}

TransformPreviewBufferMetrics TransformPreviewModel::Metrics() const noexcept
{
    return {
        voxels_.capacity(), sourcePositions_.capacity(),
        collisionPositions_.capacity(), outOfBoundsPositions_.capacity(),
        rebuildCount_};
}

} // namespace VoxelForge::Editor
