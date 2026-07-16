#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"

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
using VoxelForge::Asset::Vox::VoxModelAnalysis;
using VoxelForge::Asset::Vox::VoxModelAnalyzer;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        path_ = fs::temp_directory_path() /
            ("VoxelForgeVoxAnalyzer-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] fs::path File(std::string_view name) const
    {
        return path_ / name;
    }
private:
    fs::path path_;
};

void Require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void AppendU32(Bytes& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void Append(Bytes& destination, const Bytes& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

Bytes Chunk(std::string_view id, const Bytes& content = {},
    const Bytes& children = {})
{
    Require(id.size() == 4U, "Chunk id must contain four bytes.");
    Bytes bytes(id.begin(), id.end());
    AppendU32(bytes, static_cast<std::uint32_t>(content.size()));
    AppendU32(bytes, static_cast<std::uint32_t>(children.size()));
    Append(bytes, content);
    Append(bytes, children);
    return bytes;
}

Bytes Size(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    Bytes content;
    AppendU32(content, x); AppendU32(content, y); AppendU32(content, z);
    return Chunk("SIZE", content);
}

Bytes Xyzi(const std::vector<std::array<std::uint8_t, 4U>>& voxels)
{
    Bytes content;
    AppendU32(content, static_cast<std::uint32_t>(voxels.size()));
    for (const auto& voxel : voxels)
        content.insert(content.end(), voxel.begin(), voxel.end());
    return Chunk("XYZI", content);
}

Bytes Pack(std::uint32_t count)
{
    Bytes content; AppendU32(content, count); return Chunk("PACK", content);
}

Bytes Model(std::uint32_t x, std::uint32_t y, std::uint32_t z,
    const std::vector<std::array<std::uint8_t, 4U>>& voxels)
{
    Bytes result = Size(x, y, z); Append(result, Xyzi(voxels)); return result;
}

Bytes Vox(const Bytes& children, std::uint32_t version = 150U)
{
    Bytes bytes{'V', 'O', 'X', ' '};
    AppendU32(bytes, version); Append(bytes, Chunk("MAIN", {}, children));
    return bytes;
}

void Write(const fs::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "Unable to write VOX test file.");
}

VoxModelAnalysis Analyze(const TemporaryDirectory& temporary,
    std::string_view name, const Bytes& bytes)
{
    const fs::path path = temporary.File(name);
    Write(path, bytes);
    return VoxModelAnalyzer{}.Analyze(path);
}
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        const Bytes minimalChildren = Model(2U, 3U, 4U,
            {{0U, 0U, 0U, 5U}});
        const VoxModelAnalysis minimal = Analyze(
            temporary, "minimal.vox", Vox(minimalChildren));
        Require(minimal.Valid, "Minimal VOX must be valid.");
        Require(minimal.FormatVersion == 150U, "Version was not extracted.");
        Require(minimal.ModelCount == 1U && minimal.Models.size() == 1U,
            "MAIN/model count was not extracted.");
        Require(minimal.SizeX == 2U && minimal.SizeY == 3U &&
            minimal.SizeZ == 4U, "SIZE was not extracted.");
        Require(minimal.VoxelCount == 1U, "XYZI count was not extracted.");
        Require(minimal.UsedPaletteColorCount == 1U,
            "Used palette color count is incorrect.");
        Require(!minimal.HasCustomPalette,
            "Default palette was reported as custom.");
        Require(minimal.FileSize == Vox(minimalChildren).size(),
            "File size was not extracted.");

        Bytes unknown = Chunk("JUNK", {1U, 2U, 3U});
        Append(unknown, minimalChildren);
        Require(Analyze(temporary, "unknown.vox", Vox(unknown)).Valid,
            "Unknown chunk was not ignored.");

        const VoxModelAnalysis repeatedColors = Analyze(temporary,
            "colors.vox", Vox(Model(3U, 1U, 1U,
                {{0U, 0U, 0U, 7U}, {1U, 0U, 0U, 7U},
                 {2U, 0U, 0U, 9U}})));
        Require(repeatedColors.UsedPaletteColorCount == 2U,
            "Repeated colors were counted more than once.");

        Bytes paletteChildren = minimalChildren;
        Append(paletteChildren, Chunk("RGBA", Bytes(1024U, 255U)));
        Require(Analyze(temporary, "palette.vox", Vox(paletteChildren))
                .HasCustomPalette, "RGBA was not detected.");

        Bytes packed = Pack(2U);
        Append(packed, Model(2U, 2U, 2U,
            {{0U, 0U, 0U, 1U}, {1U, 1U, 1U, 2U}}));
        Append(packed, Model(4U, 5U, 6U,
            {{3U, 4U, 5U, 3U}}));
        const VoxModelAnalysis multi = Analyze(
            temporary, "packed.vox", Vox(packed));
        Require(multi.Valid && multi.ModelCount == 2U &&
            multi.Models.size() == 2U, "PACK was not analyzed.");
        Require(multi.VoxelCount == 3U,
            "Multi-model voxel total is incorrect.");
        Require(multi.SizeX == 2U && multi.Models[1].SizeX == 4U,
            "Primary or per-model dimensions are incorrect.");
        Require(multi.UsedPaletteColorCount == 3U,
            "Multi-model palette usage is incorrect.");

        Bytes invalidSignature = Vox(minimalChildren);
        invalidSignature[0] = 'B';
        const VoxModelAnalysis signature = Analyze(
            temporary, "signature.vox", invalidSignature);
        Require(!signature.Valid && signature.Error.find("signature") !=
            std::string::npos, "Invalid signature was accepted.");
        Require(!Analyze(temporary, "header.vox", {'V', 'O', 'X', ' '}).Valid,
            "Truncated header was accepted.");
        Require(!Analyze(temporary, "empty.vox", {}).Valid,
            "Empty file was accepted.");
        Require(!VoxModelAnalyzer{}.Analyze(temporary.File("missing.vox")).Valid,
            "Missing file was accepted.");

        Bytes truncatedSize = Chunk("SIZE", Bytes(8U, 0U));
        Require(!Analyze(temporary, "size-truncated.vox",
            Vox(truncatedSize)).Valid, "Truncated SIZE was accepted.");
        Bytes zeroDimensions = Model(0U, 1U, 1U,
            {{0U, 0U, 0U, 1U}});
        Require(!Analyze(temporary, "zero.vox", Vox(zeroDimensions)).Valid,
            "Zero dimensions were accepted.");

        Bytes shortXyziContent; AppendU32(shortXyziContent, 2U);
        shortXyziContent.insert(shortXyziContent.end(),
            {0U, 0U, 0U, 1U});
        Bytes shortXyzi = Size(2U, 2U, 2U);
        Append(shortXyzi, Chunk("XYZI", shortXyziContent));
        Require(!Analyze(temporary, "xyzi-truncated.vox",
            Vox(shortXyzi)).Valid, "Truncated XYZI was accepted.");

        Bytes trailingXyzi; AppendU32(trailingXyzi, 0U);
        trailingXyzi.push_back(0U);
        Bytes inconsistent = Size(1U, 1U, 1U);
        Append(inconsistent, Chunk("XYZI", trailingXyzi));
        Require(!Analyze(temporary, "xyzi-count.vox",
            Vox(inconsistent)).Valid, "Inconsistent XYZI size was accepted.");

        Bytes oversizedContent{'J', 'U', 'N', 'K'};
        AppendU32(oversizedContent, 0xFFFFFFFFU); AppendU32(oversizedContent, 0U);
        Require(!Analyze(temporary, "content-overflow.vox",
            Vox(oversizedContent)).Valid, "Oversized content was accepted.");
        Bytes oversizedChildren{'J', 'U', 'N', 'K'};
        AppendU32(oversizedChildren, 0U); AppendU32(oversizedChildren, 0xFFFFFFFFU);
        Require(!Analyze(temporary, "children-overflow.vox",
            Vox(oversizedChildren)).Valid, "Oversized children were accepted.");

        Bytes incompleteChunk{'S', 'I', 'Z', 'E', 12U};
        Require(!Analyze(temporary, "chunk-truncated.vox",
            Vox(incompleteChunk)).Valid, "Truncated chunk was accepted.");

        const VoxModelAnalysis deterministicA = Analyze(
            temporary, "deterministic.vox", invalidSignature);
        const VoxModelAnalysis deterministicB =
            VoxModelAnalyzer{}.Analyze(temporary.File("deterministic.vox"));
        Require(deterministicA.Error == deterministicB.Error &&
            !deterministicA.Error.empty(), "Errors are not deterministic.");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
