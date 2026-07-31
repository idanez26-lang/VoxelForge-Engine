#include "SelectionProjectionCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace VoxelForge::Editor::InteractionV2
{
namespace
{
[[nodiscard]] bool SameViewport(
    const ViewportRectangle& left, const ViewportRectangle& right) noexcept
{
    return left.X == right.X && left.Y == right.Y &&
        left.Width == right.Width && left.Height == right.Height;
}

[[nodiscard]] bool SameVec3(const Vec3 left, const Vec3 right) noexcept
{
    return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
}

[[nodiscard]] bool PositionLess(
    const Asset::Voxel::VoxelPosition left,
    const Asset::Voxel::VoxelPosition right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}
}

bool SelectionProjectionCache::Matches(
    const Asset::Voxel::VoxelDocument& document,
    const ViewportInputFrame& input) const noexcept
{
    return valid_ &&
        documentGeneration_ == input.DocumentGeneration &&
        documentRevision_ == document.GetRevision() &&
        viewProjection_ == input.ViewProjection &&
        SameViewport(viewport_, input.Viewport) &&
        SameVec3(modelCenter_, input.ModelCenter) &&
        framebufferScale_ == input.FramebufferScale;
}

bool SelectionProjectionCache::Ensure(
    const Asset::Voxel::VoxelDocument& document,
    const ViewportInputFrame& input,
    ViewportInteractionMetrics& metrics)
{
    if (Matches(document, input)) return true;
    const auto* model = document.GetModel(0U);
    if (model == nullptr || input.Viewport.Width <= 0.0F ||
        input.Viewport.Height <= 0.0F ||
        !IsFinite(input.ViewProjection))
    {
        Reset();
        return false;
    }

    const std::size_t previousCapacity = projected_.capacity();
    projected_.clear();
    model->ForEachVoxel(
        [this, &input](const Asset::Voxel::VoxelPosition position,
                      const Asset::Voxel::Voxel)
        {
            const float minimumX = static_cast<float>(position.X) -
                input.ModelCenter.X;
            const float minimumY = static_cast<float>(position.Y) -
                input.ModelCenter.Y;
            const float minimumZ = static_cast<float>(position.Z) -
                input.ModelCenter.Z;
            float screenMinimumX = std::numeric_limits<float>::max();
            float screenMinimumY = std::numeric_limits<float>::max();
            float screenMaximumX = std::numeric_limits<float>::lowest();
            float screenMaximumY = std::numeric_limits<float>::lowest();
            bool anyVisibleDepth = false;
            for (const float x : {minimumX, minimumX + 1.0F})
                for (const float y : {minimumY, minimumY + 1.0F})
                    for (const float z : {minimumZ, minimumZ + 1.0F})
                    {
                        const Vec3 ndc = TransformPoint(
                            input.ViewProjection, {x, y, z});
                        if (!IsFinite(ndc)) continue;
                        anyVisibleDepth = anyVisibleDepth ||
                            (ndc.Z >= 0.0F && ndc.Z <= 1.0F);
                        const float screenX = input.Viewport.X +
                            (ndc.X + 1.0F) * 0.5F * input.Viewport.Width;
                        const float screenY = input.Viewport.Y +
                            (1.0F - ndc.Y) * 0.5F * input.Viewport.Height;
                        screenMinimumX = std::min(screenMinimumX, screenX);
                        screenMinimumY = std::min(screenMinimumY, screenY);
                        screenMaximumX = std::max(screenMaximumX, screenX);
                        screenMaximumY = std::max(screenMaximumY, screenY);
                    }
            if (!anyVisibleDepth ||
                screenMinimumX > input.Viewport.X + input.Viewport.Width ||
                screenMaximumX < input.Viewport.X ||
                screenMinimumY > input.Viewport.Y + input.Viewport.Height ||
                screenMaximumY < input.Viewport.Y)
                return;
            projected_.push_back({
                position,
                {screenMinimumX, screenMinimumY,
                 screenMaximumX, screenMaximumY}});
        });
    std::sort(projected_.begin(), projected_.end(),
        [](const ProjectedVoxel& left, const ProjectedVoxel& right)
        {
            return PositionLess(left.Position, right.Position);
        });
    viewProjection_ = input.ViewProjection;
    viewport_ = input.Viewport;
    modelCenter_ = input.ModelCenter;
    documentGeneration_ = input.DocumentGeneration;
    documentRevision_ = document.GetRevision();
    framebufferScale_ = input.FramebufferScale;
    valid_ = true;
    ++metrics.ProjectionBuilds;
    metrics.ProjectedVoxelCount = projected_.size();
    if (projected_.capacity() > previousCapacity) ++metrics.CapacityGrowths;
    return true;
}

std::span<const Asset::Voxel::VoxelPosition>
SelectionProjectionCache::Query(
    const ScreenRectangle& rectangle,
    ViewportInteractionMetrics& metrics)
{
    const std::size_t previousCapacity = queryResult_.capacity();
    queryResult_.clear();
    for (const ProjectedVoxel& projected : projected_)
    {
        if (rectangle.Intersects(projected.Footprint))
            queryResult_.push_back(projected.Position);
    }
    ++metrics.ProjectionQueries;
    if (queryResult_.capacity() > previousCapacity) ++metrics.CapacityGrowths;
    return queryResult_;
}

void SelectionProjectionCache::Reset() noexcept
{
    projected_.clear();
    queryResult_.clear();
    valid_ = false;
    documentGeneration_ = 0U;
    documentRevision_ = 0U;
}

} // namespace VoxelForge::Editor::InteractionV2
