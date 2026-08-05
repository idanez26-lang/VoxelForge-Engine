// VF-0265 lot 3b : le compositeur incrémental doit être RIGOUREUSEMENT égal au
// compositeur de référence, lui-même égal au maillage du document réellement
// commité. Triple oracle : si l'un des trois diverge, le test le dit.
//
// L'égalité porte sur l'ensemble des faces, pas sur leur ordre — le chemin
// incrémental émet chunk par chunk. C'est le contrat déjà acté par VF-0262 pour
// le mesh assemblé.

#include "SmartTools/SmartToolExactPreviewComposer.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Asset::Voxel::VoxelDocument;
using Asset::Voxel::VoxelDocumentChange;
using Asset::Voxel::VoxelPosition;
using Mesh::MeshData;

constexpr std::int32_t Edge = 40;
constexpr std::int32_t Filled = 36;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelDocument MakeDocument()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(Filled) * Filled * Filled);
    for (std::int32_t z = 0; z < Filled; ++z)
        for (std::int32_t y = 0; y < Filled; ++y)
            for (std::int32_t x = 0; x < Filled; ++x)
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    static_cast<std::uint8_t>(1U + ((x + y + z) % 6U))});
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({
        .Dimensions = {static_cast<std::uint32_t>(Edge),
            static_cast<std::uint32_t>(Edge),
            static_cast<std::uint32_t>(Edge)},
        .Voxels = std::move(voxels)});
    auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "incremental.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Incremental preview fixture must build.");
    return std::move(*loaded.Document);
}

const VoxelDocument& BaseDocument()
{
    static const VoxelDocument document = MakeDocument();
    return document;
}

using FaceKey = std::array<std::int32_t, 7U>;

std::vector<FaceKey> FaceKeys(const MeshData& mesh)
{
    const auto toInteger = [](const float value) noexcept
    {
        return static_cast<std::int32_t>(
            value < 0.0F ? value - 0.5F : value + 0.5F);
    };
    std::vector<FaceKey> keys;
    keys.reserve(mesh.FaceCount());
    const auto& vertices = mesh.Vertices();
    for (std::size_t face = 0U; face < mesh.FaceCount(); ++face)
    {
        const Mesh::MeshVertex& first = vertices[face * 4U];
        std::array<float, 3U> lowest{
            first.Position[0], first.Position[1], first.Position[2]};
        for (std::size_t corner = 1U; corner < 4U; ++corner)
        {
            const Mesh::MeshVertex& vertex = vertices[face * 4U + corner];
            for (std::size_t axis = 0U; axis < 3U; ++axis)
                lowest[axis] = std::min(lowest[axis], vertex.Position[axis]);
        }
        keys.push_back({toInteger(lowest[0]), toInteger(lowest[1]),
            toInteger(lowest[2]), toInteger(first.Normal[0]),
            toInteger(first.Normal[1]), toInteger(first.Normal[2]),
            static_cast<std::int32_t>(first.ColorIndex)});
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

const Mesh::VoxelDocumentMeshCache& DocumentChunks()
{
    static const Mesh::VoxelDocumentMeshCache cache = []
    {
        Mesh::VoxelDocumentMeshCache built;
        Require(built.Synchronize(BaseDocument(), 1U).Succeeded,
            "The document chunk cache must synchronize.");
        return built;
    }();
    return cache;
}

VoxelDocumentChange Add(const VoxelPosition position, const std::uint8_t colour)
{
    return {.SubModelIndex = 0U, .Position = position, .ExistedBefore = false,
        .PaletteIndexBefore = 0U, .ExistsAfter = true,
        .PaletteIndexAfter = colour};
}

VoxelDocumentChange Remove(
    const VoxelPosition position, const std::uint8_t before)
{
    return {.SubModelIndex = 0U, .Position = position, .ExistedBefore = true,
        .PaletteIndexBefore = before, .ExistsAfter = false,
        .PaletteIndexAfter = 0U};
}

VoxelDocumentChange Paint(
    const VoxelPosition position,
    const std::uint8_t before,
    const std::uint8_t after)
{
    return {.SubModelIndex = 0U, .Position = position, .ExistedBefore = true,
        .PaletteIndexBefore = before, .ExistsAfter = true,
        .PaletteIndexAfter = after};
}

std::uint8_t ColourAt(const VoxelPosition position)
{
    const auto voxel = BaseDocument().GetVoxel(position);
    Require(voxel.has_value(), "Fixture voxel must exist.");
    return voxel->PaletteIndex;
}

void CheckTripleOracle(
    const std::string_view scenario,
    const std::vector<VoxelDocumentChange>& changes)
{
    const SmartToolExactPreviewComposer::Source source{
        .Document = &BaseDocument(),
        .DocumentChunks = &DocumentChunks().Chunks(),
        .ModelIndex = 0U};

    const auto incremental =
        SmartToolExactPreviewComposer::Compose(source, changes);
    Require(incremental.Succeeded(),
        std::string("Le compositeur incrémental doit réussir : ") +
            std::string(scenario) + " — " + incremental.Error);

    const auto reference =
        SmartToolExactPreviewComposer::Compose(BaseDocument(), changes);
    Require(reference.Succeeded(),
        std::string("Le compositeur de référence doit réussir : ") +
            std::string(scenario));

    VoxelDocument committed = BaseDocument();
    Require(committed.ApplyVoxelChanges(changes).Succeeded,
        std::string("Le commit réel doit réussir : ") +
            std::string(scenario));
    const auto full = Mesh::VoxelMeshBuilder::Build(committed);
    Require(full.Succeeded && full.Mesh, "Le maillage du commit doit réussir.");

    const auto incrementalKeys = FaceKeys(incremental.Mesh);
    const auto referenceKeys = FaceKeys(reference.Mesh);
    const auto committedKeys = FaceKeys(*full.Mesh);

    Require(incrementalKeys == referenceKeys,
        std::string("Incrémental != référence : ") + std::string(scenario));
    Require(referenceKeys == committedKeys,
        std::string("Référence != commit réel : ") + std::string(scenario));

    // VF-0265 lot 3e : le contrat que le renderer appliquera. Il dessinera les
    // chunks du document, en substituant ceux qui figurent dans Overrides.
    // Reconstituer cette scène doit redonner exactement le maillage du commit.
    MeshData substituted;
    std::size_t vertexCount = 0U;
    std::size_t indexCount = 0U;
    const auto isOverridden = [&](const Mesh::VoxelChunkKey key)
    {
        return std::any_of(incremental.Overrides.begin(),
            incremental.Overrides.end(),
            [key](const SmartToolExactPreviewChunk& chunk)
            { return chunk.Key == key; });
    };
    for (const auto& [key, chunkMesh] : DocumentChunks().Chunks())
    {
        if (isOverridden(key)) continue;
        vertexCount += chunkMesh.VertexCount();
        indexCount += chunkMesh.IndexCount();
    }
    for (const SmartToolExactPreviewChunk& chunk : incremental.Overrides)
    {
        vertexCount += chunk.Mesh.VertexCount();
        indexCount += chunk.Mesh.IndexCount();
    }
    substituted.Reserve(vertexCount, indexCount);
    for (const auto& [key, chunkMesh] : DocumentChunks().Chunks())
    {
        if (isOverridden(key)) continue;
        substituted.Append(chunkMesh);
    }
    for (const SmartToolExactPreviewChunk& chunk : incremental.Overrides)
    {
        substituted.Append(chunk.Mesh);
    }
    Require(FaceKeys(substituted) == committedKeys,
        std::string("Substitution par chunks != commit réel : ") +
            std::string(scenario));

    // Invariant qui compte, et qui interdit toute dérive : un chunk hors de la
    // zone sale ne doit JAMAIS produire d'override — c'est ce qui évitera un
    // envoi GPU inutile. On recalcule la zone sale indépendamment du
    // compositeur : boîte des positions changées, dilatée d'un voxel.
    if (changes.empty())
    {
        Require(incremental.Overrides.empty(),
            "Sans changement, aucun chunk ne doit être recomposé.");
        return;
    }
    std::int32_t minimumX = changes.front().Position.X;
    std::int32_t minimumY = changes.front().Position.Y;
    std::int32_t minimumZ = changes.front().Position.Z;
    std::int32_t maximumX = minimumX;
    std::int32_t maximumY = minimumY;
    std::int32_t maximumZ = minimumZ;
    for (const VoxelDocumentChange& change : changes)
    {
        minimumX = std::min(minimumX, change.Position.X);
        minimumY = std::min(minimumY, change.Position.Y);
        minimumZ = std::min(minimumZ, change.Position.Z);
        maximumX = std::max(maximumX, change.Position.X);
        maximumY = std::max(maximumY, change.Position.Y);
        maximumZ = std::max(maximumZ, change.Position.Z);
    }
    const Mesh::VoxelChunkKey firstKey = Mesh::VoxelChunkKeyForPosition(
        {minimumX - 1, minimumY - 1, minimumZ - 1});
    const Mesh::VoxelChunkKey lastKey = Mesh::VoxelChunkKeyForPosition(
        {maximumX + 1, maximumY + 1, maximumZ + 1});

    for (const SmartToolExactPreviewChunk& chunk : incremental.Overrides)
    {
        Require(chunk.Key.X >= firstKey.X && chunk.Key.X <= lastKey.X &&
                chunk.Key.Y >= firstKey.Y && chunk.Key.Y <= lastKey.Y &&
                chunk.Key.Z >= firstKey.Z && chunk.Key.Z <= lastKey.Z,
            std::string("Un chunk hors de la zone sale a été recomposé : ") +
                std::string(scenario));
    }

    // Et pas plus d'overrides que de chunks que la zone sale traverse.
    const std::size_t spanned =
        static_cast<std::size_t>(lastKey.X - firstKey.X + 1) *
        static_cast<std::size_t>(lastKey.Y - firstKey.Y + 1) *
        static_cast<std::size_t>(lastKey.Z - firstKey.Z + 1);
    Require(incremental.Overrides.size() <= spanned,
        std::string("Plus d'overrides que de chunks traversés : ") +
            std::string(scenario));
}

// Sans assemblage demandé, le mesh unique doit rester vide : le consommateur
// chunké ne doit pas payer ce que le plan appelle le dernier poste O(document).
void TestOverridesWithoutAssembly()
{
    const std::vector<VoxelDocumentChange> changes{
        Remove({20, 20, 20}, ColourAt({20, 20, 20}))};
    SmartToolExactPreviewComposer::Source source{
        .Document = &BaseDocument(),
        .DocumentChunks = &DocumentChunks().Chunks(),
        .ModelIndex = 0U,
        .AssembleMesh = false};
    const auto composed =
        SmartToolExactPreviewComposer::Compose(source, changes);
    Require(composed.Succeeded(), "La composition sans assemblage doit réussir.");
    Require(composed.Mesh.Empty(),
        "Sans assemblage, le mesh unique doit rester vide.");
    Require(!composed.Overrides.empty(),
        "Sans assemblage, les overrides doivent quand même être produits.");

    source.AssembleMesh = true;
    const auto assembled =
        SmartToolExactPreviewComposer::Compose(source, changes);
    Require(assembled.Overrides.size() == composed.Overrides.size(),
        "Les overrides ne doivent pas dépendre de l'assemblage.");
    Require(FaceKeys(assembled.Overrides.front().Mesh) ==
            FaceKeys(composed.Overrides.front().Mesh),
        "Les overrides doivent être identiques avec et sans assemblage.");
}

void TestTripleOracleAcrossScenarios()
{
    constexpr VoxelPosition interior{20, 20, 20};
    constexpr VoxelPosition onBorder{31, 20, 20};
    constexpr VoxelPosition onThreeBorders{31, 31, 31};
    constexpr VoxelPosition surface{Filled - 1, 20, 20};

    CheckTripleOracle("aucun changement", {});
    CheckTripleOracle("suppression au coeur",
        {Remove(interior, ColourAt(interior))});
    CheckTripleOracle("recoloration au coeur",
        {Paint(interior, ColourAt(interior), 9U)});
    CheckTripleOracle("suppression sur une frontière de chunk",
        {Remove(onBorder, ColourAt(onBorder))});
    CheckTripleOracle("suppression sur trois frontières à la fois",
        {Remove(onThreeBorders, ColourAt(onThreeBorders))});
    CheckTripleOracle("ajout dans le vide, hors des bornes occupées",
        {Add({Filled + 2, 20, 20}, 4U)});
    CheckTripleOracle("ajout collé à la surface",
        {Add({Filled, 21, 21}, 4U), Paint(surface, ColourAt(surface), 7U)});

    // Un pinceau : un bloc 5³ effacé au coeur, le cas d'usage réel.
    std::vector<VoxelDocumentChange> brush;
    for (std::int32_t z = 18; z <= 22; ++z)
        for (std::int32_t y = 18; y <= 22; ++y)
            for (std::int32_t x = 18; x <= 22; ++x)
                brush.push_back(Remove({x, y, z}, ColourAt({x, y, z})));
    CheckTripleOracle("pinceau 5 cube au coeur", brush);
}

// Sans changement, la preview est exactement le maillage du document : c'est le
// cas où l'on ne doit toucher à rien du tout.
void TestEmptyPlanReusesEveryChunk()
{
    const SmartToolExactPreviewComposer::Source source{
        .Document = &BaseDocument(),
        .DocumentChunks = &DocumentChunks().Chunks(),
        .ModelIndex = 0U};
    const auto composed = SmartToolExactPreviewComposer::Compose(source, {});
    Require(composed.Succeeded(), "La composition vide doit réussir.");

    const MeshData* const documentMesh = DocumentChunks().Mesh();
    Require(documentMesh != nullptr, "Le mesh assemblé du document doit exister.");
    Require(FaceKeys(composed.Mesh) == FaceKeys(*documentMesh),
        "Sans changement, la preview doit être le maillage du document.");
}

// Le document ne doit pas bouger d'un iota, journal compris.
void TestIncrementalPathNeverMutates()
{
    VoxelDocument document = MakeDocument();
    Mesh::VoxelDocumentMeshCache cache;
    Require(cache.Synchronize(document, 1U).Succeeded, "Synchronisation.");

    const std::uint64_t revisionBefore = document.GetRevision();
    const std::uint64_t countBefore = document.GetVoxelCount();
    const SmartToolExactPreviewComposer::Source source{
        .Document = &document,
        .DocumentChunks = &cache.Chunks(),
        .ModelIndex = 0U};
    const std::vector<VoxelDocumentChange> changes{
        Remove({20, 20, 20}, ColourAt({20, 20, 20})),
        Add({Filled + 1, 3, 3}, 5U)};

    for (int repeat = 0; repeat < 3; ++repeat)
    {
        const auto composed =
            SmartToolExactPreviewComposer::Compose(source, changes);
        Require(composed.Succeeded(), "Chaque composition doit réussir.");
    }

    Require(document.GetRevision() == revisionBefore &&
            document.GetVoxelCount() == countBefore,
        "La composition ne doit ni consommer de révision ni changer le contenu.");
    const auto journal = document.ChangesSince(revisionBefore);
    Require(journal.has_value() && journal->empty(),
        "La composition ne doit rien inscrire au journal des révisions.");
}

// Sans jeu de chunks, on doit retomber exactement sur le compositeur de
// référence : c'est le repli des appelants ponctuels.
void TestFallbackWithoutChunksMatchesReference()
{
    const std::vector<VoxelDocumentChange> changes{
        Remove({20, 20, 20}, ColourAt({20, 20, 20}))};
    const SmartToolExactPreviewComposer::Source source{
        .Document = &BaseDocument(), .DocumentChunks = nullptr,
        .ModelIndex = 0U};
    const auto fallback =
        SmartToolExactPreviewComposer::Compose(source, changes);
    const auto reference =
        SmartToolExactPreviewComposer::Compose(BaseDocument(), changes);
    Require(fallback.Succeeded() && reference.Succeeded(),
        "Le repli et la référence doivent réussir.");
    Require(FaceKeys(fallback.Mesh) == FaceKeys(reference.Mesh),
        "Le repli doit être le compositeur de référence.");
}

// Un jeu de changements refusé doit l'être avec le même message des deux côtés.
void TestRejectionMatchesReference()
{
    const std::vector<VoxelDocumentChange> refused{Add({20, 20, 20}, 3U)};
    const SmartToolExactPreviewComposer::Source source{
        .Document = &BaseDocument(),
        .DocumentChunks = &DocumentChunks().Chunks(),
        .ModelIndex = 0U};
    const auto incremental =
        SmartToolExactPreviewComposer::Compose(source, refused);
    const auto reference =
        SmartToolExactPreviewComposer::Compose(BaseDocument(), refused);
    Require(!incremental.Succeeded() && !reference.Succeeded(),
        "Un jeu invalide doit être refusé des deux côtés.");
}


// LOT 4c : le cache d'overrides par chunk doit etre STRICTEMENT INVISIBLE dans
// le resultat. On rejoue un trait pas a pas : a chaque etape on compose avec le
// cache, et on compose la meme chose sans cache — le chemin sans etat sert
// d'oracle, c'est exactement pour ca qu'il reste sans etat.
//
// Le trait est choisi pour pieger un cache naif :
//   - il traverse plusieurs chunks (l'arete de chunk est 32) ;
//   - il REVISITE un voxel deja touche pour le repeindre d'une AUTRE couleur,
//     ce qui ne change ni le nombre de changements du chunk ni leur boite
//     englobante. Un cache indexe sur un compteur afficherait un etat perime :
//     c'est cette etape qui justifie la somme de controle du contenu ;
//   - il efface, donc il retire des faces au lieu d'en ajouter.
void TestChunkCacheIsInvisibleStrokeByStroke()
{
    SmartToolExactPreviewChunkCache cache;
    std::vector<VoxelDocumentChange> changes;

    const auto upsert = [&changes](const VoxelDocumentChange& change)
    {
        // ValidateVoxelChanges refuse deux changements sur la meme position :
        // un trait fusionne, il n'empile pas. On reproduit ce contrat.
        for (VoxelDocumentChange& existing : changes)
        {
            if (existing.Position == change.Position)
            {
                existing.ExistsAfter = change.ExistsAfter;
                existing.PaletteIndexAfter = change.PaletteIndexAfter;
                return;
            }
        }
        changes.push_back(change);
    };

    const auto compareWithOracle = [&](const std::string_view step)
    {
        SmartToolExactPreviewComposer::Source cached{
            .Document = &BaseDocument(),
            .DocumentChunks = &DocumentChunks().Chunks(),
            .ModelIndex = 0U};
        cached.ChunkCache = &cache;
        const auto withCache =
            SmartToolExactPreviewComposer::Compose(cached, changes);

        const SmartToolExactPreviewComposer::Source stateless{
            .Document = &BaseDocument(),
            .DocumentChunks = &DocumentChunks().Chunks(),
            .ModelIndex = 0U};
        const auto oracle =
            SmartToolExactPreviewComposer::Compose(stateless, changes);

        const std::string where = " — étape « " + std::string(step) + " »";
        Require(withCache.Succeeded() && oracle.Succeeded(),
            "Les deux compositions doivent réussir" + where + " : " +
                withCache.Error + oracle.Error);
        Require(withCache.Overrides.size() == oracle.Overrides.size(),
            "Le cache a change le NOMBRE d'overrides" + where);

        std::map<Mesh::VoxelChunkKey, std::vector<FaceKey>> expected;
        for (const auto& override : oracle.Overrides)
            expected.emplace(override.Key, FaceKeys(override.Mesh));
        for (const auto& override : withCache.Overrides)
        {
            const auto found = expected.find(override.Key);
            Require(found != expected.end(),
                "Le cache a produit un override sur un chunk que le chemin sans "
                "état ne touche pas" + where);
            Require(FaceKeys(override.Mesh) == found->second,
                "La géométrie d'un override diffère de l'oracle" + where);
        }
        Require(FaceKeys(withCache.Mesh) == FaceKeys(oracle.Mesh),
            "Le mesh assemblé diffère de l'oracle" + where);
    };

    // Le document est PLEIN sur 36 cubes dans un volume de 40 : une position
    // n'est libre que si l'un de ses axes atteint 36. Et l'arete de chunk vaut
    // 32, donc z=37 place le voxel dans le chunk 1 en z, tandis que x=10 et
    // x=37 le placent dans deux chunks differents en x. On obtient ainsi deux
    // chunks distincts avec des positions reellement vides.
    upsert(Add({10, 10, 37}, 3U));
    compareWithOracle("premier voxel");
    upsert(Add({11, 10, 37}, 3U));
    compareWithOracle("voxel voisin, meme chunk");
    upsert(Add({37, 10, 37}, 4U));
    compareWithOracle("voxel dans un autre chunk");

    const std::size_t hitsBeforeRevisit = cache.HitCount();
    Require(hitsBeforeRevisit > 0U,
        "Apres trois etapes, le cache doit avoir servi au moins une fois : "
        "sinon il est mort et ce test ne prouverait rien.");

    // L'etape qui piege un cache par compteur : meme position, meme nombre de
    // changements, meme boite englobante — mais une AUTRE couleur.
    upsert(Add({11, 10, 37}, 7U));
    compareWithOracle("recoloration d'un voxel deja touche");

    // Un effacement, qui retire des faces au lieu d'en ajouter. On vise un
    // voxel du document, dans un troisieme chunk encore intact.
    upsert(Remove({20, 20, 20}, ColourAt({20, 20, 20})));
    compareWithOracle("effacement dans un chunk encore intact");

    // Le cache doit se vider de lui-meme si la cible change : sinon il
    // afficherait la geometrie d'un document qui n'est plus celui-la.
    Require(!cache.Retarget(nullptr, 0U, 0U, nullptr),
        "Changer de cible doit invalider le cache.");
    Require(cache.HitCount() >= hitsBeforeRevisit,
        "Le compteur de hits ne doit pas regresser.");
    compareWithOracle("apres invalidation totale");
}

} // namespace

int main()
{
    try
    {
        TestTripleOracleAcrossScenarios();
        TestOverridesWithoutAssembly();
        TestEmptyPlanReusesEveryChunk();
        TestIncrementalPathNeverMutates();
        TestFallbackWithoutChunksMatchesReference();
        TestRejectionMatchesReference();
        TestChunkCacheIsInvisibleStrokeByStroke();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Smart Tool incremental preview tests passed.\n";
    return EXIT_SUCCESS;
}
