#include "Platform/FileDialogService.h"
#include "Platform/ProjectFolderOpener.h"
#include "Project/ProjectDialogPreferences.h"
#include "Project/QualityOfLifeLogic.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{
using namespace VoxelForge::Editor;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeQoL-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class SimulatedFolderOpener final : public ProjectFolderOpener
{
public:
    bool Open(const std::filesystem::path& folder, std::string& error) override
    {
        OpenedPath = folder;
        if (!Succeed)
        {
            error = "simulated failure";
            return false;
        }
        error.clear();
        return true;
    }

    std::filesystem::path OpenedPath;
    bool Succeed = true;
};

bool Expect(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}
}

int main()
{
    using namespace VoxelForge::Editor;
    TemporaryDirectory temporary;
    const std::filesystem::path preferencesFile =
        temporary.Path() / "profile" / "preferences.ini";
    ProjectDialogPreferences preferences(preferencesFile);
    if (!Expect(preferences.Load(), "An absent preference file must be valid.") ||
        !Expect(preferences.LastCreateParent().empty(),
            "Absent preferences must have an empty create folder.") ||
        !Expect(!std::filesystem::exists(preferencesFile),
            "Loading preferences must not create a file.")) return 1;

    const std::filesystem::path createDirectory = temporary.Path() / "Create";
    const std::filesystem::path openDirectory = temporary.Path() / "Open";
    std::filesystem::create_directories(createDirectory);
    std::filesystem::create_directories(openDirectory);
    if (!Expect(preferences.SetLastCreateParent(createDirectory),
            "The create directory must be saved.") ||
        !Expect(preferences.SetLastOpenDirectory(openDirectory),
            "The open directory must be saved.")) return 1;

    ProjectDialogPreferences reloaded(preferencesFile);
    if (!Expect(reloaded.Load(), "Saved preferences must load.") ||
        !Expect(reloaded.LastCreateParent() ==
                std::filesystem::absolute(createDirectory).lexically_normal(),
            "The create directory must round-trip.") ||
        !Expect(reloaded.LastOpenDirectory() ==
                std::filesystem::absolute(openDirectory).lexically_normal(),
            "The open directory must round-trip.") ||
        !Expect(reloaded.StorageFilePath().string().starts_with(
                temporary.Path().string()),
            "Tests must use only the injected temporary profile.")) return 1;

    std::filesystem::remove(openDirectory);
    ProjectDialogPreferences missingPath(preferencesFile);
    if (!Expect(missingPath.Load(), "Preferences with stale paths must load.") ||
        !Expect(missingPath.LastOpenDirectory().empty(),
            "A disappeared directory must be ignored.")) return 1;

    const std::filesystem::path invalidPreferencesFile =
        temporary.Path() / "invalid-preferences.ini";
    {
        std::ofstream invalid(invalidPreferencesFile);
        invalid << "unknown=value\nlast_create_parent=missing\n"
                   "last_open_directory=\ntruncated";
    }
    ProjectDialogPreferences invalidPreferences(invalidPreferencesFile);
    if (!Expect(invalidPreferences.Load(),
            "Unknown, truncated, and invalid preference lines must be ignored.") ||
        !Expect(invalidPreferences.LastCreateParent().empty() &&
                invalidPreferences.LastOpenDirectory().empty(),
            "Invalid preference paths must not be retained.")) return 1;

    auto channel = std::make_unique<FileDialogResultChannel>();
    const auto producer = channel->CreateProducer();
    if (!Expect(producer.Publish({FileDialogStatus::Success,
                FileDialogKind::ProjectFile, temporary.Path() / "A.vfproject", {}}),
            "A live channel must accept Success.") ||
        !Expect(producer.Publish({FileDialogStatus::Cancelled,
                FileDialogKind::ProjectParentFolder, {}, {}}),
            "A live channel must accept Cancelled.") ||
        !Expect(producer.Publish({FileDialogStatus::Error,
                FileDialogKind::ProjectFile, {}, "simulated"}),
            "A live channel must accept Error.")) return 1;
    const auto success = channel->Consume();
    const auto cancelled = channel->Consume();
    const auto error = channel->Consume();
    if (!Expect(success && success->Status == FileDialogStatus::Success,
            "Success must be consumed on the main side.") ||
        !Expect(cancelled && cancelled->Status == FileDialogStatus::Cancelled,
            "Cancelled must be consumed on the main side.") ||
        !Expect(error && error->Status == FileDialogStatus::Error &&
                error->Error == "simulated",
            "Error details must be preserved.")) return 1;
    channel.reset();
    if (!Expect(!producer.Publish({FileDialogStatus::Success,
                FileDialogKind::ProjectFile, {}, {}}),
            "A late callback must be ignored after channel destruction.")) return 1;

    auto simulatedDialog = CreateSimulatedFileDialogService();
    if (!Expect(simulatedDialog->ChooseProjectFile(temporary.Path()),
            "The first simulated dialog request must start.") ||
        !Expect(!simulatedDialog->ChooseProjectParentFolder(temporary.Path()),
            "A concurrent dialog request must be refused.") ||
        !Expect(simulatedDialog->InjectSimulatedResult({
                FileDialogStatus::Cancelled,
                FileDialogKind::ProjectFile,
                {},
                {}}),
            "The active simulated request must accept its result.")) return 1;
    const auto simulatedCancel = simulatedDialog->ConsumeResult();
    if (!Expect(simulatedCancel &&
                simulatedCancel->Status == FileDialogStatus::Cancelled &&
                !simulatedDialog->IsPending(),
            "Consuming a simulated result must release the active request."))
        return 1;

    ShortcutContext shortcut;
    if (!Expect(CanRunProjectShortcut(ProjectShortcut::NewProject, shortcut),
            "New must be available without a project.") ||
        !Expect(!CanRunProjectShortcut(ProjectShortcut::SaveProject, shortcut),
            "Save must be disabled without a project.")) return 1;
    shortcut.HasProject = true;
    if (!Expect(CanRunProjectShortcut(ProjectShortcut::CloseProject, shortcut),
            "Close must be available with a project.")) return 1;
    shortcut.TextInput = true;
    if (!Expect(!CanRunProjectShortcut(ProjectShortcut::CloseProject, shortcut),
            "Project shortcuts must be blocked during text input.")) return 1;

    SimulatedFolderOpener opener;
    std::string openError;
    if (!Expect(opener.Open(createDirectory, openError) &&
            opener.OpenedPath == createDirectory,
            "The platform backend must receive the exact project folder.")) return 1;
    opener.Succeed = false;
    if (!Expect(!opener.Open(createDirectory, openError) && !openError.empty(),
            "Platform errors must be returned without opening a real shell.")) return 1;

    const std::filesystem::path reservedFolder =
        std::filesystem::absolute(
            temporary.Path() / "Folder # 100%" / "Name:Test");
    const std::string reservedUri = BuildProjectFolderUri(reservedFolder);
    if (!Expect(reservedUri.starts_with("file:///"),
            "A Windows file URI must use the file:/// prefix.") ||
        !Expect(reservedUri.find("Folder%20%23%20100%25") != std::string::npos,
            "Spaces, #, and % must be percent-encoded in folder URIs.") ||
        !Expect(reservedUri.find("Name%3ATest") != std::string::npos,
            "A colon outside a Windows drive prefix must be encoded.") ||
        !Expect(reservedUri.find('#') == std::string::npos,
            "Reserved URI characters must not remain unescaped.")) return 1;

    DirtyActionConfirmation confirmation;
    std::string activeProject = "Current";
    std::string activeModel = "Edited";
    if (!Expect(!confirmation.Request(
            DestructiveAction::OpenProject, true) && confirmation.IsPending(),
            "Dirty actions must wait for confirmation.")) return 1;
    static_cast<void>(confirmation.Request(
        DestructiveAction::CloseProject, true));
    const auto originalAction = confirmation.Discard();
    if (!Expect(originalAction == DestructiveAction::OpenProject,
            "A second dirty request must not replace the pending action.")) return 1;
    static_cast<void>(confirmation.Request(
        DestructiveAction::OpenProject, true));
    confirmation.Cancel();
    if (!Expect(activeProject == "Current" && activeModel == "Edited" &&
            !confirmation.IsPending(),
            "Cancel must conserve the project and model.")) return 1;
    static_cast<void>(confirmation.Request(DestructiveAction::CloseProject, true));
    const auto discarded = confirmation.Discard();
    if (discarded == DestructiveAction::CloseProject)
    {
        activeProject.clear();
        activeModel.clear();
    }
    if (!Expect(activeProject.empty() && activeModel.empty(),
            "Discard must release the pending destructive action.")) return 1;

    return 0;
}
