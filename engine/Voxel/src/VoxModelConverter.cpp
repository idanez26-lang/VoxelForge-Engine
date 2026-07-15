#include "VoxelForge/Voxel/VoxModelConverter.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace VoxelForge::Voxel
{

namespace
{
VoxModelConversionResult Failure(
    const VoxModelConversionError error,
    std::string message)
{
    return {false, error, std::move(message), std::nullopt};
}
}

VoxModelConversionResult VoxModelConverter::Convert(
    const Asset::Vox::VoxModel& source,
    const std::string_view name)
{
    if (source.Models.empty())
    {
        return Failure(
            VoxModelConversionError::EmptyModel,
            "VOX model contains no grids.");
    }

    VoxelModel destination;
    destination.SetName(name);

    // Validate the complete dense payload before allocating the first grid.
    // PACK files therefore share the same 32 MiB v1 payload budget.
    std::uint64_t totalDenseVoxelCount = 0U;

    for (const Asset::Vox::VoxModelMetadata& sourceGrid : source.Models)
    {
        const Asset::Vox::VoxDimensions dimensions = sourceGrid.Dimensions;

        if (dimensions.X == 0U || dimensions.Y == 0U || dimensions.Z == 0U)
        {
            return Failure(
                VoxModelConversionError::InvalidDimensions,
                "VOX grid dimensions must be greater than zero.");
        }

        constexpr std::uint64_t maximum =
            std::numeric_limits<std::uint64_t>::max();
        const std::uint64_t width = dimensions.X;
        const std::uint64_t height = dimensions.Y;
        const std::uint64_t depth = dimensions.Z;

        if (width > maximum / height)
        {
            return Failure(
                VoxModelConversionError::GridTooLarge,
                "VOX grid dimensions overflow the dense voxel count.");
        }

        const std::uint64_t planeSize = width * height;

        if (planeSize > maximum / depth)
        {
            return Failure(
                VoxModelConversionError::GridTooLarge,
                "VOX grid dimensions overflow the dense voxel count.");
        }

        const std::uint64_t gridVoxelCount = planeSize * depth;

        if (gridVoxelCount > VoxelGrid::MaximumVoxelCount ||
            totalDenseVoxelCount >
                VoxelGrid::MaximumVoxelCount - gridVoxelCount)
        {
            return Failure(
                VoxModelConversionError::GridTooLarge,
                "VOX model exceeds the dense Voxel Core allocation limit.");
        }

        totalDenseVoxelCount += gridVoxelCount;

        for (const Asset::Vox::VoxVoxel& sourceVoxel : sourceGrid.Voxels)
        {
            if (sourceVoxel.ColorIndex == 0U ||
                sourceVoxel.X >= dimensions.X ||
                sourceVoxel.Y >= dimensions.Y ||
                sourceVoxel.Z >= dimensions.Z)
            {
                return Failure(
                    VoxModelConversionError::InvalidVoxel,
                    "VOX voxel has invalid coordinates or color index 0.");
            }
        }
    }

    for (std::size_t index = 0U; index < source.Palette.size(); ++index)
    {
        const Asset::Vox::VoxColor& color = source.Palette[index];
        static_cast<void>(destination.Palette().Set(
            index,
            {color.Red, color.Green, color.Blue, color.Alpha}));
    }

    for (const Asset::Vox::VoxModelMetadata& sourceGrid : source.Models)
    {
        const Asset::Vox::VoxDimensions dimensions = sourceGrid.Dimensions;

        VoxelGrid grid;

        if (!grid.Resize(dimensions.X, dimensions.Y, dimensions.Z))
        {
            return Failure(
                VoxModelConversionError::GridTooLarge,
                "VOX grid exceeds the dense Voxel Core allocation limit.");
        }

        // Classic VOX XYZI coordinates already name X, Y and Z explicitly.
        // Voxel Core preserves them without swapping axes or remapping indices.
        for (const Asset::Vox::VoxVoxel& sourceVoxel : sourceGrid.Voxels)
        {
            const Voxel* existing =
                grid.Get(sourceVoxel.X, sourceVoxel.Y, sourceVoxel.Z);

            if (existing != nullptr && existing->IsOccupied())
            {
                return Failure(
                    VoxModelConversionError::DuplicateVoxel,
                    "VOX grid contains duplicate voxel coordinates.");
            }

            if (!grid.Set(
                    sourceVoxel.X,
                    sourceVoxel.Y,
                    sourceVoxel.Z,
                    {sourceVoxel.ColorIndex, Voxel::OccupiedFlag}))
            {
                return Failure(
                    VoxModelConversionError::InvalidVoxel,
                    "Unable to place a VOX voxel in its destination grid.");
            }
        }

        destination.AddGrid(std::move(grid));
    }

    return {
        true,
        VoxModelConversionError::None,
        "VOX model conversion succeeded.",
        std::move(destination)};
}

} // namespace VoxelForge::Voxel
