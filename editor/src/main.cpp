#include "EditorLayer.h"
#include "EditorWindowTitle.h"

#include "VoxelForge/Core/Application.h"
#include "VoxelForge/Core/ApplicationSpecification.h"
#include "VoxelForge/Core/Window/Window.h"
#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Project/RecentProjects.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t SmokeTestFrameCount = 5;
constexpr std::size_t ViewportSmokeTestFrameCount = 30;
constexpr std::size_t VoxelSelectionSmokeTestFrameCount = 30;
constexpr std::size_t EraseVoxelSmokeTestFrameCount = 35;
constexpr std::size_t PaintVoxelSmokeTestFrameCount = 35;
constexpr std::size_t VoxelSaveSmokeTestFrameCount = 30;
constexpr std::size_t AddVoxelSmokeTestFrameCount = 40;
constexpr std::size_t ModelImportSmokeTestFrameCount = 30;
constexpr std::size_t DragDropImportSmokeTestFrameCount = 20;
constexpr std::size_t VoxelDocumentSmokeTestFrameCount = 20;
constexpr std::size_t QualityOfLifeSmokeTestFrameCount = 8;

struct CommandLine final
{
    bool SmokeTest = false;
    bool ViewportSmokeTest = false;
    bool ViewportVisualTest = false;
    bool VoxelSelectionSmokeTest = false;
    bool VoxelSelectionVisualTest = false;
    bool EraseVoxelSmokeTest = false;
    bool EraseVoxelVisualTest = false;
    bool PaintVoxelSmokeTest = false;
    bool PaintVoxelVisualTest = false;
    bool VoxelSaveSmokeTest = false;
    bool AddVoxelSmokeTest = false;
    bool ModelImportSmokeTest = false;
    bool ModelImportVisualTest = false;
    bool DragDropImportSmokeTest = false;
    bool VoxelDocumentSmokeTest = false;
    bool QualityOfLifeSmokeTest = false;
};

CommandLine ParseCommandLine(const int count, char* arguments[])
{
    CommandLine result;
    for (int index = 1; index < count; ++index)
    {
        const std::string_view argument(arguments[index]);
        result.SmokeTest |= argument == "--smoke-test";
        result.ViewportSmokeTest |= argument == "--viewport-smoke-test";
        result.ViewportVisualTest |= argument == "--viewport-visual-test";
        result.VoxelSelectionSmokeTest |=
            argument == "--voxel-selection-smoke-test";
        result.VoxelSelectionVisualTest |=
            argument == "--voxel-selection-visual-test";
        result.EraseVoxelSmokeTest |=
            argument == "--erase-voxel-smoke-test";
        result.EraseVoxelVisualTest |=
            argument == "--erase-voxel-visual-test";
        result.PaintVoxelSmokeTest |=
            argument == "--paint-voxel-smoke-test";
        result.PaintVoxelVisualTest |=
            argument == "--paint-voxel-visual-test";
        result.VoxelSaveSmokeTest |=
            argument == "--voxel-save-smoke-test";
        result.AddVoxelSmokeTest |=
            argument == "--add-voxel-smoke-test";
        result.ModelImportSmokeTest |=
            argument == "--model-import-smoke-test";
        result.ModelImportVisualTest |=
            argument == "--model-import-visual-test";
        result.DragDropImportSmokeTest |=
            argument == "--drag-drop-import-smoke-test";
        result.VoxelDocumentSmokeTest |=
            argument == "--voxel-document-smoke-test";
        result.QualityOfLifeSmokeTest |=
            argument == "--quality-of-life-smoke-test";
    }
    return result;
}

void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendChunk(
    std::vector<std::uint8_t>& destination,
    const std::array<char, 4>& id,
    const std::vector<std::uint8_t>& content,
    const std::vector<std::uint8_t>& children = {})
{
    destination.insert(destination.end(), id.begin(), id.end());
    AppendU32(destination, static_cast<std::uint32_t>(content.size()));
    AppendU32(destination, static_cast<std::uint32_t>(children.size()));
    destination.insert(destination.end(), content.begin(), content.end());
    destination.insert(destination.end(), children.begin(), children.end());
}

class ViewportTestFixture final
{
public:
    bool Prepare()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        parent_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeViewport-" + std::to_string(unique));
        std::error_code error;
        if (!std::filesystem::create_directories(parent_, error) || error)
        {
            return false;
        }
        return true;
    }

    bool Create(
        VoxelForge::Project::ProjectManager& projectManager,
        const bool singleVoxel,
        const bool externalSource)
    {
        const auto project = projectManager.CreateProject("ViewportTest", parent_);
        if (!project)
        {
            return false;
        }
        voxPath_ = externalSource
            ? parent_ / "castle.vox"
            : project->RootPath() / "Assets" / "sample.vox";

        std::vector<std::uint8_t> size;
        AppendU32(size, 3U); AppendU32(size, 3U); AppendU32(size, 3U);
        std::vector<std::uint8_t> xyzi;
        const std::array<std::array<std::uint8_t, 4>, 7> fullVoxels{{
            {{1U, 1U, 1U, 1U}}, {{0U, 1U, 1U, 2U}},
            {{2U, 1U, 1U, 3U}}, {{1U, 0U, 1U, 4U}},
            {{1U, 2U, 1U, 5U}}, {{1U, 1U, 0U, 6U}},
            {{1U, 1U, 2U, 7U}}}};
        const std::vector<std::array<std::uint8_t, 4>> voxels = singleVoxel
            ? std::vector<std::array<std::uint8_t, 4>>{{{1U, 1U, 1U, 1U}}}
            : std::vector<std::array<std::uint8_t, 4>>(
                fullVoxels.begin(), fullVoxels.end());
        AppendU32(xyzi, static_cast<std::uint32_t>(voxels.size()));
        for (const auto& voxel : voxels)
        {
            xyzi.insert(xyzi.end(), voxel.begin(), voxel.end());
        }
        std::vector<std::uint8_t> children;
        AppendChunk(children, {'S', 'I', 'Z', 'E'}, size);
        AppendChunk(children, {'X', 'Y', 'Z', 'I'}, xyzi);
        std::vector<std::uint8_t> bytes{'V', 'O', 'X', ' '};
        AppendU32(bytes, 150U);
        AppendChunk(bytes, {'M', 'A', 'I', 'N'}, {}, children);
        std::ofstream output(voxPath_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(output);
    }

    bool CreateThumbnailVisualSources()
    {
        const auto writeModel = [](const std::filesystem::path& path,
            const std::uint32_t sizeX, const std::uint32_t sizeY,
            const std::uint32_t sizeZ, const std::uint8_t color)
        {
            std::vector<std::uint8_t> size;
            AppendU32(size, sizeX); AppendU32(size, sizeY); AppendU32(size, sizeZ);
            std::vector<std::uint8_t> xyzi;
            AppendU32(xyzi, 4U);
            const std::array<std::array<std::uint8_t, 4>, 4> voxels{{
                {{0U, 0U, 0U, color}},
                {{static_cast<std::uint8_t>(sizeX - 1U), 0U, 0U,
                    static_cast<std::uint8_t>(color + 1U)}},
                {{0U, static_cast<std::uint8_t>(sizeY - 1U), 0U,
                    static_cast<std::uint8_t>(color + 2U)}},
                {{0U, 0U, static_cast<std::uint8_t>(sizeZ - 1U),
                    static_cast<std::uint8_t>(color + 3U)}}}};
            for (const auto& voxel : voxels)
                xyzi.insert(xyzi.end(), voxel.begin(), voxel.end());
            std::vector<std::uint8_t> children;
            AppendChunk(children, {'S', 'I', 'Z', 'E'}, size);
            AppendChunk(children, {'X', 'Y', 'Z', 'I'}, xyzi);
            std::vector<std::uint8_t> bytes{'V', 'O', 'X', ' '};
            AppendU32(bytes, 150U);
            AppendChunk(bytes, {'M', 'A', 'I', 'N'}, {}, children);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(output);
        };
        return writeModel(parent_ / "tower.vox", 2U, 8U, 3U, 20U) &&
            writeModel(parent_ / "bridge.vox", 9U, 2U, 3U, 40U);
    }

    bool CreateDragDropSources()
    {
        const std::filesystem::path viewportSource =
            parent_ / "ViewportDrop" / "tower.vox";
        const std::filesystem::path collisionSource =
            parent_ / "CollisionDrop" / "tower.vox";
        std::error_code error;
        std::filesystem::create_directories(
            viewportSource.parent_path(), error);
        if (error) return false;
        std::filesystem::create_directories(
            collisionSource.parent_path(), error);
        if (error) return false;
        if (!std::filesystem::copy_file(voxPath_, viewportSource,
                std::filesystem::copy_options::none, error) || error)
            return false;
        if (!std::filesystem::copy_file(voxPath_, collisionSource,
                std::filesystem::copy_options::none, error) || error)
            return false;
        dragDropPaths_ = {voxPath_, viewportSource, collisionSource};
        return true;
    }

    ~ViewportTestFixture()
    {
        std::error_code ignored;
        if (!parent_.empty()) std::filesystem::remove_all(parent_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& VoxPath() const noexcept
    {
        return voxPath_;
    }

    [[nodiscard]] std::filesystem::path RecentProjectsPath() const
    {
        return parent_ / "recent-projects.txt";
    }

    [[nodiscard]] const std::filesystem::path& Parent() const noexcept
    {
        return parent_;
    }

    [[nodiscard]] const std::vector<std::filesystem::path>&
        DragDropPaths() const noexcept
    {
        return dragDropPaths_;
    }

private:
    std::filesystem::path parent_;
    std::filesystem::path voxPath_;
    std::vector<std::filesystem::path> dragDropPaths_;
};
}

int main(const int argumentCount, char* arguments[])
{
    try
    {
        const CommandLine commandLine =
            ParseCommandLine(argumentCount, arguments);
        ViewportTestFixture viewportFixture;
        const bool viewportTest =
            commandLine.ViewportSmokeTest || commandLine.ViewportVisualTest ||
            commandLine.VoxelSelectionSmokeTest ||
            commandLine.VoxelSelectionVisualTest ||
            commandLine.EraseVoxelSmokeTest ||
            commandLine.EraseVoxelVisualTest ||
            commandLine.PaintVoxelSmokeTest ||
            commandLine.PaintVoxelVisualTest ||
            commandLine.VoxelSaveSmokeTest ||
            commandLine.AddVoxelSmokeTest ||
            commandLine.ModelImportSmokeTest ||
            commandLine.ModelImportVisualTest ||
            commandLine.DragDropImportSmokeTest ||
            commandLine.VoxelDocumentSmokeTest ||
            commandLine.QualityOfLifeSmokeTest;
        if (viewportTest && !viewportFixture.Prepare())
        {
            std::cerr << "[FATAL] Unable to prepare viewport test fixture.\n";
            return 1;
        }
        VoxelForge::Project::ProjectManager projectManager(
            viewportTest
                ? viewportFixture.RecentProjectsPath()
                : VoxelForge::Project::RecentProjects::DefaultStorageFilePath());
        if (viewportTest && !viewportFixture.Create(
                projectManager,
                commandLine.AddVoxelSmokeTest,
                commandLine.ModelImportSmokeTest ||
                    commandLine.ModelImportVisualTest ||
                    commandLine.DragDropImportSmokeTest))
        {
            std::cerr << "[FATAL] Unable to create viewport test fixture.\n";
            return 1;
        }
        if (commandLine.ModelImportVisualTest &&
            !viewportFixture.CreateThumbnailVisualSources())
        {
            std::cerr << "[FATAL] Unable to create thumbnail visual fixtures.\n";
            return 1;
        }
        if (commandLine.DragDropImportSmokeTest &&
            !viewportFixture.CreateDragDropSources())
        {
            std::cerr << "[FATAL] Unable to create drag-drop smoke fixtures.\n";
            return 1;
        }

        VoxelForge::Core::ApplicationSpecification specification;
        specification.Name = VoxelForge::Editor::FormatEditorWindowTitle();
        VoxelForge::Core::Application application(std::move(specification));
        auto editorLayer = std::make_unique<VoxelForge::Editor::EditorLayer>(
                projectManager,
                [&application](std::string title)
                {
                    return application.GetWindow().SetTitle(std::move(title));
                },
                [&application]() noexcept { application.Close(); },
                commandLine.EraseVoxelSmokeTest
                    ? EraseVoxelSmokeTestFrameCount
                    : commandLine.PaintVoxelSmokeTest
                    ? PaintVoxelSmokeTestFrameCount
                    : commandLine.VoxelSaveSmokeTest
                    ? VoxelSaveSmokeTestFrameCount
                    : commandLine.AddVoxelSmokeTest
                    ? AddVoxelSmokeTestFrameCount
                    : commandLine.ModelImportSmokeTest
                    ? ModelImportSmokeTestFrameCount
                    : commandLine.DragDropImportSmokeTest
                    ? DragDropImportSmokeTestFrameCount
                    : commandLine.VoxelDocumentSmokeTest
                    ? VoxelDocumentSmokeTestFrameCount
                    : commandLine.QualityOfLifeSmokeTest
                    ? QualityOfLifeSmokeTestFrameCount
                    : commandLine.VoxelSelectionSmokeTest
                    ? VoxelSelectionSmokeTestFrameCount
                    : commandLine.ViewportSmokeTest
                    ? ViewportSmokeTestFrameCount
                    : (commandLine.SmokeTest ? SmokeTestFrameCount : 0U),
                viewportTest ? viewportFixture.VoxPath()
                             : std::filesystem::path{},
                commandLine.ViewportSmokeTest ||
                    commandLine.VoxelSelectionSmokeTest ||
                    commandLine.EraseVoxelSmokeTest ||
                    commandLine.PaintVoxelSmokeTest ||
                    commandLine.VoxelSaveSmokeTest ||
                    commandLine.AddVoxelSmokeTest ||
                    commandLine.ModelImportSmokeTest ||
                    commandLine.ModelImportVisualTest ||
                    commandLine.DragDropImportSmokeTest ||
                    commandLine.VoxelDocumentSmokeTest,
                commandLine.VoxelSelectionSmokeTest,
                commandLine.VoxelSelectionVisualTest,
                commandLine.EraseVoxelSmokeTest,
                commandLine.EraseVoxelVisualTest,
                commandLine.PaintVoxelSmokeTest,
                commandLine.PaintVoxelVisualTest,
                commandLine.VoxelSaveSmokeTest,
                commandLine.AddVoxelSmokeTest,
                commandLine.ModelImportSmokeTest,
                commandLine.ModelImportVisualTest,
                commandLine.QualityOfLifeSmokeTest,
                commandLine.QualityOfLifeSmokeTest
                    ? viewportFixture.Parent() : std::filesystem::path{},
                commandLine.DragDropImportSmokeTest,
                commandLine.DragDropImportSmokeTest
                    ? viewportFixture.DragDropPaths()
                    : std::vector<std::filesystem::path>{},
                commandLine.VoxelDocumentSmokeTest);
        VoxelForge::Editor::EditorLayer* const editorLayerPointer =
            editorLayer.get();
        application.SetWindowCloseRequestCallback(
            [editorLayerPointer]()
            {
                return editorLayerPointer->RequestWindowClose();
            });
        application.PushLayer(std::move(editorLayer));

        const int result = application.Run();
        projectManager.CloseProject();
        return result;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FATAL] Unhandled exception: " << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "[FATAL] Unknown unhandled exception.\n";
        return 1;
    }
}
