#include "SelectionProjectionCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

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

// VF-STAB-01 bug 5 — near-plane clipping of a voxel's screen footprint.
//
// TransformPoint only returns NaN when |w| <= 1e-8. For a corner BEHIND the
// camera w is negative and the perspective divide returns a finite, sign-flipped
// point, so accumulating all eight corners blindly inflates or mirrors the
// footprint and box-selection then picks up (or drops) the wrong voxels.
//
// The projection uses the D3D depth convention (the visible test below is
// ndc.Z in [0,1]), so clip space keeps 0 <= z <= w and the near plane is the
// z = 0 half-space. We clip the cube's 12 edges against that plane and project
// only what survives, which keeps a deterministic footprint for a cube that
// straddles the near plane instead of discarding it or letting it wrap around.
struct ClipVertex final
{
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;
    float W = 0.0F;
};

[[nodiscard]] ClipVertex TransformToClip(
    const Matrix4& matrix, const Vec3 point) noexcept
{
    return {
        matrix[0] * point.X + matrix[1] * point.Y +
            matrix[2] * point.Z + matrix[3],
        matrix[4] * point.X + matrix[5] * point.Y +
            matrix[6] * point.Z + matrix[7],
        matrix[8] * point.X + matrix[9] * point.Y +
            matrix[10] * point.Z + matrix[11],
        matrix[12] * point.X + matrix[13] * point.Y +
            matrix[14] * point.Z + matrix[15]};
}

[[nodiscard]] ClipVertex LerpClip(
    const ClipVertex& from, const ClipVertex& to, const float t) noexcept
{
    return {
        from.X + (to.X - from.X) * t,
        from.Y + (to.Y - from.Y) * t,
        from.Z + (to.Z - from.Z) * t,
        from.W + (to.W - from.W) * t};
}

// The 12 edges of the unit cube, as index pairs over the 8 corners enumerated
// as bit 0 = X, bit 1 = Y, bit 2 = Z.
constexpr std::array<std::pair<std::size_t, std::size_t>, 12U> CubeEdges{{
    {0U, 1U}, {2U, 3U}, {4U, 5U}, {6U, 7U},
    {0U, 2U}, {1U, 3U}, {4U, 6U}, {5U, 7U},
    {0U, 4U}, {1U, 5U}, {2U, 6U}, {3U, 7U}}};
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

            std::array<ClipVertex, 8U> corners{};
            bool allInFront = true;
            bool anyInFront = false;
            for (std::size_t index = 0U; index < 8U; ++index)
            {
                const float x = minimumX +
                    ((index & 1U) != 0U ? 1.0F : 0.0F);
                const float y = minimumY +
                    ((index & 2U) != 0U ? 1.0F : 0.0F);
                const float z = minimumZ +
                    ((index & 4U) != 0U ? 1.0F : 0.0F);
                corners[index] =
                    TransformToClip(input.ViewProjection, {x, y, z});
                const bool inFront = corners[index].Z >= 0.0F;
                allInFront = allInFront && inFront;
                anyInFront = anyInFront || inFront;
            }
            // Entirely behind the near plane: nothing of this voxel is on
            // screen, and every corner would project sign-flipped.
            if (!anyInFront) return;

            const auto accumulate = [&](const ClipVertex& clip)
            {
                if (!std::isfinite(clip.W) || std::abs(clip.W) <= 1.0e-8F)
                    return;
                const Vec3 ndc{clip.X / clip.W, clip.Y / clip.W,
                    clip.Z / clip.W};
                if (!IsFinite(ndc)) return;
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
            };

            if (allInFront)
            {
                // Fast path, and the overwhelmingly common one: no edge of the
                // cube crosses the near plane, so no clipping work is needed.
                for (const ClipVertex& corner : corners) accumulate(corner);
            }
            else
            {
                // The cube straddles the near plane. Clip each edge at z = 0 so
                // the footprint covers exactly the visible part of the cube.
                for (const auto& [from, to] : CubeEdges)
                {
                    const ClipVertex& start = corners[from];
                    const ClipVertex& end = corners[to];
                    const bool startInFront = start.Z >= 0.0F;
                    const bool endInFront = end.Z >= 0.0F;
                    if (startInFront) accumulate(start);
                    if (startInFront == endInFront) continue;
                    const float span = start.Z - end.Z;
                    if (!std::isfinite(span) || std::abs(span) <= 1.0e-8F)
                        continue;
                    const float t = start.Z / span;
                    if (!std::isfinite(t) || t < 0.0F || t > 1.0F) continue;
                    accumulate(LerpClip(start, end, t));
                }
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
