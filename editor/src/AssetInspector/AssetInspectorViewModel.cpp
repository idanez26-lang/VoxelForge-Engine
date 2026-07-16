#include "AssetInspectorViewModel.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace VoxelForge::Editor
{
namespace
{
std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const char c)
    {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
    });
    return value;
}
}

bool AssetInspectorViewModel::SetAssetsRoot(
    const std::filesystem::path& assetsRoot)
{
    if (assetsRoot.empty()) return false;
    std::error_code error;
    const auto absolute =
        std::filesystem::absolute(assetsRoot, error).lexically_normal();
    if (error || !std::filesystem::is_directory(absolute, error) || error ||
        !metadataService_.SetModelsDirectory(absolute / "Models"))
        return false;
    if (assetsRoot_ != absolute)
    {
        assetsRoot_ = absolute;
        selectedEntry_.reset();
        state_ = {};
    }
    return true;
}

void AssetInspectorViewModel::ClearProject() noexcept
{
    assetsRoot_.clear();
    selectedEntry_.reset();
    state_ = {};
    metadataService_.ClearModelsDirectory();
}

bool AssetInspectorViewModel::UpdateSelection(
    const std::optional<AssetEntry>& selectedEntry)
{
    const bool sameSelection = selectedEntry_ && selectedEntry &&
        selectedEntry_->RelativePath() == selectedEntry->RelativePath() &&
        selectedEntry_->FileSize() == selectedEntry->FileSize() &&
        selectedEntry_->LastWriteTime() == selectedEntry->LastWriteTime();
    if ((!selectedEntry_ && !selectedEntry) || sameSelection) return false;
    selectedEntry_ = selectedEntry;
    BuildState(false);
    return true;
}

bool AssetInspectorViewModel::Reanalyze()
{
    if (!selectedEntry_ || state_.Kind != AssetInspectorKind::VoxModel)
        return false;
    BuildState(true);
    return state_.Analysis.has_value() && state_.Analysis->Valid;
}

const AssetInspectorState& AssetInspectorViewModel::State() const noexcept
{
    return state_;
}

const std::optional<AssetEntry>&
AssetInspectorViewModel::Selection() const noexcept
{
    return selectedEntry_;
}

std::string AssetInspectorViewModel::FormatDimensions(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z)
{
    return std::to_string(x) + " \xC3\x97 " + std::to_string(y) +
        " \xC3\x97 " + std::to_string(z);
}

std::string AssetInspectorViewModel::FormatInteger(std::uint64_t value)
{
    std::string result = std::to_string(value);
    for (std::ptrdiff_t position =
             static_cast<std::ptrdiff_t>(result.size()) - 3;
         position > 0;
         position -= 3)
        result.insert(static_cast<std::size_t>(position), ",");
    return result;
}

std::string AssetInspectorViewModel::FormatFileSize(const std::uint64_t bytes)
{
    constexpr double Kilo = 1024.0;
    constexpr double Mega = Kilo * Kilo;
    constexpr double Giga = Mega * Kilo;
    std::ostringstream output;
    if (bytes < 1024U) return std::to_string(bytes) + " B";
    output << std::fixed << std::setprecision(2);
    if (static_cast<double>(bytes) < Mega)
        output << static_cast<double>(bytes) / Kilo << " KB";
    else if (static_cast<double>(bytes) < Giga)
        output << static_cast<double>(bytes) / Mega << " MB";
    else output << static_cast<double>(bytes) / Giga << " GB";
    return output.str();
}

std::string AssetInspectorViewModel::FormatModifiedTime(
    const std::filesystem::file_time_type value)
{
    const auto systemTime = std::chrono::time_point_cast<
        std::chrono::system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time) != 0) return "Unavailable";
#else
    if (localtime_r(&time, &local) == nullptr) return "Unavailable";
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void AssetInspectorViewModel::BuildState(const bool forceReanalysis)
{
    state_ = {};
    if (!selectedEntry_) return;
    const AssetEntry& entry = *selectedEntry_;
    state_.Name = entry.Name();
    state_.RelativePath = entry.RelativePath();
    state_.FileSize = entry.FileSize()
        ? FormatFileSize(static_cast<std::uint64_t>(*entry.FileSize())) : "Unavailable";
    state_.LastModified = entry.LastWriteTime()
        ? FormatModifiedTime(*entry.LastWriteTime()) : "Unavailable";
    if (entry.IsDirectory())
    {
        state_.Kind = AssetInspectorKind::Folder;
        state_.TypeLabel = "Folder";
        return;
    }
    if (LowerAscii(entry.Extension()) != ".vox")
    {
        state_.Kind = AssetInspectorKind::OtherFile;
        state_.TypeLabel = "File";
        return;
    }
    state_.Kind = AssetInspectorKind::VoxModel;
    state_.TypeLabel = "Voxel Model";
    const MetadataAnalysisResult analyzed =
        metadataService_.AnalyzeAndUpdateMetadata(
            entry.AbsolutePath(), forceReanalysis);
    const MetadataReadResult metadata = metadataService_.ReadMetadata(
        metadataService_.MetadataPathFor(entry.AbsolutePath()));
    if (metadata.Succeeded)
    {
        state_.AssetId = metadata.Metadata.AssetId;
        state_.Importer = metadata.Metadata.Importer;
        state_.ImporterVersion =
            std::to_string(metadata.Metadata.ImporterVersion);
    }
    state_.Analysis = analyzed.Metadata.Analysis
        ? analyzed.Metadata.Analysis
        : metadata.Succeeded ? metadata.Metadata.Analysis : std::nullopt;
    if (!state_.Analysis)
    {
        state_.AnalysisError = analyzed.Message;
        return;
    }
    const auto& analysis = *state_.Analysis;
    if (!analysis.Valid)
    {
        state_.AnalysisError = analysis.Error;
        return;
    }
    state_.Dimensions = FormatDimensions(
        analysis.SizeX, analysis.SizeY, analysis.SizeZ);
    state_.VoxelCount = FormatInteger(analysis.VoxelCount);
    state_.ModelCount = FormatInteger(analysis.ModelCount);
    state_.PaletteColors = FormatInteger(analysis.UsedPaletteColorCount);
    state_.CustomPalette = analysis.HasCustomPalette ? "Yes" : "No";
    state_.VoxVersion = std::to_string(analysis.FormatVersion);
}

} // namespace VoxelForge::Editor
