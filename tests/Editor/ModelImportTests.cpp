#include "ModelImport/ModelImportService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeModelImport-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bool Check(const bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (path.extension() == ".vox")
    {
        const auto writeU32 = [&output](const std::uint32_t value)
        {
            for (unsigned int shift = 0U; shift < 32U; shift += 8U)
                output.put(static_cast<char>(value >> shift));
        };
        const std::uint32_t junkSize = static_cast<std::uint32_t>(content.size());
        const std::uint32_t childrenSize =
            12U + junkSize + 24U + 20U;
        output.write("VOX ", 4); writeU32(150U);
        output.write("MAIN", 4); writeU32(0U); writeU32(childrenSize);
        output.write("JUNK", 4); writeU32(junkSize); writeU32(0U);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.write("SIZE", 4); writeU32(12U); writeU32(0U);
        writeU32(2U); writeU32(2U); writeU32(2U);
        output.write("XYZI", 4); writeU32(8U); writeU32(0U);
        writeU32(1U); output.put(0); output.put(0); output.put(0); output.put(1);
    }
    else
    {
        output << content;
    }
    return static_cast<bool>(output);
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}
}

int main()
{
    TemporaryDirectory temporary;
    const auto project = temporary.Path() / "Project";
    const auto sources = temporary.Path() / "Sources";
    std::filesystem::create_directories(project / "Assets");
    std::filesystem::create_directories(sources);

    ModelImportService service;
    std::size_t refreshCount = 0U;
    service.SetRefreshCallback([&refreshCount]() { ++refreshCount; });
    if (!Check(service.SetProjectRoot(project), "Project root setup failed."))
        return 1;

    const auto castle = sources / "castle.vox";
    if (!WriteFile(castle, "castle-v1")) return 1;
    const std::string castleV1 = ReadFile(castle);
    const ModelImportResult simple = service.ImportModel(castle);
    const auto importedCastle = project / "Assets" / "Models" / "castle.vox";
    if (!Check(simple.Succeeded() &&
            simple.Status == ModelImportStatus::Imported &&
            simple.DestinationPath == importedCastle &&
            std::filesystem::is_regular_file(importedCastle) &&
            ReadFile(importedCastle) == castleV1 && refreshCount == 1U,
            "Simple import, Models creation, or refresh failed.")) return 1;

    const auto tree = sources / "tree.vox";
    const auto door = sources / "door.vox";
    WriteFile(tree, "tree");
    WriteFile(door, "door");
    const auto multiple = service.ImportModels({tree, door});
    if (!Check(multiple.size() == 2U && multiple[0].Succeeded() &&
            multiple[1].Succeeded() && refreshCount == 2U &&
            std::filesystem::is_regular_file(
                service.ModelsDirectory() / "tree.vox") &&
            std::filesystem::is_regular_file(
                service.ModelsDirectory() / "door.vox"),
            "Multiple import or single batch refresh failed.")) return 1;

    WriteFile(castle, "castle-v2");
    const std::string castleV2 = ReadFile(castle);
    const ModelImportResult collision = service.ImportModel(castle);
    if (!Check(collision.Status == ModelImportStatus::Collision &&
            ReadFile(importedCastle) == castleV1 && refreshCount == 2U,
            "Collision prompt state changed the destination.")) return 1;

    const ModelImportResult renamed = service.ImportModel(
        castle, ModelImportCollisionAction::Rename);
    const ModelImportResult renamedAgain = service.ImportModel(
        castle, ModelImportCollisionAction::Rename);
    if (!Check(renamed.Status == ModelImportStatus::Renamed &&
            renamed.DestinationPath.filename() == "castle (1).vox" &&
            renamedAgain.DestinationPath.filename() == "castle (2).vox",
            "Collision rename sequence is incorrect.")) return 1;

    const std::size_t refreshBeforeReplace = refreshCount;
    const ModelImportResult replaced = service.ImportModel(
        castle, ModelImportCollisionAction::Replace);
    if (!Check(replaced.Status == ModelImportStatus::Replaced &&
            ReadFile(importedCastle) == castleV2 &&
            refreshCount == refreshBeforeReplace + 1U,
            "Replace did not overwrite and refresh.")) return 1;

    WriteFile(castle, "castle-v3");
    const std::size_t refreshBeforeIgnore = refreshCount;
    const ModelImportResult skipped = service.ImportModel(
        castle, ModelImportCollisionAction::Skip);
    const ModelImportResult cancelled = service.ImportModel(
        castle, ModelImportCollisionAction::Cancel);
    if (!Check(skipped.Status == ModelImportStatus::Skipped &&
            cancelled.Status == ModelImportStatus::Cancelled &&
            ReadFile(importedCastle) == castleV2 &&
            refreshCount == refreshBeforeIgnore,
            "Skip or cancel modified assets.")) return 1;

    if (!Check(ModelImportService::ClassifyFormat("model.vfvoxel") ==
                ModelImportFormat::VoxelForgeVoxel &&
            ModelImportService::ClassifyFormat("model.qb") ==
                ModelImportFormat::Qubicle &&
            ModelImportService::ClassifyFormat("model.obj") ==
                ModelImportFormat::WavefrontObj &&
            !ModelImportService::IsFormatEnabled(ModelImportFormat::Qubicle),
            "Prepared format classification is incorrect.")) return 1;

    for (int index = 0; index < 11; ++index)
    {
        const auto source = sources /
            ("recent" + std::to_string(index) + ".vox");
        WriteFile(source, std::to_string(index));
        if (!service.ImportModel(source).Succeeded()) return 1;
    }
    if (!Check(service.RecentImports().size() ==
            ModelImportService::MaximumRecentImports &&
            service.RecentImports().front().filename() == "recent10.vox" &&
            service.RecentImports().back().filename() == "recent1.vox",
            "Session recent imports are not capped at ten.")) return 1;

    ModelImportService missingProject;
    if (!Check(!missingProject.ImportModel(castle).Succeeded(),
            "Import without a project must fail.")) return 1;
    return 0;
}
