#include "VoxelCreation/DirectCreationFlowService.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class Fixture final
{
public:
    Fixture()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        Root = fs::temp_directory_path() /
            ("VoxelForgeDirectCreation-" + std::to_string(unique));
        fs::create_directories(Root / "Assets" / "Models");
        fs::create_directories(Root / "Cache");
    }

    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }

    fs::path Root;
};

struct FlowHarness final
{
    void Configure(DirectCreationFlowService& flow)
    {
        flow.SetViewportPreparationCallback(
            [this](const fs::path& path)
            {
                Steps.push_back("viewport");
                CameraFramed = true;
                OpenedPath = path;
                return DirectCreationStepResult{true, {}};
            });
        flow.SetPencilActivationCallback(
            [this](const fs::path&)
            {
                Steps.push_back("pencil");
                PencilActive = true;
                return DirectCreationStepResult{true, {}};
            });
        flow.SetWorkplanePreparationCallback(
            [this](const fs::path&)
            {
                Steps.push_back("workplane");
                WorkplaneVisible = true;
                return DirectCreationStepResult{true, {}};
            });
        flow.SetViewportFocusCallback(
            [this](const fs::path&)
            {
                Steps.push_back("focus");
                FocusRequested = true;
                return DirectCreationStepResult{true, {}};
            });
    }

    std::vector<std::string> Steps;
    fs::path OpenedPath;
    bool CameraFramed = false;
    bool PencilActive = false;
    bool WorkplaneVisible = false;
    bool FocusRequested = false;
};

void ConfigureCreation(
    VoxelModelCreationService& creation,
    const fs::path& projectRoot,
    std::size_t& browserRefreshes,
    std::size_t& opens,
    bool thumbnailSucceeds = true)
{
    Require(creation.SetProjectRoot(projectRoot),
        "Unable to configure model creation.");
    creation.SetThumbnailCallback(
        [thumbnailSucceeds](const fs::path&)
        {
            return VoxelModelCreationStepResult{thumbnailSucceeds,
                thumbnailSucceeds ? std::string{} : "thumbnail unavailable"};
        });
    creation.SetAssetBrowserCallback(
        [&browserRefreshes](const fs::path&)
        {
            ++browserRefreshes;
            return true;
        });
    creation.SetOpenCallback(
        [&opens](const fs::path&)
        {
            ++opens;
            return true;
        });
}

void TestNormalCreationAndRename()
{
    Fixture fixture;
    std::size_t refreshes = 0U;
    std::size_t opens = 0U;
    VoxelModelCreationService creation;
    ConfigureCreation(creation, fixture.Root, refreshes, opens);
    DirectCreationFlowService flow;
    FlowHarness harness;
    harness.Configure(flow);

    const VoxelModelCreationRequest request{"Maison", {64U, 64U, 64U}};
    const DirectCreationFlowResult created = flow.Create(creation, request);
    Require(created.Ready() && created.Creation.Opened &&
        created.Creation.AssetBrowserRefreshed &&
        created.Creation.ThumbnailGenerated && refreshes == 1U && opens == 1U,
        "Normal direct creation did not become ready.");
    Require(fs::is_regular_file(created.Creation.ModelPath) &&
        fs::is_regular_file(created.Creation.ModelPath.string() + ".vfmeta"),
        "Model or metadata was not created.");
    Require(harness.OpenedPath == created.Creation.ModelPath &&
        harness.CameraFramed && harness.PencilActive &&
        harness.WorkplaneVisible && harness.FocusRequested &&
        harness.Steps == std::vector<std::string>{
            "viewport", "pencil", "workplane", "focus"},
        "Direct preparation order is incorrect.");

    const auto collision = flow.Create(creation, request);
    Require(collision.Creation.Status ==
        VoxelModelCreationStatus::Collision,
        "Collision should remain delegated to the existing pipeline.");
    const auto renamed = flow.Create(
        creation, request, VoxelModelCreationCollisionAction::Rename);
    Require(renamed.Ready() && renamed.Creation.Status ==
        VoxelModelCreationStatus::Renamed &&
        renamed.Creation.ModelPath.filename() == "Maison (1).vox" &&
        refreshes == 2U && opens == 2U,
        "Automatic rename did not complete direct creation.");
}

void TestOptionalFailuresContinue()
{
    Fixture fixture;
    std::size_t refreshes = 0U;
    std::size_t opens = 0U;
    VoxelModelCreationService creation;
    ConfigureCreation(creation, fixture.Root, refreshes, opens, false);
    creation.SetMetadataCallback([](const fs::path&)
    {
        MetadataAnalysisResult result;
        result.Message = "metadata unavailable";
        return result;
    });
    DirectCreationFlowService flow;
    FlowHarness harness;
    harness.Configure(flow);

    const auto result = flow.Create(
        creation, {"WarningsAllowed", {16U, 16U, 16U}});
    Require(result.Ready() && fs::is_regular_file(result.Creation.ModelPath) &&
        !fs::exists(result.Creation.ModelPath.string() + ".vfmeta") &&
        !result.Creation.ThumbnailGenerated &&
        result.Warning.find("Metadata creation failed") != std::string::npos &&
        result.Warning.find("Thumbnail generation failed") !=
            std::string::npos,
        "Optional metadata and thumbnail failures should only warn.");
}

void TestImpossibleCreationDoesNotPrepare()
{
    Fixture fixture;
    std::size_t refreshes = 0U;
    std::size_t opens = 0U;
    VoxelModelCreationService creation;
    ConfigureCreation(creation, fixture.Root, refreshes, opens);
    DirectCreationFlowService flow;
    FlowHarness harness;
    harness.Configure(flow);
    const std::size_t before = static_cast<std::size_t>(
        std::distance(fs::directory_iterator(fixture.Root / "Assets" / "Models"),
            fs::directory_iterator{}));

    const auto failed = flow.Create(
        creation, {"bad:name", {16U, 16U, 16U}});
    const std::size_t after = static_cast<std::size_t>(
        std::distance(fs::directory_iterator(fixture.Root / "Assets" / "Models"),
            fs::directory_iterator{}));
    Require(!failed.Creation.Succeeded() && !failed.Ready() &&
        harness.Steps.empty() && refreshes == 0U && opens == 0U &&
        before == after,
        "Impossible creation must not open or prepare partial state.");
}
}

int main()
{
    try
    {
        TestNormalCreationAndRename();
        TestOptionalFailuresContinue();
        TestImpossibleCreationDoesNotPrepare();
        std::cout << "Direct creation flow tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Direct creation flow tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
