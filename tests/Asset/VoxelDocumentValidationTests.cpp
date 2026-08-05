// VF-0265 lot 1 : ValidateVoxelChanges est l'unique implémentation des règles
// de rejet des changements de voxels. Deux contrats à prouver :
//   1. elle rejette exactement ce qu'ApplyVoxelChanges rejette, avec le même
//      code d'erreur — c'est ce qui étendra « Preview == Commit » aux échecs ;
//   2. elle ne mute rien : ni révision, ni journal, ni contenu.

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge::Asset::Voxel;
using VoxelForge::Asset::Vox::DefaultVoxPalette;
using VoxelForge::Asset::Vox::VoxModel;
using VoxelForge::Asset::Vox::VoxVoxel;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelDocument MakeDocument()
{
    VoxModel source{};
    source.Version = 150U;
    source.Palette = DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {8U, 8U, 8U},
        .Voxels = {{.X = 1U, .Y = 1U, .Z = 1U, .ColorIndex = 4U}}});
    auto loaded = VoxDocumentLoader{}.Build(source, "validation.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Validation document fixture must build.");
    return std::move(*loaded.Document);
}

VoxelDocumentChange Add(
    const VoxelPosition position, const std::uint8_t colour)
{
    return {.SubModelIndex = 0U,
        .Position = position,
        .ExistedBefore = false,
        .PaletteIndexBefore = 0U,
        .ExistsAfter = true,
        .PaletteIndexAfter = colour};
}

// Chaque cause de rejet doit produire le MÊME code d'erreur que l'application
// réelle sur un document identique.
void TestRejectionsMatchApply()
{
    struct Case final
    {
        const char* Name;
        std::vector<VoxelDocumentChange> Changes;
    };

    const std::vector<Case> cases{
        {"index de sous-modèle invalide",
         {{.SubModelIndex = 7U,
           .Position = {0, 0, 0},
           .ExistedBefore = false,
           .PaletteIndexBefore = 0U,
           .ExistsAfter = true,
           .PaletteIndexAfter = 3U}}},
        {"position hors des dimensions", {Add({99, 0, 0}, 3U)}},
        {"index de palette nul", {Add({2, 0, 0}, 0U)}},
        {"états avant et après identiques",
         {{.SubModelIndex = 0U,
           .Position = {2, 0, 0},
           .ExistedBefore = false,
           .PaletteIndexBefore = 0U,
           .ExistsAfter = false,
           .PaletteIndexAfter = 0U}}},
        {"même position deux fois",
         {Add({2, 0, 0}, 3U), Add({2, 0, 0}, 5U)}},
        {"état avant qui ne correspond pas",
         {{.SubModelIndex = 0U,
           .Position = {1, 1, 1},
           .ExistedBefore = true,
           .PaletteIndexBefore = 99U,
           .ExistsAfter = true,
           .PaletteIndexAfter = 5U}}}};

    for (const Case& scenario : cases)
    {
        const auto document = MakeDocument();
        const VoxelDocumentOperationResult validated =
            document.ValidateVoxelChanges(scenario.Changes);

        auto applied = MakeDocument();
        const VoxelDocumentOperationResult appliedResult =
            applied.ApplyVoxelChanges(scenario.Changes);

        Require(!validated.Succeeded && !appliedResult.Succeeded,
            std::string("Ce cas doit être refusé des deux côtés : ") +
                scenario.Name);
        Require(validated.Error == appliedResult.Error,
            std::string("Codes d'erreur divergents pour : ") + scenario.Name);
    }
}

// Un jeu applicable doit être accepté, et l'accepter ne doit rien changer.
void TestAcceptanceIsPureAndSilent()
{
    auto document = MakeDocument();
    const std::vector<VoxelDocumentChange> changes{
        Add({2, 0, 0}, 3U), Add({3, 0, 0}, 7U)};

    const std::uint64_t revisionBefore = document.GetRevision();
    const std::uint64_t countBefore = document.GetVoxelCount();
    const auto boundsBefore = document.GetBounds(0U);
    const auto paletteBefore = document.GetPaletteSnapshot();

    for (int repeat = 0; repeat < 5; ++repeat)
    {
        Require(document.ValidateVoxelChanges(changes).Succeeded,
            "Un jeu de changements applicable doit être accepté.");
    }

    Require(document.GetRevision() == revisionBefore,
        "Valider ne doit pas consommer de révision.");
    Require(document.GetVoxelCount() == countBefore &&
            document.GetBounds(0U) == boundsBefore &&
            document.GetPaletteSnapshot() == paletteBefore,
        "Valider ne doit modifier ni le contenu ni la palette.");

    // Le journal d'invalidation ne doit rien avoir enregistré : c'est la
    // garantie que la preview ne polluera pas les caches de mesh.
    const auto journal = document.ChangesSince(revisionBefore);
    Require(journal.has_value() && journal->empty(),
        "Valider ne doit rien inscrire au journal des révisions.");

    // Et le jeu validé doit bien s'appliquer ensuite.
    Require(document.ApplyVoxelChanges(changes).Succeeded &&
            document.GetVoxelCount() == countBefore + 2U,
        "Un jeu validé doit s'appliquer sans surprise.");
}

// Un jeu vide est applicable : il ne doit être ni refusé, ni compté comme une
// modification.
void TestEmptySetIsAccepted()
{
    const auto document = MakeDocument();
    Require(document.ValidateVoxelChanges({}).Succeeded,
        "Un jeu de changements vide doit être accepté.");
}

} // namespace

int main()
{
    try
    {
        TestRejectionsMatchApply();
        TestAcceptanceIsPureAndSilent();
        TestEmptySetIsAccepted();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Voxel document validation tests passed.\n";
    return EXIT_SUCCESS;
}
