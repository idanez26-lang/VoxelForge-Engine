#include "VoxelStamps/Palette/PaletteMappingEngine.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

constexpr StampColor Color(const std::uint8_t red, const std::uint8_t green = 0U,
    const std::uint8_t blue = 0U, const std::uint8_t alpha = 255U)
{
    return {.Red = red, .Green = green, .Blue = blue, .Alpha = alpha};
}

Asset::Voxel::VoxelDocumentPaletteSnapshot DefaultSnapshot()
{
    return {.Colors = Asset::Vox::DefaultVoxPalette(), .HasCustomPalette = false};
}

PaletteMappingRequest Request(const std::vector<StampPaletteEntry>& palette,
    const std::vector<StampVoxel>& voxels)
{
    return {.StampPalette = palette, .StampVoxels = voxels, .DocumentPalette = DefaultSnapshot()};
}

std::vector<StampPaletteEntry> Palette(std::initializer_list<StampColor> colors)
{
    std::vector<StampPaletteEntry> result;
    result.reserve(colors.size());
    std::uint8_t localId = 0U;
    for (const StampColor color : colors)
    {
        result.push_back({.LocalColorId = localId++, .Color = color});
    }
    return result;
}

std::vector<StampVoxel> Voxels(const std::size_t count)
{
    std::vector<StampVoxel> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index)
    {
        result.push_back({.Position = {.X = static_cast<std::int32_t>(index)},
            .LocalColorId = static_cast<std::uint8_t>(index)});
    }
    return result;
}

void TestEmptyStampAndBasicValidation()
{
    const std::vector<StampPaletteEntry> emptyPalette;
    const std::vector<StampVoxel> emptyVoxels;
    const auto empty = PaletteMappingEngine::Plan(Request(emptyPalette, emptyVoxels));
    Require(empty.Status == PaletteMappingStatus::NoChange && empty.Plan.LocalToDocument.empty() &&
                !empty.Plan.HasPaletteChanges(),
        "Empty palette and voxel inputs must produce a no-change plan.");

    const std::vector<StampVoxel> voxel{{.LocalColorId = 0U}};
    Require(PaletteMappingEngine::Plan(Request(emptyPalette, voxel)).Status ==
                PaletteMappingStatus::InvalidStampPalette,
        "Voxels without a palette must fail explicitly.");
    const auto palette = Palette({Color(1U)});
    Require(PaletteMappingEngine::Plan(Request(palette, emptyVoxels)).Status ==
                PaletteMappingStatus::InvalidStampVoxelSet,
        "A non-empty palette without voxels must fail explicitly.");
}

void TestExactReuseAndCanonicalOutput()
{
    const auto palette = Palette({Color(12U, 34U, 56U), Color(99U, 88U, 77U)});
    const auto voxels = Voxels(2U);
    auto request = Request(palette, voxels);
    request.DocumentPalette.Colors[9U] = palette[1].Color;
    request.DocumentPalette.Colors[42U] = palette[0].Color;
    request.DocumentPalette.Colors[3U] = palette[0].Color;
    request.DocumentPalette.HasCustomPalette = true;
    request.OccupiedDocumentPaletteIndices[3U] = true;
    request.OccupiedDocumentPaletteIndices[9U] = true;
    request.OccupiedDocumentPaletteIndices[42U] = true;
    const auto result = PaletteMappingEngine::Plan(request);
    Require(result.Status == PaletteMappingStatus::Success && result.Plan.AddedColorCount == 0U &&
                result.Plan.ReusedColorCount == 2U && result.Plan.LocalToDocument ==
                    std::vector<PaletteMappingEntry>{{0U, 3U}, {1U, 9U}},
        "Exact matches must use the smallest document index and canonical local-ID order.");
    Require(result.Plan.FinalDocumentPalette == request.DocumentPalette,
        "Exact reuse must not change the planned palette.");
}

void TestFreeExactSlotAndNewColors()
{
    const auto palette = Palette({Color(17U, 18U, 19U), Color(20U, 21U, 22U), Color(23U, 24U, 25U)});
    const auto voxels = Voxels(3U);
    auto request = Request(palette, voxels);
    request.DocumentPalette.Colors[2U] = palette[0].Color;
    request.DocumentPalette.HasCustomPalette = true;
    request.OccupiedDocumentPaletteIndices[1U] = true;
    const auto result = PaletteMappingEngine::Plan(request);
    Require(result.IsSuccess() && result.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 2U}, {1U, 3U}, {2U, 4U}} &&
                result.Plan.ReusedColorCount == 1U && result.Plan.AddedColorCount == 2U,
        "A free exact slot must become unavailable before later new colors are allocated.");
    Require(result.Plan.AddedColors == std::vector<PlannedPaletteColor>{{3U, palette[1].Color}, {4U, palette[2].Color}} &&
                result.Plan.FinalDocumentPalette.Colors[2U] == palette[0].Color &&
                result.Plan.FinalDocumentPalette.Colors[3U] == palette[1].Color &&
                result.Plan.FinalDocumentPalette.Colors[4U] == palette[2].Color,
        "Only allocated free slots may differ in the final planned palette.");
}

void TestDuplicateColorsAndDeterminism()
{
    const auto palette = Palette({Color(240U), Color(240U), Color(1U, 2U, 3U)});
    const auto voxels = Voxels(3U);
    auto request = Request(palette, voxels);
    request.OccupiedDocumentPaletteIndices[1U] = true;
    const auto first = PaletteMappingEngine::Plan(request);
    const auto second = PaletteMappingEngine::Plan(request);
    Require(first == second && first.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 2U}, {1U, 2U}, {2U, 3U}} &&
                first.Plan.AddedColorCount == 2U,
        "Duplicate exact stamp colors must share an index and repeated plans must be identical.");
    Require(first.Plan.AddedColors == std::vector<PlannedPaletteColor>{{2U, palette[0].Color}, {3U, palette[2].Color}} &&
                first.Plan.ReusedColors.empty(),
        "A newly added duplicate must not be counted or emitted a second time as reused.");

    auto existing = Request(palette, voxels);
    existing.DocumentPalette.Colors[8U] = palette[0].Color;
    existing.DocumentPalette.HasCustomPalette = true;
    const auto existingResult = PaletteMappingEngine::Plan(existing);
    Require(existingResult.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 8U}, {1U, 8U}, {2U, 1U}} &&
                existingResult.Plan.ReusedColors == std::vector<PlannedPaletteColor>{{8U, palette[0].Color}} &&
                existingResult.Plan.ReusedColorCount == 1U,
        "A duplicated existing color must be counted and emitted exactly once.");
}

void TestLogicalEmptyDocumentAndCompleteReuse()
{
    const auto newPalette = Palette({Color(91U, 92U, 93U)});
    const auto oneVoxel = Voxels(1U);
    auto logicallyEmpty = Request(newPalette, oneVoxel);
    logicallyEmpty.DocumentPalette.Colors[17U] = Color(44U, 45U, 46U);
    logicallyEmpty.DocumentPalette.HasCustomPalette = true;
    const auto allocated = PaletteMappingEngine::Plan(logicallyEmpty);
    Require(allocated.IsSuccess() && allocated.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 1U}} &&
                allocated.Plan.AddedColors == std::vector<PlannedPaletteColor>{{1U, newPalette[0].Color}},
        "A custom but logically empty document must allocate from the smallest non-reserved index.");

    const auto identicalPalette = Palette({Color(3U, 4U, 5U, 254U), Color(6U, 7U, 8U, 254U)});
    const auto twoVoxels = Voxels(2U);
    auto completeReuse = Request(identicalPalette, twoVoxels);
    completeReuse.DocumentPalette.Colors[4U] = identicalPalette[0].Color;
    completeReuse.DocumentPalette.Colors[9U] = identicalPalette[1].Color;
    completeReuse.DocumentPalette.HasCustomPalette = true;
    completeReuse.OccupiedDocumentPaletteIndices[4U] = true;
    completeReuse.OccupiedDocumentPaletteIndices[9U] = true;
    const auto reused = PaletteMappingEngine::Plan(completeReuse);
    Require(reused.IsSuccess() && reused.Plan.AddedColorCount == 0U &&
                reused.Plan.ReusedColorCount == 2U && reused.Plan.FinalDocumentPalette == completeReuse.DocumentPalette,
        "An identical palette must reuse every color and leave the planned palette unchanged.");
}

void TestSeveralExistingColorsMixedWithNewColors()
{
    const auto palette = Palette({Color(31U, 1U, 2U), Color(32U, 1U, 2U),
        Color(33U, 1U, 2U), Color(34U, 1U, 2U)});
    const auto fourVoxels = Voxels(4U);
    auto request = Request(palette, fourVoxels);
    request.DocumentPalette.Colors[5U] = palette[0].Color;
    request.DocumentPalette.Colors[7U] = palette[1].Color;
    request.DocumentPalette.Colors[12U] = palette[2].Color;
    request.DocumentPalette.HasCustomPalette = true;
    request.OccupiedDocumentPaletteIndices[5U] = true;
    request.OccupiedDocumentPaletteIndices[7U] = true;
    request.OccupiedDocumentPaletteIndices[12U] = true;
    const auto result = PaletteMappingEngine::Plan(request);
    Require(result.IsSuccess() && result.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 5U}, {1U, 7U}, {2U, 12U}, {3U, 1U}} &&
                result.Plan.ReusedColorCount == 3U && result.Plan.AddedColorCount == 1U,
        "Several existing colors and a new color must map together without changing existing slots.");
}

void TestLaterExactMatchIsReservedBeforeEarlierAllocation()
{
    const auto palette = Palette({Color(71U, 72U, 73U), Color(81U, 82U, 83U)});
    const auto twoVoxels = Voxels(2U);
    auto request = Request(palette, twoVoxels);
    request.PaletteCapacity = 4U;
    request.DocumentPalette.Colors[2U] = palette[1].Color;
    request.DocumentPalette.HasCustomPalette = true;
    request.OccupiedDocumentPaletteIndices[1U] = true;
    const auto result = PaletteMappingEngine::Plan(request);
    Require(result.IsSuccess() && result.Plan.LocalToDocument ==
                std::vector<PaletteMappingEntry>{{0U, 3U}, {1U, 2U}} &&
                result.Plan.ReusedColors == std::vector<PlannedPaletteColor>{{2U, palette[1].Color}} &&
                result.Plan.AddedColors == std::vector<PlannedPaletteColor>{{3U, palette[0].Color}},
        "An exact match later in the Stamp must be reserved before an earlier new color allocates.");
}

void TestCapacityAndReservedIndex()
{
    const auto palette = Palette({Color(1U)});
    const auto voxels = Voxels(1U);
    auto oneSlot = Request(palette, voxels);
    oneSlot.PaletteCapacity = 1U;
    Require(PaletteMappingEngine::Plan(oneSlot).Status == PaletteMappingStatus::PaletteCapacityExceeded,
        "Capacity one with reserved index zero has no usable color slots.");

    auto full = Request(palette, voxels);
    for (std::size_t index = 1U; index < 256U; ++index) full.OccupiedDocumentPaletteIndices[index] = true;
    Require(PaletteMappingEngine::Plan(full).Status == PaletteMappingStatus::PaletteCapacityExceeded,
        "A full 255-color document palette must reject a missing color.");

    auto invalidCapacity = Request(palette, voxels);
    invalidCapacity.PaletteCapacity = 0U;
    Require(PaletteMappingEngine::Plan(invalidCapacity).Status == PaletteMappingStatus::InvalidPaletteCapacity,
        "Capacity zero must be rejected.");
    invalidCapacity.PaletteCapacity = 257U;
    Require(PaletteMappingEngine::Plan(invalidCapacity).Status == PaletteMappingStatus::InvalidPaletteCapacity,
        "Capacity above 256 must be rejected.");
    auto invalidReserved = Request(palette, voxels);
    invalidReserved.PaletteCapacity = 8U;
    invalidReserved.ReservedDocumentPaletteIndex = 8U;
    Require(PaletteMappingEngine::Plan(invalidReserved).Status ==
                PaletteMappingStatus::InvalidReservedPaletteIndex,
        "A reserved index outside capacity must be rejected.");
    auto occupiedOutsideCapacity = Request(palette, voxels);
    occupiedOutsideCapacity.PaletteCapacity = 8U;
    occupiedOutsideCapacity.OccupiedDocumentPaletteIndices[8U] = true;
    Require(PaletteMappingEngine::Plan(occupiedOutsideCapacity).Status ==
                PaletteMappingStatus::InvalidOccupiedPaletteIndex,
        "Occupied indices outside capacity must be rejected.");
    auto reservedOccupied = Request(palette, voxels);
    reservedOccupied.OccupiedDocumentPaletteIndices[0U] = true;
    Require(PaletteMappingEngine::Plan(reservedOccupied).Status == PaletteMappingStatus::InvalidOccupiedPaletteIndex,
        "The reserved index must never be marked occupied.");

    std::vector<StampPaletteEntry> maximumPalette;
    maximumPalette.reserve(256U);
    for (std::size_t index = 0U; index < 256U; ++index)
    {
        maximumPalette.push_back({.LocalColorId = static_cast<std::uint8_t>(index),
            .Color = Color(static_cast<std::uint8_t>(index), 1U, 2U, 254U)});
    }
    const auto maximumResult = PaletteMappingEngine::Plan(Request(maximumPalette, Voxels(256U)));
    Require(maximumResult.Status == PaletteMappingStatus::PaletteCapacityExceeded &&
                maximumResult.Plan.LocalToDocument.empty() && maximumResult.Plan.AddedColors.empty(),
        "A 256-color stamp must overflow the 255 non-reserved destination slots explicitly.");
}

void TestInvalidInputsAndExtremeRgba()
{
    auto palette = Palette({Color(0U, 255U, 0U, 0U)});
    const auto voxels = Voxels(1U);
    auto result = PaletteMappingEngine::Plan(Request(palette, voxels));
    Require(result.IsSuccess() && result.Plan.FinalDocumentPalette.Colors[1U] == palette[0].Color,
        "Extreme RGBA values must be mapped exactly.");

    palette[0].LocalColorId = 1U;
    Require(PaletteMappingEngine::Plan(Request(palette, voxels)).Status == PaletteMappingStatus::InvalidStampPalette,
        "Non-canonical stamp local IDs must fail.");
    auto invalidVoxel = Voxels(1U);
    invalidVoxel[0].LocalColorId = 1U;
    const auto validPalette = Palette({Color(1U)});
    Require(PaletteMappingEngine::Plan(Request(validPalette, invalidVoxel)).Status ==
                PaletteMappingStatus::InvalidStampVoxelReference,
        "Out-of-range voxel local IDs must fail.");
    const auto twoColorPalette = Palette({Color(1U), Color(2U)});
    Require(PaletteMappingEngine::Plan(Request(twoColorPalette, voxels)).Status ==
                PaletteMappingStatus::InvalidStampVoxelReference,
        "Every supplied stamp palette entry must be referenced by at least one voxel.");
    auto invalidSnapshot = Request(validPalette, voxels);
    invalidSnapshot.DocumentPalette.Colors[1U] = Color(99U);
    Require(PaletteMappingEngine::Plan(invalidSnapshot).Status ==
                PaletteMappingStatus::InvalidDocumentPaletteSnapshot,
        "A non-custom snapshot must contain the default palette exactly.");
}

} // namespace

int main()
{
    try
    {
        TestEmptyStampAndBasicValidation();
        TestExactReuseAndCanonicalOutput();
        TestFreeExactSlotAndNewColors();
        TestDuplicateColorsAndDeterminism();
        TestLogicalEmptyDocumentAndCompleteReuse();
        TestSeveralExistingColorsMixedWithNewColors();
        TestLaterExactMatchIsReservedBeforeEarlierAllocation();
        TestCapacityAndReservedIndex();
        TestInvalidInputsAndExtremeRgba();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Palette mapping engine tests passed.\n";
    return EXIT_SUCCESS;
}
