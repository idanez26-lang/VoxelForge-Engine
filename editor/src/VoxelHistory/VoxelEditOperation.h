#pragma once

#include "Selection/SelectionService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Core/UUID.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

using VoxelChange = Asset::Voxel::VoxelDocumentChange;
using VoxelPaletteChange = Asset::Voxel::VoxelDocumentPaletteChange;

struct VoxelEditSelectionSnapshot final
{
    std::uint64_t DocumentGeneration = 0U;
    std::vector<Asset::Voxel::VoxelPosition> Voxels;
    SelectionBounds Bounds{};
};

struct VoxelEditSelectionTransition final
{
    VoxelEditSelectionSnapshot Before;
    VoxelEditSelectionSnapshot After;
};

/// Redo for a Stamp Variant consumes only the immutable voxel/palette facts
/// stored in this operation.  It never reloads a source asset or rerolls.
enum class VoxelEditStampReplayPolicy : std::uint8_t
{
    RestoreStoredOperation
};

struct VoxelEditStampVariantMetadata final
{
    Core::UUID GroupId{0U};
    std::uint64_t GroupRevision = 0U;
    Core::UUID VariantId{0U};
    Core::UUID StampId{0U};
    std::string ExpectedContentHash;
    std::uint64_t PlacementSessionSeed = 0U;
    std::uint64_t SelectionSeed = 0U;
    std::uint64_t PlacementOrdinal = 0U;
    VoxelEditStampReplayPolicy ReplayPolicy =
        VoxelEditStampReplayPolicy::RestoreStoredOperation;

    [[nodiscard]] bool operator==(
        const VoxelEditStampVariantMetadata&) const noexcept = default;
};

struct VoxelEditOperation final
{
    std::string Label;
    std::vector<VoxelChange> Changes;
    std::shared_ptr<const VoxelEditSelectionTransition> SelectionTransition;
    std::shared_ptr<const VoxelPaletteChange> PaletteChange;
    std::shared_ptr<const VoxelEditStampVariantMetadata> StampVariantMetadata;
};

[[nodiscard]] std::size_t EstimateVoxelEditOperationMemory(
    const VoxelEditOperation& operation) noexcept;

} // namespace VoxelForge::Editor
