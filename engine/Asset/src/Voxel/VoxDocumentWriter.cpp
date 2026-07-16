#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace VoxelForge::Asset::Voxel
{
namespace
{
using Bytes = std::vector<std::uint8_t>;

struct SerializableVoxel final
{
    VoxelPosition Position{};
    std::uint8_t PaletteIndex = 0U;
};

VoxDocumentWriteResult Failure(
    const VoxDocumentWriteError error,
    std::string message)
{
    return {error, std::move(message), {}};
}

bool AddFits(const std::size_t left, const std::size_t right) noexcept
{
    return right <= std::numeric_limits<std::size_t>::max() - left;
}

bool AppendU32(Bytes& destination, const std::uint32_t value)
{
    if (!AddFits(destination.size(), 4U)) return false;
    destination.push_back(static_cast<std::uint8_t>(value & 0xffU));
    destination.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    destination.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    destination.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    return true;
}

bool AppendId(Bytes& destination, const std::string_view id)
{
    if (id.size() != 4U || !AddFits(destination.size(), id.size()))
        return false;
    destination.insert(destination.end(), id.begin(), id.end());
    return true;
}

bool AppendChunk(
    Bytes& destination,
    const std::string_view id,
    const Bytes& content,
    const Bytes& children = {})
{
    if (content.size() > std::numeric_limits<std::uint32_t>::max() ||
        children.size() > std::numeric_limits<std::uint32_t>::max() ||
        !AddFits(destination.size(), 12U) ||
        !AddFits(destination.size() + 12U, content.size()) ||
        !AddFits(destination.size() + 12U + content.size(), children.size()))
        return false;
    return AppendId(destination, id) &&
        AppendU32(destination, static_cast<std::uint32_t>(content.size())) &&
        AppendU32(destination, static_cast<std::uint32_t>(children.size())) &&
        (destination.insert(destination.end(), content.begin(), content.end()), true) &&
        (destination.insert(destination.end(), children.begin(), children.end()), true);
}

bool VoxelsEqual(
    const VoxelSubModel& expected,
    const VoxelSubModel& actual)
{
    if (expected.VoxelCount() != actual.VoxelCount()) return false;
    bool equal = true;
    expected.ForEachVoxel(
        [&actual, &equal](const VoxelPosition& position, const Voxel& voxel)
        {
            if (!equal) return;
            const std::optional<Voxel> other = actual.GetVoxel(position);
            equal = other.has_value() && *other == voxel;
        });
    return equal;
}
}

VoxDocumentWriteResult VoxDocumentWriter::Serialize(
    const VoxelDocument& document) const
{
    const std::uint32_t version = document.VoxVersion() == 0U
        ? DefaultVoxVersion : document.VoxVersion();
    if (version != DefaultVoxVersion)
        return Failure(VoxDocumentWriteError::UnsupportedVersion,
            "VOX writer supports format version 150 only.");
    const std::size_t modelCount = document.GetModelCount();
    if (modelCount == 0U)
        return Failure(VoxDocumentWriteError::NoModels,
            "VOX document must contain at least one sub-model.");
    if (modelCount > Vox::MaximumVoxModelCount ||
        modelCount > std::numeric_limits<std::uint32_t>::max())
        return Failure(VoxDocumentWriteError::TooManyModels,
            "VOX document exceeds the supported sub-model count.");

    Bytes children;
    if (modelCount > 1U)
    {
        Bytes pack;
        if (!AppendU32(pack, static_cast<std::uint32_t>(modelCount)) ||
            !AppendChunk(children, "PACK", pack))
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "PACK chunk size overflowed.");
    }

    std::uint64_t totalVoxelCount = 0U;
    for (std::size_t modelIndex = 0U; modelIndex < modelCount; ++modelIndex)
    {
        const VoxelSubModel* model = document.GetModel(modelIndex);
        if (model == nullptr)
            return Failure(VoxDocumentWriteError::IncoherentDocument,
                "VOX document contains an inaccessible sub-model.");
        const VoxelDimensions dimensions = model->Dimensions();
        if (dimensions.X == 0U || dimensions.Y == 0U || dimensions.Z == 0U ||
            dimensions.X > MaximumXyziCoordinate + 1U ||
            dimensions.Y > MaximumXyziCoordinate + 1U ||
            dimensions.Z > MaximumXyziCoordinate + 1U)
            return Failure(VoxDocumentWriteError::InvalidDimensions,
                "VOX XYZI dimensions must be between 1 and 256 per axis.");
        if (model->VoxelCount() > Vox::MaximumVoxVoxelCount ||
            model->VoxelCount() > std::numeric_limits<std::uint32_t>::max() ||
            totalVoxelCount > Vox::MaximumVoxVoxelCount - model->VoxelCount())
            return Failure(VoxDocumentWriteError::TooManyVoxels,
                "VOX document exceeds the supported voxel count.");

        std::vector<SerializableVoxel> voxels;
        voxels.reserve(model->VoxelCount());
        bool invalidCoordinate = false;
        bool invalidPalette = false;
        model->ForEachVoxel(
            [&](const VoxelPosition& position, const Voxel& voxel)
            {
                if (position.X < 0 || position.Y < 0 || position.Z < 0 ||
                    position.X > static_cast<std::int32_t>(MaximumXyziCoordinate) ||
                    position.Y > static_cast<std::int32_t>(MaximumXyziCoordinate) ||
                    position.Z > static_cast<std::int32_t>(MaximumXyziCoordinate) ||
                    static_cast<std::uint32_t>(position.X) >= dimensions.X ||
                    static_cast<std::uint32_t>(position.Y) >= dimensions.Y ||
                    static_cast<std::uint32_t>(position.Z) >= dimensions.Z)
                    invalidCoordinate = true;
                if (voxel.PaletteIndex == 0U) invalidPalette = true;
                voxels.push_back({position, voxel.PaletteIndex});
            });
        if (invalidCoordinate)
            return Failure(VoxDocumentWriteError::CoordinateOutOfRange,
                "VOX XYZI coordinates must be within 0..255 and model bounds.");
        if (invalidPalette)
            return Failure(VoxDocumentWriteError::InvalidPaletteIndex,
                "VOX palette index 0 is reserved for empty space.");
        std::sort(voxels.begin(), voxels.end(),
            [](const SerializableVoxel& left, const SerializableVoxel& right)
            {
                if (left.Position.X != right.Position.X)
                    return left.Position.X < right.Position.X;
                if (left.Position.Y != right.Position.Y)
                    return left.Position.Y < right.Position.Y;
                if (left.Position.Z != right.Position.Z)
                    return left.Position.Z < right.Position.Z;
                return left.PaletteIndex < right.PaletteIndex;
            });

        Bytes sizeContent;
        if (!AppendU32(sizeContent, dimensions.X) ||
            !AppendU32(sizeContent, dimensions.Y) ||
            !AppendU32(sizeContent, dimensions.Z) ||
            !AppendChunk(children, "SIZE", sizeContent))
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "SIZE chunk overflowed.");

        const std::uint64_t xyziSize = 4ULL + voxels.size() * 4ULL;
        if (xyziSize > std::numeric_limits<std::uint32_t>::max())
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "XYZI chunk size overflowed.");
        Bytes xyzi;
        xyzi.reserve(static_cast<std::size_t>(xyziSize));
        if (!AppendU32(xyzi, static_cast<std::uint32_t>(voxels.size())))
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "XYZI voxel count overflowed.");
        for (const SerializableVoxel& voxel : voxels)
        {
            xyzi.push_back(static_cast<std::uint8_t>(voxel.Position.X));
            xyzi.push_back(static_cast<std::uint8_t>(voxel.Position.Y));
            xyzi.push_back(static_cast<std::uint8_t>(voxel.Position.Z));
            xyzi.push_back(voxel.PaletteIndex);
        }
        if (!AppendChunk(children, "XYZI", xyzi))
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "XYZI chunk overflowed.");
        totalVoxelCount += voxels.size();
    }

    if (totalVoxelCount != document.GetVoxelCount())
        return Failure(VoxDocumentWriteError::IncoherentDocument,
            "VOX document voxel count is inconsistent with its sub-models.");

    if (document.HasCustomPalette())
    {
        Bytes rgba;
        rgba.reserve(1024U);
        const auto& palette = document.GetPalette();
        for (std::size_t index = 1U; index < palette.size(); ++index)
        {
            const VoxelColor color = palette[index];
            rgba.insert(rgba.end(),
                {color.Red, color.Green, color.Blue, color.Alpha});
        }
        const VoxelColor unused = palette[0U];
        rgba.insert(rgba.end(),
            {unused.Red, unused.Green, unused.Blue, unused.Alpha});
        if (!AppendChunk(children, "RGBA", rgba))
            return Failure(VoxDocumentWriteError::IntegerOverflow,
                "RGBA chunk overflowed.");
    }

    Bytes bytes;
    bytes.reserve(20U + children.size());
    if (!AppendId(bytes, "VOX ") || !AppendU32(bytes, version) ||
        !AppendChunk(bytes, "MAIN", {}, children))
        return Failure(VoxDocumentWriteError::IntegerOverflow,
            "VOX file size overflowed.");
    if (bytes.size() > Vox::MaximumVoxFileSize)
        return Failure(VoxDocumentWriteError::FileTooLarge,
            "Serialized VOX exceeds the supported file size limit.");
    return {VoxDocumentWriteError::None, "VOX document serialized.",
        std::move(bytes)};
}

bool AreVoxelDocumentsEquivalent(
    const VoxelDocument& expected,
    const VoxelDocument& actual,
    std::string& difference)
{
    difference.clear();
    const std::uint32_t expectedVersion = expected.VoxVersion() == 0U
        ? VoxDocumentWriter::DefaultVoxVersion : expected.VoxVersion();
    if (expectedVersion != actual.VoxVersion())
        difference = "VOX versions differ.";
    else if (expected.GetModelCount() != actual.GetModelCount())
        difference = "Sub-model counts differ.";
    else if (expected.GetVoxelCount() != actual.GetVoxelCount())
        difference = "Total voxel counts differ.";
    else if (expected.GetPalette() != actual.GetPalette())
        difference = "VOX palettes differ.";
    else
    {
        for (std::size_t index = 0U; index < expected.GetModelCount(); ++index)
        {
            const VoxelSubModel* left = expected.GetModel(index);
            const VoxelSubModel* right = actual.GetModel(index);
            if (left == nullptr || right == nullptr)
            {
                difference = "A sub-model is inaccessible.";
                break;
            }
            if (left->Dimensions() != right->Dimensions())
            {
                difference = "Sub-model dimensions differ at index " +
                    std::to_string(index) + ".";
                break;
            }
            if (left->Bounds() != right->Bounds())
            {
                difference = "Sub-model bounds differ at index " +
                    std::to_string(index) + ".";
                break;
            }
            if (!VoxelsEqual(*left, *right))
            {
                difference = "Sub-model voxels differ at index " +
                    std::to_string(index) + ".";
                break;
            }
        }
    }
    return difference.empty();
}

const char* VoxDocumentWriteErrorName(
    const VoxDocumentWriteError error) noexcept
{
    switch (error)
    {
    case VoxDocumentWriteError::None: return "None";
    case VoxDocumentWriteError::UnsupportedVersion: return "Unsupported version";
    case VoxDocumentWriteError::NoModels: return "No models";
    case VoxDocumentWriteError::TooManyModels: return "Too many models";
    case VoxDocumentWriteError::InvalidDimensions: return "Invalid dimensions";
    case VoxDocumentWriteError::CoordinateOutOfRange: return "Coordinate out of range";
    case VoxDocumentWriteError::InvalidPaletteIndex: return "Invalid palette index";
    case VoxDocumentWriteError::TooManyVoxels: return "Too many voxels";
    case VoxDocumentWriteError::IntegerOverflow: return "Integer overflow";
    case VoxDocumentWriteError::FileTooLarge: return "File too large";
    case VoxDocumentWriteError::IncoherentDocument: return "Incoherent document";
    }
    return "Incoherent document";
}

} // namespace VoxelForge::Asset::Voxel
