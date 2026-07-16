#include "Project/QualityOfLifeLogic.h"
#include "VoxelSave/VoxelSaveState.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;

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

void TestPathAndDirtyRules(const TemporaryDirectory& temporary)
{
    const auto source = temporary.Path() / "Assets" / "castle.vox";
    Require(DeriveVoxelSavePath(source) == source,
        "The internal .vox source must be the save destination.");
    const auto internal = temporary.Path() / "Assets" / "castle.VFVOXEL";
    Require(DeriveVoxelSavePath(internal).empty(),
        "The professional VOX save pipeline must not target .vfvoxel.");

    VoxelSaveState state;
    state.OnModelLoaded(source);
    Require(!state.IsDirty() && state.SavePath() == source,
        "A loaded VOX model must start clean with its internal path.");
    state.MarkModified();
    Require(state.IsDirty(), "Paint or Erase must mark the model dirty.");
    state.MarkSaved();
    Require(!state.IsDirty(), "Successful Save must clear dirty.");
    state.MarkModified();
    Require(state.IsDirty(), "Undo or Redo after Save must restore dirty.");

    DirtyActionConfirmation confirmation;
    Require(!confirmation.Request(DestructiveAction::CloseProject, true),
        "A failed/dirty save must retain close confirmation.");
    Require(confirmation.IsPending(),
        "Dirty close action must remain pending while Save fails.");
    const auto closeAfterSave = confirmation.ContinueAfterSuccessfulSave();
    Require(closeAfterSave == DestructiveAction::CloseProject &&
        !confirmation.IsPending(),
        "Successful Save must continue the exact pending action.");
    Require(!confirmation.Request(DestructiveAction::OpenProject, true),
        "Dirty project replacement must request confirmation.");
    const auto discard = confirmation.Discard();
    Require(discard == DestructiveAction::OpenProject,
        "Don't Save must continue the exact pending action.");
    Require(!confirmation.Request(DestructiveAction::ExitApplication, true),
        "Dirty application exit must request confirmation.");
    confirmation.Cancel();
    Require(!confirmation.IsPending(),
        "Cancel must abort the pending destructive action.");
    Require(confirmation.Request(DestructiveAction::CloseProject, false),
        "A successful/clean save must allow closing without confirmation.");
}

} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporary;
        TestPathAndDirtyRules(temporary);
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
