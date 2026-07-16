#pragma once

#include "VoxelDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VoxelForge::Asset::Voxel
{

enum class VoxDocumentWriteError
{
    None,
    UnsupportedVersion,
    NoModels,
    TooManyModels,
    InvalidDimensions,
    CoordinateOutOfRange,
    InvalidPaletteIndex,
    TooManyVoxels,
    IntegerOverflow,
    FileTooLarge,
    IncoherentDocument
};

struct VoxDocumentWriteResult final
{
    VoxDocumentWriteError Error = VoxDocumentWriteError::None;
    std::string Message;
    std::vector<std::uint8_t> Bytes;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == VoxDocumentWriteError::None;
    }
};

class VoxDocumentWriter final
{
public:
    static constexpr std::uint32_t DefaultVoxVersion = 150U;
    static constexpr std::uint32_t MaximumXyziCoordinate = 255U;

    [[nodiscard]] VoxDocumentWriteResult Serialize(
        const VoxelDocument& document) const;
};

[[nodiscard]] bool AreVoxelDocumentsEquivalent(
    const VoxelDocument& expected,
    const VoxelDocument& actual,
    std::string& difference);

[[nodiscard]] const char* VoxDocumentWriteErrorName(
    VoxDocumentWriteError error) noexcept;

} // namespace VoxelForge::Asset::Voxel
