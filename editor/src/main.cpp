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

struct CommandLine final
{
    bool SmokeTest = false;
    bool ViewportSmokeTest = false;
    bool ViewportVisualTest = false;
    bool VoxelSelectionSmokeTest = false;
    bool VoxelSelectionVisualTest = false;
    bool EraseVoxelSmokeTest = false;
    bool EraseVoxelVisualTest = false;
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

    bool Create(VoxelForge::Project::ProjectManager& projectManager)
    {
        const auto project = projectManager.CreateProject("ViewportTest", parent_);
        if (!project)
        {
            return false;
        }
        voxPath_ = project->RootPath() / "Assets" / "sample.vox";

        std::vector<std::uint8_t> size;
        AppendU32(size, 3U); AppendU32(size, 3U); AppendU32(size, 3U);
        std::vector<std::uint8_t> xyzi;
        const std::array<std::array<std::uint8_t, 4>, 7> voxels{{
            {{1U, 1U, 1U, 1U}}, {{0U, 1U, 1U, 2U}},
            {{2U, 1U, 1U, 3U}}, {{1U, 0U, 1U, 4U}},
            {{1U, 2U, 1U, 5U}}, {{1U, 1U, 0U, 6U}},
            {{1U, 1U, 2U, 7U}}}};
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

private:
    std::filesystem::path parent_;
    std::filesystem::path voxPath_;
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
            commandLine.EraseVoxelVisualTest;
        if (viewportTest && !viewportFixture.Prepare())
        {
            std::cerr << "[FATAL] Unable to prepare viewport test fixture.\n";
            return 1;
        }
        VoxelForge::Project::ProjectManager projectManager(
            viewportTest
                ? viewportFixture.RecentProjectsPath()
                : VoxelForge::Project::RecentProjects::DefaultStorageFilePath());
        if (viewportTest && !viewportFixture.Create(projectManager))
        {
            std::cerr << "[FATAL] Unable to create viewport test fixture.\n";
            return 1;
        }

        VoxelForge::Core::ApplicationSpecification specification;
        specification.Name = VoxelForge::Editor::FormatEditorWindowTitle();
        VoxelForge::Core::Application application(std::move(specification));
        application.PushLayer(
            std::make_unique<VoxelForge::Editor::EditorLayer>(
                projectManager,
                [&application](std::string title)
                {
                    return application.GetWindow().SetTitle(std::move(title));
                },
                [&application]() noexcept { application.Close(); },
                commandLine.EraseVoxelSmokeTest
                    ? EraseVoxelSmokeTestFrameCount
                    : commandLine.VoxelSelectionSmokeTest
                    ? VoxelSelectionSmokeTestFrameCount
                    : commandLine.ViewportSmokeTest
                    ? ViewportSmokeTestFrameCount
                    : (commandLine.SmokeTest ? SmokeTestFrameCount : 0U),
                viewportTest ? viewportFixture.VoxPath()
                             : std::filesystem::path{},
                commandLine.ViewportSmokeTest ||
                    commandLine.VoxelSelectionSmokeTest ||
                    commandLine.EraseVoxelSmokeTest,
                commandLine.VoxelSelectionSmokeTest,
                commandLine.VoxelSelectionVisualTest,
                commandLine.EraseVoxelSmokeTest,
                commandLine.EraseVoxelVisualTest));

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
