#pragma once

#include "AssetBrowser/AssetEntry.h"
#include "ModelImport/ModelAssetMetadataService.h"

#include <filesystem>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

enum class AssetInspectorKind
{
    None,
    Folder,
    VoxModel,
    OtherFile
};

struct AssetInspectorState final
{
    AssetInspectorKind Kind = AssetInspectorKind::None;
    std::string Name;
    std::filesystem::path RelativePath;
    std::string TypeLabel;
    std::string Dimensions;
    std::string VoxelCount;
    std::string ModelCount;
    std::string PaletteColors;
    std::string CustomPalette;
    std::string VoxVersion;
    std::string AssetId;
    std::string Importer;
    std::string ImporterVersion;
    std::string FileSize;
    std::string LastModified;
    std::string AnalysisError;
    std::optional<Asset::Vox::VoxModelAnalysis> Analysis;
};

class AssetInspectorViewModel final
{
public:
    [[nodiscard]] bool SetAssetsRoot(
        const std::filesystem::path& assetsRoot);
    void ClearProject() noexcept;
    [[nodiscard]] bool UpdateSelection(
        const std::optional<AssetEntry>& selectedEntry);
    [[nodiscard]] bool Reanalyze();

    [[nodiscard]] const AssetInspectorState& State() const noexcept;
    [[nodiscard]] const std::optional<AssetEntry>& Selection() const noexcept;

    [[nodiscard]] static std::string FormatDimensions(
        std::uint32_t x, std::uint32_t y, std::uint32_t z);
    [[nodiscard]] static std::string FormatInteger(std::uint64_t value);
    [[nodiscard]] static std::string FormatFileSize(std::uint64_t bytes);
    [[nodiscard]] static std::string FormatModifiedTime(
        std::filesystem::file_time_type value);

private:
    void BuildState(bool forceReanalysis);

    std::filesystem::path assetsRoot_;
    std::optional<AssetEntry> selectedEntry_;
    AssetInspectorState state_;
    ModelAssetMetadataService metadataService_;
};

} // namespace VoxelForge::Editor
