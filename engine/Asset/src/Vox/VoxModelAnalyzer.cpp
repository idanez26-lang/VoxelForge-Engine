#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"

#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include <array>
#include <system_error>

namespace VoxelForge::Asset::Vox
{

VoxModelAnalysis VoxModelAnalyzer::Analyze(
    const std::filesystem::path& filePath) const
{
    VoxModelAnalysis analysis;
    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(filePath, error);
    if (!error)
    {
        analysis.FileSize = static_cast<std::uint64_t>(fileSize);
    }

    const AssetImportOutcome<VoxModel> outcome = VoxImporter{}.Inspect(filePath);
    if (!outcome.Result.Succeeded || !outcome.Asset)
    {
        analysis.Error = outcome.Result.Message.empty()
            ? "VOX analysis failed."
            : outcome.Result.Message;
        return analysis;
    }

    const VoxModel& model = *outcome.Asset;
    analysis.Valid = true;
    analysis.FormatVersion = model.Version;
    analysis.ModelCount = model.DeclaredModelCount;
    analysis.VoxelCount = model.TotalVoxelCount();
    analysis.HasCustomPalette = model.HasCustomPalette;
    analysis.Models.reserve(model.Models.size());

    std::array<bool, 256U> usedColors{};
    for (const VoxModelMetadata& subModel : model.Models)
    {
        analysis.Models.push_back({
            subModel.Dimensions.X,
            subModel.Dimensions.Y,
            subModel.Dimensions.Z,
            static_cast<std::uint64_t>(subModel.VoxelCount())});
        for (const VoxVoxel& voxel : subModel.Voxels)
        {
            usedColors[voxel.ColorIndex] = true;
        }
    }

    if (!analysis.Models.empty())
    {
        const VoxSubModelAnalysis& primary = analysis.Models.front();
        analysis.SizeX = primary.SizeX;
        analysis.SizeY = primary.SizeY;
        analysis.SizeZ = primary.SizeZ;
    }
    for (const bool used : usedColors)
    {
        if (used) ++analysis.UsedPaletteColorCount;
    }
    return analysis;
}

} // namespace VoxelForge::Asset::Vox
