#include "VoxelStamps/Palette/PaletteMappingEngine.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument Document()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({.Dimensions = {4U, 4U, 4U},
        .Voxels = {{.X = 0U, .Y = 0U, .Z = 0U, .ColorIndex = 5U}}});
    source.Palette[5U] = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U};
    source.HasCustomPalette = true;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, "palette-mapping-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document, "Palette mapping smoke document must load.");
    return std::move(*loaded.Document);
}

} // namespace

int main()
{
    try
    {
        Asset::Voxel::VoxelDocument document = Document();
        const auto snapshotBefore = document.GetPaletteSnapshot();
        const std::uint64_t revisionBefore = document.GetRevision();
        const std::uint64_t voxelCountBefore = document.GetVoxelCount();
        const std::vector<StampPaletteEntry> palette{{.LocalColorId = 0U,
                .Color = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U}},
            {.LocalColorId = 1U, .Color = {.Red = 200U, .Green = 100U, .Blue = 50U, .Alpha = 255U}}};
        const std::vector<StampVoxel> voxels{{.Position = {}, .LocalColorId = 0U},
            {.Position = {.X = 1}, .LocalColorId = 1U}};
        PaletteMappingRequest request{.StampPalette = palette, .StampVoxels = voxels,
            .DocumentPalette = snapshotBefore};
        request.OccupiedDocumentPaletteIndices[5U] = true;

        const auto plan = PaletteMappingEngine::Plan(request);
        Require(plan.IsSuccess() && plan.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 5U}, {1U, 1U}} &&
                plan.Plan.ReusedColorCount == 1U && plan.Plan.AddedColorCount == 1U,
            "Smoke planning must reuse the active color and allocate the smallest free index.");
        Require(plan.Plan.FinalDocumentPalette.Colors[1U] == palette[1].Color &&
                plan.Plan.FinalDocumentPalette.Colors[5U] == palette[0].Color,
            "Smoke plan must expose the final palette without applying it.");
        Require(document.GetPaletteSnapshot() == snapshotBefore && document.GetRevision() == revisionBefore &&
                    document.GetVoxelCount() == voxelCountBefore,
            "Palette planning must not mutate the document snapshot, revision, or voxels.");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
