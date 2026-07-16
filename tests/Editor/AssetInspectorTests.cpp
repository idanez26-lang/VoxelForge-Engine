#include "AssetInspector/AssetInspectorViewModel.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        path_ = fs::temp_directory_path() /
            ("VoxelForgeAssetInspector-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& Path() const noexcept { return path_; }
private:
    fs::path path_;
};

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void U32(Bytes& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void Append(Bytes& destination, const Bytes& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

Bytes Chunk(const char* id, const Bytes& content = {},
    const Bytes& children = {})
{
    Bytes result(id, id + 4); U32(result, static_cast<std::uint32_t>(content.size()));
    U32(result, static_cast<std::uint32_t>(children.size()));
    Append(result, content); Append(result, children); return result;
}

Bytes Model(std::uint32_t x, std::uint32_t y, std::uint32_t z,
    std::uint8_t color)
{
    Bytes size; U32(size, x); U32(size, y); U32(size, z);
    Bytes xyzi; U32(xyzi, 1U);
    xyzi.insert(xyzi.end(), {0U, 0U, 0U, color});
    Bytes result = Chunk("SIZE", size); Append(result, Chunk("XYZI", xyzi));
    return result;
}

Bytes Vox(const Bytes& children)
{
    Bytes result{'V', 'O', 'X', ' '}; U32(result, 150U);
    Append(result, Chunk("MAIN", {}, children)); return result;
}

void Write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

AssetEntry Entry(const fs::path& absolute, const fs::path& assets)
{
    std::error_code error;
    const bool directory = fs::is_directory(absolute, error);
    return AssetEntry(
        absolute.filename().string(), absolute,
        absolute.lexically_relative(assets),
        directory ? AssetEntryType::Directory : AssetEntryType::File,
        directory ? std::string{} : absolute.extension().string(),
        directory ? std::nullopt : std::optional<std::uintmax_t>(
            fs::file_size(absolute)),
        fs::last_write_time(absolute));
}
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        const fs::path project = temporary.Path() / "Project";
        const fs::path assets = project / "Assets";
        const fs::path models = assets / "Models";
        fs::create_directories(models);
        AssetInspectorViewModel inspector;
        Require(inspector.SetAssetsRoot(assets), "Assets root setup failed.");
        Require(!inspector.UpdateSelection(std::nullopt) &&
            inspector.State().Kind == AssetInspectorKind::None,
            "Empty selection state failed.");

        Require(inspector.UpdateSelection(Entry(models, assets)) &&
            inspector.State().Kind == AssetInspectorKind::Folder &&
            inspector.State().Name == "Models" &&
            inspector.State().RelativePath == "Models",
            "Folder state failed.");

        Bytes packedContent; Bytes packCount; U32(packCount, 2U);
        Append(packedContent, Chunk("PACK", packCount));
        Append(packedContent, Model(12U, 8U, 6U, 3U));
        Append(packedContent, Model(4U, 5U, 7U, 4U));
        const fs::path castle = models / "castle.vox";
        Write(castle, Vox(packedContent));
        Require(inspector.UpdateSelection(Entry(castle, assets)),
            "VOX selection did not update.");
        const AssetInspectorState valid = inspector.State();
        Require(valid.Kind == AssetInspectorKind::VoxModel && valid.Analysis &&
            valid.Analysis->Valid && valid.Analysis->Models.size() == 2U &&
            valid.Dimensions == "12 × 8 × 6" && valid.VoxelCount == "2" &&
            valid.ModelCount == "2" && valid.PaletteColors == "2" &&
            valid.CustomPalette == "No" && valid.VoxVersion == "150" &&
            !valid.AssetId.empty(), "Valid/multi-model Inspector state failed.");

        Require(AssetInspectorViewModel::FormatDimensions(1U, 2U, 3U) ==
                "1 × 2 × 3" &&
            AssetInspectorViewModel::FormatInteger(184532U) == "184,532" &&
            AssetInspectorViewModel::FormatFileSize(1499464U) == "1.43 MB" &&
            !AssetInspectorViewModel::FormatModifiedTime(
                fs::last_write_time(castle)).empty(),
            "Inspector formatting failed.");

        const fs::path invalid = models / "invalid.vox";
        Write(invalid, {'B', 'A', 'D'});
        Require(inspector.UpdateSelection(Entry(invalid, assets)) &&
            inspector.State().Kind == AssetInspectorKind::VoxModel &&
            !inspector.State().AnalysisError.empty(),
            "Invalid model state failed.");

        Require(inspector.UpdateSelection(Entry(castle, assets)) &&
            inspector.State().Name == "castle.vox",
            "Selection change failed.");
        const std::string assetId = inspector.State().AssetId;
        Require(inspector.Reanalyze() && inspector.State().AssetId == assetId,
            "Explicit reanalysis failed.");

        const fs::path renamed = models / "fortress.vox";
        fs::rename(castle, renamed);
        fs::rename(castle.string() + ".vfmeta",
            renamed.string() + ".vfmeta");
        ModelAssetMetadataService metadata;
        Require(metadata.SetModelsDirectory(models),
            "Metadata service setup failed after rename.");
        Require(metadata.EnsureMetadata(renamed).Succeeded(),
            "Renamed metadata update failed.");
        Require(inspector.UpdateSelection(Entry(renamed, assets)) &&
            inspector.State().Name == "fortress.vox" &&
            inspector.State().AssetId == assetId &&
            inspector.State().Dimensions == "12 × 8 × 6",
            "Rename state did not preserve analysis or id.");

        fs::remove(renamed);
        fs::remove(renamed.string() + ".vfmeta");
        Require(inspector.UpdateSelection(std::nullopt) &&
            inspector.State().Kind == AssetInspectorKind::None,
            "Deleted selection was not cleared.");

        inspector.ClearProject();
        Require(inspector.State().Kind == AssetInspectorKind::None &&
            !inspector.Selection(), "Project close retained state.");
        const fs::path secondAssets = temporary.Path() / "Second" / "Assets";
        fs::create_directories(secondAssets / "Models");
        Require(inspector.SetAssetsRoot(secondAssets) &&
            inspector.State().Kind == AssetInspectorKind::None,
            "Project change retained previous state.");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
