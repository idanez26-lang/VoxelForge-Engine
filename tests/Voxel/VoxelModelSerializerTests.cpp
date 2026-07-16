#include "VoxelForge/Voxel/VoxelFileFormat.h"
#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include <chrono>
#include <cstddef>
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
using VoxelForge::Voxel::Voxel;
using VoxelForge::Voxel::VoxelColor;
using VoxelForge::Voxel::VoxelDeserializationResult;
using VoxelForge::Voxel::VoxelGrid;
using VoxelForge::Voxel::VoxelModel;
using VoxelForge::Voxel::VoxelModelSerializer;

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeSerializerTests-" + std::to_string(unique));
        Require(std::filesystem::create_directories(path_),
            "Unable to create serializer test directory.");
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "Unable to read test file.");
    std::vector<std::uint8_t> bytes;
    char byte = 0;
    while (input.get(byte))
    {
        bytes.push_back(
            static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }
    return bytes;
}

void WriteBytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(output.good(), "Unable to create corrupt test file.");
    for (const std::uint8_t byte : bytes)
    {
        output.put(static_cast<char>(byte));
    }
    Require(output.good(), "Unable to write corrupt test file.");
}

void WriteU32(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    Require(offset + 4U <= bytes.size(), "Invalid test mutation offset.");
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

void WriteU64(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint64_t value)
{
    WriteU32(bytes, offset, static_cast<std::uint32_t>(value));
    WriteU32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
}

[[nodiscard]] VoxelModel MakeModel()
{
    VoxelModel model;
    model.SetName("Chateau \xF0\x9F\x8F\xB0");
    for (std::size_t index = 0; index < 256U; ++index)
    {
        Require(model.Palette().Set(index, {
            static_cast<std::uint8_t>(index),
            static_cast<std::uint8_t>(255U - index),
            static_cast<std::uint8_t>((index * 3U) & 0xFFU),
            static_cast<std::uint8_t>(index == 0U ? 0U : 255U)}),
            "Unable to prepare test palette.");
    }

    VoxelGrid first;
    Require(first.Resize(3U, 2U, 2U), "Unable to create first test grid.");
    Require(first.Set(0U, 0U, 0U, {0U, Voxel::OccupiedFlag}),
        "Unable to set palette index zero voxel.");
    Require(first.Set(2U, 1U, 1U, {
        255U,
        static_cast<std::uint8_t>(Voxel::OccupiedFlag | (1U << 5U))}),
        "Unable to set reserved flags voxel.");
    Require(first.Set(1U, 0U, 1U, {87U, 1U << 6U}),
        "Unable to set empty flagged voxel.");
    model.AddGrid(std::move(first));

    VoxelGrid second;
    Require(second.Resize(1U, 1U, 1U), "Unable to create second test grid.");
    Require(second.Set(0U, 0U, 0U, {42U, Voxel::OccupiedFlag}),
        "Unable to set second test grid.");
    model.AddGrid(std::move(second));
    return model;
}

void RequireModelsEqual(const VoxelModel& expected, const VoxelModel& actual)
{
    Require(expected.Name() == actual.Name(), "Model name was not preserved.");
    Require(expected.Palette().Data() == actual.Palette().Data(),
        "Complete palette was not preserved.");
    Require(expected.GridCount() == actual.GridCount(),
        "Grid count was not preserved.");
    for (std::size_t index = 0; index < expected.GridCount(); ++index)
    {
        const VoxelGrid& left = expected.Grids()[index];
        const VoxelGrid& right = actual.Grids()[index];
        Require(left.Width() == right.Width() &&
                left.Height() == right.Height() &&
                left.Depth() == right.Depth(),
            "Grid dimensions were not preserved.");
        Require(left.Data() == right.Data(),
            "Voxel memory order, colors, or flags were not preserved.");
        Require(left.OccupiedVoxelCount() == right.OccupiedVoxelCount(),
            "Occupied voxel count was not restored.");
    }
}

void TestEmptyAndCompleteRoundTrips(const TemporaryDirectory& temporary)
{
    const auto emptyPath = temporary.Path() / "empty.vfvoxel";
    const VoxelModel empty;
    Require(VoxelModelSerializer::Save(emptyPath, empty).Succeeded,
        "Empty model save failed.");
    const auto emptyLoad = VoxelModelSerializer::Load(emptyPath);
    Require(emptyLoad.Model.has_value(), "Empty model load failed.");
    RequireModelsEqual(empty, *emptyLoad.Model);

    VoxelModel model = MakeModel();
    const auto firstPath = temporary.Path() / "complete.vfvoxel";
    const auto secondPath = temporary.Path() / "deterministic.vfvoxel";
    Require(VoxelModelSerializer::Save(firstPath, model).Succeeded,
        "Complete model save failed.");
    const VoxelDeserializationResult loaded =
        VoxelModelSerializer::Load(firstPath);
    Require(loaded.Model.has_value(), "Complete model load failed.");
    RequireModelsEqual(model, *loaded.Model);
    Require(loaded.Model->TotalOccupiedVoxelCount() == 3U,
        "Occupied count round-trip is incorrect.");

    Require(VoxelModelSerializer::Save(secondPath, model).Succeeded,
        "Deterministic comparison save failed.");
    Require(ReadBytes(firstPath) == ReadBytes(secondPath),
        "Identical models did not produce deterministic bytes.");
    Require(!std::filesystem::exists(firstPath.string() + ".tmp") &&
            !std::filesystem::exists(firstPath.string() + ".bak"),
        "Successful save left transaction files behind.");
}

void TestCorruptFiles(const TemporaryDirectory& temporary)
{
    using namespace VoxelForge::Voxel::VoxelFileFormat;
    const auto validPath = temporary.Path() / "valid.vfvoxel";
    Require(VoxelModelSerializer::Save(validPath, MakeModel()).Succeeded,
        "Unable to prepare corruption baseline.");
    const std::vector<std::uint8_t> valid = ReadBytes(validPath);

    const auto expectRejected = [&](std::string_view name,
                                    std::vector<std::uint8_t> bytes)
    {
        const auto path = temporary.Path() / std::string(name);
        WriteBytes(path, bytes);
        const auto result = VoxelModelSerializer::Load(path);
        Require(!result.Model && !result.Message.empty(),
            "Corrupt VFVOXEL file was accepted.");
    };

    auto bytes = valid;
    bytes[0] ^= 0xFFU;
    expectRejected("signature.vfvoxel", bytes);

    bytes = valid;
    bytes[8] = 2U;
    expectRejected("version.vfvoxel", bytes);

    expectRejected("header-truncated.vfvoxel",
        std::vector<std::uint8_t>(valid.begin(), valid.begin() + 12));

    const std::size_t nameByteCount = MakeModel().Name().size();
    const std::size_t paletteStart = 24U + 8U + nameByteCount + 8U;
    expectRejected("palette-truncated.vfvoxel",
        std::vector<std::uint8_t>(valid.begin(),
            valid.begin() + static_cast<std::ptrdiff_t>(paletteStart + 100U)));

    const std::size_t gridsHeader = paletteStart + 256U * 4U;
    const std::size_t firstGrid = gridsHeader + 8U;
    bytes = valid;
    WriteU32(bytes, firstGrid + 4U, MaximumGridDimension + 1U);
    expectRejected("dimension.vfvoxel", bytes);

    bytes = valid;
    WriteU32(bytes, gridsHeader + 4U, MaximumGridCount + 1U);
    expectRejected("grid-count.vfvoxel", bytes);

    bytes = valid;
    WriteU64(bytes, firstGrid + 16U, 11U);
    expectRejected("voxel-count.vfvoxel", bytes);

    expectRejected("grid-truncated.vfvoxel",
        std::vector<std::uint8_t>(valid.begin(), valid.end() - 1));

    bytes = valid;
    bytes.push_back(0x7FU);
    WriteU64(bytes, 16U, bytes.size());
    expectRejected("trailing.vfvoxel", bytes);

    const auto oversized = temporary.Path() / "oversized.vfvoxel";
    WriteBytes(oversized, {0U});
    std::error_code error;
    std::filesystem::resize_file(oversized, MaximumFileByteCount + 1U, error);
    Require(!error, "Unable to create sparse oversized test file.");
    Require(!VoxelModelSerializer::Load(oversized).Model,
        "Oversized VFVOXEL file was accepted.");
}

void TestTransactionalReplacement(const TemporaryDirectory& temporary)
{
    const auto path = temporary.Path() / "transaction.vfvoxel";
    VoxelModel oldModel = MakeModel();
    oldModel.SetName("Old model");
    Require(VoxelModelSerializer::Save(path, oldModel).Succeeded,
        "Initial transactional save failed.");
    const std::vector<std::uint8_t> oldBytes = ReadBytes(path);

    VoxelModel replacement = MakeModel();
    replacement.SetName("Replacement model");
    Require(VoxelModelSerializer::Save(path, replacement).Succeeded,
        "Transactional replacement failed.");
    const auto replacementLoad = VoxelModelSerializer::Load(path);
    Require(replacementLoad.Model &&
            replacementLoad.Model->Name() == "Replacement model",
        "Transactional replacement did not publish the new model.");
    Require(!std::filesystem::exists(path.string() + ".tmp") &&
            !std::filesystem::exists(path.string() + ".bak"),
        "Transactional replacement left auxiliary files.");

    WriteBytes(path, oldBytes);
    const auto staleBackup = std::filesystem::path(path.string() + ".bak");
    WriteBytes(staleBackup, {1U, 2U, 3U});
    const auto failed = VoxelModelSerializer::Save(path, replacement);
    Require(!failed.Succeeded && !failed.Message.empty(),
        "Unsafe replacement with stale backup was accepted.");
    Require(ReadBytes(path) == oldBytes,
        "Failed replacement changed the previous valid file.");
    Require(!std::filesystem::exists(path.string() + ".tmp"),
        "Failed replacement left a temporary file.");

    VoxelModel invalid = replacement;
    invalid.SetName(std::string("Bad UTF-8 \xC0\xAF", 12U));
    const auto invalidSave = VoxelModelSerializer::Save(path, invalid);
    Require(!invalidSave.Succeeded && ReadBytes(path) == oldBytes,
        "Preflight failure changed the previous file.");
}

} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporary;
        TestEmptyAndCompleteRoundTrips(temporary);
        TestCorruptFiles(temporary);
        TestTransactionalReplacement(temporary);
        std::cout << "Voxel model serializer tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel model serializer tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
