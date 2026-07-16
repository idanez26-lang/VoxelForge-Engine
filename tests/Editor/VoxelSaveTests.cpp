#include "Project/QualityOfLifeLogic.h"
#include "VoxelSave/VoxelSaveState.h"

#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
using VoxelForge::Voxel::Voxel;
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
            ("VoxelForgeVoxelSave-" + std::to_string(unique));
        Require(std::filesystem::create_directories(path_),
            "Unable to create VoxelSave test directory.");
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

VoxelModel MakeModel()
{
    VoxelModel model;
    model.SetName("Save workflow");
    Require(model.Palette().Set(7U, {10U, 20U, 30U, 255U}),
        "Unable to prepare save palette.");
    Require(model.Palette().Set(8U, {40U, 50U, 60U, 255U}),
        "Unable to prepare paint palette.");
    VoxelGrid grid;
    Require(grid.Resize(2U, 1U, 1U), "Unable to prepare save grid.");
    Require(grid.Set(0U, 0U, 0U, {7U, Voxel::OccupiedFlag}) &&
            grid.Set(1U, 0U, 0U, {7U, Voxel::OccupiedFlag}),
        "Unable to prepare save voxels.");
    model.AddGrid(std::move(grid));
    return model;
}

void TestPathAndDirtyRules(const TemporaryDirectory& temporary)
{
    const auto source = temporary.Path() / "Assets" / "castle.vox";
    Require(DeriveVoxelSavePath(source) ==
            temporary.Path() / "Assets" / "castle.vfvoxel",
        ".vox save path was not derived safely.");
    const auto internal = temporary.Path() / "Assets" / "castle.VFVOXEL";
    Require(DeriveVoxelSavePath(internal) == internal,
        "Existing .vfvoxel path must remain the save source.");

    VoxelSaveState state;
    state.OnModelLoaded(source);
    Require(!state.IsDirty() && state.SavePath().extension() == ".vfvoxel",
        "A loaded model must start clean with a derived path.");
    state.MarkModified();
    Require(state.IsDirty(), "Paint or Erase must mark the model dirty.");
    state.MarkSaved();
    Require(!state.IsDirty(), "Successful Save must clear dirty.");
    state.MarkModified();
    Require(state.IsDirty(), "Undo or Redo after Save must restore dirty.");

    DirtyActionConfirmation confirmation;
    Require(!confirmation.Request(DestructiveAction::CloseProject, true),
        "A failed/dirty save must retain close confirmation.");
    confirmation.Cancel();
    Require(confirmation.Request(DestructiveAction::CloseProject, false),
        "A successful/clean save must allow closing without confirmation.");
}

void TestSaveLoadPaintEraseAndFailure(const TemporaryDirectory& temporary)
{
    std::filesystem::create_directories(temporary.Path() / "Assets");
    const auto source = temporary.Path() / "Assets" / "castle.vox";
    VoxelSaveState state;
    state.OnModelLoaded(source);
    VoxelModel model = MakeModel();

    state.MarkModified();
    Require(VoxelModelSerializer::Save(state.SavePath(), model).Succeeded,
        "Initial editor model save failed.");
    state.MarkSaved();
    Require(!state.IsDirty() && std::filesystem::is_regular_file(state.SavePath()),
        "Successful editor save did not create a clean .vfvoxel.");

    VoxelGrid* grid = model.GetGrid(0U);
    Require(grid != nullptr &&
            grid->Set(0U, 0U, 0U, {8U, Voxel::OccupiedFlag}),
        "Paint setup failed.");
    state.MarkModified();
    Require(VoxelModelSerializer::Save(state.SavePath(), model).Succeeded,
        "Painted model save failed.");
    state.MarkSaved();
    auto loaded = VoxelModelSerializer::Load(state.SavePath());
    Require(loaded.Model && loaded.Model->GetGrid(0U)->Get(0U, 0U, 0U)->
            ColorIndex == 8U,
        "Painted color did not reload.");

    Require(grid->Set(1U, 0U, 0U, {}), "Erase setup failed.");
    state.MarkModified();
    Require(VoxelModelSerializer::Save(state.SavePath(), model).Succeeded,
        "Erased model save failed.");
    state.MarkSaved();
    loaded = VoxelModelSerializer::Load(state.SavePath());
    Require(loaded.Model &&
            loaded.Model->GetGrid(0U)->OccupiedVoxelCount() == 1U,
        "Erased voxel did not reload.");

    const auto previousSize = std::filesystem::file_size(state.SavePath());
    const auto staleBackup =
        std::filesystem::path(state.SavePath().string() + ".bak");
    {
        std::ofstream backup(staleBackup, std::ios::binary);
        backup.put('x');
    }
    state.MarkModified();
    Require(!VoxelModelSerializer::Save(state.SavePath(), model).Succeeded,
        "Simulated transaction failure unexpectedly succeeded.");
    Require(state.IsDirty() &&
            std::filesystem::file_size(state.SavePath()) == previousSize &&
            VoxelModelSerializer::Load(state.SavePath()).Model.has_value(),
        "Failed Save changed dirty state or damaged the previous file.");

    state.OnModelLoaded(state.SavePath());
    Require(!state.IsDirty() && state.SavePath().extension() == ".vfvoxel",
        "Replacing the model from .vfvoxel did not reset save state.");
}

} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporary;
        TestPathAndDirtyRules(temporary);
        TestSaveLoadPaintEraseAndFailure(temporary);
        std::cout << "Editor voxel save tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Editor voxel save tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
