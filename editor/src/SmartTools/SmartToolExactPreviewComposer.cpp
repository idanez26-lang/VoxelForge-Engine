#include "SmartTools/SmartToolExactPreviewComposer.h"

#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <exception>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
[[nodiscard]] Voxel::VoxelPalette BuildRenderPalette(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelPalette palette;
    const auto& source = document.GetPalette();
    for (std::size_t index = 0U; index < source.size(); ++index)
    {
        static_cast<void>(palette.Set(index, {source[index].Red, source[index].Green,
            source[index].Blue, source[index].Alpha}));
    }
    return palette;
}
}

SmartToolExactPreviewMesh SmartToolExactPreviewComposer::Compose(
    const Asset::Voxel::VoxelDocument& document, const SmartToolPlan& plan)
{
    std::vector<Asset::Voxel::VoxelDocumentChange> changes;
    changes.reserve(plan.Cells().size());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange()) continue;
        changes.push_back({0U, cell.WorldPosition, cell.Before.Exists,
            cell.Before.PaletteIndex, cell.After.Exists, cell.After.PaletteIndex});
    }
    return Compose(document, changes);
}

SmartToolExactPreviewMesh SmartToolExactPreviewComposer::Compose(
    const Asset::Voxel::VoxelDocument& document,
    const std::span<const Asset::Voxel::VoxelDocumentChange> changes)
{
    SmartToolExactPreviewMesh result;
    result.Active = true;
    try
    {
        // The copy is the preview-only final document. Applying the immutable
        // Before -> After cells makes Remove (including the last voxel) use the
        // exact same visible topology as the post-commit document.
        Asset::Voxel::VoxelDocument finalDocument = document;
        if (!changes.empty())
        {
            const Asset::Voxel::VoxelDocumentOperationResult applied =
                finalDocument.ApplyVoxelChanges(changes);
            if (!applied.Succeeded)
            {
                result.Error = "Unable to compose Smart Tool final preview: " +
                    applied.Message;
                return result;
            }
        }
        Mesh::MeshBuildResult built = Mesh::VoxelMeshBuilder::Build(finalDocument);
        if (!built.Succeeded || !built.Mesh)
        {
            result.Error = "Unable to build Smart Tool final preview mesh: " +
                built.Message;
            return result;
        }
        result.Mesh = std::move(*built.Mesh);
        result.Palette = BuildRenderPalette(finalDocument);
    }
    catch (const std::exception& exception)
    {
        result.Error = "Unable to compose Smart Tool final preview: " +
            std::string(exception.what());
    }
    catch (...)
    {
        result.Error = "Unable to compose Smart Tool final preview.";
    }
    return result;
}

const SmartToolExactPreviewMesh& SmartToolExactPreviewCache::Resolve(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentIdentity,
    SmartToolPlanPtr plan)
{
    const std::uint64_t revision = document.GetRevision();
    if (document_ != &document || documentIdentity_ != documentIdentity ||
        documentRevision_ != revision || plan_ != plan)
    {
        document_ = &document;
        documentIdentity_ = documentIdentity;
        documentRevision_ = revision;
        plan_ = std::move(plan);
        mesh_ = plan_ ? SmartToolExactPreviewComposer::Compose(document, *plan_)
                      : SmartToolExactPreviewMesh{};
        ++buildCount_;
    }
    return mesh_;
}

void SmartToolExactPreviewCache::Clear() noexcept
{
    document_ = nullptr;
    documentIdentity_ = 0U;
    documentRevision_ = 0U;
    plan_.reset();
    mesh_ = {};
}

std::size_t SmartToolExactPreviewCache::BuildCount() const noexcept
{
    return buildCount_;
}
} // namespace VoxelForge::Editor
