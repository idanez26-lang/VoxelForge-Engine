#include "ModelAssetMetadataService.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::uint32_t CurrentFormatVersion = 1U;
constexpr std::uint32_t CurrentImporterVersion = 1U;

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const char value)
    {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value + ('a' - 'A')) : value;
    });
    return value;
}

std::string EscapeValue(const std::string& value)
{
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char character : value)
    {
        if (character == '%' || character == '=' || character == '\r' ||
            character == '\n')
        {
            output << '%' << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned int>(character);
        }
        else
        {
            output << static_cast<char>(character);
        }
    }
    return output.str();
}

bool UnescapeValue(const std::string& value, std::string& decoded)
{
    decoded.clear();
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (value[index] != '%')
        {
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2U >= value.size()) return false;
        unsigned int byte = 0U;
        const char* first = value.data() + index + 1U;
        const auto parsed = std::from_chars(first, first + 2U, byte, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != first + 2U) return false;
        decoded.push_back(static_cast<char>(byte));
        index += 2U;
    }
    return true;
}

template<typename Value>
bool ParseInteger(const std::string& text, Value& value)
{
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool MetadataEquals(
    const ModelAssetMetadata& left,
    const ModelAssetMetadata& right) noexcept
{
    return left.FormatVersion == right.FormatVersion &&
        left.AssetId == right.AssetId && left.AssetType == right.AssetType &&
        left.SourceFile == right.SourceFile &&
        left.SourceExtension == right.SourceExtension &&
        left.Importer == right.Importer &&
        left.ImporterVersion == right.ImporterVersion &&
        left.FileSize == right.FileSize &&
        left.SourceModifiedTime == right.SourceModifiedTime;
}

std::string DefaultAssetId()
{
    std::random_device device;
    std::mt19937_64 generator(
        (static_cast<std::uint64_t>(device()) << 32U) ^ device());
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16) << generator()
           << std::setw(16) << generator();
    return output.str();
}
}

bool MetadataOperationResult::Succeeded() const noexcept
{
    return Status != MetadataEnsureStatus::Failed;
}

ModelAssetMetadataService::ModelAssetMetadataService(
    AssetIdGenerator assetIdGenerator,
    BeforeInstallCallback beforeInstall)
    : assetIdGenerator_(std::move(assetIdGenerator)),
      beforeInstall_(std::move(beforeInstall))
{
}

bool ModelAssetMetadataService::SetModelsDirectory(
    const std::filesystem::path& modelsDirectory)
{
    if (modelsDirectory.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(modelsDirectory, error);
    if (error) return false;
    const auto canonical = std::filesystem::weakly_canonical(modelsDirectory, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
        return false;
    modelsDirectory_ = canonical;
    issuedAssetIds_.clear();
    std::filesystem::directory_iterator iterator(modelsDirectory_, error);
    if (!error)
    {
        for (const auto& entry : iterator)
        {
            if (!IsMetadataFile(entry.path())) continue;
            const MetadataReadResult existing = ReadMetadata(entry.path());
            if (existing.Succeeded && IsValidAssetId(existing.Metadata.AssetId))
                issuedAssetIds_.insert(LowerAscii(existing.Metadata.AssetId));
        }
    }
    return true;
}

void ModelAssetMetadataService::ClearModelsDirectory() noexcept
{
    modelsDirectory_.clear();
    issuedAssetIds_.clear();
}

MetadataOperationResult ModelAssetMetadataService::CreateMetadata(
    const std::filesystem::path& modelPath)
{
    std::string error;
    const ModelAssetMetadata metadata = BuildMetadata(
        modelPath, GenerateAssetId(), error);
    if (!error.empty() || !WriteMetadata(modelPath, metadata, error))
        return {MetadataEnsureStatus::Failed, {}, std::move(error)};
    return {MetadataEnsureStatus::Created, metadata, "Metadata created."};
}

MetadataReadResult ModelAssetMetadataService::ReadMetadata(
    const std::filesystem::path& metadataPath) const
{
    if (!IsMetadataFile(metadataPath))
        return {false, {}, "Metadata path must end with .vfmeta."};
    std::error_code pathError;
    const auto absoluteMetadata =
        std::filesystem::absolute(metadataPath, pathError).lexically_normal();
    if (pathError || (!modelsDirectory_.empty() &&
            absoluteMetadata.parent_path() != modelsDirectory_))
        return {false, {}, "Metadata path must stay inside Assets/Models."};
    const auto linkStatus = std::filesystem::symlink_status(
        absoluteMetadata, pathError);
    if (pathError || std::filesystem::is_symlink(linkStatus) ||
        !std::filesystem::is_regular_file(linkStatus))
        return {false, {}, "Metadata must be a regular, non-symbolic file."};
    std::ifstream input(absoluteMetadata, std::ios::binary);
    if (!input) return {false, {}, "Metadata file cannot be opened."};
    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
            return {false, {}, "Malformed metadata line."};
        std::string decoded;
        if (!UnescapeValue(line.substr(separator + 1U), decoded) ||
            !values.emplace(line.substr(0U, separator), std::move(decoded)).second)
            return {false, {}, "Invalid or duplicate metadata field."};
    }
    static constexpr std::array<const char*, 9U> RequiredFields{
        "format_version", "asset_id", "asset_type", "source_file",
        "source_extension", "importer", "importer_version", "file_size",
        "source_modified_time"};
    for (const char* field : RequiredFields)
        if (!values.contains(field))
            return {false, {}, std::string("Missing metadata field: ") + field};
    ModelAssetMetadata result;
    if (!ParseInteger(values["format_version"], result.FormatVersion) ||
        !ParseInteger(values["importer_version"], result.ImporterVersion) ||
        !ParseInteger(values["file_size"], result.FileSize) ||
        !ParseInteger(values["source_modified_time"], result.SourceModifiedTime))
        return {false, {}, "Invalid numeric metadata field."};
    result.AssetId = values["asset_id"];
    result.AssetType = values["asset_type"];
    result.SourceFile = values["source_file"];
    result.SourceExtension = values["source_extension"];
    result.Importer = values["importer"];
    return {true, std::move(result), {}};
}

bool ModelAssetMetadataService::WriteMetadata(
    const std::filesystem::path& modelPath,
    const ModelAssetMetadata& metadata,
    std::string& errorMessage) const
{
    errorMessage.clear();
    std::filesystem::path resolved;
    if (!ResolveModelPath(modelPath, resolved, errorMessage)) return false;
    if (!ValidateMetadata(metadata, resolved, errorMessage)) return false;
    const std::filesystem::path destination = MetadataPathFor(resolved);
    const std::filesystem::path temporary = destination.string() + ".tmp";
    const std::filesystem::path backup = destination.string() + ".bak";
    std::error_code error;
    if (std::filesystem::exists(temporary, error) || error ||
        std::filesystem::exists(backup, error) || error)
    {
        errorMessage = "A metadata temporary or backup file already exists.";
        return false;
    }
    const bool destinationPresent = std::filesystem::exists(destination, error);
    if (error)
    {
        errorMessage = "Unable to inspect existing metadata.";
        return false;
    }
    const auto destinationStatus = destinationPresent
        ? std::filesystem::symlink_status(destination, error)
        : std::filesystem::file_status{};
    if (error)
    {
        errorMessage = "Unable to inspect existing metadata.";
        return false;
    }
    if (destinationPresent &&
        (std::filesystem::is_symlink(destinationStatus) ||
         !std::filesystem::is_regular_file(destinationStatus)))
    {
        errorMessage = "Existing metadata is not a safe regular file.";
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { errorMessage = "Unable to create metadata temporary file."; return false; }
        output << "# VoxelForge Asset Metadata\n"
               << "format_version=" << metadata.FormatVersion << '\n'
               << "asset_id=" << EscapeValue(metadata.AssetId) << '\n'
               << "asset_type=" << EscapeValue(metadata.AssetType) << '\n'
               << "source_file=" << EscapeValue(metadata.SourceFile) << '\n'
               << "source_extension=" << EscapeValue(metadata.SourceExtension) << '\n'
               << "importer=" << EscapeValue(metadata.Importer) << '\n'
               << "importer_version=" << metadata.ImporterVersion << '\n'
               << "file_size=" << metadata.FileSize << '\n'
               << "source_modified_time=" << metadata.SourceModifiedTime << '\n';
        if (!output) { errorMessage = "Unable to write metadata temporary file."; }
    }
    if (errorMessage.empty() && beforeInstall_ && !beforeInstall_())
        errorMessage = "Metadata installation was rejected.";
    if (!errorMessage.empty())
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    const bool hadDestination = std::filesystem::exists(destination, error);
    if (error) { std::filesystem::remove(temporary, error); errorMessage = "Unable to inspect metadata destination."; return false; }
    if (hadDestination)
    {
        std::filesystem::rename(destination, backup, error);
        if (error) { std::filesystem::remove(temporary, error); errorMessage = "Unable to back up existing metadata."; return false; }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error)
    {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        if (hadDestination) std::filesystem::rename(backup, destination, cleanup);
        errorMessage = "Unable to install metadata: " + error.message();
        return false;
    }
    if (hadDestination)
    {
        std::filesystem::remove(backup, error);
        if (error) { errorMessage = "Metadata installed, but backup cleanup failed."; return false; }
    }
    return true;
}

bool ModelAssetMetadataService::ValidateMetadata(
    const ModelAssetMetadata& metadata,
    const std::filesystem::path& modelPath,
    std::string& errorMessage) const
{
    if (metadata.FormatVersion != CurrentFormatVersion)
        errorMessage = "Unsupported metadata format version.";
    else if (!IsValidAssetId(metadata.AssetId))
        errorMessage = "Invalid asset id.";
    else if (metadata.AssetType != "voxel_model" ||
        metadata.SourceFile != modelPath.filename().string() ||
        LowerAscii(metadata.SourceExtension) != ".vox" ||
        metadata.Importer != "vox" ||
        metadata.ImporterVersion != CurrentImporterVersion)
        errorMessage = "Metadata does not describe this VOX model.";
    else
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(modelPath, error);
        if (error || metadata.FileSize != size)
            errorMessage = "Metadata file size is stale.";
        else
        {
            const auto modified = std::filesystem::last_write_time(modelPath, error);
            if (error || metadata.SourceModifiedTime != static_cast<std::int64_t>(
                    modified.time_since_epoch().count()))
                errorMessage = "Metadata modification time is stale.";
            else return true;
        }
    }
    return false;
}

MetadataOperationResult ModelAssetMetadataService::EnsureMetadata(
    const std::filesystem::path& modelPath)
{
    std::string error;
    std::filesystem::path resolved;
    if (!ResolveModelPath(modelPath, resolved, error))
        return {MetadataEnsureStatus::Failed, {}, std::move(error)};
    std::error_code existenceError;
    const bool metadataExisted = std::filesystem::exists(
        MetadataPathFor(resolved), existenceError);
    if (existenceError)
        return {MetadataEnsureStatus::Failed, {},
            "Unable to inspect existing metadata."};
    const auto existing = ReadMetadata(MetadataPathFor(resolved));
    std::string assetId = existing.Succeeded && IsValidAssetId(existing.Metadata.AssetId)
        ? existing.Metadata.AssetId : GenerateAssetId();
    ModelAssetMetadata expected = BuildMetadata(resolved, std::move(assetId), error);
    if (!error.empty()) return {MetadataEnsureStatus::Failed, {}, std::move(error)};
    if (existing.Succeeded)
    {
        std::string validationError;
        if (ValidateMetadata(existing.Metadata, resolved, validationError) &&
            MetadataEquals(existing.Metadata, expected))
            return {MetadataEnsureStatus::Unchanged, existing.Metadata, "Metadata unchanged."};
    }
    if (!WriteMetadata(resolved, expected, error))
        return {MetadataEnsureStatus::Failed, {}, std::move(error)};
    return {metadataExisted ? MetadataEnsureStatus::Repaired :
        MetadataEnsureStatus::Created, expected,
        metadataExisted ? "Metadata repaired." : "Metadata created."};
}

MetadataRebuildReport ModelAssetMetadataService::RebuildMetadata()
{
    MetadataRebuildReport report;
    if (modelsDirectory_.empty()) { report.Errors = 1U; report.ErrorMessages.emplace_back("No Models directory is configured."); return report; }
    std::error_code error;
    std::filesystem::directory_iterator iterator(modelsDirectory_, error);
    if (error) { report.Errors = 1U; report.ErrorMessages.push_back(error.message()); return report; }
    for (const auto& entry : iterator)
    {
        const auto status = entry.symlink_status(error);
        if (error) { ++report.Errors; report.ErrorMessages.push_back(error.message()); error.clear(); continue; }
        if (std::filesystem::is_symlink(status) || !entry.is_regular_file(error) ||
            LowerAscii(entry.path().extension().string()) != ".vox")
        { ++report.Ignored; error.clear(); continue; }
        const MetadataOperationResult result = EnsureMetadata(entry.path());
        switch (result.Status)
        {
        case MetadataEnsureStatus::Created: ++report.Created; break;
        case MetadataEnsureStatus::Unchanged: ++report.Unchanged; break;
        case MetadataEnsureStatus::Repaired: ++report.Repaired; break;
        case MetadataEnsureStatus::Failed: ++report.Errors; report.ErrorMessages.push_back(result.Message); break;
        }
    }
    return report;
}

std::filesystem::path ModelAssetMetadataService::MetadataPathFor(
    const std::filesystem::path& modelPath) const
{
    return std::filesystem::path(modelPath.string() + ".vfmeta");
}

const std::filesystem::path& ModelAssetMetadataService::ModelsDirectory() const noexcept
{
    return modelsDirectory_;
}

bool ModelAssetMetadataService::IsMetadataFile(
    const std::filesystem::path& path) noexcept
{
    return LowerAscii(path.extension().string()) == ".vfmeta";
}

bool ModelAssetMetadataService::IsValidAssetId(const std::string& assetId) noexcept
{
    return assetId.size() == 32U && std::all_of(assetId.begin(), assetId.end(), [](const char value)
    {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F');
    });
}

bool ModelAssetMetadataService::ResolveModelPath(
    const std::filesystem::path& modelPath,
    std::filesystem::path& resolvedPath,
    std::string& errorMessage) const
{
    if (modelsDirectory_.empty()) { errorMessage = "No Models directory is configured."; return false; }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(modelPath, error).lexically_normal();
    if (error || absolute.parent_path() != modelsDirectory_)
    { errorMessage = "Model must be a direct child of Assets/Models."; return false; }
    const auto linkStatus = std::filesystem::symlink_status(absolute, error);
    if (error || std::filesystem::is_symlink(linkStatus) ||
        !std::filesystem::is_regular_file(linkStatus) ||
        LowerAscii(absolute.extension().string()) != ".vox")
    { errorMessage = "Model must be a regular, non-symbolic .vox file."; return false; }
    const auto canonical = std::filesystem::weakly_canonical(absolute, error);
    if (error || canonical.parent_path() != modelsDirectory_)
    { errorMessage = "Model path escapes Assets/Models."; return false; }
    resolvedPath = canonical;
    return true;
}

ModelAssetMetadata ModelAssetMetadataService::BuildMetadata(
    const std::filesystem::path& modelPath,
    std::string assetId,
    std::string& errorMessage) const
{
    ModelAssetMetadata metadata;
    std::filesystem::path resolved;
    if (!ResolveModelPath(modelPath, resolved, errorMessage)) return metadata;
    std::error_code error;
    metadata.AssetId = std::move(assetId);
    metadata.SourceFile = resolved.filename().string();
    metadata.SourceExtension = LowerAscii(resolved.extension().string());
    metadata.FileSize = std::filesystem::file_size(resolved, error);
    if (error) { errorMessage = "Unable to read model size: " + error.message(); return {}; }
    const auto time = std::filesystem::last_write_time(resolved, error);
    if (error) { errorMessage = "Unable to read model modification time: " + error.message(); return {}; }
    metadata.SourceModifiedTime = static_cast<std::int64_t>(time.time_since_epoch().count());
    return metadata;
}

std::string ModelAssetMetadataService::GenerateAssetId() const
{
    for (std::size_t attempt = 0U; attempt < 32U; ++attempt)
    {
        const std::string generated =
            assetIdGenerator_ ? assetIdGenerator_() : DefaultAssetId();
        if (!IsValidAssetId(generated)) continue;
        const std::string normalized = LowerAscii(generated);
        if (issuedAssetIds_.insert(normalized).second) return normalized;
    }
    for (;;)
    {
        const std::string generated = DefaultAssetId();
        if (issuedAssetIds_.insert(generated).second) return generated;
    }
}
} // namespace VoxelForge::Editor
