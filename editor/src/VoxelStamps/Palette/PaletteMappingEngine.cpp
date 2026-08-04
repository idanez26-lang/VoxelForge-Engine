#include "VoxelStamps/Palette/PaletteMappingEngine.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <new>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{
[[nodiscard]] PaletteMappingResult MakeError(const PaletteMappingStatus status) noexcept
{
    return {.Status = status};
}

[[nodiscard]] bool IsValidDocumentPalette(
    const Asset::Voxel::VoxelDocumentPaletteSnapshot& snapshot) noexcept
{
    return snapshot.HasCustomPalette || snapshot.Colors == Asset::Vox::DefaultVoxPalette();
}

struct ColorIndexRecord final
{
    StampColor Color{};
    std::uint8_t Index = 0U;
};

struct StampColorMappingRecord final
{
    StampColor Color{};
    std::uint8_t FirstLocalColorId = 0U;
    std::uint8_t DocumentPaletteIndex = 0U;
    bool HasMapping = false;
    bool IsReused = false;
};

[[nodiscard]] constexpr bool IsColorLess(const StampColor& left, const StampColor& right) noexcept
{
    if (left.Red != right.Red) return left.Red < right.Red;
    if (left.Green != right.Green) return left.Green < right.Green;
    if (left.Blue != right.Blue) return left.Blue < right.Blue;
    return left.Alpha < right.Alpha;
}

[[nodiscard]] constexpr bool IsRecordLess(const ColorIndexRecord& left,
                                          const ColorIndexRecord& right) noexcept
{
    return IsColorLess(left.Color, right.Color) ||
           (!IsColorLess(right.Color, left.Color) && left.Index < right.Index);
}

template <std::size_t Size>
[[nodiscard]] const ColorIndexRecord* FindColor(const std::array<ColorIndexRecord, Size>& records,
                                                const std::size_t count,
                                                const StampColor color) noexcept
{
    const auto first = records.begin();
    const auto end = first + static_cast<std::ptrdiff_t>(count);
    const auto found = std::lower_bound(first, end, color,
                                        [](const ColorIndexRecord& record, const StampColor value)
                                        { return IsColorLess(record.Color, value); });
    return found != end && found->Color == color ? &*found : nullptr;
}

[[nodiscard]] constexpr bool IsStampMappingLess(const StampColorMappingRecord& left,
                                                const StampColorMappingRecord& right) noexcept
{
    return IsColorLess(left.Color, right.Color) ||
           (!IsColorLess(right.Color, left.Color) &&
            left.FirstLocalColorId < right.FirstLocalColorId);
}

template <std::size_t Size>
[[nodiscard]] StampColorMappingRecord* FindStampMapping(
    std::array<StampColorMappingRecord, Size>& records, const std::size_t count,
    const StampColor color) noexcept
{
    const auto first = records.begin();
    const auto end = first + static_cast<std::ptrdiff_t>(count);
    const auto found = std::lower_bound(
        first, end, color, [](const StampColorMappingRecord& record, const StampColor value)
        { return IsColorLess(record.Color, value); });
    return found != end && found->Color == color ? &*found : nullptr;
}

}  // namespace

PaletteMappingResult PaletteMappingEngine::Plan(const PaletteMappingRequest& request) noexcept
{
    try
    {
        if (request.PaletteCapacity == 0U || request.PaletteCapacity > 256U)
        {
            return MakeError(PaletteMappingStatus::InvalidPaletteCapacity);
        }
        if (request.ReservedDocumentPaletteIndex >= request.PaletteCapacity)
        {
            return MakeError(PaletteMappingStatus::InvalidReservedPaletteIndex);
        }
        if (!IsValidDocumentPalette(request.DocumentPalette))
        {
            return MakeError(PaletteMappingStatus::InvalidDocumentPaletteSnapshot);
        }

        for (std::size_t index = 0U; index < request.OccupiedDocumentPaletteIndices.size(); ++index)
        {
            if (request.OccupiedDocumentPaletteIndices[index] &&
                (index >= request.PaletteCapacity || index == request.ReservedDocumentPaletteIndex))
            {
                return MakeError(PaletteMappingStatus::InvalidOccupiedPaletteIndex);
            }
        }

        if (request.StampPalette.empty())
        {
            return request.StampVoxels.empty()
                       ? PaletteMappingResult{.Status = PaletteMappingStatus::NoChange,
                                              .Plan = {.FinalDocumentPalette =
                                                           request.DocumentPalette}}
                       : MakeError(PaletteMappingStatus::InvalidStampPalette);
        }
        if (request.StampVoxels.empty())
        {
            return MakeError(PaletteMappingStatus::InvalidStampVoxelSet);
        }
        if (request.StampPalette.size() > 256U)
        {
            return MakeError(PaletteMappingStatus::InvalidStampPalette);
        }
        for (std::size_t localId = 0U; localId < request.StampPalette.size(); ++localId)
        {
            if (request.StampPalette[localId].LocalColorId != localId)
            {
                return MakeError(PaletteMappingStatus::InvalidStampPalette);
            }
        }
        std::array<bool, 256U> referencedLocalColorIds{};
        for (const StampVoxel& voxel : request.StampVoxels)
        {
            if (voxel.LocalColorId >= request.StampPalette.size())
            {
                return MakeError(PaletteMappingStatus::InvalidStampVoxelReference);
            }
            referencedLocalColorIds[voxel.LocalColorId] = true;
        }
        for (std::size_t localId = 0U; localId < request.StampPalette.size(); ++localId)
        {
            if (!referencedLocalColorIds[localId])
            {
                return MakeError(PaletteMappingStatus::InvalidStampVoxelReference);
            }
        }

        std::array<bool, 256U> requiredLocalColorIds{};
        if (request.RequiredLocalColorIds)
        {
            for (const std::uint8_t localColorId : *request.RequiredLocalColorIds)
            {
                if (localColorId >= request.StampPalette.size())
                {
                    return MakeError(
                        PaletteMappingStatus::InvalidRequiredLocalColorId);
                }
                requiredLocalColorIds[localColorId] = true;
            }
        }
        else
        {
            std::fill_n(requiredLocalColorIds.begin(),
                        request.StampPalette.size(), true);
        }

        if (std::none_of(requiredLocalColorIds.begin(),
                         requiredLocalColorIds.end(),
                         [](const bool required) { return required; }))
        {
            return {.Status = PaletteMappingStatus::NoChange,
                    .Plan = {.FinalDocumentPalette =
                                 request.DocumentPalette}};
        }

        std::array<ColorIndexRecord, 255U> documentColorIndex{};
        std::size_t documentColorCount = 0U;
        for (std::size_t index = 0U; index < request.PaletteCapacity; ++index)
        {
            if (index != request.ReservedDocumentPaletteIndex)
            {
                documentColorIndex[documentColorCount++] = {
                    .Color = request.DocumentPalette.Colors[index],
                    .Index = static_cast<std::uint8_t>(index)};
            }
        }
        std::sort(documentColorIndex.begin(),
                  documentColorIndex.begin() + static_cast<std::ptrdiff_t>(documentColorCount),
                  IsRecordLess);

        std::array<StampColorMappingRecord, 256U> stampColorMappings{};
        std::size_t stampColorMappingCount = 0U;
        for (std::size_t localId = 0U;
             localId < request.StampPalette.size(); ++localId)
        {
            if (!requiredLocalColorIds[localId])
            {
                continue;
            }
            stampColorMappings[stampColorMappingCount++] = {
                .Color = request.StampPalette[localId].Color,
                .FirstLocalColorId = static_cast<std::uint8_t>(localId)};
        }
        std::sort(stampColorMappings.begin(),
                  stampColorMappings.begin() + static_cast<std::ptrdiff_t>(stampColorMappingCount),
                  IsStampMappingLess);
        std::size_t uniqueStampColorCount = 0U;
        for (std::size_t index = 0U; index < stampColorMappingCount; ++index)
        {
            if (uniqueStampColorCount == 0U ||
                stampColorMappings[index].Color !=
                    stampColorMappings[uniqueStampColorCount - 1U].Color)
            {
                stampColorMappings[uniqueStampColorCount++] = stampColorMappings[index];
            }
        }
        stampColorMappingCount = uniqueStampColorCount;

        PaletteMappingPlan plan{.FinalDocumentPalette = request.DocumentPalette};
        std::array<bool, 256U> occupied = request.OccupiedDocumentPaletteIndices;

        // Phase one reserves every exact match against the original document palette. This
        // protects a later exact-match slot from an earlier new Stamp color allocation.
        for (std::size_t index = 0U; index < stampColorMappingCount; ++index)
        {
            StampColorMappingRecord& mapping = stampColorMappings[index];
            if (const ColorIndexRecord* exact =
                    FindColor(documentColorIndex, documentColorCount, mapping.Color);
                exact != nullptr)
            {
                const std::uint8_t documentIndex = exact->Index;
                occupied[documentIndex] = true;
                mapping.DocumentPaletteIndex = documentIndex;
                mapping.HasMapping = true;
                mapping.IsReused = true;
            }
        }

        std::array<std::uint8_t, 255U> freeDocumentIndices{};
        std::size_t freeDocumentIndexCount = 0U;
        for (std::size_t index = 0U; index < request.PaletteCapacity; ++index)
        {
            if (index != request.ReservedDocumentPaletteIndex && !occupied[index])
            {
                freeDocumentIndices[freeDocumentIndexCount++] = static_cast<std::uint8_t>(index);
            }
        }
        std::size_t nextFreeDocumentIndex = 0U;

        // Phase two allocates only colors that had no exact match, in canonical first-local-ID
        // order.
        for (const StampPaletteEntry& stampEntry : request.StampPalette)
        {
            if (!requiredLocalColorIds[stampEntry.LocalColorId])
            {
                continue;
            }
            StampColorMappingRecord* mapping =
                FindStampMapping(stampColorMappings, stampColorMappingCount, stampEntry.Color);
            if (mapping == nullptr || mapping->FirstLocalColorId != stampEntry.LocalColorId ||
                mapping->HasMapping)
            {
                continue;
            }
            if (nextFreeDocumentIndex == freeDocumentIndexCount)
            {
                return MakeError(PaletteMappingStatus::PaletteCapacityExceeded);
            }

            const std::uint8_t documentIndex = freeDocumentIndices[nextFreeDocumentIndex++];
            plan.FinalDocumentPalette.Colors[documentIndex] = stampEntry.Color;
            plan.FinalDocumentPalette.HasCustomPalette = true;
            mapping->DocumentPaletteIndex = documentIndex;
            mapping->HasMapping = true;
        }

        plan.LocalToDocument.reserve(request.StampPalette.size());
        plan.ReusedColors.reserve(request.StampPalette.size());
        plan.AddedColors.reserve(request.StampPalette.size());
        for (const StampPaletteEntry& stampEntry : request.StampPalette)
        {
            if (!requiredLocalColorIds[stampEntry.LocalColorId])
            {
                continue;
            }
            const StampColorMappingRecord* mapping =
                FindStampMapping(stampColorMappings, stampColorMappingCount, stampEntry.Color);
            plan.LocalToDocument.push_back({.LocalColorId = stampEntry.LocalColorId,
                                            .DocumentPaletteIndex = mapping->DocumentPaletteIndex});
            if (mapping->FirstLocalColorId == stampEntry.LocalColorId)
            {
                if (mapping->IsReused)
                {
                    plan.ReusedColors.push_back(
                        {.DocumentPaletteIndex = mapping->DocumentPaletteIndex,
                         .Color = stampEntry.Color});
                    ++plan.ReusedColorCount;
                }
                else
                {
                    plan.AddedColors.push_back(
                        {.DocumentPaletteIndex = mapping->DocumentPaletteIndex,
                         .Color = stampEntry.Color});
                    ++plan.AddedColorCount;
                }
            }
        }

        return {.Status = PaletteMappingStatus::Success, .Plan = std::move(plan)};
    }
    catch (const std::bad_alloc&)
    {
        return MakeError(PaletteMappingStatus::AllocationFailure);
    }
}

}  // namespace VoxelForge::Editor::Stamps
