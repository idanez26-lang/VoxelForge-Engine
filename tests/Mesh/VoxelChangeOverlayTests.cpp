// VF-0265 lot 2 : le test d'or de la vue « document + changements ».
//
// Contrat central : mailler l'état final par régions, à travers la vue, doit
// donner EXACTEMENT les mêmes faces que mailler intégralement un document dans
// lequel les changements ont réellement été appliqués. C'est ce qui rend
// « Preview == Commit » démontrable au lieu de plausible.
//
// L'égalité porte sur l'ENSEMBLE des faces, pas sur leur ordre : c'est le
// contrat déjà acté par VF-0262 pour le mesh assemblé par chunks.

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelChangeOverlay.h"
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
using Asset::Voxel::VoxelDocumentChange;
using Asset::Voxel::VoxelPosition;

// Dimensions du sous-modèle, et matière qui n'en occupe qu'une partie. L'écart
// est volontaire : les bornes occupées s'arrêtent à 35 alors que le volume
// éditable va jusqu'à 39. C'est le seul moyen de tester le piège du clipping —
// le constructeur de mesh clippe la région aux BORNES, pas aux dimensions, donc
// un voxel ajouté entre 36 et 39 serait perdu si la vue ne corrigeait pas ses
// bornes. Ajouter au-delà de 39 serait en revanche refusé, au commit comme en
// preview : ce n'est pas un piège, c'est une erreur.
constexpr std::int32_t Edge = 40;
constexpr std::int32_t Filled = 36;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

// Un bloc plein de 36³ dans un volume de 40³ : les frontières de chunk
// (multiples de 32) tombent donc à l'intérieur de la matière, ce qui est le cas
// qui casse en premier, et il reste de la place libre au-delà des bornes.
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
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, "overlay.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Overlay document fixture must build.");
    return std::move(*loaded.Document);
}

// Identité d'une face, indépendante de l'ordre d'émission : le coin minimal, la
// normale et la couleur. Numérique et non textuelle — sur des dizaines de
// milliers de faces et une dizaine de scénarios, des clés en chaînes de
// caractères font passer ce test de la fraction de seconde à la demi-minute.
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

std::uint8_t ColourAt(const VoxelDocument& document, const VoxelPosition p)
{
    const auto voxel = document.GetVoxel(p);
    Require(voxel.has_value(), "Fixture voxel must exist.");
    return voxel->PaletteIndex;
}

// Maille l'état final par régions couvrant tout le volume, à travers la vue,
// et compare l'ensemble des faces au maillage complet d'un document réellement
// muté. Le découpage en régions est celui des chunks : c'est celui que le
// compositeur utilisera.
void CheckGoldenEquality(
    const std::string_view scenario,
    const std::vector<VoxelDocumentChange>& changes)
{
    const auto document = MakeDocument();

    Asset::Voxel::VoxelDocumentOperationResult validation{};
    const auto overlay =
        VoxelChangeOverlay::TryCreate(document, changes, 0U, validation);
    Require(overlay.has_value() && validation.Succeeded,
        std::string("La vue doit accepter ce scénario : ") +
            std::string(scenario));

    // Union des maillages régionaux sur une partition qui couvre tout le volume
    // éditable. Elle part de zéro : une position de voxel est toujours positive
    // ou nulle, donc les chunks négatifs sont vides par construction — les
    // parcourir ne prouvait rien et triplait la durée du test.
    std::vector<FaceKey> assembled;
    for (std::int32_t z = 0; z < Edge; z += VoxelChunkEdgeLength)
        for (std::int32_t y = 0; y < Edge; y += VoxelChunkEdgeLength)
            for (std::int32_t x = 0; x < Edge; x += VoxelChunkEdgeLength)
            {
                const auto region = VoxelMeshBuilder::Build(*overlay,
                    {x, y, z},
                    {x + VoxelChunkEdgeLength - 1,
                     y + VoxelChunkEdgeLength - 1,
                     z + VoxelChunkEdgeLength - 1});
                Require(region.Succeeded && region.Mesh,
                    std::string("Chaque région doit se mailler : ") +
                        std::string(scenario));
                const auto keys = FaceKeys(*region.Mesh);
                assembled.insert(assembled.end(), keys.begin(), keys.end());
            }
    std::sort(assembled.begin(), assembled.end());

    // Oracle : un document dans lequel les changements sont réellement commis.
    auto mutated = MakeDocument();
    Require(mutated.ApplyVoxelChanges(changes).Succeeded,
        std::string("L'oracle doit pouvoir appliquer les changements : ") +
            std::string(scenario));
    const auto full = VoxelMeshBuilder::Build(mutated);
    Require(full.Succeeded && full.Mesh, "L'oracle doit se mailler.");
    const auto expected = FaceKeys(*full.Mesh);

    Require(assembled == expected,
        std::string("Preview != Commit sur le scénario : ") +
            std::string(scenario));
}

void TestGoldenEqualityAcrossScenarios()
{
    const auto reference = MakeDocument();
    constexpr VoxelPosition interior{20, 20, 20};
    constexpr VoxelPosition onChunkBorder{31, 20, 20};
    constexpr VoxelPosition onThreeBorders{31, 31, 31};
    constexpr VoxelPosition surface{Filled - 1, 20, 20};

    CheckGoldenEquality("aucun changement", {});
    CheckGoldenEquality("suppression au coeur",
        {Remove(interior, ColourAt(reference, interior))});
    CheckGoldenEquality("recoloration au coeur",
        {Paint(interior, ColourAt(reference, interior), 9U)});
    CheckGoldenEquality("suppression sur une frontière de chunk",
        {Remove(onChunkBorder, ColourAt(reference, onChunkBorder))});
    CheckGoldenEquality("suppression sur trois frontières à la fois",
        {Remove(onThreeBorders, ColourAt(reference, onThreeBorders))});
    // Dans les dimensions, mais au-delà des bornes occupées : c'est le cas que
    // le clipping régional du constructeur ferait disparaître si la vue ne
    // corrigeait pas ses bornes.
    CheckGoldenEquality("ajout hors des bornes occupées",
        {Add({Filled + 1, 20, 20}, 4U)});
    CheckGoldenEquality("ajout détaché, à deux voxels de la surface",
        {Add({Filled + 2, 20, 20}, 4U)});
    CheckGoldenEquality("ajout collé à la surface",
        {Add({Filled, 21, 21}, 4U),
         Paint(surface, ColourAt(reference, surface), 7U)});

    // Une cavité : plusieurs suppressions contiguës, le pire cas pour la
    // visibilité des faces internes.
    std::vector<VoxelDocumentChange> cavity;
    for (std::int32_t z = 30; z <= 33; ++z)
        for (std::int32_t y = 30; y <= 33; ++y)
            for (std::int32_t x = 30; x <= 33; ++x)
                cavity.push_back(
                    Remove({x, y, z}, ColourAt(reference, {x, y, z})));
    CheckGoldenEquality("cavité à cheval sur huit chunks", cavity);
}

// La vue ne doit rien changer au document, et surtout ne rien inscrire au
// journal des révisions : c'est ce qui garantit que la preview ne polluera pas
// les caches de mesh.
void TestOverlayNeverMutates()
{
    auto document = MakeDocument();
    const std::uint64_t revisionBefore = document.GetRevision();
    const std::uint64_t countBefore = document.GetVoxelCount();
    const auto boundsBefore = document.GetBounds(0U);
    const auto meshBefore = VoxelMeshBuilder::Build(document);
    Require(meshBefore.Succeeded && meshBefore.Mesh, "Maillage initial.");

    const std::vector<VoxelDocumentChange> changes{
        Remove({20, 20, 20}, ColourAt(document, {20, 20, 20})),
        Add({Filled + 2, Filled + 2, Filled + 2}, 5U)};

    Asset::Voxel::VoxelDocumentOperationResult validation{};
    const auto overlay =
        VoxelChangeOverlay::TryCreate(document, changes, 0U, validation);
    Require(overlay.has_value(), "La vue doit se créer.");

    for (int repeat = 0; repeat < 3; ++repeat)
    {
        const auto built = VoxelMeshBuilder::Build(
            *overlay, {0, 0, 0}, {Edge, Edge, Edge});
        Require(built.Succeeded, "Chaque maillage de la vue doit réussir.");
    }

    Require(document.GetRevision() == revisionBefore,
        "La vue ne doit consommer aucune révision.");
    Require(document.GetVoxelCount() == countBefore &&
            document.GetBounds(0U) == boundsBefore,
        "La vue ne doit modifier ni le contenu ni les bornes.");
    const auto journal = document.ChangesSince(revisionBefore);
    Require(journal.has_value() && journal->empty(),
        "La vue ne doit rien inscrire au journal des révisions.");
    const auto meshAfter = VoxelMeshBuilder::Build(document);
    Require(meshAfter.Succeeded && meshAfter.Mesh &&
            FaceKeys(*meshAfter.Mesh) == FaceKeys(*meshBefore.Mesh),
        "Le maillage du document doit être inchangé après composition.");
}

// Les bornes doivent englober les ajouts, sinon le clipping régional du
// constructeur les perdrait silencieusement.
void TestBoundsCoverAdditionsAndDirtyBoxIsDilated()
{
    const auto document = MakeDocument();
    Asset::Voxel::VoxelDocumentOperationResult validation{};
    const auto overlay = VoxelChangeOverlay::TryCreate(
        document, {{Add({Filled + 2, 5, 5}, 6U)}}, 0U, validation);
    Require(overlay.has_value(),
        "La vue doit accepter un ajout hors des bornes occupées.");
    Require(overlay->Bounds().HasValue &&
            overlay->Bounds().Maximum.X >= Filled + 2,
        "Les bornes doivent englober les positions ajoutées.");
    Require(overlay->DirtyBounds().HasValue &&
            overlay->DirtyBounds().Minimum.X == Filled + 1 &&
            overlay->DirtyBounds().Maximum.X == Filled + 3 &&
            overlay->DirtyBounds().Minimum.Y == 4 &&
            overlay->DirtyBounds().Maximum.Y == 6,
        "La zone sale doit être dilatée d'un voxel sur chaque axe.");
    Require(overlay->VoxelCount() == document.GetVoxelCount() + 1U,
        "Un ajout doit incrémenter le compte de voxels de la vue.");

    Asset::Voxel::VoxelDocumentOperationResult empty{};
    const auto none = VoxelChangeOverlay::TryCreate(document, {}, 0U, empty);
    Require(none.has_value() && none->Empty() &&
            !none->DirtyBounds().HasValue,
        "Sans changement, la vue est le document et la zone sale est vide.");
}

// Un refus de la vue doit être le refus du commit, avec le même code.
void TestRejectionsMatchCommit()
{
    const std::vector<std::vector<VoxelDocumentChange>> refused{
        {Add({Edge + 200, 0, 0}, 3U)},
        {Add({5, 5, 5}, 0U)},
        {Add({0, 0, 0}, 3U)},
        {Add({12, 12, 12}, 3U), Add({12, 12, 12}, 4U)}};

    for (const auto& changes : refused)
    {
        const auto document = MakeDocument();
        Asset::Voxel::VoxelDocumentOperationResult validation{};
        const auto overlay =
            VoxelChangeOverlay::TryCreate(document, changes, 0U, validation);

        auto applied = MakeDocument();
        const auto appliedResult = applied.ApplyVoxelChanges(changes);

        Require(!overlay.has_value() && !validation.Succeeded &&
                !appliedResult.Succeeded,
            "Ce jeu doit être refusé des deux côtés.");
        Require(validation.Error == appliedResult.Error,
            "Le code d'erreur de la vue doit être celui du commit.");
    }

    // Un sous-modèle inexistant est refusé avant toute validation.
    const auto document = MakeDocument();
    Asset::Voxel::VoxelDocumentOperationResult validation{};
    Require(!VoxelChangeOverlay::TryCreate(document, {}, 9U, validation)
                 .has_value() &&
            validation.Error ==
                Asset::Voxel::VoxelDocumentError::InvalidModelIndex,
        "Un index de sous-modèle invalide doit être refusé.");
}

} // namespace

int main()
{
    try
    {
        TestGoldenEqualityAcrossScenarios();
        TestOverlayNeverMutates();
        TestBoundsCoverAdditionsAndDirtyBoxIsDilated();
        TestRejectionsMatchCommit();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Voxel change overlay tests passed.\n";
    return EXIT_SUCCESS;
}
