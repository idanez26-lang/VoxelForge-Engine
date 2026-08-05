// VF-0265 lot 3 : le filtre de faces est la seule pièce vraiment nouvelle du
// compositeur incrémental. Il est testé ici en isolation, contre un oracle
// indépendant : le maillage régional du constructeur.
//
// Contrat : retirer d'un maillage de chunk les faces des voxels d'une boîte,
// puis réunir avec un maillage régional de cette même boîte, doit redonner
// exactement le maillage du chunk. C'est ce qui autorise la réutilisation d'un
// chunk qui intersecte la zone sale, au lieu de le reconstruire — 28 ms.

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelChunkGrid.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Mesh;
using Asset::Voxel::VoxelDocument;
using Asset::Voxel::VoxelPosition;

constexpr std::int32_t Edge = 34;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelDocument MakeDocument()
{
    std::vector<Asset::Vox::VoxVoxel> voxels;
    for (std::int32_t z = 0; z < Edge; ++z)
        for (std::int32_t y = 0; y < Edge; ++y)
            for (std::int32_t x = 0; x < Edge; ++x)
            {
                // Un damier partiel : des faces internes existent, ce qui rend
                // le filtre bien plus discriminant qu'un bloc plein.
                if (((x / 3) + (y / 3) + (z / 3)) % 2 == 0) continue;
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    static_cast<std::uint8_t>(1U + ((x + y + z) % 5U))});
            }
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({
        .Dimensions = {static_cast<std::uint32_t>(Edge),
            static_cast<std::uint32_t>(Edge),
            static_cast<std::uint32_t>(Edge)},
        .Voxels = std::move(voxels)});
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, "filter.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Filter document fixture must build.");
    return std::move(*loaded.Document);
}

// Identité d'une face, indépendante de l'ordre d'émission : le coin minimal,
// la normale et la couleur. Volontairement NUMÉRIQUE — la première version
// fabriquait quatre chaînes par face, sur des dizaines de milliers de faces, et
// ce test prenait 38 secondes en Debug contre 0,02 s pour ses voisins.
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
        const MeshVertex& first = vertices[face * 4U];
        std::array<float, 3U> lowest{
            first.Position[0], first.Position[1], first.Position[2]};
        for (std::size_t corner = 1U; corner < 4U; ++corner)
        {
            const MeshVertex& vertex = vertices[face * 4U + corner];
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

MeshData BuildChunk(const VoxelDocument& document, const VoxelChunkKey key)
{
    const auto built = VoxelMeshBuilder::Build(
        document, VoxelChunkMinimum(key), VoxelChunkMaximum(key));
    Require(built.Succeeded && built.Mesh, "Chunk build must succeed.");
    return std::move(*built.Mesh);
}

// Le contrat central : chunk = (chunk privé des faces de la boîte) + (maillage
// régional de la boîte). Le filtre et le constructeur doivent se recoller
// exactement, sans face perdue ni dédoublée.
void TestFilterAndRegionRecomposeTheChunk()
{
    const auto document = MakeDocument();
    constexpr VoxelChunkKey key{0, 0, 0};
    const MeshData chunk = BuildChunk(document, key);
    Require(chunk.FaceCount() > 0U, "Le chunk de référence doit avoir des faces.");
    // Calculé UNE fois : c'est la référence de toutes les boîtes.
    const std::vector<FaceKey> expected = FaceKeys(chunk);

    struct Box final
    {
        const char* Name;
        VoxelPosition Minimum;
        VoxelPosition Maximum;
    };
    const std::array<Box, 5U> boxes{{
        {"boîte au coeur du chunk", {10, 10, 10}, {12, 12, 12}},
        {"boîte au coin d'origine", {0, 0, 0}, {2, 2, 2}},
        {"boîte sur la frontière haute", {29, 29, 29}, {31, 31, 31}},
        {"boîte qui dépasse le chunk", {28, 28, 28}, {40, 40, 40}},
        {"boîte d'une seule cellule", {17, 5, 23}, {17, 5, 23}}}};

    for (const Box& box : boxes)
    {
        const MeshData kept = chunk.FacesOutsideBox(box.Minimum.X,
            box.Minimum.Y, box.Minimum.Z, box.Maximum.X, box.Maximum.Y,
            box.Maximum.Z);

        // Le maillage régional est limité au chunk : hors du chunk, les faces
        // appartiennent aux voisins et ne doivent pas être recomposées ici.
        const VoxelPosition regionMinimum{
            std::max(box.Minimum.X, VoxelChunkMinimum(key).X),
            std::max(box.Minimum.Y, VoxelChunkMinimum(key).Y),
            std::max(box.Minimum.Z, VoxelChunkMinimum(key).Z)};
        const VoxelPosition regionMaximum{
            std::min(box.Maximum.X, VoxelChunkMaximum(key).X),
            std::min(box.Maximum.Y, VoxelChunkMaximum(key).Y),
            std::min(box.Maximum.Z, VoxelChunkMaximum(key).Z)};
        const auto region = VoxelMeshBuilder::Build(
            document, regionMinimum, regionMaximum);
        Require(region.Succeeded && region.Mesh,
            std::string("Le maillage régional doit réussir : ") + box.Name);

        MeshData recomposed;
        recomposed.Append(kept);
        recomposed.Append(*region.Mesh);

        Require(FaceKeys(recomposed) == expected,
            std::string("Filtre + région doit redonner le chunk : ") +
                box.Name);
    }
}

// Cas limites du filtre, indépendamment du constructeur.
void TestFilterEdgeCases()
{
    const auto document = MakeDocument();
    constexpr VoxelChunkKey key{0, 0, 0};
    const MeshData chunk = BuildChunk(document, key);

    // Une boîte disjointe du chunk ne retire rien.
    const MeshData untouched =
        chunk.FacesOutsideBox(200, 200, 200, 210, 210, 210);
    Require(FaceKeys(untouched) == FaceKeys(chunk),
        "Une boîte disjointe ne doit retirer aucune face.");

    // Une boîte couvrant tout le chunk retire tout.
    const MeshData emptied = chunk.FacesOutsideBox(-1, -1, -1, 40, 40, 40);
    Require(emptied.FaceCount() == 0U && emptied.Empty(),
        "Une boîte couvrant le chunk doit tout retirer.");

    // Idempotence : refiltrer sur la même boîte ne change plus rien.
    const MeshData once = chunk.FacesOutsideBox(10, 10, 10, 12, 12, 12);
    const MeshData twice = once.FacesOutsideBox(10, 10, 10, 12, 12, 12);
    Require(FaceKeys(once) == FaceKeys(twice),
        "Le filtre doit être idempotent.");
    Require(once.FaceCount() < chunk.FaceCount(),
        "Le filtre doit réellement retirer des faces.");

    // Les tampons restent cohérents : six indices par face, quatre sommets.
    Require(once.IndexCount() == once.FaceCount() * 6U &&
            once.VertexCount() == once.FaceCount() * 4U,
        "Les tampons filtrés doivent rester cohérents.");
}

} // namespace

int main()
{
    try
    {
        TestFilterAndRegionRecomposeTheChunk();
        TestFilterEdgeCases();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Mesh data face filter tests passed.\n";
    return EXIT_SUCCESS;
}
