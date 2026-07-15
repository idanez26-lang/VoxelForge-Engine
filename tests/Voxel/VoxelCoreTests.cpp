#include "VoxelForge/Voxel/Voxel.h"
#include "VoxelForge/Voxel/VoxelColor.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"
#include "VoxelForge/Voxel/VoxelPalette.h"
#include "VoxelForge/Voxel/VoxModelConverter.h"

#include "VoxelForge/Asset/Vox/VoxModel.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using VoxelForge::Voxel::Voxel;
using VoxelForge::Voxel::VoxelColor;
using VoxelForge::Voxel::VoxelGrid;
using VoxelForge::Voxel::VoxelModel;
using VoxelForge::Voxel::VoxelPalette;
using VoxelForge::Voxel::VoxModelConversionError;
using VoxelForge::Voxel::VoxModelConverter;

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

Voxel Occupied(const std::uint8_t colorIndex)
{
    return {colorIndex, Voxel::OccupiedFlag};
}

void TestVoxelAndColor()
{
    static_assert(sizeof(VoxelColor) == 4U);
    static_assert(sizeof(Voxel) == 2U);
    static_assert(std::is_trivially_copyable_v<VoxelColor>);
    static_assert(std::is_trivially_copyable_v<Voxel>);

    const Voxel empty;
    Require(!empty.IsOccupied(), "A default voxel must be empty.");
    Require(empty.ColorIndex == 0U, "Default color index must be zero.");

    Voxel voxel = Occupied(0U);
    Require(voxel.IsOccupied(), "Palette index 0 must support occupancy.");
    voxel.SetOccupied(false);
    Require(!voxel.IsOccupied(), "Explicit occupancy clearing failed.");

    constexpr std::uint8_t reservedFlag = 1U << 5U;
    voxel = {4U, reservedFlag};
    voxel.SetOccupied(true);
    Require(
        voxel.Flags == (reservedFlag | Voxel::OccupiedFlag),
        "Setting occupancy must preserve reserved flags.");
    voxel.SetOccupied(false);
    Require(
        voxel.Flags == reservedFlag,
        "Clearing occupancy must preserve reserved flags.");

    Require(
        VoxelColor{1U, 2U, 3U, 4U} == VoxelColor{1U, 2U, 3U, 4U},
        "Equal colors must compare equal.");
    Require(
        !(VoxelColor{1U, 2U, 3U, 4U} == VoxelColor{4U, 3U, 2U, 1U}),
        "Different colors must not compare equal.");
}

void TestPalette()
{
    static_assert(std::is_same_v<
        decltype(std::declval<VoxelPalette&>().Get(0U)),
        const VoxelColor*>);

    VoxelPalette palette;
    Require(palette.Size() == 256U, "Palette must contain 256 colors.");
    Require(
        palette.Data().size() == 256U,
        "Palette storage must expose 256 contiguous colors.");
    Require(
        palette.Get(0U) != nullptr &&
            *palette.Get(0U) == VoxelColor{},
        "Default palette must be transparent black.");
    Require(
        palette.Set(0U, {1U, 2U, 3U, 4U}) &&
            palette.Set(255U, {5U, 6U, 7U, 8U}),
        "Valid palette writes failed.");
    Require(
        !palette.Set(256U, {}) && palette.Get(256U) == nullptr,
        "Out-of-range palette access must fail explicitly.");

    VoxelPalette copy = palette;
    Require(copy.Set(0U, {9U, 9U, 9U, 9U}), "Copy write failed.");
    Require(
        *palette.Get(0U) == VoxelColor{1U, 2U, 3U, 4U},
        "Palette copies must be independent.");
    copy.Reset();
    Require(
        *copy.Get(0U) == VoxelColor{} &&
            *copy.Get(255U) == VoxelColor{},
        "Palette reset failed.");
}

void TestGridBasicsAndMemoryOrder()
{
    static_assert(std::is_same_v<
        decltype(std::declval<VoxelGrid&>().Get(0U, 0U, 0U)),
        const Voxel*>);
    static_assert(std::is_same_v<
        decltype(std::declval<VoxelGrid&>().Data()),
        const std::vector<Voxel>&>);

    VoxelGrid grid;
    Require(grid.Empty(), "Default grid must be empty.");
    Require(
        grid.Width() == 0U && grid.Height() == 0U && grid.Depth() == 0U,
        "Default grid dimensions must be zero.");
    Require(grid.Resize(3U, 2U, 2U), "Valid grid resize failed.");
    Require(
        grid.Width() == 3U && grid.Height() == 2U && grid.Depth() == 2U &&
            grid.VoxelCount() == 12U,
        "Grid dimensions or count are incorrect.");
    Require(grid.IsInside(2U, 1U, 1U), "Valid coordinate rejected.");
    Require(!grid.IsInside(3U, 1U, 1U), "Invalid coordinate accepted.");
    Require(grid.Get(3U, 0U, 0U) == nullptr, "Invalid Get must return null.");
    Require(
        !grid.Set(0U, 2U, 0U, Occupied(1U)),
        "Invalid Set must fail.");

    Require(grid.Set(0U, 0U, 0U, Occupied(10U)), "Set at index 0 failed.");
    Require(grid.Set(2U, 0U, 0U, Occupied(20U)), "Set on X failed.");
    Require(grid.Set(0U, 1U, 0U, Occupied(30U)), "Set on Y failed.");
    Require(grid.Set(0U, 0U, 1U, Occupied(40U)), "Set on Z failed.");
    Require(grid.OccupiedVoxelCount() == 4U, "Occupied counter is wrong.");
    Require(
        grid.Data()[0U].ColorIndex == 10U &&
            grid.Data()[2U].ColorIndex == 20U &&
            grid.Data()[3U].ColorIndex == 30U &&
            grid.Data()[6U].ColorIndex == 40U,
        "Memory order is not x + y*width + z*width*height.");

    Require(
        grid.Set(0U, 0U, 0U, Occupied(11U)) &&
            grid.OccupiedVoxelCount() == 4U,
        "Replacing an occupied voxel changed the counter.");
    Require(
        grid.Set(0U, 0U, 0U, {}) && grid.OccupiedVoxelCount() == 3U,
        "Clearing one voxel did not update the counter.");
}

void TestGridFillClearAndResize()
{
    VoxelGrid grid;
    Require(grid.Resize(2U, 3U, 4U), "Grid resize failed.");
    grid.Fill(Occupied(7U));
    Require(
        grid.OccupiedVoxelCount() == 24U &&
            grid.Get(1U, 2U, 3U)->ColorIndex == 7U,
        "Grid fill failed.");
    grid.Clear();
    Require(
        grid.VoxelCount() == 24U && grid.OccupiedVoxelCount() == 0U &&
            !grid.Get(1U, 2U, 3U)->IsOccupied(),
        "Clear must preserve dimensions and empty all voxels.");

    constexpr std::uint8_t reservedFlag = 1U << 5U;
    grid.Fill({42U, reservedFlag});
    Require(
        grid.OccupiedVoxelCount() == 0U &&
            grid.Get(1U, 2U, 3U)->Flags == reservedFlag,
        "Fill with an empty voxel must keep the counter at zero.");
    grid.Clear();
    Require(
        grid.Get(1U, 2U, 3U)->ColorIndex == 0U &&
            grid.Get(1U, 2U, 3U)->Flags == 0U,
        "Clear must restore the stable empty value, including reserved flags.");

    Require(grid.Set(1U, 1U, 1U, Occupied(9U)), "Pre-resize Set failed.");
    Require(grid.Resize(1U, 1U, 1U), "Second resize failed.");
    Require(
        grid.VoxelCount() == 1U && grid.OccupiedVoxelCount() == 0U &&
            !grid.Get(0U, 0U, 0U)->IsOccupied(),
        "Successful Resize must discard previous data.");

    grid.Fill(Occupied(3U));
    Require(grid.Resize(0U, 8U, 8U), "Zero-axis resize must succeed.");
    Require(
        grid.Empty() && grid.Width() == 0U && grid.Height() == 0U &&
            grid.Depth() == 0U && grid.OccupiedVoxelCount() == 0U,
        "A zero axis must produce the canonical empty grid.");
}

void TestGridLimitsAndFailureState()
{
    VoxelGrid grid;
    Require(grid.Resize(2U, 2U, 2U), "Initial resize failed.");
    Require(grid.Set(1U, 1U, 1U, Occupied(5U)), "Initial Set failed.");

    Require(
        !grid.Resize(257U, 256U, 256U),
        "Grid beyond the dense payload limit was accepted.");
    Require(
        grid.Width() == 2U && grid.Height() == 2U && grid.Depth() == 2U &&
            grid.OccupiedVoxelCount() == 1U &&
            grid.Get(1U, 1U, 1U)->ColorIndex == 5U,
        "Failed Resize must preserve the previous state.");
    Require(
        !grid.Resize(UINT32_MAX, UINT32_MAX, UINT32_MAX),
        "Overflowing dimensions were accepted.");
    Require(
        grid.Width() == 2U && grid.OccupiedVoxelCount() == 1U,
        "Overflow rejection changed grid state.");

    VoxelGrid maximumGrid;
    Require(
        maximumGrid.Resize(256U, 256U, 256U) &&
            maximumGrid.VoxelCount() == VoxelGrid::MaximumVoxelCount,
        "The exact dense v1 limit must be accepted.");
    Require(
        maximumGrid.Resize(0U, 0U, 0U) && maximumGrid.Empty(),
        "Maximum test grid did not release to the canonical empty state.");
}

VoxelGrid MakeGrid(
    const std::uint32_t size,
    const std::uint8_t colorIndex)
{
    VoxelGrid grid;
    Require(grid.Resize(size, 1U, 1U), "Model test grid resize failed.");
    Require(grid.Set(0U, 0U, 0U, Occupied(colorIndex)), "Model Set failed.");
    return grid;
}

void TestModel()
{
    static_assert(std::is_same_v<
        decltype(std::declval<VoxelModel&>().Grids()),
        const std::vector<VoxelGrid>&>);

    VoxelModel model;
    model.SetName("Castle");
    Require(model.Name() == "Castle", "Model name was not stored.");
    model.AddGrid(MakeGrid(2U, 1U));
    model.AddGrid(MakeGrid(3U, 2U));
    Require(
        model.GridCount() == 2U && model.TotalVoxelCount() == 5U &&
            model.TotalOccupiedVoxelCount() == 2U,
        "Model totals are incorrect.");
    Require(
        model.GetGrid(0U) != nullptr && model.GetGrid(2U) == nullptr,
        "Bounded model grid access is incorrect.");
    Require(
        model.GetGrid(0U)->Set(1U, 0U, 0U, Occupied(3U)) &&
            model.TotalOccupiedVoxelCount() == 3U,
        "Mutable model grid access must retain grid counter invariants.");

    VoxelModel copy = model;
    Require(copy.RemoveGrid(0U), "Valid grid removal failed.");
    Require(!copy.RemoveGrid(5U), "Invalid grid removal succeeded.");
    Require(
        copy.GridCount() == 1U && model.GridCount() == 2U,
        "Model copies must be independent.");

    VoxelModel moved = std::move(copy);
    Require(moved.GridCount() == 1U, "Model move lost grid data.");
    model.Clear();
    Require(
        model.Name().empty() && model.GridCount() == 0U &&
            model.TotalVoxelCount() == 0U &&
            *model.Palette().Get(0U) == VoxelColor{},
        "Model clear failed.");
}

VoxelForge::Asset::Vox::VoxModel MakeSourceModel()
{
    using namespace VoxelForge::Asset::Vox;

    VoxModel source;
    source.Version = 150U;
    source.Palette[0U] = {1U, 2U, 3U, 4U};
    source.Palette[7U] = {10U, 20U, 30U, 40U};
    source.Models.push_back({
        {3U, 4U, 5U},
        {{2U, 3U, 4U, 7U}, {0U, 0U, 0U, 1U}}});
    source.Models.push_back({{2U, 1U, 1U}, {{1U, 0U, 0U, 2U}}});
    return source;
}

void TestVoxConversion()
{
    const auto source = MakeSourceModel();
    const auto sourceModels = source.Models;
    const auto sourcePalette = source.Palette;
    const auto result = VoxModelConverter::Convert(source, "Imported Castle");
    Require(result.Succeeded && result.Model, "Valid VOX conversion failed.");

    const VoxelModel& model = *result.Model;
    Require(model.Name() == "Imported Castle", "Converted name is wrong.");
    Require(model.GridCount() == 2U, "PACK grids were not preserved.");
    Require(
        model.Grids()[0].Width() == 3U &&
            model.Grids()[0].Height() == 4U &&
            model.Grids()[0].Depth() == 5U,
        "VOX dimensions were not preserved.");
    const Voxel* converted = model.Grids()[0].Get(2U, 3U, 4U);
    Require(
        converted != nullptr && converted->IsOccupied() &&
            converted->ColorIndex == 7U,
        "VOX coordinates or color index were not preserved.");
    Require(
        *model.Palette().Get(0U) == VoxelColor{1U, 2U, 3U, 4U} &&
            *model.Palette().Get(7U) == VoxelColor{10U, 20U, 30U, 40U},
        "VOX palette was not copied without remapping.");
    Require(
        model.TotalVoxelCount() == 62U &&
            model.TotalOccupiedVoxelCount() == 3U,
        "Converted model totals are incorrect.");
    Require(
        source.Models == sourceModels && source.Palette == sourcePalette,
        "Conversion modified its VOX source.");
}

void TestInvalidVoxConversion()
{
    using namespace VoxelForge::Asset::Vox;

    VoxModel source;
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::EmptyModel,
        "Empty VOX model was accepted.");

    source.Models.push_back({{0U, 1U, 1U}, {}});
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::InvalidDimensions,
        "Zero VOX dimension was accepted.");

    source.Models = {{{257U, 256U, 256U}, {}}};
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::GridTooLarge,
        "Excessive dense VOX grid was accepted.");

    source.Models = {
        {{256U, 256U, 256U}, {}},
        {{1U, 1U, 1U}, {}}};
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::GridTooLarge,
        "PACK payload beyond the model-wide dense limit was accepted.");

    source.Models = {{{2U, 2U, 2U}, {{2U, 0U, 0U, 1U}}}};
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::InvalidVoxel,
        "Out-of-bounds VOX coordinate was accepted.");

    source.Models = {{{2U, 2U, 2U}, {{0U, 0U, 0U, 0U}}}};
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::InvalidVoxel,
        "Reserved VOX color index 0 was accepted.");

    source.Models = {{{2U, 2U, 2U},
        {{1U, 1U, 1U, 1U}, {1U, 1U, 1U, 2U}}}};
    Require(
        VoxModelConverter::Convert(source).Error ==
            VoxModelConversionError::DuplicateVoxel,
        "Duplicate VOX coordinates were accepted.");
}

void PrintTypeSizes()
{
    std::cout << "sizeof(Voxel)=" << sizeof(Voxel) << '\n'
              << "sizeof(VoxelColor)=" << sizeof(VoxelColor) << '\n'
              << "sizeof(VoxelPalette)=" << sizeof(VoxelPalette) << '\n'
              << "sizeof(VoxelGrid)=" << sizeof(VoxelGrid) << '\n'
              << "sizeof(VoxelModel)=" << sizeof(VoxelModel) << '\n';
}
}

int main()
{
    try
    {
        TestVoxelAndColor();
        TestPalette();
        TestGridBasicsAndMemoryOrder();
        TestGridFillClearAndResize();
        TestGridLimitsAndFailureState();
        TestModel();
        TestVoxConversion();
        TestInvalidVoxConversion();
        PrintTypeSizes();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
