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

bool SmartToolExactPreviewChunkCache::ChunkSignature::operator==(
    const ChunkSignature& other) const noexcept
{
    return Count == other.Count && Checksum == other.Checksum &&
        Bounds == other.Bounds;
}

std::size_t SmartToolExactPreviewChunkCache::KeyHash::operator()(
    const Mesh::VoxelChunkKey key) const noexcept
{
    // Melange simple mais disperse : les cles voisines ne doivent pas se
    // retrouver dans le meme seau, sinon un trait rectiligne degenere la table.
    std::uint64_t hash = 0x9E3779B97F4A7C15ULL;
    const auto mix = [&hash](const std::int32_t value) noexcept
    {
        hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(value));
        hash *= 0xFF51AFD7ED558CCDULL;
        hash ^= hash >> 29;
    };
    mix(key.X); mix(key.Y); mix(key.Z);
    return static_cast<std::size_t>(hash);
}

bool SmartToolExactPreviewChunkCache::Retarget(const void* const document,
    const std::uint64_t revision, const std::size_t modelIndex,
    const void* const chunkSet) noexcept
{
    if (document_ == document && documentRevision_ == revision &&
        modelIndex_ == modelIndex && chunkSet_ == chunkSet)
    {
        return true;
    }
    // Le document a bouge sous nos pieds : tout override en cache decrit un
    // etat qui n'existe plus. Le garder afficherait une preview fausse.
    entries_.clear();
    document_ = document;
    documentRevision_ = revision;
    modelIndex_ = modelIndex;
    chunkSet_ = chunkSet;
    return false;
}

const SmartToolExactPreviewChunk* SmartToolExactPreviewChunkCache::Find(
    const Mesh::VoxelChunkKey key,
    const ChunkSignature& signature) const noexcept
{
    const auto found = entries_.find(key);
    if (found == entries_.end()) return nullptr;
    if (!(found->second.Signature == signature)) return nullptr;
    ++hits_;
    return &found->second.Chunk;
}

void SmartToolExactPreviewChunkCache::Store(
    SmartToolExactPreviewChunk chunk, const ChunkSignature& signature)
{
    const Mesh::VoxelChunkKey key = chunk.Key;
    entries_[key] = Entry{std::move(chunk), signature};
}

void SmartToolExactPreviewChunkCache::RetainOnly(
    const std::vector<Mesh::VoxelChunkKey>& keys)
{
    if (entries_.size() == keys.size()) return;
    std::unordered_map<Mesh::VoxelChunkKey, Entry, KeyHash> kept;
    kept.reserve(keys.size());
    for (const Mesh::VoxelChunkKey key : keys)
    {
        const auto found = entries_.find(key);
        if (found != entries_.end()) kept.emplace(key, std::move(found->second));
    }
    entries_ = std::move(kept);
}

void SmartToolExactPreviewChunkCache::Clear() noexcept
{
    entries_.clear();
    document_ = nullptr;
    documentRevision_ = 0U;
    modelIndex_ = 0U;
    chunkSet_ = nullptr;
    hits_ = 0U;
    rebuilds_ = 0U;
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

        // LOT 4c : signature par chunk. Un changement en p concerne tout chunk
        // dont la dilatation de 1 contient p — donc son propre chunk, et les
        // voisins quand p est sur une frontiere. On parcourt les changements UNE
        // fois, en travail entier, sans hachage de position ni tri.
        using Signature = SmartToolExactPreviewChunkCache::ChunkSignature;
        std::map<Mesh::VoxelChunkKey, Signature> signatures;
        for (const Asset::Voxel::VoxelDocumentChange& change : changes)
        {
            const Asset::Voxel::VoxelPosition p = change.Position;
            // Somme de controle du CONTENU : repeindre un voxel deja touche
            // d'une autre couleur doit invalider le chunk, alors que ni le
            // compte ni la boite englobante ne bougeraient.
            std::uint64_t hash = 0x9E3779B97F4A7C15ULL;
            const auto mix = [&hash](const std::uint64_t value) noexcept
            {
                hash ^= value;
                hash *= 0xFF51AFD7ED558CCDULL;
                hash ^= hash >> 29;
            };
            mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.X)));
            mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.Y)));
            mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.Z)));
            mix(change.ExistsAfter ? 0x1ULL : 0x2ULL);
            mix(static_cast<std::uint64_t>(change.PaletteIndexAfter));
            mix(change.ExistedBefore ? 0x4ULL : 0x8ULL);
            mix(static_cast<std::uint64_t>(change.PaletteIndexBefore));

            const Mesh::VoxelChunkKey low = Mesh::VoxelChunkKeyForPosition(
                {p.X - 1, p.Y - 1, p.Z - 1});
            const Mesh::VoxelChunkKey high = Mesh::VoxelChunkKeyForPosition(
                {p.X + 1, p.Y + 1, p.Z + 1});
            for (std::int32_t z = low.Z; z <= high.Z; ++z)
                for (std::int32_t y = low.Y; y <= high.Y; ++y)
                    for (std::int32_t x = low.X; x <= high.X; ++x)
                    {
                        Signature& signature =
                            signatures[Mesh::VoxelChunkKey{x, y, z}];
                        ++signature.Count;
                        signature.Checksum += hash;
                        if (!signature.Bounds.HasValue)
                        {
                            signature.Bounds.HasValue = true;
                            signature.Bounds.Minimum = p;
                            signature.Bounds.Maximum = p;
                        }
                        else
                        {
                            signature.Bounds.Minimum = {
                                std::min(signature.Bounds.Minimum.X, p.X),
                                std::min(signature.Bounds.Minimum.Y, p.Y),
                                std::min(signature.Bounds.Minimum.Z, p.Z)};
                            signature.Bounds.Maximum = {
                                std::max(signature.Bounds.Maximum.X, p.X),
                                std::max(signature.Bounds.Maximum.Y, p.Y),
                                std::max(signature.Bounds.Maximum.Z, p.Z)};
                        }
                    }
        }

        SmartToolExactPreviewChunkCache* const cache = source.ChunkCache;
        if (cache != nullptr)
        {
            static_cast<void>(cache->Retarget(source.Document,
                source.Document->GetRevision(), source.ModelIndex,
                source.DocumentChunks));
        }

        // Deux passes, comme l'assemblage du cache de chunks du document. La
        // première collecte les morceaux sans rien concaténer, la seconde
        // réserve une fois puis concatène. Concaténer au fil de l'eau
        // réallouerait le tampon à CHAQUE morceau — MeshData::Append réserve à
        // la taille exacte — soit des centaines de réallocations sur un document
        // creux étalé sur des centaines de chunks : 398 Mo alloués et 55 ms
        // mesurés avant cette correction.
        std::vector<const Mesh::MeshData*> reused;
        reused.reserve(source.DocumentChunks->size());
        std::vector<Mesh::VoxelChunkKey> overrideKeys;
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
            return true;
        };

        // Produit l'override d'un chunk, depuis le cache si sa signature est
        // inchangee. LOT 4c : c'est ici que le cout cesse de suivre le trait
        // accumule pour ne suivre que ce qui bouge.
        const auto resolveOverride = [&](const Mesh::VoxelChunkKey key,
                                         const Mesh::MeshData* const chunkMesh,
                                         const Signature& signature) -> bool
        {
            if (cache != nullptr)
            {
                if (const SmartToolExactPreviewChunk* const hit =
                        cache->Find(key, signature))
                {
                    faceCount += hit->Mesh.FaceCount();
                    result.Overrides.push_back(*hit);
                    overrideKeys.push_back(key);
                    return true;
                }
            }
            // Region localisee : seules les faces a moins d'un voxel d'un
            // changement CONCERNANT CE CHUNK peuvent avoir change. Utiliser la
            // boite globale du trait, comme avant, reconstruisait bien plus que
            // necessaire.
            Asset::Voxel::VoxelBounds local = signature.Bounds;
            local.Minimum = {local.Minimum.X - 1, local.Minimum.Y - 1,
                local.Minimum.Z - 1};
            local.Maximum = {local.Maximum.X + 1, local.Maximum.Y + 1,
                local.Maximum.Z + 1};
            Asset::Voxel::VoxelPosition minimum{};
            Asset::Voxel::VoxelPosition maximum{};
            if (!IntersectChunk(key, local, minimum, maximum))
            {
                // La dilatation atteint le chunk mais pas son volume propre :
                // rien a surimprimer ici.
                if (chunkMesh != nullptr)
                {
                    faceCount += chunkMesh->FaceCount();
                    reused.push_back(chunkMesh);
                }
                return true;
            }
            SmartToolExactPreviewChunk override{};
            override.Key = key;
            if (!makeOverride(chunkMesh, minimum, maximum, override.Mesh))
                return false;
            faceCount += override.Mesh.FaceCount();
            if (cache != nullptr)
            {
                override.Revision = cache->NextRevision();
                cache->NoteRebuild();
                cache->Store(override, signature);
            }
            result.Overrides.push_back(std::move(override));
            overrideKeys.push_back(key);
            return true;
        };

        for (const auto& [key, chunkMesh] : *source.DocumentChunks)
        {
            const auto signature = signatures.find(key);
            if (signature == signatures.end())
            {
                // Chunk intact : il reste celui du document. Aucun override,
                // donc aucun envoi GPU et aucun changement de dessin.
                faceCount += chunkMesh.FaceCount();
                reused.push_back(&chunkMesh);
                continue;
            }
            if (!resolveOverride(key, &chunkMesh, signature->second))
                return result;
        }

        // Chunks que la zone sale atteint mais que le document ne connaît pas :
        // un ajout dans le vide crée de la matière là où aucun chunk n'existait.
        for (const auto& [key, signature] : signatures)
        {
            if (source.DocumentChunks->find(key) != source.DocumentChunks->end())
                continue;
            if (!resolveOverride(key, nullptr, signature)) return result;
        }

        if (cache != nullptr) cache->RetainOnly(overrideKeys);

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
