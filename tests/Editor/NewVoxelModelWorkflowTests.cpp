#include "VoxelCreation/NewVoxelModelWorkflow.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "SmartToolTestSupport.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge;
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
            ("VoxelForgeInstantNewModel-" + std::to_string(unique));
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

class EditSession final : public VoxelEditSession
{
public:
    explicit EditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document)
    {
        model_.SetName("Instant New Model");
        const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
        Require(source != nullptr, "Created document has no sub-model.");
        const Asset::Voxel::VoxelDimensions dimensions = source->Dimensions();
        Voxel::VoxelGrid grid;
        Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
            "Unable to create compatibility grid.");
        model_.AddGrid(std::move(grid));
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 1U;
    }

    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }

    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }

    CommandResult RebuildActiveVoxelMesh() override
    {
        ++RebuildCount;
        return CommandResult::Success();
    }

    void CompleteVoxelEdit() noexcept override { ++CompletedEdits; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t RebuildCount = 0U;
    std::size_t CompletedEdits = 0U;
};

struct Harness final
{
    void Configure(
        VoxelModelCreationService& creation,
        DirectCreationFlowService& direct,
        const fs::path& projectRoot)
    {
        Require(creation.SetProjectRoot(projectRoot),
            "Unable to configure project root.");
        creation.SetThumbnailCallback([](const fs::path&)
        {
            return VoxelModelCreationStepResult{true, {}};
        });
        creation.SetAssetBrowserCallback([this](const fs::path& path)
        {
            ++RefreshCount;
            RevealedPath = path;
            return true;
        });
        creation.SetOpenCallback([this](const fs::path& path)
        {
            ++OpenCount;
            const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Load(path);
            if (!loaded.Succeeded()) return false;
            Document.emplace(std::move(*loaded.Document));
            return true;
        });
        direct.SetViewportPreparationCallback([this](const fs::path&)
        {
            CameraFramed = true;
            return DirectCreationStepResult{true, {}};
        });
        direct.SetPencilActivationCallback([this](const fs::path&)
        {
            PencilActive = true;
            ColorValid = true;
            return DirectCreationStepResult{true, {}};
        });
        direct.SetWorkplanePreparationCallback([this](const fs::path&)
        {
            WorkplaneReady = true;
            return DirectCreationStepResult{true, {}};
        });
        direct.SetViewportFocusCallback([this](const fs::path&)
        {
            FocusRequested = true;
            return DirectCreationStepResult{true, {}};
        });
    }

    std::optional<Asset::Voxel::VoxelDocument> Document;
    fs::path RevealedPath;
    std::size_t RefreshCount = 0U;
    std::size_t OpenCount = 0U;
    bool CameraFramed = false;
    bool PencilActive = false;
    bool ColorValid = false;
    bool WorkplaneReady = false;
    bool FocusRequested = false;
};

std::vector<char> ReadBytes(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

void TestInstantCreationAndEditing()
{
    Fixture fixture;
    VoxelModelCreationService creation;
    DirectCreationFlowService direct;
    Harness harness;
    harness.Configure(creation, direct, fixture.Root);
    NewVoxelModelWorkflow workflow;

    const DirectCreationFlowResult result = workflow.Create(creation, direct);
    Require(result.Ready() && result.Creation.ModelPath.filename() ==
            "New Model.vox",
        "The first instant model did not use the default name.");
    Require(harness.Document.has_value() &&
            harness.Document->GetVoxelCount() == 0U &&
            harness.RefreshCount == 1U && harness.OpenCount == 1U &&
            harness.RevealedPath == result.Creation.ModelPath &&
            harness.CameraFramed && harness.PencilActive &&
            harness.ColorValid && harness.WorkplaneReady &&
            harness.FocusRequested,
        "The empty model was not opened and prepared in one workflow.");
    const auto dimensions = harness.Document->GetDimensions(0U);
    const VoxelModelCreationRequest defaults =
        NewVoxelModelWorkflow::DefaultRequest();
    Require(dimensions && dimensions->X == defaults.Dimensions.X &&
            dimensions->Y == defaults.Dimensions.Y &&
            dimensions->Z == defaults.Dimensions.Z &&
            dimensions->X == 64U,
        "Instant creation did not preserve the existing default dimensions.");

    EditSession session(*harness.Document);
    VoxelEditHistory history;
    const Asset::Voxel::VoxelPosition firstVoxel{32, 0, 32};
    SmartBrushState brushState;
    brushState.PaletteIndex = 1U;
    const VoxelToolResult pencil = VoxelPencilTool::Apply(
        TestSupport::MakePencilContext(session, *harness.Document, 0U,
            brushState, firstVoxel, {0, 1, 0}, &history));
    Require(pencil.Changed && harness.Document->HasVoxel(firstVoxel) &&
            history.CanUndo() && history.UndoCount() == 1U,
        "The first Workplane click did not use the normal Pencil history.");
    Require(history.Undo(session) &&
            !harness.Document->HasVoxel(firstVoxel) && history.CanRedo(),
        "Undo did not remove the first voxel.");
    Require(history.Redo(session) &&
            harness.Document->HasVoxel(firstVoxel) && !history.CanRedo(),
        "Redo did not restore the first voxel.");
}

void TestUniqueNamesNeverReplace()
{
    Fixture fixture;
    VoxelModelCreationService creation;
    DirectCreationFlowService direct;
    Harness harness;
    harness.Configure(creation, direct, fixture.Root);
    NewVoxelModelWorkflow workflow;

    const auto first = workflow.Create(creation, direct);
    const std::vector<char> original = ReadBytes(first.Creation.ModelPath);
    const auto second = workflow.Create(creation, direct);
    const auto third = workflow.Create(creation, direct);
    Require(first.Creation.ModelPath.filename() == "New Model.vox" &&
            second.Creation.ModelPath.filename() == "New Model (1).vox" &&
            third.Creation.ModelPath.filename() == "New Model (2).vox" &&
            second.Creation.Status == VoxelModelCreationStatus::Renamed &&
            third.Creation.Status == VoxelModelCreationStatus::Renamed,
        "Successive instant creations did not generate deterministic names.");
    Require(ReadBytes(first.Creation.ModelPath) == original &&
            fs::is_regular_file(second.Creation.ModelPath) &&
            fs::is_regular_file(third.Creation.ModelPath) &&
            harness.RefreshCount == 3U && harness.OpenCount == 3U,
        "A previous model was replaced or the browser/open callbacks duplicated.");
    Require(!fs::exists(first.Creation.ModelPath.string() + ".vfcreate.tmp") &&
            !fs::exists(first.Creation.ModelPath.string() + ".vfcreate.bak"),
        "Instant creation left transaction files behind.");
}

void TestNoProjectFailsWithoutPartialFile()
{
    VoxelModelCreationService creation;
    DirectCreationFlowService direct;
    NewVoxelModelWorkflow workflow;
    const DirectCreationFlowResult result = workflow.Create(creation, direct);
    Require(!result.Creation.Succeeded() && !result.Ready() &&
            result.Creation.ModelPath.empty(),
        "Instant creation without a project did not fail cleanly.");
}
}

int main()
{
    try
    {
        TestInstantCreationAndEditing();
        TestUniqueNamesNeverReplace();
        TestNoProjectFailsWithoutPartialFile();
        std::cout << "Instant New Model workflow tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Instant New Model workflow tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
