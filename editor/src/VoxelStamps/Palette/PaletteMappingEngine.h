#pragma once

#include "VoxelStamps/StampTypes.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

/// Status returned by the pure palette-mapping planner.
enum class PaletteMappingStatus
{
    Success,
    NoChange,
    InvalidPaletteCapacity,
    InvalidReservedPaletteIndex,
    InvalidDocumentPaletteSnapshot,
    InvalidOccupiedPaletteIndex,
    InvalidStampPalette,
    InvalidStampVoxelSet,
    InvalidStampVoxelReference,
    InvalidRequiredLocalColorId,
    PaletteCapacityExceeded,
    AllocationFailure
};

[[nodiscard]] constexpr std::string_view PaletteMappingStatusMessage(
    const PaletteMappingStatus status) noexcept
{
    switch (status)
    {
    case PaletteMappingStatus::Success:
        return "Palette mapping plan is valid.";
    case PaletteMappingStatus::NoChange:
        return "The empty stamp requires no palette mapping.";
    case PaletteMappingStatus::InvalidPaletteCapacity:
        return "Palette capacity must be between one and 256 entries.";
    case PaletteMappingStatus::InvalidReservedPaletteIndex:
        return "The reserved palette index must be within palette capacity.";
    case PaletteMappingStatus::InvalidDocumentPaletteSnapshot:
        return "A non-custom document palette must equal DefaultVoxPalette.";
    case PaletteMappingStatus::InvalidOccupiedPaletteIndex:
        return "Occupied palette indices must be in capacity and must not include the reserved index.";
    case PaletteMappingStatus::InvalidStampPalette:
        return "Stamp palette IDs must be canonical and contiguous from zero.";
    case PaletteMappingStatus::InvalidStampVoxelSet:
        return "A non-empty stamp palette requires at least one voxel.";
    case PaletteMappingStatus::InvalidStampVoxelReference:
        return "Every stamp voxel must reference an existing palette entry, and every palette entry must be referenced.";
    case PaletteMappingStatus::InvalidRequiredLocalColorId:
        return "Every required local color ID must reference an existing Stamp palette entry.";
    case PaletteMappingStatus::PaletteCapacityExceeded:
        return "No non-reserved unoccupied document palette index is available.";
    case PaletteMappingStatus::AllocationFailure:
        return "Palette mapping plan allocation failed.";
    }

    return "Unknown palette mapping status.";
}

struct PaletteMappingEntry final
{
    std::uint8_t LocalColorId = 0U;
    std::uint8_t DocumentPaletteIndex = 0U;

    [[nodiscard]] bool operator==(const PaletteMappingEntry&) const noexcept = default;
};

struct PlannedPaletteColor final
{
    std::uint8_t DocumentPaletteIndex = 0U;
    StampColor Color{};

    [[nodiscard]] bool operator==(const PlannedPaletteColor&) const noexcept = default;
};

/// Immutable request data for deterministic palette planning.  OccupiedDocumentPaletteIndices
/// is deliberately explicit: a stored color in an unoccupied slot can be reused without a
/// palette write, whereas an occupied slot represents a document color in active use.
struct PaletteMappingRequest final
{
    std::span<const StampPaletteEntry> StampPalette;
    std::span<const StampVoxel> StampVoxels;
    Asset::Voxel::VoxelDocumentPaletteSnapshot DocumentPalette;
    std::array<bool, 256U> OccupiedDocumentPaletteIndices{};
    std::size_t PaletteCapacity = 256U;
    std::uint8_t ReservedDocumentPaletteIndex = 0U;
    // Nullopt maps every Stamp color. An engaged span maps only the selected
    // local IDs; an engaged empty span intentionally produces no mapping.
    std::optional<std::span<const std::uint8_t>> RequiredLocalColorIds;
};

/// Pure planning output. No document, history, renderer, transaction, or Undo/Redo object is
/// referenced or mutated.  Entries are always emitted in canonical LocalColorId order.
struct PaletteMappingPlan final
{
    std::vector<PaletteMappingEntry> LocalToDocument;
    std::vector<PlannedPaletteColor> ReusedColors;
    std::vector<PlannedPaletteColor> AddedColors;
    Asset::Voxel::VoxelDocumentPaletteSnapshot FinalDocumentPalette;
    std::size_t AddedColorCount = 0U;
    std::size_t ReusedColorCount = 0U;

    [[nodiscard]] bool HasPaletteChanges() const noexcept
    {
        return AddedColorCount != 0U;
    }

    [[nodiscard]] bool operator==(const PaletteMappingPlan&) const noexcept = default;
};

struct PaletteMappingResult final
{
    PaletteMappingStatus Status = PaletteMappingStatus::Success;
    PaletteMappingPlan Plan{};

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Status == PaletteMappingStatus::Success || Status == PaletteMappingStatus::NoChange;
    }

    [[nodiscard]] bool operator==(const PaletteMappingResult&) const noexcept = default;
};

/// Plans exact RGBA mappings from a Stamp palette to a document palette.
///
/// The algorithm is deterministic: a fixed document-color table is sorted by RGBA then index and
/// queried with lower_bound. It first reserves every exact document match, then allocates missing
/// unique Stamp colors in canonical LocalColorId order. It only matches exact RGBA values; it
/// intentionally performs no quantization, nearest-color lookup, blending, replacement, or
/// mutation. A missing exact color receives the smallest unoccupied non-reserved index, or
/// returns PaletteCapacityExceeded.
class PaletteMappingEngine final
{
public:
    [[nodiscard]] static PaletteMappingResult Plan(const PaletteMappingRequest& request) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
