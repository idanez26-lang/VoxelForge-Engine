#include "Transform/TransformPlacementPreviewAdapter.h"

#include <array>
#include <new>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{

VoxelPlacementPreview BuildTransformPlacementPreview(
    const TransformPreviewRenderData& preview) noexcept
{
    try
    {
        constexpr std::array<float, 4U> collisionColor{
            1.0F, 0.16F, 0.10F, 1.0F};
        constexpr std::array<float, 4U> outOfBoundsColor{
            1.0F, 0.56F, 0.08F, 1.0F};
        constexpr float scale = 1.0F / 255.0F;

        std::vector<VoxelPreviewInstance> instances;
        instances.reserve(preview.Voxels.size());
        for (const TransformPreviewVoxel& voxel : preview.Voxels)
        {
            VoxelPreviewSemantic semantic = VoxelPreviewSemantic::Valid;
            std::array<float, 4U> color{};
            if (voxel.State == TransformPreviewVoxelState::Collision)
            {
                semantic = VoxelPreviewSemantic::Overlap;
                color = collisionColor;
            }
            else if (voxel.State == TransformPreviewVoxelState::OutOfBounds)
            {
                semantic = VoxelPreviewSemantic::Invalid;
                color = outOfBoundsColor;
            }
            else
            {
                const Asset::Voxel::VoxelColor paletteColor =
                    preview.Palette[voxel.Value.PaletteIndex];
                color = {
                    paletteColor.Red * scale, paletteColor.Green * scale,
                    paletteColor.Blue * scale, 1.0F};
            }
            instances.push_back({voxel.PreviewPosition, semantic, color, 1.0F});
        }
        return VoxelPlacementPreview::FromInstances(
            preview.Revision, std::move(instances));
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

} // namespace VoxelForge::Editor
