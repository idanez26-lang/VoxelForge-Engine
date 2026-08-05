#include "VoxelStamps/Palette/StampDocumentPaletteCache.h"

namespace VoxelForge::Editor::Stamps
{

StampOccupiedPaletteIndices StampDocumentPaletteCache::Scan(
    const Asset::Voxel::VoxelDocument& document) noexcept
{
    StampOccupiedPaletteIndices indices{};
    for (std::size_t modelIndex = 0U; modelIndex < document.GetModelCount();
         ++modelIndex)
    {
        const Asset::Voxel::VoxelSubModel* const model =
            document.GetModel(modelIndex);
        if (model == nullptr)
        {
            continue;
        }
        model->ForEachVoxel(
            [&indices](
                const Asset::Voxel::VoxelPosition,
                const Asset::Voxel::Voxel voxel)
            { indices[voxel.PaletteIndex] = true; });
    }
    return indices;
}

const StampOccupiedPaletteIndices& StampDocumentPaletteCache::Resolve(
    const Asset::Voxel::VoxelDocument& document)
{
    const auto instanceToken =
        reinterpret_cast<std::uintptr_t>(std::addressof(document));
    const std::uint64_t revision = document.GetRevision();
    if (valid_ && documentInstanceToken_ == instanceToken &&
        documentRevision_ == revision)
    {
        ++hitCount_;
        return indices_;
    }

    indices_ = Scan(document);
    documentInstanceToken_ = instanceToken;
    documentRevision_ = revision;
    valid_ = true;
    ++missCount_;
    return indices_;
}

void StampDocumentPaletteCache::Clear() noexcept
{
    indices_ = {};
    documentInstanceToken_ = 0U;
    documentRevision_ = 0U;
    valid_ = false;
}

std::uint64_t StampDocumentPaletteCache::HitCount() const noexcept
{
    return hitCount_;
}

std::uint64_t StampDocumentPaletteCache::MissCount() const noexcept
{
    return missCount_;
}

} // namespace VoxelForge::Editor::Stamps
