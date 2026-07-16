#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include "VoxelForge/Asset/AssetImportResult.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include <cstdint>
#include <system_error>
#include <utility>

namespace VoxelForge::Asset::Voxel
{
namespace
{
VoxelDocumentError MapImportError(const AssetImportError error) noexcept
{
    switch (error)
    {
    case AssetImportError::FileNotFound:
        return VoxelDocumentError::FileNotFound;
    case AssetImportError::ReadFailure:
        return VoxelDocumentError::ReadFailure;
    case AssetImportError::InvalidDimensions:
        return VoxelDocumentError::InvalidDimensions;
    case AssetImportError::TooManyModels:
        return VoxelDocumentError::TooManyModels;
    case AssetImportError::TooManyVoxels:
        return VoxelDocumentError::TooManyVoxels;
    case AssetImportError::InvalidSignature:
    case AssetImportError::MissingMainChunk:
        return VoxelDocumentError::InvalidVox;
    case AssetImportError::FileTooSmall:
    case AssetImportError::FileTooLarge:
    case AssetImportError::InvalidHeader:
    case AssetImportError::InvalidChunk:
    case AssetImportError::MissingSizeChunk:
    case AssetImportError::InvalidVoxel:
    case AssetImportError::TooManyChunks:
    case AssetImportError::InconsistentModelCount:
        return VoxelDocumentError::MalformedChunk;
    case AssetImportError::None:
    default:
        return VoxelDocumentError::InvalidVox;
    }
}

VoxDocumentLoadResult Failure(
    const VoxelDocumentError error,
    std::string message,
    std::vector<std::string> warnings = {})
{
    return {error, std::move(message), std::move(warnings), std::nullopt};
}
}

VoxDocumentLoadResult VoxDocumentLoader::Load(
    const std::filesystem::path& sourcePath,
    std::optional<std::string> assetId) const
{
    const AssetImportOutcome<Vox::VoxModel> imported =
        Vox::VoxImporter{}.Inspect(sourcePath);
    if (!imported.Result.Succeeded || !imported.Asset)
    {
        return Failure(
            MapImportError(imported.Result.Error),
            imported.Result.Message.empty()
                ? "Unable to load VOX document."
                : imported.Result.Message,
            imported.Result.Warnings);
    }
    VoxDocumentLoadResult result = Build(
        *imported.Asset, sourcePath, std::move(assetId));
    result.Warnings.insert(
        result.Warnings.begin(),
        imported.Result.Warnings.begin(),
        imported.Result.Warnings.end());
    return result;
}

VoxDocumentLoadResult VoxDocumentLoader::Build(
    const Vox::VoxModel& source,
    const std::filesystem::path& sourcePath,
    std::optional<std::string> assetId) const
{
    if (source.Version != 150U)
    {
        return Failure(
            VoxelDocumentError::UnsupportedVersion,
            "VoxelDocument v1 supports VOX version 150 only.");
    }
    if (source.Models.empty())
    {
        return Failure(
            VoxelDocumentError::InvalidVox,
            "VOX document contains no sub-model.");
    }
    if (source.Models.size() > Vox::MaximumVoxModelCount)
    {
        return Failure(
            VoxelDocumentError::TooManyModels,
            "VOX document exceeds the supported sub-model count.");
    }

    VoxelDocument document;
    std::error_code pathError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(sourcePath, pathError).lexically_normal();
    document.sourcePath_ = pathError ? sourcePath.lexically_normal() : absolute;
    document.assetId_ = std::move(assetId);
    document.voxVersion_ = source.Version;
    document.palette_ = source.Palette;
    document.hasCustomPalette_ = source.HasCustomPalette;
    document.models_.reserve(source.Models.size());

    for (const Vox::VoxModelMetadata& sourceModel : source.Models)
    {
        const Vox::VoxDimensions dimensions = sourceModel.Dimensions;
        if (dimensions.X == 0U || dimensions.Y == 0U ||
            dimensions.Z == 0U || dimensions.X > Vox::MaximumVoxDimension ||
            dimensions.Y > Vox::MaximumVoxDimension ||
            dimensions.Z > Vox::MaximumVoxDimension)
        {
            return Failure(
                VoxelDocumentError::InvalidDimensions,
                "VOX document contains invalid sub-model dimensions.");
        }
        if (sourceModel.Voxels.size() > Vox::MaximumVoxVoxelCount ||
            document.voxelCount_ >
                Vox::MaximumVoxVoxelCount - sourceModel.Voxels.size())
        {
            return Failure(
                VoxelDocumentError::TooManyVoxels,
                "VOX document exceeds the supported voxel count.");
        }

        VoxelSubModel model;
        model.dimensions_ = {dimensions.X, dimensions.Y, dimensions.Z};
        model.voxels_.reserve(sourceModel.Voxels.size());
        for (const Vox::VoxVoxel& sourceVoxel : sourceModel.Voxels)
        {
            const VoxelPosition position{
                sourceVoxel.X, sourceVoxel.Y, sourceVoxel.Z};
            if (sourceVoxel.ColorIndex == 0U)
            {
                return Failure(
                    VoxelDocumentError::InvalidPaletteIndex,
                    "VOX document contains reserved palette index 0.");
            }
            if (!model.Contains(position))
            {
                return Failure(
                    VoxelDocumentError::OutOfBounds,
                    "VOX document contains an invalid voxel.");
            }
            const auto [iterator, inserted] = model.voxels_.emplace(
                position, Voxel{sourceVoxel.ColorIndex});
            static_cast<void>(iterator);
            if (!inserted)
            {
                return Failure(
                    VoxelDocumentError::DuplicateVoxel,
                    "VOX document contains duplicate voxel coordinates.");
            }
            model.ExtendBounds(position);
        }
        document.voxelCount_ += sourceModel.Voxels.size();
        document.models_.push_back(std::move(model));
    }

    return {
        VoxelDocumentError::None,
        "VOX document loaded.",
        {},
        std::move(document)};
}

} // namespace VoxelForge::Asset::Voxel
