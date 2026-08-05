#include "SmartTools/SmartToolExactPreviewComposer.h"

#include "VoxelForge/Mesh/VoxelChangeOverlay.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <deque>
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

// VF-0265 (lot 3b) : intersection d'un chunk avec la zone sale, en coordonnées
// inclusives. Renvoie faux quand ils sont disjoints — le chunk est alors
// réutilisé tel quel, sans un seul calcul.
[[nodiscard]] bool IntersectChunk(
    const Mesh::VoxelChunkKey key,
    const Asset::Voxel::VoxelBounds& dirty,
    Asset::Voxel::VoxelPosition& minimum,
    Asset::Voxel::VoxelPosition& maximum) noexcept
{
    if (!dirty.HasValue) return false;
    const Asset::Voxel::VoxelPosition chunkMinimum = Mesh::VoxelChunkMinimum(key);
    const Asset::Voxel::VoxelPosition chunkMaximum = Mesh::VoxelChunkMaximum(key);
    minimum = {std::max(chunkMinimum.X, dirty.Minimum.X),
        std::max(chunkMinimum.Y, dirty.Minimum.Y),
        std::max(chunkMinimum.Z, dirty.Minimum.Z)};
    maximum = {std::min(chunkMaximum.X, dirty.Maximum.X),
        std::min(chunkMaximum.Y, dirty.Maximum.Y),
        std::min(chunkMaximum.Z, dirty.Maximum.Z)};
    return minimum.X <= maximum.X && minimum.Y <= maximum.Y &&
        minimum.Z <= maximum.Z;
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

SmartToolExactPreviewMesh SmartToolExactPreviewComposer::Compose(
    const Source& source, const SmartToolPlan& plan)
{
    std::vector<Asset::Voxel::VoxelDocumentChange> changes;
    changes.reserve(plan.Cells().size());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange()) continue;
        changes.push_back({source.ModelIndex, cell.WorldPosition,
            cell.Before.Exists, cell.Before.PaletteIndex, cell.After.Exists,
            cell.After.PaletteIndex});
    }
    return Compose(source, changes);
}

// VF-0265 (lot 3b) : composition incrémentale.
//
// Le coût ne suit plus la taille du document mais celle de la zone modifiée.
// Trois cas par chunk : disjoint de la zone sale, il est réutilisé tel quel ;
// intersecté, on retire ses faces sales et on ne remaille que la boîte
// concernée ; absent du document alors que la zone sale l'atteint (ajout dans
// le vide), on le construit depuis la vue.
SmartToolExactPreviewMesh SmartToolExactPreviewComposer::Compose(
    const Source& source,
    const std::span<const Asset::Voxel::VoxelDocumentChange> changes)
{
    SmartToolExactPreviewMesh result;
    if (source.Document == nullptr)
    {
        result.Error = "Unable to compose Smart Tool final preview: "
                       "no active document.";
        return result;
    }
    // Sans jeu de chunks, aucune réutilisation n'est possible : on retombe sur
    // le compositeur de référence, qui reste l'oracle des tests.
    if (source.DocumentChunks == nullptr)
    {
        return Compose(*source.Document, changes);
    }

    result.Active = true;
    try
    {
        Asset::Voxel::VoxelDocumentOperationResult validation{};
        const auto overlay = Mesh::VoxelChangeOverlay::TryCreate(
            *source.Document, changes, source.ModelIndex, validation);
        if (!overlay)
        {
            result.Error = "Unable to compose Smart Tool final preview: " +
                validation.Message;
            return result;
        }

        const Asset::Voxel::VoxelBounds& dirty = overlay->DirtyBounds();

        // Deux passes, comme l'assemblage du cache de chunks du document. La
        // première collecte les morceaux sans rien concaténer, la seconde
        // réserve une fois puis concatène. Concaténer au fil de l'eau
        // réallouerait le tampon à CHAQUE morceau — MeshData::Append réserve à
        // la taille exacte — soit des centaines de réallocations sur un document
        // creux étalé sur des centaines de chunks : 398 Mo alloués et 55 ms
        // mesurés avant cette correction.
        std::vector<const Mesh::MeshData*> reused;
        reused.reserve(source.DocumentChunks->size());
        std::size_t faceCount = 0U;

        // Fabrique l'override d'un chunk : les faces propres qu'on garde, plus
        // le maillage de l'état final de la seule boîte sale. Une réservation,
        // deux concaténations.
        const auto makeOverride = [&](const Mesh::MeshData* const chunkMesh,
                                      const Asset::Voxel::VoxelPosition minimum,
                                      const Asset::Voxel::VoxelPosition maximum,
                                      Mesh::MeshData& out) -> bool
        {
            Mesh::MeshData kept;
            if (chunkMesh != nullptr)
            {
                kept = chunkMesh->FacesOutsideBox(minimum.X, minimum.Y,
                    minimum.Z, maximum.X, maximum.Y, maximum.Z);
            }
            Mesh::MeshBuildResult built =
                Mesh::VoxelMeshBuilder::Build(*overlay, minimum, maximum);
            if (!built.Succeeded || !built.Mesh)
            {
                result.Error = "Unable to build Smart Tool final preview mesh: " +
                    built.Message;
                return false;
            }
            out.Reserve(kept.VertexCount() + built.Mesh->VertexCount(),
                kept.IndexCount() + built.Mesh->IndexCount());
            out.Append(kept);
            out.Append(*built.Mesh);
            faceCount += out.FaceCount();
            return true;
        };

        for (const auto& [key, chunkMesh] : *source.DocumentChunks)
        {
            Asset::Voxel::VoxelPosition minimum{};
            Asset::Voxel::VoxelPosition maximum{};
            if (!IntersectChunk(key, dirty, minimum, maximum))
            {
                // Chunk intact : il reste celui du document. Aucun override,
                // donc aucun envoi GPU et aucun changement de dessin.
                faceCount += chunkMesh.FaceCount();
                reused.push_back(&chunkMesh);
                continue;
            }
            SmartToolExactPreviewChunk override{};
            override.Key = key;
            if (!makeOverride(&chunkMesh, minimum, maximum, override.Mesh))
            {
                return result;
            }
            result.Overrides.push_back(std::move(override));
        }

        // Chunks que la zone sale atteint mais que le document ne connaît pas :
        // un ajout dans le vide crée de la matière là où aucun chunk n'existait.
        if (dirty.HasValue)
        {
            const Mesh::VoxelChunkKey first =
                Mesh::VoxelChunkKeyForPosition(dirty.Minimum);
            const Mesh::VoxelChunkKey last =
                Mesh::VoxelChunkKeyForPosition(dirty.Maximum);
            for (std::int32_t z = first.Z; z <= last.Z; ++z)
                for (std::int32_t y = first.Y; y <= last.Y; ++y)
                    for (std::int32_t x = first.X; x <= last.X; ++x)
                    {
                        const Mesh::VoxelChunkKey key{x, y, z};
                        if (source.DocumentChunks->find(key) !=
                            source.DocumentChunks->end())
                        {
                            continue;
                        }
                        Asset::Voxel::VoxelPosition minimum{};
                        Asset::Voxel::VoxelPosition maximum{};
                        if (!IntersectChunk(key, dirty, minimum, maximum))
                        {
                            continue;
                        }
                        SmartToolExactPreviewChunk override{};
                        override.Key = key;
                        if (!makeOverride(
                                nullptr, minimum, maximum, override.Mesh))
                        {
                            return result;
                        }
                        result.Overrides.push_back(std::move(override));
                    }
        }

        // Même plafond global que le cache de chunks du document : on refuse
        // plutôt que de publier un maillage tronqué.
        if (faceCount > Mesh::VoxelMeshBuilder::MaximumFaceCount)
        {
            result.Error = "Smart Tool final preview mesh exceeds the v1 "
                           "face limit.";
            return result;
        }

        // Assemblage du mesh unique : c'est le dernier poste qui suit encore la
        // taille du document. Un consommateur qui dessine les overrides chunk
        // par chunk le désactive et ne paie rien.
        if (source.AssembleMesh)
        {
            std::size_t vertexCount = 0U;
            std::size_t indexCount = 0U;
            for (const Mesh::MeshData* const piece : reused)
            {
                vertexCount += piece->VertexCount();
                indexCount += piece->IndexCount();
            }
            for (const SmartToolExactPreviewChunk& override : result.Overrides)
            {
                vertexCount += override.Mesh.VertexCount();
                indexCount += override.Mesh.IndexCount();
            }
            Mesh::MeshData mesh;
            mesh.Reserve(vertexCount, indexCount);
            for (const Mesh::MeshData* const piece : reused)
            {
                mesh.Append(*piece);
            }
            for (const SmartToolExactPreviewChunk& override : result.Overrides)
            {
                mesh.Append(override.Mesh);
            }
            result.Mesh = std::move(mesh);
        }
        result.Palette = BuildRenderPalette(*source.Document);
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
    // Sans jeu de chunks : chemin de référence, comportement inchangé.
    return Resolve(
        SmartToolExactPreviewComposer::Source{&document, nullptr, 0U},
        documentIdentity, std::move(plan));
}

const SmartToolExactPreviewMesh& SmartToolExactPreviewCache::Resolve(
    const SmartToolExactPreviewComposer::Source& source,
    const std::uint64_t documentIdentity,
    SmartToolPlanPtr plan)
{
    if (source.Document == nullptr)
    {
        Clear();
        return mesh_;
    }
    const std::uint64_t revision = source.Document->GetRevision();
    // VF-0265 (lot 3c) : le jeu de chunks entre dans la clé. Changer de source
    // de chunks — modèle rechargé, cache vidé — doit recomposer, sinon la
    // preview réutiliserait des chunks qui ne décrivent plus ce document.
    if (document_ != source.Document || documentIdentity_ != documentIdentity ||
        documentRevision_ != revision || documentChunks_ != source.DocumentChunks ||
        modelIndex_ != source.ModelIndex || plan_ != plan)
    {
        document_ = source.Document;
        documentIdentity_ = documentIdentity;
        documentRevision_ = revision;
        documentChunks_ = source.DocumentChunks;
        modelIndex_ = source.ModelIndex;
        plan_ = std::move(plan);
        mesh_ = plan_
            ? SmartToolExactPreviewComposer::Compose(source, *plan_)
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
    documentChunks_ = nullptr;
    modelIndex_ = 0U;
    plan_.reset();
    mesh_ = {};
}

std::size_t SmartToolExactPreviewCache::BuildCount() const noexcept
{
    return buildCount_;
}
} // namespace VoxelForge::Editor
