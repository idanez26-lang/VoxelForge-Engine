#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoxelForge::Asset::Vox
{

namespace
{
using ByteBuffer = std::vector<std::uint8_t>;

struct ChunkHeader final
{
    std::array<char, 4> Id{};
    std::uint32_t ContentSize = 0;
    std::uint32_t ChildrenSize = 0;
    std::size_t ContentBegin = 0;
    std::size_t ContentEnd = 0;
    std::size_t ChildrenBegin = 0;
    std::size_t ChildrenEnd = 0;
};

struct ParseState final
{
    VoxModel Model;
    AssetImportResult Result;
    std::optional<VoxDimensions> PendingDimensions;
    std::uint64_t TotalVoxelCount = 0;
    std::array<bool, 7> WarnedAdvancedSceneChunks{};
    bool PackSeen = false;
    bool PaletteSeen = false;
};

AssetImportOutcome<VoxModel> Failure(
    const AssetImportError error,
    std::string message)
{
    return {
        {false, error, std::move(message), {}},
        std::nullopt};
}

bool Fail(
    ParseState& state,
    const AssetImportError error,
    std::string message)
{
    state.Result.Succeeded = false;
    state.Result.Error = error;
    state.Result.Message = std::move(message);
    return false;
}

bool ReadU32(
    const ByteBuffer& bytes,
    const std::size_t offset,
    const std::size_t end,
    std::uint32_t& value)
{
    if (offset > end || end - offset < 4U || end > bytes.size())
    {
        return false;
    }

    value = static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    return true;
}

bool IsChunk(const ChunkHeader& chunk, const std::string_view id)
{
    return id.size() == chunk.Id.size() &&
        std::equal(chunk.Id.begin(), chunk.Id.end(), id.begin());
}

std::string ChunkName(const ChunkHeader& chunk)
{
    return std::string(chunk.Id.begin(), chunk.Id.end());
}

bool ReadChunkHeader(
    ParseState& state,
    const ByteBuffer& bytes,
    const std::size_t offset,
    const std::size_t rangeEnd,
    ChunkHeader& chunk)
{
    if (offset > rangeEnd || rangeEnd - offset < 12U)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "Truncated VOX chunk header.");
    }

    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        chunk.Id.size(),
        chunk.Id.begin());

    if (!ReadU32(bytes, offset + 4U, rangeEnd, chunk.ContentSize) ||
        !ReadU32(bytes, offset + 8U, rangeEnd, chunk.ChildrenSize))
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "Unable to read VOX chunk sizes.");
    }

    chunk.ContentBegin = offset + 12U;

    if (chunk.ContentSize > rangeEnd - chunk.ContentBegin)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "Chunk " + ChunkName(chunk) +
                " content exceeds its parent bounds.");
    }

    chunk.ContentEnd = chunk.ContentBegin + chunk.ContentSize;
    chunk.ChildrenBegin = chunk.ContentEnd;

    if (chunk.ChildrenSize > rangeEnd - chunk.ChildrenBegin)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "Chunk " + ChunkName(chunk) +
                " children exceed their parent bounds.");
    }

    chunk.ChildrenEnd = chunk.ChildrenBegin + chunk.ChildrenSize;
    return true;
}

bool RequireLeafChunk(ParseState& state, const ChunkHeader& chunk)
{
    if (chunk.ChildrenSize == 0U)
    {
        return true;
    }

    return Fail(
        state,
        AssetImportError::InvalidChunk,
        "Chunk " + ChunkName(chunk) + " must not contain child chunks.");
}

bool ReadDimensions(
    ParseState& state,
    const ByteBuffer& bytes,
    const ChunkHeader& chunk)
{
    if (!RequireLeafChunk(state, chunk))
    {
        return false;
    }

    if (chunk.ContentSize != 12U)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "SIZE chunk must contain exactly 12 bytes.");
    }

    if (state.PendingDimensions)
    {
        return Fail(
            state,
            AssetImportError::MissingSizeChunk,
            "A SIZE chunk was not followed by its XYZI chunk.");
    }

    VoxDimensions dimensions;
    static_cast<void>(ReadU32(
        bytes, chunk.ContentBegin, chunk.ContentEnd, dimensions.X));
    static_cast<void>(ReadU32(
        bytes, chunk.ContentBegin + 4U, chunk.ContentEnd, dimensions.Y));
    static_cast<void>(ReadU32(
        bytes, chunk.ContentBegin + 8U, chunk.ContentEnd, dimensions.Z));

    if (dimensions.X == 0U || dimensions.Y == 0U || dimensions.Z == 0U)
    {
        return Fail(
            state,
            AssetImportError::InvalidDimensions,
            "VOX model dimensions must be greater than zero.");
    }

    if (dimensions.X > MaximumVoxDimension ||
        dimensions.Y > MaximumVoxDimension ||
        dimensions.Z > MaximumVoxDimension)
    {
        return Fail(
            state,
            AssetImportError::InvalidDimensions,
            "VOX model dimensions exceed the v1 limit of " +
                std::to_string(MaximumVoxDimension) + " per axis.");
    }

    state.PendingDimensions = dimensions;
    return true;
}

bool ReadVoxels(
    ParseState& state,
    const ByteBuffer& bytes,
    const ChunkHeader& chunk)
{
    if (!RequireLeafChunk(state, chunk))
    {
        return false;
    }

    if (!state.PendingDimensions)
    {
        return Fail(
            state,
            AssetImportError::MissingSizeChunk,
            "XYZI chunk encountered without a preceding SIZE chunk.");
    }

    if (chunk.ContentSize < 4U)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "XYZI chunk is truncated before its voxel count.");
    }

    std::uint32_t voxelCount = 0;
    static_cast<void>(ReadU32(
        bytes, chunk.ContentBegin, chunk.ContentEnd, voxelCount));

    if (voxelCount > MaximumVoxVoxelCount ||
        state.TotalVoxelCount > MaximumVoxVoxelCount - voxelCount)
    {
        return Fail(
            state,
            AssetImportError::TooManyVoxels,
            "VOX voxel count exceeds the v1 limit of " +
                std::to_string(MaximumVoxVoxelCount) + ".");
    }

    const std::uint64_t requiredSize =
        4ULL + static_cast<std::uint64_t>(voxelCount) * 4ULL;

    if (requiredSize > chunk.ContentSize)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "XYZI chunk declares more voxels than its data contains.");
    }

    if (requiredSize != chunk.ContentSize)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "XYZI chunk contains unexpected trailing data.");
    }

    const VoxDimensions dimensions = *state.PendingDimensions;
    std::size_t voxelOffset = chunk.ContentBegin + 4U;

    for (std::uint32_t index = 0; index < voxelCount; ++index)
    {
        const std::uint8_t x = bytes[voxelOffset];
        const std::uint8_t y = bytes[voxelOffset + 1U];
        const std::uint8_t z = bytes[voxelOffset + 2U];
        const std::uint8_t colorIndex = bytes[voxelOffset + 3U];

        if (x >= dimensions.X || y >= dimensions.Y || z >= dimensions.Z)
        {
            return Fail(
                state,
                AssetImportError::InvalidVoxel,
                "Voxel coordinates lie outside the preceding SIZE bounds.");
        }

        // MagicaVoxel reserves palette index 0 for empty space. XYZI voxels
        // must reference one of the usable palette entries 1 through 255.
        if (colorIndex == 0U)
        {
            return Fail(
                state,
                AssetImportError::InvalidVoxel,
                "XYZI contains reserved color index 0.");
        }

        voxelOffset += 4U;
    }

    state.TotalVoxelCount += voxelCount;
    state.Model.Models.push_back({dimensions, voxelCount});
    state.PendingDimensions.reset();

    if (state.Model.Models.size() > MaximumVoxModelCount)
    {
        return Fail(
            state,
            AssetImportError::TooManyModels,
            "VOX model count exceeds the v1 limit of " +
                std::to_string(MaximumVoxModelCount) + ".");
    }

    return true;
}

bool ReadPack(
    ParseState& state,
    const ByteBuffer& bytes,
    const ChunkHeader& chunk)
{
    if (!RequireLeafChunk(state, chunk))
    {
        return false;
    }

    if (state.PackSeen || chunk.ContentSize != 4U)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            state.PackSeen
                ? "Multiple PACK chunks are not supported."
                : "PACK chunk must contain exactly 4 bytes.");
    }

    if (!state.Model.Models.empty() || state.PendingDimensions)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "PACK chunk must appear before all SIZE and XYZI chunks.");
    }

    std::uint32_t modelCount = 0;
    static_cast<void>(ReadU32(
        bytes, chunk.ContentBegin, chunk.ContentEnd, modelCount));

    if (modelCount == 0U || modelCount > MaximumVoxModelCount)
    {
        return Fail(
            state,
            AssetImportError::TooManyModels,
            "PACK model count must be between 1 and " +
                std::to_string(MaximumVoxModelCount) + ".");
    }

    state.PackSeen = true;
    state.Model.HasPackChunk = true;
    state.Model.DeclaredModelCount = modelCount;
    return true;
}

bool ReadPalette(
    ParseState& state,
    const ByteBuffer& bytes,
    const ChunkHeader& chunk)
{
    if (!RequireLeafChunk(state, chunk))
    {
        return false;
    }

    if (state.PaletteSeen || chunk.ContentSize != 1024U)
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            state.PaletteSeen
                ? "Multiple RGBA chunks are not supported."
                : "RGBA chunk must contain exactly 1024 bytes.");
    }

    state.Model.Palette = {};

    // The file stores colors 0..254 for voxel indices 1..255. The final
    // stored color is unused by the VOX specification, and palette index 0
    // remains transparent/reserved for empty space.
    for (std::size_t paletteIndex = 1U;
         paletteIndex < VoxPaletteSize;
         ++paletteIndex)
    {
        const std::size_t source =
            chunk.ContentBegin + (paletteIndex - 1U) * 4U;
        state.Model.Palette[paletteIndex] = {
            bytes[source],
            bytes[source + 1U],
            bytes[source + 2U],
            bytes[source + 3U]};
    }

    state.PaletteSeen = true;
    state.Model.HasCustomPalette = true;
    return true;
}

std::optional<std::size_t> AdvancedSceneChunkIndex(
    const ChunkHeader& chunk)
{
    constexpr std::array<std::string_view, 7> advancedChunks = {
        "nTRN", "nGRP", "nSHP", "LAYR", "MATL", "rOBJ", "rCAM"};

    for (std::size_t index = 0; index < advancedChunks.size(); ++index)
    {
        if (IsChunk(chunk, advancedChunks[index]))
        {
            return index;
        }
    }

    return std::nullopt;
}

bool ProcessChunk(
    ParseState& state,
    const ByteBuffer& bytes,
    const ChunkHeader& chunk)
{
    if (IsChunk(chunk, "PACK"))
    {
        return ReadPack(state, bytes, chunk);
    }

    if (IsChunk(chunk, "SIZE"))
    {
        return ReadDimensions(state, bytes, chunk);
    }

    if (IsChunk(chunk, "XYZI"))
    {
        return ReadVoxels(state, bytes, chunk);
    }

    if (IsChunk(chunk, "RGBA"))
    {
        return ReadPalette(state, bytes, chunk);
    }

    if (IsChunk(chunk, "MAIN"))
    {
        return Fail(
            state,
            AssetImportError::InvalidChunk,
            "Unexpected nested MAIN chunk.");
    }

    ++state.Model.IgnoredChunkCount;

    const std::optional<std::size_t> advancedIndex =
        AdvancedSceneChunkIndex(chunk);

    if (advancedIndex &&
        !state.WarnedAdvancedSceneChunks[*advancedIndex])
    {
        state.WarnedAdvancedSceneChunks[*advancedIndex] = true;
        state.Result.Warnings.push_back(
            "Advanced scene chunk " + ChunkName(chunk) +
            " was ignored; scene graphs are not supported in v1.");
    }

    return true;
}

bool ParseChunkRange(
    ParseState& state,
    const ByteBuffer& bytes,
    const std::size_t begin,
    const std::size_t end,
    const bool processRecognizedChunks,
    const std::uint32_t depth)
{
    std::size_t offset = begin;

    while (offset < end)
    {
        if (state.Model.ParsedChunkCount >= MaximumVoxChunkCount)
        {
            return Fail(
                state,
                AssetImportError::TooManyChunks,
                "VOX chunk count exceeds the v1 limit of " +
                    std::to_string(MaximumVoxChunkCount) + ".");
        }

        ChunkHeader chunk;

        if (!ReadChunkHeader(state, bytes, offset, end, chunk))
        {
            return false;
        }

        ++state.Model.ParsedChunkCount;

        if (processRecognizedChunks && !ProcessChunk(state, bytes, chunk))
        {
            return false;
        }
        else if (!processRecognizedChunks)
        {
            ++state.Model.IgnoredChunkCount;
        }

        if (chunk.ChildrenSize > 0U)
        {
            if (depth >= MaximumVoxChunkDepth)
            {
                return Fail(
                    state,
                    AssetImportError::InvalidChunk,
                    "VOX chunk nesting exceeds the v1 limit of " +
                        std::to_string(MaximumVoxChunkDepth) + ".");
            }

            if (!ParseChunkRange(
                    state,
                    bytes,
                    chunk.ChildrenBegin,
                    chunk.ChildrenEnd,
                    false,
                    depth + 1U))
            {
                return false;
            }
        }

        offset = chunk.ChildrenEnd;
    }

    return offset == end;
}
}

AssetImportOutcome<VoxModel> VoxImporter::Inspect(
    const std::filesystem::path& filePath) const
{
    std::error_code fileError;

    if (!std::filesystem::is_regular_file(filePath, fileError) || fileError)
    {
        return Failure(
            AssetImportError::FileNotFound,
            "VOX file does not exist or is not a regular file.");
    }

    const std::uintmax_t fileSize =
        std::filesystem::file_size(filePath, fileError);

    if (fileError)
    {
        return Failure(
            AssetImportError::ReadFailure,
            "Unable to determine the VOX file size: " +
                fileError.message());
    }

    if (fileSize < 20U)
    {
        return Failure(
            AssetImportError::FileTooSmall,
            "VOX file is too small to contain a header and MAIN chunk.");
    }

    if (fileSize > MaximumVoxFileSize)
    {
        return Failure(
            AssetImportError::FileTooLarge,
            "VOX file exceeds the v1 limit of " +
                std::to_string(MaximumVoxFileSize) + " bytes.");
    }

    ByteBuffer bytes(static_cast<std::size_t>(fileSize));
    std::ifstream stream(filePath, std::ios::binary);

    if (!stream || !stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))
    {
        return Failure(
            AssetImportError::ReadFailure,
            "Unable to read the complete VOX file.");
    }

    if (!std::equal(
            bytes.begin(),
            bytes.begin() + 4,
            std::array<std::uint8_t, 4>{'V', 'O', 'X', ' '}.begin()))
    {
        return Failure(
            AssetImportError::InvalidSignature,
            "Invalid VOX signature; expected 'VOX '.");
    }

    ParseState state;
    state.Model.Palette = DefaultVoxPalette();

    if (!ReadU32(bytes, 4U, bytes.size(), state.Model.Version))
    {
        return Failure(
            AssetImportError::InvalidHeader,
            "VOX version field is truncated.");
    }

    ChunkHeader mainChunk;

    if (!ReadChunkHeader(state, bytes, 8U, bytes.size(), mainChunk))
    {
        return {std::move(state.Result), std::nullopt};
    }

    if (!IsChunk(mainChunk, "MAIN"))
    {
        return Failure(
            AssetImportError::MissingMainChunk,
            "VOX header is not followed by a MAIN chunk.");
    }

    state.Model.ParsedChunkCount = 1U;

    if (mainChunk.ContentSize != 0U)
    {
        return Failure(
            AssetImportError::InvalidHeader,
            "MAIN chunk content size must be zero.");
    }

    if (mainChunk.ChildrenEnd != bytes.size())
    {
        return Failure(
            AssetImportError::InvalidHeader,
            "MAIN chunk size does not cover the complete VOX file.");
    }

    if (!ParseChunkRange(
            state,
            bytes,
            mainChunk.ChildrenBegin,
            mainChunk.ChildrenEnd,
            true,
            0U))
    {
        return {std::move(state.Result), std::nullopt};
    }

    if (state.PendingDimensions)
    {
        return Failure(
            AssetImportError::MissingSizeChunk,
            "Final SIZE chunk is not followed by an XYZI chunk.");
    }

    if (state.Model.Models.empty())
    {
        return Failure(
            AssetImportError::MissingSizeChunk,
            "VOX file contains no complete SIZE and XYZI model pair.");
    }

    if (state.PackSeen &&
        state.Model.DeclaredModelCount != state.Model.Models.size())
    {
        return Failure(
            AssetImportError::InconsistentModelCount,
            "PACK model count does not match the parsed SIZE/XYZI pairs.");
    }

    if (!state.PackSeen && state.Model.Models.size() != 1U)
    {
        return Failure(
            AssetImportError::InconsistentModelCount,
            "Multiple models require a matching PACK chunk.");
    }

    if (state.Model.Version != 150U)
    {
        state.Result.Warnings.push_back(
            "VOX version " + std::to_string(state.Model.Version) +
            " differs from the documented version 150.");
    }

    state.Result.Succeeded = true;
    state.Result.Error = AssetImportError::None;
    state.Result.Message = "VOX inspection succeeded.";
    return {std::move(state.Result), std::move(state.Model)};
}

} // namespace VoxelForge::Asset::Vox
