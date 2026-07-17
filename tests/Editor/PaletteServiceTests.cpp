#include "Palette/PaletteService.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

PaletteService::Palette BuildPalette()
{
    PaletteService::Palette palette{};
    for (std::size_t index = 0U; index < palette.size(); ++index)
    {
        palette[index] = {
            static_cast<std::uint8_t>(index),
            static_cast<std::uint8_t>(255U - index),
            static_cast<std::uint8_t>((index * 3U) % 256U),
            255U};
    }
    return palette;
}

void TestEmptyAndCompletePalette()
{
    PaletteService service;
    Require(!service.HasActivePalette() && service.ActivePalette() == nullptr &&
        !service.ActiveIndex() && !service.ActiveColor() &&
        service.RecentColors().empty() && !service.SelectColor(1U) &&
        !service.RecordActiveColorUsage(),
        "An empty palette panel state should expose no selection.");

    const PaletteService::Palette palette = BuildPalette();
    service.SetPalette(palette, "Document Palette");
    Require(service.HasActivePalette() && service.ActivePalette() != nullptr &&
        service.ActivePalette()->size() == PaletteService::PaletteSize &&
        service.ActivePaletteName() == "Document Palette" &&
        service.ActiveIndex() == PaletteService::FirstSelectableIndex &&
        service.ActiveColor()->Color == palette[1U],
        "The complete 256-color palette was not activated.");
}

void TestSelectionValidationAndColorChanges()
{
    PaletteService service;
    const PaletteService::Palette palette = BuildPalette();
    service.SetPalette(palette, "Custom VOX Palette");
    Require(!service.SelectColor(0U) && !service.SelectColor(256U) &&
        service.ActiveIndex() == 1U,
        "Reserved or out-of-range indices must be rejected atomically.");
    Require(service.SelectColor(42U) && service.ActiveIndex() == 42U &&
        service.ActiveColor()->Index == 42U &&
        service.ActiveColor()->Color == palette[42U],
        "Selecting a color did not update index and RGBA together.");
    Require(service.SelectColor(255U) && service.ActiveColor()->Color ==
        palette[255U], "The final selectable color should remain available.");
}

void TestRecentUsageIsBoundedAndSessionIndependent()
{
    PaletteService service;
    service.SetPalette(BuildPalette(), "Document Palette");
    for (std::size_t index = 1U; index <= 10U; ++index)
    {
        Require(service.SelectColor(index) && service.RecordActiveColorUsage(),
            "Unable to record a valid recent color.");
    }
    Require(service.RecentColors().size() ==
        PaletteService::RecentColorLimit &&
        service.RecentColors().front().Index == 10U &&
        service.RecentColors().back().Index == 3U,
        "Recent colors must be newest-first and limited to eight.");

    Require(service.SelectColor(6U) && service.RecordActiveColorUsage() &&
        service.RecentColors().size() == PaletteService::RecentColorLimit &&
        service.RecentColors().front().Index == 6U,
        "Reusing a color should move it to the front without duplication.");
    std::size_t occurrences = 0U;
    for (const PaletteColorSelection& recent : service.RecentColors())
        if (recent.Index == 6U) ++occurrences;
    Require(occurrences == 1U, "Recent colors contain a duplicate index.");

    const std::size_t restoredIndex = 17U;
    service.SetPalette(BuildPalette(), "Restored Palette");
    Require(service.RecentColors().empty() &&
        service.SelectColor(restoredIndex) &&
        service.ActiveIndex() == restoredIndex &&
        service.RecentColors().empty(),
        "Restoring a session index must not restore recent color history.");
    service.Clear();
    Require(!service.HasActivePalette() && service.RecentColors().empty(),
        "Closing a palette must release all session-only state.");
}
}

int main()
{
    try
    {
        TestEmptyAndCompletePalette();
        TestSelectionValidationAndColorChanges();
        TestRecentUsageIsBoundedAndSessionIndependent();
        std::cout << "Palette service tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Palette service tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
