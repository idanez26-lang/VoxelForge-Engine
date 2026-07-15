#include "VoxelForge/Asset/AssetImportResult.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using VoxelForge::Asset::AssetImportError;
using VoxelForge::Asset::Vox::VoxColor;
using VoxelForge::Asset::Vox::VoxImporter;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
            ("VoxelForgeVoxImporter-" + std::to_string(unique));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] fs::path File(const std::string_view name) const
    {
        return path_ / name;
    }

    [[nodiscard]] const fs::path& Root() const noexcept
    {
        return path_;
    }

private:
    fs::path path_;
};

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

void AppendU32(Bytes& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendId(Bytes& bytes, const std::string_view id)
{
    Require(id.size() == 4U, "Chunk IDs must contain four characters.");

    for (const char character : id)
    {
        bytes.push_back(static_cast<std::uint8_t>(character));
    }
}

Bytes Chunk(
    const std::string_view id,
    const Bytes& content = {},
    const Bytes& children = {})
{
    Bytes bytes;
    AppendId(bytes, id);
    AppendU32(bytes, static_cast<std::uint32_t>(content.size()));
    AppendU32(bytes, static_cast<std::uint32_t>(children.size()));
    bytes.insert(bytes.end(), content.begin(), content.end());
    bytes.insert(bytes.end(), children.begin(), children.end());
    return bytes;
}

void Append(Bytes& destination, const Bytes& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

Bytes SizeChunk(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z)
{
    Bytes content;
    AppendU32(content, x);
    AppendU32(content, y);
    AppendU32(content, z);
    return Chunk("SIZE", content);
}

Bytes XyziChunk(
    const std::vector<std::array<std::uint8_t, 4>>& voxels)
{
    Bytes content;
    AppendU32(content, static_cast<std::uint32_t>(voxels.size()));

    for (const auto& voxel : voxels)
    {
        content.insert(content.end(), voxel.begin(), voxel.end());
    }

    return Chunk("XYZI", content);
}

Bytes PackChunk(const std::uint32_t modelCount)
{
    Bytes content;
    AppendU32(content, modelCount);
    return Chunk("PACK", content);
}

Bytes VoxFile(const Bytes& children, const std::uint32_t version = 150U)
{
    Bytes bytes{'V', 'O', 'X', ' '};
    AppendU32(bytes, version);
    Append(bytes, Chunk("MAIN", {}, children));
    return bytes;
}

void WriteBytes(const fs::path& path, const Bytes& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(stream), "Unable to create temporary VOX file.");
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(stream), "Unable to write temporary VOX file.");
}

auto Inspect(
    const TemporaryDirectory& directory,
    const std::string_view name,
    const Bytes& bytes)
{
    const fs::path path = directory.File(name);
    WriteBytes(path, bytes);
    return VoxImporter{}.Inspect(path);
}

Bytes MinimalModel(
    const std::uint32_t x = 2U,
    const std::uint32_t y = 3U,
    const std::uint32_t z = 4U,
    const std::array<std::uint8_t, 4> voxel = {1U, 2U, 3U, 1U})
{
    Bytes children;
    Append(children, SizeChunk(x, y, z));
    Append(children, XyziChunk({voxel}));
    return children;
}

void TestMinimalAndDefaultPalette(const TemporaryDirectory& directory)
{
    const auto outcome = Inspect(
        directory, "minimal.vox", VoxFile(MinimalModel()));
    Require(outcome.Result.Succeeded, "Minimal VOX must succeed.");
    Require(outcome.Asset.has_value(), "Successful import needs a model.");
    Require(outcome.Asset->Version == 150U, "VOX version was not read.");
    Require(outcome.Asset->Models.size() == 1U, "Expected one model.");
    Require(
        outcome.Asset->Models[0].Dimensions.X == 2U &&
        outcome.Asset->Models[0].Dimensions.Y == 3U &&
        outcome.Asset->Models[0].Dimensions.Z == 4U,
        "Model dimensions are incorrect.");
    Require(
        outcome.Asset->TotalVoxelCount() == 1U,
        "Voxel count is incorrect.");
    Require(
        outcome.Asset->Models[0].Voxels ==
            std::vector<VoxelForge::Asset::Vox::VoxVoxel>{
                {1U, 2U, 3U, 1U}},
        "XYZI voxel data was not preserved.");
    Require(
        !outcome.Asset->HasCustomPalette,
        "Palette should be the default palette.");
    Require(
        outcome.Asset->Palette[0] == VoxColor{0U, 0U, 0U, 0U} &&
        outcome.Asset->Palette[1] == VoxColor{255U, 255U, 255U, 255U} &&
        outcome.Asset->Palette[36] == VoxColor{255U, 0U, 0U, 255U} &&
        outcome.Asset->Palette[215] == VoxColor{0U, 0U, 51U, 255U} &&
        outcome.Asset->Palette[216] == VoxColor{238U, 0U, 0U, 255U} &&
        outcome.Asset->Palette[255] == VoxColor{17U, 17U, 17U, 255U},
        "Default palette does not match the official palette.");
}

void TestCustomPalette(const TemporaryDirectory& directory)
{
    Bytes children = MinimalModel();
    Bytes palette(1024U, 0U);
    palette[0] = 10U;
    palette[1] = 20U;
    palette[2] = 30U;
    palette[3] = 40U;
    palette[1016] = 50U;
    palette[1017] = 60U;
    palette[1018] = 70U;
    palette[1019] = 80U;
    palette[1020] = 90U;
    palette[1021] = 100U;
    palette[1022] = 110U;
    palette[1023] = 120U;
    Append(children, Chunk("RGBA", palette));
    const auto outcome = Inspect(
        directory, "palette.vox", VoxFile(children));
    Require(outcome.Result.Succeeded, "Custom palette VOX must succeed.");
    Require(outcome.Asset->HasCustomPalette, "Custom palette not detected.");
    Require(
        outcome.Asset->Palette[0] == VoxColor{0U, 0U, 0U, 0U} &&
        outcome.Asset->Palette[1] == VoxColor{10U, 20U, 30U, 40U} &&
        outcome.Asset->Palette[255] == VoxColor{50U, 60U, 70U, 80U},
        "RGBA palette index mapping is incorrect.");
}

void TestPackAndUnknownChunks(const TemporaryDirectory& directory)
{
    Bytes children;
    Append(children, PackChunk(2U));
    Append(children, Chunk("JUNK", {1U, 2U, 3U}));
    Append(children, Chunk("nTRN", {}));
    Append(children, Chunk("nTRN", {}));
    Append(children, MinimalModel());
    Append(children, MinimalModel(1U, 1U, 1U, {0U, 0U, 0U, 2U}));
    const auto outcome = Inspect(
        directory, "packed.vox", VoxFile(children));
    Require(outcome.Result.Succeeded, "PACK VOX must succeed.");
    Require(
        outcome.Asset->Models.size() == 2U &&
        outcome.Asset->DeclaredModelCount == 2U &&
        outcome.Asset->HasPackChunk,
        "PACK metadata is incorrect.");
    Require(
        outcome.Asset->IgnoredChunkCount == 3U,
        "Unknown chunks were not counted.");
    Require(
        outcome.Result.Warnings.size() == 1U,
        "Advanced scene chunk should produce one warning.");
}

void TestInvalidHeadersAndBounds(const TemporaryDirectory& directory)
{
    Bytes invalidSignature = VoxFile(MinimalModel());
    invalidSignature[0] = 'B';
    Require(
        Inspect(directory, "signature.vox", invalidSignature).Result.Error ==
            AssetImportError::InvalidSignature,
        "Invalid signature was accepted.");

    Require(
        Inspect(directory, "small.vox", {'V', 'O', 'X'}).Result.Error ==
            AssetImportError::FileTooSmall,
        "Truncated file was accepted.");

    Bytes truncatedChildren{'S', 'I', 'Z', 'E', 12U};
    Require(
        Inspect(
            directory,
            "truncated-chunk.vox",
            VoxFile(truncatedChildren)).Result.Error ==
            AssetImportError::InvalidChunk,
        "Truncated chunk header was accepted.");

    Bytes oversizedChunk;
    AppendId(oversizedChunk, "JUNK");
    AppendU32(oversizedChunk, 100U);
    AppendU32(oversizedChunk, 0U);
    Require(
        Inspect(
            directory,
            "chunk-bounds.vox",
            VoxFile(oversizedChunk)).Result.Error ==
            AssetImportError::InvalidChunk,
        "Out-of-bounds chunk was accepted.");

    Bytes noMain{'V', 'O', 'X', ' '};
    AppendU32(noMain, 150U);
    Append(noMain, Chunk("JUNK"));
    Require(
        Inspect(directory, "main.vox", noMain).Result.Error ==
            AssetImportError::MissingMainChunk,
        "Missing MAIN chunk was accepted.");
}

void TestInvalidModels(const TemporaryDirectory& directory)
{
    Bytes truncatedXyzi;
    AppendU32(truncatedXyzi, 2U);
    truncatedXyzi.insert(
        truncatedXyzi.end(), {0U, 0U, 0U, 1U});
    Bytes children = SizeChunk(2U, 2U, 2U);
    Append(children, Chunk("XYZI", truncatedXyzi));
    Require(
        Inspect(directory, "xyzi-truncated.vox", VoxFile(children))
                .Result.Error == AssetImportError::InvalidChunk,
        "Truncated XYZI data was accepted.");

    Require(
        Inspect(
            directory,
            "outside.vox",
            VoxFile(MinimalModel(2U, 2U, 2U, {2U, 0U, 0U, 1U})))
                .Result.Error == AssetImportError::InvalidVoxel,
        "Out-of-bounds voxel was accepted.");

    Require(
        Inspect(
            directory,
            "color-zero.vox",
            VoxFile(MinimalModel(2U, 2U, 2U, {0U, 0U, 0U, 0U})))
                .Result.Error == AssetImportError::InvalidVoxel,
        "Reserved palette index 0 was accepted.");

    Require(
        Inspect(
            directory,
            "missing-size.vox",
            VoxFile(XyziChunk({{0U, 0U, 0U, 1U}})))
                .Result.Error == AssetImportError::MissingSizeChunk,
        "XYZI without SIZE was accepted.");

    Require(
        Inspect(
            directory,
            "dimension.vox",
            VoxFile(MinimalModel(
                VoxelForge::Asset::Vox::MaximumVoxDimension + 1U,
                1U,
                1U,
                {0U, 0U, 0U, 1U})))
                .Result.Error == AssetImportError::InvalidDimensions,
        "Excessive dimensions were accepted.");

    Bytes excessiveCount;
    AppendU32(
        excessiveCount,
        static_cast<std::uint32_t>(
            VoxelForge::Asset::Vox::MaximumVoxVoxelCount + 1U));
    children = SizeChunk(1U, 1U, 1U);
    Append(children, Chunk("XYZI", excessiveCount));
    Require(
        Inspect(directory, "voxel-limit.vox", VoxFile(children))
                .Result.Error == AssetImportError::TooManyVoxels,
        "Excessive voxel count was accepted.");

    children.clear();
    Append(children, PackChunk(2U));
    Append(children, MinimalModel());
    Require(
        Inspect(directory, "pack-count.vox", VoxFile(children))
                .Result.Error == AssetImportError::InconsistentModelCount,
        "Inconsistent PACK count was accepted.");

    children = MinimalModel();
    Append(children, PackChunk(1U));
    Require(
        Inspect(directory, "late-pack.vox", VoxFile(children))
                .Result.Error == AssetImportError::InvalidChunk,
        "PACK after model chunks was accepted.");
}

void TestFileSizeLimit(const TemporaryDirectory& directory)
{
    const fs::path path = directory.File("too-large.vox");
    WriteBytes(path, VoxFile(MinimalModel()));
    std::error_code error;
    fs::resize_file(
        path,
        VoxelForge::Asset::Vox::MaximumVoxFileSize + 1U,
        error);
    Require(!error, "Unable to create sparse oversized test file.");
    Require(
        VoxImporter{}.Inspect(path).Result.Error ==
            AssetImportError::FileTooLarge,
        "Oversized VOX file was accepted.");
}

void TestDefensiveLimitsAndDuplicates(
    const TemporaryDirectory& directory)
{
    Bytes children;
    Append(
        children,
        PackChunk(VoxelForge::Asset::Vox::MaximumVoxModelCount + 1U));
    Append(children, MinimalModel());
    Require(
        Inspect(directory, "model-limit.vox", VoxFile(children))
                .Result.Error == AssetImportError::TooManyModels,
        "Excessive PACK model count was accepted.");

    children = MinimalModel();
    const Bytes palette(1024U, 255U);
    Append(children, Chunk("RGBA", palette));
    Append(children, Chunk("RGBA", palette));
    Require(
        Inspect(directory, "duplicate-palette.vox", VoxFile(children))
                .Result.Error == AssetImportError::InvalidChunk,
        "Duplicate RGBA chunks were accepted.");

    children.clear();
    children.reserve(
        static_cast<std::size_t>(
            VoxelForge::Asset::Vox::MaximumVoxChunkCount + 1U) * 12U);

    for (std::uint32_t index = 0;
         index <= VoxelForge::Asset::Vox::MaximumVoxChunkCount;
         ++index)
    {
        Append(children, Chunk("JUNK"));
    }

    Require(
        Inspect(directory, "chunk-limit.vox", VoxFile(children))
                .Result.Error == AssetImportError::TooManyChunks,
        "Excessive chunk count was accepted.");

    Bytes nestedChunk = Chunk("JUNK");

    for (std::uint32_t depth = 0;
         depth <= VoxelForge::Asset::Vox::MaximumVoxChunkDepth;
         ++depth)
    {
        nestedChunk = Chunk("JUNK", {}, nestedChunk);
    }

    Require(
        Inspect(directory, "chunk-depth.vox", VoxFile(nestedChunk))
                .Result.Error == AssetImportError::InvalidChunk,
        "Excessive chunk nesting was accepted.");
}

void TestTemporaryCleanup()
{
    fs::path temporaryPath;

    {
        TemporaryDirectory directory;
        temporaryPath = directory.Root();
        WriteBytes(directory.File("cleanup.vox"), VoxFile(MinimalModel()));
        Require(fs::exists(temporaryPath), "Temporary test folder is missing.");
    }

    Require(
        !fs::exists(temporaryPath),
        "Temporary VOX test folder was not removed.");
}
}

int main()
{
    try
    {
        TemporaryDirectory directory;
        TestMinimalAndDefaultPalette(directory);
        TestCustomPalette(directory);
        TestPackAndUnknownChunks(directory);
        TestInvalidHeadersAndBounds(directory);
        TestInvalidModels(directory);
        TestFileSizeLimit(directory);
        TestDefensiveLimitsAndDuplicates(directory);
        TestTemporaryCleanup();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
