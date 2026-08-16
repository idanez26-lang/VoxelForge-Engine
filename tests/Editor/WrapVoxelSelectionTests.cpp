// VF-WRAP-V1 — tests de la geometrie pure de Wrap.
//
// Chaque exigence du §17 est mesuree sur l'ENSEMBLE de voxels produit, jamais
// sur les options : Repeat, Crop par les deux faces de chaque axe, vide du
// motif, Spacing, Mirror Repeat, multi-axes.

#include "Transform/WrapVoxelSelectionOperation.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
using Position = VoxelForge::Asset::Voxel::VoxelPosition;
using Voxel = VoxelForge::Asset::Voxel::Voxel;

void Require(const bool value, const std::string_view message)
{
    if (!value) throw std::runtime_error(std::string(message));
}

// Un motif : liste (position, palette). Le vide est ce qui n'y figure pas.
using Pattern = std::vector<std::pair<Position, std::uint8_t>>;

std::vector<TransformPreviewVoxel> Sources(const Pattern& pattern)
{
    std::vector<TransformPreviewVoxel> voxels;
    voxels.reserve(pattern.size());
    for (const auto& [position, palette] : pattern)
        voxels.push_back({position, position, Voxel{palette},
            TransformPreviewVoxelState::Valid});
    return voxels;
}

// Resultat sous forme triee position -> palette, pour des comparaisons
// exactes et lisibles.
std::map<std::tuple<int, int, int>, std::uint8_t> ResultMap(
    const VoxelWrapGeometry& geometry)
{
    std::map<std::tuple<int, int, int>, std::uint8_t> result;
    for (const TransformPreviewDestinationVoxel& voxel : geometry.Destinations)
        result[{voxel.DestinationPosition.X, voxel.DestinationPosition.Y,
            voxel.DestinationPosition.Z}] = voxel.Value.PaletteIndex;
    return result;
}

VoxelWrapGeometry Wrap(const Pattern& pattern,
    const SelectionBounds sourceBounds, const SelectionBounds newBounds,
    const VoxelWrapOptions& options = {})
{
    const std::vector<TransformPreviewVoxel> sources = Sources(pattern);
    return WrapVoxelSelectionOperation::BuildGeometry(
        sources, sourceBounds, newBounds, options);
}

// --- §17 Repeat simple : AB elargi a 6 -> ABABAB --------------------------

void TestSimpleRepeat()
{
    const Pattern ab{{{0, 0, 0}, 1U}, {{1, 0, 0}, 2U}};
    const VoxelWrapGeometry geometry = Wrap(ab,
        SelectionBounds::FromCorners({0, 0, 0}, {1, 0, 0}),
        SelectionBounds::FromCorners({0, 0, 0}, {5, 0, 0}));
    Require(geometry.Valid(), "Repeat simple : geometrie invalide.");
    const auto result = ResultMap(geometry);
    Require(result.size() == 6U, "ABABAB doit produire 6 voxels.");
    for (int x = 0; x < 6; ++x)
        Require(result.at({x, 0, 0}) == (x % 2 == 0 ? 1U : 2U),
            "La sequence ABABAB n'est pas respectee en x=" +
                std::to_string(x));
    // Les sources sont referencees en one-to-many : 6 destinations, 2 sources.
    Require(geometry.Destinations.size() == 6U,
        "Le one-to-many n'a pas produit toutes les repetitions.");
}

// --- §17 Crop : ABCD reduit a 2, par les DEUX faces de chaque axe ---------

void TestCropBothFacesEveryAxis()
{
    // Axe X : motif ABCD.
    const Pattern abcd{{{0, 0, 0}, 1U}, {{1, 0, 0}, 2U},
        {{2, 0, 0}, 3U}, {{3, 0, 0}, 4U}};
    const SelectionBounds source =
        SelectionBounds::FromCorners({0, 0, 0}, {3, 0, 0});

    // Tirer la face X+ vers l'interieur (ancre X-) : reste AB.
    VoxelWrapOptions anchorMin;
    anchorMin.X.Anchor = WrapAxisAnchor::Minimum;
    const auto cropMax = ResultMap(Wrap(abcd, source,
        SelectionBounds::FromCorners({0, 0, 0}, {1, 0, 0}), anchorMin));
    Require(cropMax.size() == 2U && cropMax.at({0, 0, 0}) == 1U &&
        cropMax.at({1, 0, 0}) == 2U,
        "Crop par la face X+ doit garder AB.");

    // Tirer la face X- vers l'interieur (ancre X+) : reste CD, EN PLACE.
    VoxelWrapOptions anchorMax;
    anchorMax.X.Anchor = WrapAxisAnchor::Maximum;
    const auto cropMin = ResultMap(Wrap(abcd, source,
        SelectionBounds::FromCorners({2, 0, 0}, {3, 0, 0}), anchorMax));
    Require(cropMin.size() == 2U && cropMin.at({2, 0, 0}) == 3U &&
        cropMin.at({3, 0, 0}) == 4U,
        "Crop par la face X- doit garder CD a leur place.");

    // Meme exigence sur Y et Z, motif a deux cellules.
    for (int axis = 1; axis <= 2; ++axis)
    {
        const auto at = [axis](const int v) -> Position
        {
            return axis == 1 ? Position{0, v, 0} : Position{0, 0, v};
        };
        const Pattern pair{
            {at(0), std::uint8_t{5U}}, {at(1), std::uint8_t{6U}}};
        const SelectionBounds axisSource =
            SelectionBounds::FromCorners(at(0), at(1));
        VoxelWrapOptions minAnchor;   // face + tiree : reste la cellule 0
        const auto keepFirst = ResultMap(Wrap(pair, axisSource,
            SelectionBounds::FromCorners(at(0), at(0)), minAnchor));
        Require(keepFirst.size() == 1U &&
            keepFirst.begin()->second == 5U,
            "Crop face maximum : la cellule d'ancre minimum doit rester.");
        VoxelWrapOptions maxAnchor;
        (axis == 1 ? maxAnchor.Y : maxAnchor.Z).Anchor =
            WrapAxisAnchor::Maximum;
        const auto keepLast = ResultMap(Wrap(pair, axisSource,
            SelectionBounds::FromCorners(at(1), at(1)), maxAnchor));
        Require(keepLast.size() == 1U &&
            keepLast.begin()->second == 6U,
            "Crop face minimum : la cellule d'ancre maximum doit rester.");
    }
}

// --- §17 Vide : |X..X| repete conserve son vide ---------------------------

void TestVoidIsPartOfThePattern()
{
    // Motif de periode 4 : occupe, vide, vide, occupe.
    const Pattern gap{{{0, 0, 0}, 1U}, {{3, 0, 0}, 1U}};
    const VoxelWrapGeometry geometry = Wrap(gap,
        SelectionBounds::FromCorners({0, 0, 0}, {3, 0, 0}),
        SelectionBounds::FromCorners({0, 0, 0}, {11, 0, 0}));
    Require(geometry.Valid(), "Motif a vide : geometrie invalide.");
    const auto result = ResultMap(geometry);
    Require(result.size() == 6U,
        "Trois repetitions de |X..X| doivent donner 6 voxels, pas un "
        "compactage.");
    for (const int x : {0, 3, 4, 7, 8, 11})
        Require(result.contains({x, 0, 0}),
            "Position occupee attendue absente en x=" + std::to_string(x));
    for (const int x : {1, 2, 5, 6, 9, 10})
        Require(!result.contains({x, 0, 0}),
            "Le vide du motif a ete comble en x=" + std::to_string(x));
}

// --- §17 Spacing : 0, 1, plusieurs ----------------------------------------

void TestSpacing()
{
    const Pattern abc{{{0, 0, 0}, 1U}, {{1, 0, 0}, 2U}, {{2, 0, 0}, 3U}};
    const SelectionBounds source =
        SelectionBounds::FromCorners({0, 0, 0}, {2, 0, 0});

    // Spacing 0 : ABCABCABC.
    const auto s0 = ResultMap(Wrap(abc, source,
        SelectionBounds::FromCorners({0, 0, 0}, {8, 0, 0})));
    Require(s0.size() == 9U, "Spacing 0 : 9 voxels attendus.");
    for (int x = 0; x < 9; ++x)
        Require(s0.at({x, 0, 0}) == static_cast<std::uint8_t>(x % 3 + 1),
            "Spacing 0 : sequence ABC brisee.");

    // Spacing 2 : ABC..ABC..ABC (periode 5).
    VoxelWrapOptions spacing2;
    spacing2.X.Spacing = 2;
    const auto s2 = ResultMap(Wrap(abc, source,
        SelectionBounds::FromCorners({0, 0, 0}, {12, 0, 0}), spacing2));
    Require(s2.size() == 9U, "Spacing 2 : 9 voxels sur 3 repetitions.");
    for (const int x : {0, 1, 2, 5, 6, 7, 10, 11, 12})
        Require(s2.contains({x, 0, 0}),
            "Spacing 2 : repetition attendue absente en x=" +
                std::to_string(x));
    for (const int x : {3, 4, 8, 9})
        Require(!s2.contains({x, 0, 0}),
            "Spacing 2 : la cellule d'espacement n'est pas vide en x=" +
                std::to_string(x));

    // Spacing 1 : le motif lui-meme n'est pas modifie.
    VoxelWrapOptions spacing1;
    spacing1.X.Spacing = 1;
    const auto s1 = ResultMap(Wrap(abc, source,
        SelectionBounds::FromCorners({0, 0, 0}, {6, 0, 0}), spacing1));
    Require(s1.size() == 6U && s1.at({0, 0, 0}) == 1U &&
        s1.at({2, 0, 0}) == 3U && !s1.contains({3, 0, 0}) &&
        s1.at({4, 0, 0}) == 1U && s1.at({6, 0, 0}) == 3U,
        "Spacing 1 : ABC.ABC attendu.");
}

// --- §17 Mirror Repeat : ABC | CBA | ABC ----------------------------------

void TestMirrorRepeat()
{
    const Pattern abc{{{0, 0, 0}, 1U}, {{1, 0, 0}, 2U}, {{2, 0, 0}, 3U}};
    const SelectionBounds source =
        SelectionBounds::FromCorners({0, 0, 0}, {2, 0, 0});
    VoxelWrapOptions mirror;
    mirror.X.MirrorRepeat = true;
    const auto result = ResultMap(Wrap(abc, source,
        SelectionBounds::FromCorners({0, 0, 0}, {8, 0, 0}), mirror));
    Require(result.size() == 9U, "Mirror Repeat : 9 voxels attendus.");
    const std::uint8_t expected[] = {1, 2, 3, 3, 2, 1, 1, 2, 3};
    for (int x = 0; x < 9; ++x)
        Require(result.at({x, 0, 0}) == expected[x],
            "ABC|CBA|ABC brise en x=" + std::to_string(x));
    // Sans Mirror Repeat, la meme configuration donne ABCABCABC : l'option
    // change reellement le resultat.
    const auto plain = ResultMap(Wrap(abc, source,
        SelectionBounds::FromCorners({0, 0, 0}, {8, 0, 0})));
    Require(plain.at({3, 0, 0}) == 1U && result.at({3, 0, 0}) == 3U,
        "Mirror Repeat n'a aucun effet mesurable.");
}

// --- §17 Multi-axes : X, Y, Z, X+Y, X+Y+Z ---------------------------------

void TestMultiAxis()
{
    // Motif 2x2x2 : huit voxels a valeurs distinctes.
    Pattern cube;
    std::uint8_t palette = 1U;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                cube.push_back({{x, y, z}, palette++});
    const SelectionBounds source =
        SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});

    struct Case final
    {
        std::string_view Name;
        Position NewMaximum;
        std::size_t Expected;
    };
    const Case cases[] = {
        {"X", {3, 1, 1}, 16U},
        {"Y", {1, 3, 1}, 16U},
        {"Z", {1, 1, 3}, 16U},
        {"X+Y", {3, 3, 1}, 32U},
        {"X+Y+Z", {3, 3, 3}, 64U},
    };
    for (const Case& item : cases)
    {
        const auto result = ResultMap(Wrap(cube, source,
            SelectionBounds::FromCorners({0, 0, 0}, item.NewMaximum)));
        Require(result.size() == item.Expected,
            std::string(item.Name) + " : compte de repetition inattendu.");
        // La valeur en toute cellule est celle du motif module 2 par axe.
        for (const auto& [key, value] : result)
        {
            const auto [x, y, z] = key;
            const std::uint8_t expected = static_cast<std::uint8_t>(
                1 + (x % 2) + 2 * (y % 2) + 4 * (z % 2));
            Require(value == expected,
                std::string(item.Name) + " : valeur fausse dans une "
                "repetition multi-axes.");
        }
    }
}

// --- Bornes serrees et garde-fous ----------------------------------------

void TestTightBoundsAndGuards()
{
    // Crop dans une zone entierement vide du motif : resultat vide, message.
    const Pattern gap{{{0, 0, 0}, 1U}, {{3, 0, 0}, 1U}};
    const SelectionBounds source =
        SelectionBounds::FromCorners({0, 0, 0}, {3, 0, 0});
    const VoxelWrapGeometry empty = Wrap(gap, source,
        SelectionBounds::FromCorners({1, 0, 0}, {2, 0, 0}));
    Require(!empty.Valid() && !empty.Message.empty(),
        "Un crop entierement vide doit etre signale, pas invente.");

    // Bornes serrees : un crop qui laisse un bord vide les resserre.
    const VoxelWrapGeometry tight = Wrap(gap, source,
        SelectionBounds::FromCorners({0, 0, 0}, {2, 0, 0}));
    Require(tight.Valid() && tight.Bounds.Minimum.X == 0 &&
        tight.Bounds.Maximum.X == 0,
        "Les bornes rendues doivent etre serrees sur le resultat occupe.");
    Require(tight.RequestedBounds.Maximum.X == 2,
        "Les bornes demandees doivent etre conservees pour la selection.");

    // Spacing negatif refuse.
    VoxelWrapOptions bad;
    bad.X.Spacing = -1;
    Require(!Wrap(gap, source, source, bad).Valid(),
        "Un spacing negatif doit etre refuse.");
}
}

int main()
{
    try
    {
        TestSimpleRepeat();
        TestCropBothFacesEveryAxis();
        TestVoidIsPartOfThePattern();
        TestSpacing();
        TestMirrorRepeat();
        TestMultiAxis();
        TestTightBoundsAndGuards();
        std::cout << "Wrap geometry tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
