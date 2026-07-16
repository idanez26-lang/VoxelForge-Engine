#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge::Asset;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        Path = fs::temp_directory_path() /
            ("VoxelForgeVoxWriter-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(Path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(Path, ignored);
    }
    fs::path Path;
};

Vox::VoxModel Source(
    std::vector<Vox::VoxModelMetadata> models,
    const bool customPalette = false)
{
    Vox::VoxModel source;
    source.Version = 150U;
    source.Models = std::move(models);
    source.Palette = Vox::DefaultVoxPalette();
    source.HasCustomPalette = customPalette;
    source.HasPackChunk = source.Models.size() > 1U;
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    if (customPalette)
    {
        source.Palette[1U] = {10U, 20U, 30U, 255U};
        source.Palette[255U] = {200U, 150U, 100U, 128U};
    }
    return source;
}

Voxel::VoxelDocument Build(
    Vox::VoxModel source,
    const fs::path& path = "writer-test.vox")
{
    auto loaded = Voxel::VoxDocumentLoader{}.Build(source, path, "asset-id");
    Require(loaded.Succeeded(), loaded.Message.empty()
        ? "Unable to build writer test document." : loaded.Message);
    return std::move(*loaded.Document);
}

void Write(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "Unable to write VOX writer fixture.");
}

std::uint32_t ReadU32(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset)
{
    Require(offset + 4U <= bytes.size(), "U32 read exceeds writer output.");
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::size_t FindChunk(
    const std::vector<std::uint8_t>& bytes,
    const std::string_view id)
{
    const auto found = std::search(bytes.begin(), bytes.end(), id.begin(), id.end());
    return found == bytes.end()
        ? std::string::npos
        : static_cast<std::size_t>(std::distance(bytes.begin(), found));
}

void VerifyRoundTrip(
    const Voxel::VoxelDocument& document,
    const fs::path& path)
{
    const Voxel::VoxDocumentWriteResult written =
        Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(written.Succeeded(), written.Message.empty()
        ? "Writer serialization failed without a message." : written.Message);
    Write(path, written.Bytes);
    const auto loaded = Voxel::VoxDocumentLoader{}.Load(path, "asset-id");
    Require(loaded.Succeeded(), loaded.Message.empty()
        ? "Writer output reload failed without a message." : loaded.Message);
    std::string difference;
    const bool equivalent = Voxel::AreVoxelDocumentsEquivalent(
        document, *loaded.Document, difference);
    Require(equivalent, difference.empty()
            ? "Writer round-trip documents differ without a diagnostic."
            : difference);
}

void TestMinimalEmptyAndChunkSizes(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument empty = Build(Source({
        {{1U, 1U, 1U}, {}}}));
    const auto result = Voxel::VoxDocumentWriter{}.Serialize(empty);
    Require(result.Succeeded(), result.Message);
    Require(result.Bytes.size() >= 56U &&
        std::string(result.Bytes.begin(), result.Bytes.begin() + 4U) == "VOX " &&
        ReadU32(result.Bytes, 4U) == 150U,
        "Writer did not emit a valid VOX header.");
    Require(FindChunk(result.Bytes, "MAIN") == 8U &&
        ReadU32(result.Bytes, 12U) == 0U &&
        ReadU32(result.Bytes, 16U) == result.Bytes.size() - 20U,
        "MAIN chunk sizes are incorrect.");
    const std::size_t xyzi = FindChunk(result.Bytes, "XYZI");
    Require(xyzi != std::string::npos && ReadU32(result.Bytes, xyzi + 4U) == 4U &&
        ReadU32(result.Bytes, xyzi + 12U) == 0U,
        "Empty XYZI chunk is not valid.");
    Require(FindChunk(result.Bytes, "RGBA") == std::string::npos,
        "Default palette should use the canonical implicit VOX palette.");
    VerifyRoundTrip(empty, temporary.Path / "empty.vox");
}

void TestDeterminismPaletteAndAnalysis(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument document = Build(Source({
        {{8U, 7U, 6U}, {
            {7U, 6U, 5U, 255U}, {0U, 0U, 0U, 1U},
            {3U, 2U, 1U, 17U}, {1U, 5U, 4U, 1U}}}}, true));
    const auto first = Voxel::VoxDocumentWriter{}.Serialize(document);
    const auto second = Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(first.Succeeded() && second.Succeeded() &&
        first.Bytes == second.Bytes,
        "Identical documents must produce identical VOX bytes.");
    const std::size_t xyzi = FindChunk(first.Bytes, "XYZI");
    const std::size_t rgba = FindChunk(first.Bytes, "RGBA");
    Require(xyzi != std::string::npos &&
        ReadU32(first.Bytes, xyzi + 4U) == 4U + 4U * 4U &&
        rgba != std::string::npos && ReadU32(first.Bytes, rgba + 4U) == 1024U,
        "XYZI or RGBA chunk size is incorrect.");
    const fs::path output = temporary.Path / "custom.vox";
    Write(output, first.Bytes);
    const Vox::VoxModelAnalysis analysis = Vox::VoxModelAnalyzer{}.Analyze(output);
    Require(analysis.Valid && analysis.ModelCount == 1U &&
        analysis.VoxelCount == 4U && analysis.SizeX == 8U &&
        analysis.SizeY == 7U && analysis.SizeZ == 6U &&
        analysis.HasCustomPalette,
        "Writer output is inconsistent with VoxModelAnalyzer.");
    VerifyRoundTrip(document, output);
}

void TestMultiModelPackAndLimits(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument multi = Build(Source({
        {{256U, 2U, 2U}, {{255U, 1U, 1U, 4U}}},
        {{3U, 4U, 5U}, {{2U, 3U, 4U, 9U}, {0U, 0U, 0U, 2U}}}}));
    const auto written = Voxel::VoxDocumentWriter{}.Serialize(multi);
    Require(written.Succeeded(), written.Message);
    const std::size_t pack = FindChunk(written.Bytes, "PACK");
    Require(pack != std::string::npos && ReadU32(written.Bytes, pack + 4U) == 4U &&
        ReadU32(written.Bytes, pack + 12U) == 2U,
        "Multi-model writer output has an invalid PACK chunk.");
    VerifyRoundTrip(multi, temporary.Path / "multi.vox");

    const Voxel::VoxelDocument tooWide = Build(Source({
        {{257U, 1U, 1U}, {}}}));
    const auto refused = Voxel::VoxDocumentWriter{}.Serialize(tooWide);
    Require(!refused.Succeeded() &&
        refused.Error == Voxel::VoxDocumentWriteError::InvalidDimensions,
        "XYZI dimensions beyond 256 must be refused without truncation.");
}

void TestMutationsAndEquivalence(TemporaryDirectory& temporary)
{
    Voxel::VoxelDocument document = Build(Source({
        {{4U, 4U, 4U}, {{0U, 0U, 0U, 2U}, {1U, 1U, 1U, 3U}}}}));
    Require(document.SetVoxel({2, 2, 2}, 4U).Changed,
        "Unable to add writer test voxel.");
    Require(document.RemoveVoxel({0, 0, 0}).Changed,
        "Unable to remove writer test voxel.");
    Require(document.SetPaletteColor(4U, {5U, 6U, 7U, 255U}).Changed,
        "Unable to customize writer test palette.");
    VerifyRoundTrip(document, temporary.Path / "mutated.vox");

    Voxel::VoxelDocument different = Build(Source({
        {{4U, 4U, 4U}, {{1U, 1U, 1U, 3U}}}}));
    std::string difference;
    Require(!Voxel::AreVoxelDocumentsEquivalent(document, different, difference) &&
        !difference.empty(),
        "Document equivalence must report real voxel differences.");
}
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        TestMinimalEmptyAndChunkSizes(temporary);
        TestDeterminismPaletteAndAnalysis(temporary);
        TestMultiModelPackAndLimits(temporary);
        TestMutationsAndEquivalence(temporary);
        std::cout << "VOX document writer tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "VOX document writer tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
