// VF-0262 lot 262-0 — profiling fixture, deliberately not a CTest.
// Compares, per document size, the cost of the full document mesh build
// (today's per-commit behaviour) with a 32^3 region build (the unit the
// incremental cache of lots 262-2/262-3 will pay per edit). The residual
// region cost also exposes what stays O(document): the collection traversal.

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelDocument;
using VoxelForge::Asset::Voxel::VoxDocumentLoader;
using VoxelForge::Asset::Vox::DefaultVoxPalette;
using VoxelForge::Asset::Vox::VoxModel;
using VoxelForge::Asset::Vox::VoxModelMetadata;
using VoxelForge::Asset::Vox::VoxVoxel;
using VoxelForge::Mesh::VoxelMeshBuilder;

constexpr int RegionSize = 32;
constexpr std::size_t RunsPerMeasure = 7U;

VoxelDocument SolidCubeDocument(const std::uint32_t edge)
{
    std::vector<VoxVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(edge) * edge * edge);
    for (std::uint32_t z = 0U; z < edge; ++z)
        for (std::uint32_t y = 0U; y < edge; ++y)
            for (std::uint32_t x = 0U; x < edge; ++x)
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    static_cast<std::uint8_t>(1U + (x + y + z) % 8U)});
    VoxModel source;
    source.Version = 150U;
    source.Palette = DefaultVoxPalette();
    source.Models.push_back(VoxModelMetadata{
        {edge, edge, edge}, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = VoxDocumentLoader{}.Build(source, "benchmark.vox");
    if (!loaded.Succeeded())
    {
        std::fprintf(stderr, "Fixture construction failed: %s\n",
            loaded.Message.c_str());
        std::exit(1);
    }
    return std::move(*loaded.Document);
}

template <typename Callable>
double MedianMilliseconds(const Callable& callable)
{
    std::vector<double> samples;
    samples.reserve(RunsPerMeasure);
    for (std::size_t run = 0U; run < RunsPerMeasure; ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        callable();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2U];
}

} // namespace

int main()
{
    std::printf(
        "voxels,full_build_ms,region32_build_ms,ratio\n");
    for (const std::uint32_t edge : {25U, 40U, 51U, 64U, 79U, 100U})
    {
        const VoxelDocument document = SolidCubeDocument(edge);
        const std::uint64_t voxelCount = document.GetVoxelCount();

        const double fullMilliseconds = MedianMilliseconds(
            [&document]
            {
                const auto built = VoxelMeshBuilder::Build(document);
                if (!built.Succeeded) std::exit(2);
            });

        // Region anchored on the cube's corner: exercises both interior
        // suppression and open faces, like a real edit near a surface.
        const int regionMaximum = std::min<int>(
            RegionSize - 1, static_cast<int>(edge) - 1);
        const double regionMilliseconds = MedianMilliseconds(
            [&document, regionMaximum]
            {
                const auto built = VoxelMeshBuilder::Build(
                    document, {0, 0, 0},
                    {regionMaximum, regionMaximum, regionMaximum});
                if (!built.Succeeded) std::exit(3);
            });

        std::printf("%llu,%.3f,%.3f,%.2f\n",
            static_cast<unsigned long long>(voxelCount),
            fullMilliseconds, regionMilliseconds,
            regionMilliseconds > 0.0
                ? fullMilliseconds / regionMilliseconds : 0.0);
        std::fflush(stdout);
    }
    return 0;
}
