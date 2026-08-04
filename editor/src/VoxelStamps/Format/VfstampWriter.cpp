#include "VoxelStamps/Format/VfstampWriter.h"
#include "VoxelStamps/Validation/StampValidationService.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;
constexpr std::size_t PaletteRecordSize = 7U;
constexpr std::size_t VoxelRecordSize = 13U;

[[nodiscard]] bool AddOverflow(const std::uint64_t left, const std::uint64_t right) noexcept
{
    return right > std::numeric_limits<std::uint64_t>::max() - left;
}
[[nodiscard]] bool MultiplyOverflow(const std::uint64_t left, const std::uint64_t right) noexcept
{
    return left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left;
}

template <typename T>
void AppendLe(std::vector<std::byte>& bytes, const T value)
{
    for (std::size_t index = 0U; index < sizeof(T); ++index)
    {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}
void AppendI32(std::vector<std::byte>& bytes, const std::int32_t value)
{
    AppendLe(bytes, static_cast<std::uint32_t>(value));
}
[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes, const std::size_t at) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[at])) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[at + 1U])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[at + 2U])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[at + 3U])) << 24U);
}
[[nodiscard]] std::uint64_t ReadU64(const std::span<const std::byte> bytes, const std::size_t at) noexcept
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[at + index])) << (index * 8U);
    return value;
}
[[nodiscard]] std::int32_t ReadI32(const std::span<const std::byte> bytes, const std::size_t at) noexcept
{
    return static_cast<std::int32_t>(ReadU32(bytes, at));
}

void HashByte(std::uint64_t& hash, const std::byte byte) noexcept
{
    hash ^= std::to_integer<unsigned char>(byte);
    hash *= FnvPrime;
}
template <typename T>
void HashLe(std::uint64_t& hash, const T value) noexcept
{
    for (std::size_t index = 0U; index < sizeof(T); ++index)
        HashByte(hash, static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
}
void HashI32(std::uint64_t& hash, const std::int32_t value) noexcept
{
    HashLe(hash, static_cast<std::uint32_t>(value));
}
[[nodiscard]] std::string Hex64(const std::uint64_t value)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string result(16U, '0');
    for (std::size_t index = 0U; index < result.size(); ++index)
        result[index] = Hex[(value >> ((15U - index) * 4U)) & 0xfU];
    return result;
}
[[nodiscard]] bool IsVoxelLess(const StampVoxel& left, const StampVoxel& right) noexcept
{
    if (left.Position.X != right.Position.X) return left.Position.X < right.Position.X;
    if (left.Position.Y != right.Position.Y) return left.Position.Y < right.Position.Y;
    if (left.Position.Z != right.Position.Z) return left.Position.Z < right.Position.Z;
    return left.LocalColorId < right.LocalColorId;
}
[[nodiscard]] std::vector<StampVoxel> SortedVoxels(const VoxelStamp& stamp)
{
    std::vector<StampVoxel> voxels(stamp.Voxels().begin(), stamp.Voxels().end());
    std::sort(voxels.begin(), voxels.end(), IsVoxelLess);
    return voxels;
}
[[nodiscard]] std::string MakeManifest(const VoxelStamp& stamp)
{
    const StampBounds& bounds = stamp.Bounds();
    const StampPivot& pivot = stamp.Pivot();
    // This fixed ASCII spelling is valid UTF-8 and is the complete V1 MANF grammar.
    return "{\"schema\":\"vfstamp-v1\",\"uuid\":\"" + Hex64(stamp.Identity().Id.Value()) +
           "\",\"dimensions\":[" + std::to_string(bounds.Dimensions.X) + "," +
           std::to_string(bounds.Dimensions.Y) + "," + std::to_string(bounds.Dimensions.Z) +
           "],\"pivot\":{\"requested\":" + std::to_string(static_cast<int>(pivot.RequestedMode)) +
           ",\"resolved\":" + std::to_string(static_cast<int>(pivot.ResolvedMode)) +
           ",\"position\":[" + std::to_string(pivot.LocalPosition.X) + "," +
           std::to_string(pivot.LocalPosition.Y) + "," + std::to_string(pivot.LocalPosition.Z) +
           "],\"normal\":[" + std::to_string(pivot.LocalNormal.X) + "," +
           std::to_string(pivot.LocalNormal.Y) + "," + std::to_string(pivot.LocalNormal.Z) +
           "],\"policy\":" + std::to_string(pivot.AutoPolicyVersion) +
           "},\"scale\":[1,1,1]}";
}

[[nodiscard]] VfstampWriteResult WriteError(const VfstampWriteError error, const std::string_view message) noexcept
{
    return {.Error = error, .Message = message};
}
[[nodiscard]] VfstampDecodeResult DecodeError(const VfstampDecodeError error, const std::string_view message) noexcept
{
    return {.Error = error, .Message = message};
}

struct ManifestData final { StampIdentity Identity; StampBounds Bounds; StampPivot Pivot; StampTransform Transform; };
[[nodiscard]] bool Consume(const std::string_view text, std::size_t& at, const std::string_view token) noexcept
{
    if (text.substr(at, token.size()) != token) return false;
    at += token.size(); return true;
}
template <typename T>
[[nodiscard]] bool ParseInteger(const std::string_view text, std::size_t& at, T& value) noexcept
{
    const char* first = text.data() + at; const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr == first) return false;
    at = static_cast<std::size_t>(result.ptr - text.data()); return true;
}
[[nodiscard]] bool ParseHex64(const std::string_view text, std::uint64_t& value) noexcept
{
    if (text.size() != 16U) return false;
    value = 0U;
    for (const char c : text) { value <<= 4U; if (c >= '0' && c <= '9') value |= c - '0'; else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10U; else return false; }
    return true;
}
[[nodiscard]] bool ParseManifest(const std::span<const std::byte> payload, ManifestData& data)
{
    std::string text; text.reserve(payload.size());
    for (const std::byte byte : payload) { const unsigned char c = std::to_integer<unsigned char>(byte); if (c > 0x7fU) return false; text.push_back(static_cast<char>(c)); }
    std::string_view view{text}; std::size_t at = 0U; std::uint64_t uuid = 0U;
    std::uint32_t dx{}, dy{}, dz{}, policy{}; int requested{}, resolved{}, nx{}, ny{}, nz{};
    if (!Consume(view, at, "{\"schema\":\"vfstamp-v1\",\"uuid\":\"") || at + 16U > view.size() ||
        !ParseHex64(view.substr(at, 16U), uuid)) return false;
    at += 16U;
    if (!Consume(view, at, "\",\"dimensions\":[") || !ParseInteger(view, at, dx) || !Consume(view, at, ",") ||
        !ParseInteger(view, at, dy) || !Consume(view, at, ",") || !ParseInteger(view, at, dz) ||
        !Consume(view, at, "],\"pivot\":{\"requested\":") || !ParseInteger(view, at, requested) ||
        !Consume(view, at, ",\"resolved\":") || !ParseInteger(view, at, resolved) ||
        !Consume(view, at, ",\"position\":[") || !ParseInteger(view, at, data.Pivot.LocalPosition.X) || !Consume(view, at, ",") ||
        !ParseInteger(view, at, data.Pivot.LocalPosition.Y) || !Consume(view, at, ",") || !ParseInteger(view, at, data.Pivot.LocalPosition.Z) ||
        !Consume(view, at, "],\"normal\":[") || !ParseInteger(view, at, nx) || !Consume(view, at, ",") ||
        !ParseInteger(view, at, ny) || !Consume(view, at, ",") || !ParseInteger(view, at, nz) ||
        !Consume(view, at, "],\"policy\":") || !ParseInteger(view, at, policy) ||
        !Consume(view, at, "},\"scale\":[1,1,1]}") || at != view.size() || requested < 0 || requested > 4 || resolved < 0 || resolved > 4 ||
        nx < -128 || nx > 127 || ny < -128 || ny > 127 || nz < -128 || nz > 127 ||
        dx > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        dy > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        dz > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) return false;
    data.Identity.Id = Core::UUID{uuid}; data.Bounds = {.Minimum = {}, .Maximum = {.X = dx == 0U ? 0 : static_cast<std::int32_t>(dx - 1U), .Y = dy == 0U ? 0 : static_cast<std::int32_t>(dy - 1U), .Z = dz == 0U ? 0 : static_cast<std::int32_t>(dz - 1U)}, .Dimensions = {.X = dx, .Y = dy, .Z = dz}};
    data.Pivot.RequestedMode = static_cast<StampPivotMode>(requested); data.Pivot.ResolvedMode = static_cast<StampPivotMode>(resolved); data.Pivot.LocalNormal = {.X = static_cast<std::int8_t>(nx), .Y = static_cast<std::int8_t>(ny), .Z = static_cast<std::int8_t>(nz)}; data.Pivot.AutoPolicyVersion = policy;
    return true;
}
[[nodiscard]] const VfstampChunk* FindChunk(const VfstampContainer& container, const std::uint32_t id) noexcept
{
    for (const VfstampChunk& chunk : container.Chunks) if (chunk.Id == id) return &chunk;
    return nullptr;
}

} // namespace

std::string CalculateVfstampLogicalContentHash(const VoxelStamp& stamp)
{
    std::uint64_t hash = FnvOffset;
    HashLe(hash, stamp.Identity().Id.Value());
    const StampBounds& bounds = stamp.Bounds();
    HashI32(hash, bounds.Minimum.X); HashI32(hash, bounds.Minimum.Y); HashI32(hash, bounds.Minimum.Z);
    HashI32(hash, bounds.Maximum.X); HashI32(hash, bounds.Maximum.Y); HashI32(hash, bounds.Maximum.Z);
    HashLe(hash, bounds.Dimensions.X); HashLe(hash, bounds.Dimensions.Y); HashLe(hash, bounds.Dimensions.Z);
    const StampPivot& pivot = stamp.Pivot();
    HashLe(hash, static_cast<std::uint8_t>(pivot.RequestedMode)); HashLe(hash, static_cast<std::uint8_t>(pivot.ResolvedMode));
    HashI32(hash, pivot.LocalPosition.X); HashI32(hash, pivot.LocalPosition.Y); HashI32(hash, pivot.LocalPosition.Z);
    HashLe(hash, static_cast<std::uint8_t>(pivot.LocalNormal.X)); HashLe(hash, static_cast<std::uint8_t>(pivot.LocalNormal.Y)); HashLe(hash, static_cast<std::uint8_t>(pivot.LocalNormal.Z)); HashLe(hash, pivot.AutoPolicyVersion);
    HashLe(hash, std::uint8_t{1U}); HashLe(hash, std::uint8_t{1U}); HashLe(hash, std::uint8_t{1U});
    for (const StampPaletteEntry& entry : stamp.Palette()) { HashLe(hash, entry.LocalColorId); HashLe(hash, entry.Color.Red); HashLe(hash, entry.Color.Green); HashLe(hash, entry.Color.Blue); HashLe(hash, entry.Color.Alpha); HashLe(hash, static_cast<std::uint8_t>(entry.HasSourcePaletteIndex)); HashLe(hash, entry.SourcePaletteIndex); }
    if (std::is_sorted(
            stamp.Voxels().begin(), stamp.Voxels().end(), IsVoxelLess))
    {
        for (const StampVoxel& voxel : stamp.Voxels()) { HashI32(hash, voxel.Position.X); HashI32(hash, voxel.Position.Y); HashI32(hash, voxel.Position.Z); HashLe(hash, voxel.LocalColorId); }
    }
    else
    {
        const std::vector<StampVoxel> voxels = SortedVoxels(stamp);
        for (const StampVoxel& voxel : voxels) { HashI32(hash, voxel.Position.X); HashI32(hash, voxel.Position.Y); HashI32(hash, voxel.Position.Z); HashLe(hash, voxel.LocalColorId); }
    }
    return Hex64(hash);
}

VfstampWriteResult WriteVfstampBytes(const VoxelStamp& stamp, const StampResourceLimits& limits) noexcept
{
    try
    {
        const StampValidationReport writeValidation = ValidateStampForWrite(stamp, limits);
        if (!writeValidation.IsValid())
        {
            const bool hashMismatch = std::any_of(
                writeValidation.Diagnostics.begin(), writeValidation.Diagnostics.end(),
                [](const StampDiagnostic& diagnostic) {
                    return diagnostic.ChunkId == VfstampChunkHash;
                });
            return WriteError(hashMismatch ? VfstampWriteError::ContentHashMismatch
                                           : VfstampWriteError::InvalidStamp,
                              "Stamp failed central validation before writing.");
        }

        // Preflight every output size before sorting voxels or allocating the
        // potentially large PAL0, VOX0 and final container buffers.
        const std::string manifest = MakeManifest(stamp);
        const std::uint64_t manifestSize = manifest.size();
        const std::uint64_t paletteCount = stamp.Palette().size();
        const std::uint64_t voxelCount = stamp.Voxels().size();
        if (MultiplyOverflow(paletteCount, PaletteRecordSize) ||
            MultiplyOverflow(voxelCount, VoxelRecordSize))
        {
            return WriteError(VfstampWriteError::ArithmeticOverflow,
                              "Vfstamp payload size multiplication overflowed.");
        }
        const std::uint64_t paletteRecords = paletteCount * PaletteRecordSize;
        const std::uint64_t voxelRecords = voxelCount * VoxelRecordSize;
        if (AddOverflow(4U, paletteRecords) || AddOverflow(8U, voxelRecords))
        {
            return WriteError(VfstampWriteError::ArithmeticOverflow,
                              "Vfstamp payload size addition overflowed.");
        }
        const std::uint64_t paletteSize = 4U + paletteRecords;
        const std::uint64_t voxelSize = 8U + voxelRecords;
        const std::uint64_t directorySize = 4U * VfstampDirectoryEntrySize;
        std::uint64_t fileSize = VfstampHeaderSize;
        std::uint64_t decodedBytes = 0U;
        for (const std::uint64_t size : {manifestSize, paletteSize, voxelSize,
                                         static_cast<std::uint64_t>(VfstampHashPayloadSize)})
        {
            if (AddOverflow(decodedBytes, size) || AddOverflow(fileSize, size))
            {
                return WriteError(VfstampWriteError::ArithmeticOverflow,
                                  "Vfstamp output size arithmetic overflowed.");
            }
            decodedBytes += size;
            fileSize += size;
        }
        if (AddOverflow(VfstampHeaderSize, directorySize) ||
            AddOverflow(fileSize, directorySize))
        {
            return WriteError(VfstampWriteError::ArithmeticOverflow,
                              "Vfstamp directory size arithmetic overflowed.");
        }
        fileSize += directorySize;
        if (fileSize > std::numeric_limits<std::size_t>::max() ||
            manifestSize > std::numeric_limits<std::size_t>::max() ||
            paletteSize > std::numeric_limits<std::size_t>::max() ||
            voxelSize > std::numeric_limits<std::size_t>::max())
        {
            return WriteError(VfstampWriteError::ArithmeticOverflow,
                              "Vfstamp output cannot be represented in memory.");
        }
        const StampLimitEvaluation sizeEvaluation = EvaluateStampLimits(
            {.DecodedBytes = decodedBytes, .FileBytes = fileSize, .ChunkCount = 4U}, limits);
        if (!sizeEvaluation.IsAllowed())
        {
            return WriteError(VfstampWriteError::ResourceLimitExceeded,
                              "Vfstamp output exceeds a configured hard resource limit.");
        }

        const std::string logicalHash = CalculateVfstampLogicalContentHash(stamp);
        const std::vector<StampVoxel> voxels = SortedVoxels(stamp);
        std::vector<std::byte> manf;
        manf.reserve(static_cast<std::size_t>(manifestSize));
        manf.assign(reinterpret_cast<const std::byte*>(manifest.data()),
                    reinterpret_cast<const std::byte*>(manifest.data() + manifest.size()));
        std::vector<std::byte> pal;
        pal.reserve(static_cast<std::size_t>(paletteSize));
        AppendLe(pal, static_cast<std::uint32_t>(paletteCount));
        for (const StampPaletteEntry& entry : stamp.Palette())
        {
            pal.push_back(static_cast<std::byte>(entry.LocalColorId));
            pal.push_back(static_cast<std::byte>(entry.Color.Red));
            pal.push_back(static_cast<std::byte>(entry.Color.Green));
            pal.push_back(static_cast<std::byte>(entry.Color.Blue));
            pal.push_back(static_cast<std::byte>(entry.Color.Alpha));
            pal.push_back(static_cast<std::byte>(entry.HasSourcePaletteIndex));
            pal.push_back(static_cast<std::byte>(entry.SourcePaletteIndex));
        }
        std::vector<std::byte> vox;
        vox.reserve(static_cast<std::size_t>(voxelSize));
        AppendLe(vox, voxelCount);
        for (const StampVoxel& voxel : voxels)
        {
            AppendI32(vox, voxel.Position.X);
            AppendI32(vox, voxel.Position.Y);
            AppendI32(vox, voxel.Position.Z);
            vox.push_back(static_cast<std::byte>(voxel.LocalColorId));
        }
        std::array<std::vector<std::byte>, 4U> payloads{
            std::move(manf), std::move(pal), std::move(vox),
            std::vector<std::byte>(VfstampHashPayloadSize)};
        std::array<std::uint32_t, 4U> ids{VfstampChunkManf, VfstampChunkPal0, VfstampChunkVox0, VfstampChunkHash};
        std::array<std::uint64_t, 4U> offsets{};
        std::array<std::uint64_t, 4U> sizes{};
        std::array<std::uint32_t, 4U> crcs{};
        std::uint64_t cursor = VfstampHeaderSize + directorySize;
        for (std::size_t index = 0U; index < payloads.size(); ++index)
        {
            offsets[index] = cursor;
            sizes[index] = payloads[index].size();
            cursor += sizes[index];
        }
        for (std::size_t index = 0U; index < payloads.size() - 1U; ++index)
        {
            crcs[index] = ComputeVfstampCrc32(payloads[index]);
        }
        const std::array<std::uint32_t, 4U> flags{VfstampChunkFlagRequired, VfstampChunkFlagRequired, VfstampChunkFlagRequired, VfstampChunkFlagRequired};
        const std::uint64_t structural = ComputeVfstampStructuralHash(VfstampFormatMajorVersion, VfstampFormatMinorVersion, ids, flags, offsets, sizes, crcs);
        payloads[3].clear(); AppendLe(payloads[3], structural); std::uint64_t logicalValue{};
        if (!ParseHex64(logicalHash, logicalValue)) return WriteError(VfstampWriteError::InternalValidationFailed, "Logical hash encoding failed.");
        AppendLe(payloads[3], logicalValue); crcs[3] = ComputeVfstampCrc32(payloads[3]);
        std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize), std::byte{0U});
        std::copy(VfstampMagic.begin(), VfstampMagic.end(), bytes.begin());
        const auto put16 = [&bytes](const std::size_t at, const std::uint16_t value) {
            bytes[at] = static_cast<std::byte>(value & 255U);
            bytes[at + 1U] = static_cast<std::byte>(value >> 8U);
        };
        const auto put32 = [&bytes](const std::size_t at, const std::uint32_t value) {
            for (std::size_t index = 0U; index < 4U; ++index)
                bytes[at + index] = static_cast<std::byte>(value >> (index * 8U));
        };
        const auto put64 = [&bytes](const std::size_t at, const std::uint64_t value) {
            for (std::size_t index = 0U; index < 8U; ++index)
                bytes[at + index] = static_cast<std::byte>(value >> (index * 8U));
        };
        put16(VfstampHeaderMajorVersionOffset,VfstampFormatMajorVersion); put16(VfstampHeaderMinorVersionOffset,VfstampFormatMinorVersion); put32(VfstampHeaderChunkCountOffset,4U); put32(VfstampHeaderDirectoryOffset,VfstampHeaderSize); put32(VfstampHeaderDirectorySizeOffset,static_cast<std::uint32_t>(directorySize));
        for (std::size_t index = 0U; index < 4U; ++index)
        {
            const std::size_t directoryOffset = VfstampHeaderSize + index * VfstampDirectoryEntrySize;
            put32(directoryOffset, ids[index]);
            put32(directoryOffset + VfstampDirectoryEntryFlagsOffset, flags[index]);
            put64(directoryOffset + VfstampDirectoryEntryPayloadOffset, offsets[index]);
            put64(directoryOffset + VfstampDirectoryEntryPayloadSizeOffset, sizes[index]);
            put32(directoryOffset + VfstampDirectoryEntryCrc32Offset, crcs[index]);
            std::copy(payloads[index].begin(), payloads[index].end(),
                      bytes.begin() + static_cast<std::size_t>(offsets[index]));
        }
        const VfstampReadResult read = ReadVfstampBytes(bytes, limits);
        if (!read.IsSuccess() || !DecodeVfstampContainer(*read.Container, limits).IsSuccess())
        {
            return WriteError(VfstampWriteError::InternalValidationFailed,
                              "Vfstamp final validation failed.");
        }
        return {.LogicalContentHash = logicalHash, .Bytes = std::move(bytes)};
    }
    catch (const std::bad_alloc&) { return WriteError(VfstampWriteError::AllocationFailure, "Vfstamp output allocation failed."); }
    catch (const std::exception&) { return WriteError(VfstampWriteError::InternalValidationFailed, "Vfstamp writer could not produce a valid container."); }
}

VfstampDecodeResult DecodeVfstampContainer(const VfstampContainer& container, const StampResourceLimits& limits) noexcept
{
    try
    {
        const VfstampChunk* manf = FindChunk(container, VfstampChunkManf);
        const VfstampChunk* pal = FindChunk(container, VfstampChunkPal0);
        const VfstampChunk* vox = FindChunk(container, VfstampChunkVox0);
        const VfstampChunk* hash = FindChunk(container, VfstampChunkHash);
        if (manf == nullptr || pal == nullptr || vox == nullptr || hash == nullptr)
        {
            return DecodeError(VfstampDecodeError::MissingChunk,
                               "Vfstamp domain chunks are missing.");
        }
        if (hash->Payload.size() != VfstampHashPayloadSize)
        {
            return DecodeError(VfstampDecodeError::InvalidHashPayload,
                               "STAMP-03 decoding requires a 16-byte HASH payload.");
        }
        std::uint64_t decoded = 0U;
        for (const std::size_t payloadSize : {manf->Payload.size(), pal->Payload.size(),
                                              vox->Payload.size(), hash->Payload.size()})
        {
            if (AddOverflow(decoded, payloadSize))
            {
                return DecodeError(VfstampDecodeError::ResourceLimitExceeded,
                                   "Vfstamp decoded-size arithmetic overflowed.");
            }
            decoded += payloadSize;
        }
        if (!EvaluateStampLimits({.DecodedBytes = decoded}, limits).IsAllowed())
        {
            return DecodeError(VfstampDecodeError::ResourceLimitExceeded,
                               "Vfstamp decoded domain data exceeds a hard limit.");
        }
        ManifestData manifest{};
        if (!ParseManifest(manf->Payload, manifest))
        {
            return DecodeError(VfstampDecodeError::InvalidManifest,
                               "MANF is not canonical V1 UTF-8 JSON.");
        }
        if (pal->Payload.size() < 4U)
        {
            return DecodeError(VfstampDecodeError::InvalidPalette, "PAL0 is truncated.");
        }
        const std::uint32_t paletteCount = ReadU32(pal->Payload, 0U);
        if (paletteCount == 0U || paletteCount > 256U ||
            MultiplyOverflow(paletteCount, PaletteRecordSize))
        {
            return DecodeError(VfstampDecodeError::InvalidPalette,
                               "PAL0 has an invalid fixed-record count.");
        }
        const std::uint64_t paletteRecordBytes = paletteCount * PaletteRecordSize;
        if (AddOverflow(4U, paletteRecordBytes) ||
            4U + paletteRecordBytes != pal->Payload.size())
        {
            return DecodeError(VfstampDecodeError::InvalidPalette,
                               "PAL0 has an invalid fixed-record size.");
        }
        if (vox->Payload.size() < 8U)
        {
            return DecodeError(VfstampDecodeError::InvalidVoxelData, "VOX0 is truncated.");
        }
        const std::uint64_t voxelCount = ReadU64(vox->Payload, 0U);
        if (voxelCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            MultiplyOverflow(voxelCount, VoxelRecordSize))
        {
            return DecodeError(VfstampDecodeError::InvalidVoxelData,
                               "VOX0 has an invalid fixed-record count.");
        }
        const std::uint64_t voxelRecordBytes = voxelCount * VoxelRecordSize;
        if (AddOverflow(8U, voxelRecordBytes) || 8U + voxelRecordBytes != vox->Payload.size())
        {
            return DecodeError(VfstampDecodeError::InvalidVoxelData,
                               "VOX0 has an invalid fixed-record size.");
        }
        const StampLimitEvaluation usage = EvaluateStampLimits(
            {.VoxelCount = voxelCount,
             .LargestAxisLength = std::max({manifest.Bounds.Dimensions.X,
                                            manifest.Bounds.Dimensions.Y,
                                            manifest.Bounds.Dimensions.Z}),
             .DecodedBytes = decoded},
            limits);
        if (!usage.IsAllowed())
        {
            return DecodeError(VfstampDecodeError::ResourceLimitExceeded,
                               "Vfstamp decoded domain data exceeds a hard limit.");
        }
        std::vector<StampPaletteEntry> palette;
        palette.reserve(paletteCount);
        for (std::uint32_t index = 0U; index < paletteCount; ++index)
        {
            const std::size_t offset = 4U + static_cast<std::size_t>(index) * PaletteRecordSize;
            const unsigned char sourceFlag =
                std::to_integer<unsigned char>(pal->Payload[offset + 5U]);
            if (sourceFlag > 1U)
            {
                return DecodeError(VfstampDecodeError::InvalidPalette,
                                   "PAL0 source-palette flag is invalid.");
            }
            palette.push_back({
                .LocalColorId = std::to_integer<std::uint8_t>(pal->Payload[offset]),
                .Color = {.Red = std::to_integer<std::uint8_t>(pal->Payload[offset + 1U]),
                          .Green = std::to_integer<std::uint8_t>(pal->Payload[offset + 2U]),
                          .Blue = std::to_integer<std::uint8_t>(pal->Payload[offset + 3U]),
                          .Alpha = std::to_integer<std::uint8_t>(pal->Payload[offset + 4U])},
                .HasSourcePaletteIndex = sourceFlag != 0U,
                .SourcePaletteIndex = std::to_integer<std::uint8_t>(pal->Payload[offset + 6U])});
        }
        std::vector<StampVoxel> voxels;
        voxels.reserve(static_cast<std::size_t>(voxelCount));
        for (std::uint64_t index = 0U; index < voxelCount; ++index)
        {
            const std::size_t offset = 8U + static_cast<std::size_t>(index) * VoxelRecordSize;
            const StampVoxel decodedVoxel{
                .Position = {.X = ReadI32(vox->Payload, offset),
                             .Y = ReadI32(vox->Payload, offset + 4U),
                             .Z = ReadI32(vox->Payload, offset + 8U)},
                .LocalColorId = std::to_integer<std::uint8_t>(vox->Payload[offset + 12U])};
            if (!voxels.empty() && IsVoxelLess(decodedVoxel, voxels.back()))
            {
                return DecodeError(VfstampDecodeError::InvalidVoxelData,
                                   "VOX0 records are not lexicographically sorted.");
            }
            voxels.push_back(decodedVoxel);
        }
        manifest.Identity.ContentHash = Hex64(ReadU64(hash->Payload, VfstampStructuralHashSize));
        StampValidationResult validation{};
        const auto stamp = VoxelStamp::TryCreate(
            manifest.Identity, manifest.Bounds, manifest.Pivot, manifest.Transform,
            std::move(palette), std::move(voxels), limits, &validation);
        if (!stamp)
        {
            return DecodeError(VfstampDecodeError::DomainValidationFailed,
                               "Vfstamp payloads violate VoxelStamp invariants.");
        }
        if (CalculateVfstampLogicalContentHash(*stamp) != manifest.Identity.ContentHash)
        {
            return DecodeError(VfstampDecodeError::LogicalHashMismatch,
                               "Vfstamp logical hash does not match domain content.");
        }
        return {.Stamp = std::move(*stamp)};
    }
    catch(const std::bad_alloc&){return DecodeError(VfstampDecodeError::AllocationFailure,"Vfstamp domain allocation failed.");}
    catch(const std::exception&){return DecodeError(VfstampDecodeError::InvalidManifest,"Vfstamp domain decoding failed.");}
}

VfstampDecodeResult DecodeVfstampBytes(const std::span<const std::byte> bytes, const StampResourceLimits& limits) noexcept
{
    const VfstampReadResult read = ReadVfstampBytes(bytes, limits);
    if (!read.IsSuccess())
    {
        return DecodeError(VfstampDecodeError::ContainerError,
                           "Vfstamp container validation failed.");
    }
    return DecodeVfstampContainer(*read.Container, limits);
}

} // namespace VoxelForge::Editor::Stamps
