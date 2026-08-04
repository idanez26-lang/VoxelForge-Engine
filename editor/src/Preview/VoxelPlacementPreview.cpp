#include "Preview/VoxelPlacementPreview.h"

#include <algorithm>
#include <new>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
void CountInstance(
    VoxelPreviewStats& statistics,
    const VoxelPreviewSemantic semantic) noexcept
{
    ++statistics.Total;
    switch (semantic)
    {
    case VoxelPreviewSemantic::Valid:
        ++statistics.Valid;
        break;
    case VoxelPreviewSemantic::Overlap:
        ++statistics.Overlap;
        break;
    case VoxelPreviewSemantic::Invalid:
        ++statistics.Invalid;
        break;
    case VoxelPreviewSemantic::Source:
        ++statistics.Source;
        break;
    case VoxelPreviewSemantic::Added:
        ++statistics.Added;
        break;
    case VoxelPreviewSemantic::Erased:
        ++statistics.Erased;
        break;
    case VoxelPreviewSemantic::Painted:
        ++statistics.Painted;
        break;
    case VoxelPreviewSemantic::Ignored:
        ++statistics.Ignored;
        break;
    case VoxelPreviewSemantic::Clipped:
        ++statistics.Clipped;
        break;
    }
}
}

std::size_t VoxelPreviewStats::Count(
    const VoxelPreviewSemantic semantic) const noexcept
{
    switch (semantic)
    {
    case VoxelPreviewSemantic::Valid:
        return Valid;
    case VoxelPreviewSemantic::Overlap:
        return Overlap;
    case VoxelPreviewSemantic::Invalid:
        return Invalid;
    case VoxelPreviewSemantic::Source:
        return Source;
    case VoxelPreviewSemantic::Added:
        return Added;
    case VoxelPreviewSemantic::Erased:
        return Erased;
    case VoxelPreviewSemantic::Painted:
        return Painted;
    case VoxelPreviewSemantic::Ignored:
        return Ignored;
    case VoxelPreviewSemantic::Clipped:
        return Clipped;
    }
    return 0U;
}

bool VoxelPreviewStats::IsConsistent() const noexcept
{
    return Total == Valid + Overlap + Invalid + Source + Added + Erased +
                        Painted + Ignored + Clipped;
}

VoxelPlacementPreview VoxelPlacementPreview::FromInstances(
    const std::uint64_t revision,
    std::vector<VoxelPreviewInstance> instances) noexcept
{
    VoxelPlacementPreview result;
    try
    {
        for (VoxelPreviewInstance& instance : instances)
        {
            for (float& channel : instance.Color)
                channel = std::clamp(channel, 0.0F, 1.0F);
            instance.Alpha = std::clamp(instance.Alpha, 0.0F, 1.0F);
            CountInstance(result.statistics_, instance.Semantic);
        }
        result.instances_ =
            std::make_shared<const std::vector<VoxelPreviewInstance>>(
                std::move(instances));
        result.revision_ = revision;
        result.renderMode_ = result.statistics_.Total <= DetailedInstanceLimit
                                 ? VoxelPreviewRenderMode::DetailedInstances
                                 : VoxelPreviewRenderMode::AggregateBounds;
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
    return result;
}

VoxelPlacementPreview VoxelPlacementPreview::Aggregate(
    const std::uint64_t revision,
    const VoxelPreviewStats statistics) noexcept
{
    VoxelPlacementPreview result;
    if (!statistics.IsConsistent()) return result;
    result.revision_ = revision;
    result.statistics_ = statistics;
    result.renderMode_ = VoxelPreviewRenderMode::AggregateBounds;
    result.instancesComplete_ = statistics.Total == 0U;
    return result;
}

VoxelPlacementPreview VoxelPlacementPreview::WithRevision(
    const std::uint64_t revision) const noexcept
{
    VoxelPlacementPreview result = *this;
    result.revision_ = revision;
    return result;
}

std::uint64_t VoxelPlacementPreview::Revision() const noexcept
{
    return revision_;
}

std::span<const VoxelPreviewInstance> VoxelPlacementPreview::Instances()
    const noexcept
{
    return instances_ ? std::span<const VoxelPreviewInstance>(*instances_)
                      : std::span<const VoxelPreviewInstance>{};
}

const VoxelPreviewStats& VoxelPlacementPreview::Statistics() const noexcept
{
    return statistics_;
}

VoxelPreviewRenderMode VoxelPlacementPreview::RenderMode() const noexcept
{
    return renderMode_;
}

bool VoxelPlacementPreview::InstancesComplete() const noexcept
{
    return instancesComplete_;
}

bool VoxelPlacementPreview::IsActive() const noexcept
{
    return statistics_.Total != 0U;
}

} // namespace VoxelForge::Editor
