#include "ProjectSession/ProjectSessionService.h"
#include "EditorCamera.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class TemporaryProject final
{
public:
    TemporaryProject()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeProjectSession-" + std::to_string(unique));
        std::filesystem::create_directories(root_ / "Assets" / "Models");
    }

    ~TemporaryProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Root() const noexcept
    {
        return root_;
    }

    void WriteSession(const std::string& contents) const
    {
        std::ofstream output(root_ / ".vfsession", std::ios::binary);
        output << contents;
        Require(static_cast<bool>(output), "Unable to write test session.");
    }

private:
    std::filesystem::path root_;
};

ProjectSessionData ValidSession()
{
    ProjectSessionData session;
    session.LastModel = "Assets/Models/Maison.vox";
    session.Camera.Position = {7.25F, 4.5F, -9.0F};
    session.Camera.RotationDegrees = {-20.0F, 125.0F, 0.0F};
    session.Camera.Distance = 14.5F;
    session.Camera.Target = {1.0F, 2.0F, 3.0F};
    session.Camera.View = ProjectSessionCameraView::Perspective;
    session.ActiveTool = ProjectSessionTool::Eraser;
    session.ActivePaletteIndex = 42U;
    return session;
}

void TestAbsentSaveAndReopen()
{
    TemporaryProject project;
    ProjectSessionService service;
    Require(service.SetProjectRoot(project.Root()),
        "Project root should be accepted.");
    Require(service.Load().Status == ProjectSessionLoadStatus::NotFound,
        "First opening should tolerate an absent session.");

    ProjectSessionData session = ValidSession();
    std::string error;
    Require(service.Save(session, error), "Session save failed: " + error);
    Require(std::filesystem::is_regular_file(service.SessionPath()),
        "Session file was not created.");
    Require(!std::filesystem::exists(service.SessionPath().string() + ".tmp") &&
        !std::filesystem::exists(service.SessionPath().string() + ".bak"),
        "Session transaction residue remains.");

    const ProjectSessionLoadResult loaded = service.Load();
    Require(loaded.Loaded() && loaded.CameraValid,
        "Saved session should reload with a valid camera.");
    Require(loaded.Session.LastModel == session.LastModel,
        "Last model was not preserved.");
    Require(loaded.Session.Camera == session.Camera,
        "Camera state was not preserved exactly.");
    Require(loaded.Session.ActiveTool == ProjectSessionTool::Eraser,
        "Active tool was not preserved.");
    Require(loaded.Session.ActivePaletteIndex == 42U,
        "Active palette index was not preserved.");
}

void TestValidationAndFallbacks()
{
    TemporaryProject project;
    ProjectSessionService service;
    Require(service.SetProjectRoot(project.Root()),
        "Project root should be accepted.");
    Require(ProjectSessionService::IsValidModelPath(
        "Assets/Models/House.vox"), "Valid model path was refused.");
    Require(!ProjectSessionService::IsValidModelPath(
        "Assets/../outside.vox"), "Escaping model path was accepted.");
    Require(!ProjectSessionService::IsValidModelPath(
        "Assets/Models/House.obj"), "Unsupported model path was accepted.");

    project.WriteSession(
        "format_version=1\n"
        "last_model=Assets/Models/Missing.vox\n"
        "camera_position=0,0,0\n"
        "camera_rotation=95,0,0\n"
        "camera_target=0,0,0\n"
        "camera_distance=-1\n"
        "camera_view=Perspective\n"
        "active_tool=RemovedTool\n"
        "active_palette_index=999\n");
    const ProjectSessionLoadResult invalidCamera = service.Load();
    Require(invalidCamera.Loaded() && !invalidCamera.CameraValid,
        "Invalid camera should retain the session for Frame fallback.");
    Require(invalidCamera.Session.ActiveTool == ProjectSessionTool::Pencil,
        "Unknown tool should fall back to Pencil.");
    Require(invalidCamera.Session.ActivePaletteIndex == 1U,
        "Invalid palette index should fall back to one.");
    Require(invalidCamera.Session.LastModel ==
        std::filesystem::path("Assets/Models/Missing.vox"),
        "Missing model path should remain available to the workspace.");

    project.WriteSession("format_version=1\ninvalid line\n");
    Require(service.Load().Status == ProjectSessionLoadStatus::Corrupt,
        "Corrupt session should be rejected.");
    std::string error;
    Require(service.Save(ValidSession(), error),
        "Closing after a corrupt session should replace it: " + error);
    Require(service.Load().Loaded(),
        "Replacement session should be readable.");

    project.WriteSession("format_version=2\n");
    Require(service.Load().Status ==
        ProjectSessionLoadStatus::UnsupportedVersion,
        "Future session version should be rejected for migration.");
}

void TestCameraRestore()
{
    EditorCamera camera;
    camera.Frame(16.0F, 12.0F, 8.0F);
    camera.Orbit(53.0F, -21.0F);
    camera.Pan(17.0F, -8.0F, 720.0F);
    camera.Zoom(2.0F);
    const EditorCameraState expected = camera.CaptureState();

    EditorCamera restored;
    restored.Reset();
    Require(restored.RestoreState(expected),
        "Valid camera state should restore.");
    Require(restored.CaptureState() == expected,
        "Camera state should restore exactly.");

    EditorCameraState inconsistent = expected;
    inconsistent.Position.X += 1.0F;
    const EditorCameraState before = restored.CaptureState();
    Require(!restored.RestoreState(inconsistent),
        "Inconsistent camera position should be rejected.");
    Require(restored.CaptureState() == before,
        "Rejected camera state must not alter the current camera.");
}

void TestFillToolPersistence()
{
    TemporaryProject project;
    ProjectSessionService service;
    Require(service.SetProjectRoot(project.Root()),
        "Project root should be accepted for Fill session test.");
    ProjectSessionData session = ValidSession();
    session.ActiveTool = ProjectSessionTool::Fill;
    std::string error;
    Require(service.Save(session, error),
        "Fill session save failed: " + error);
    const ProjectSessionLoadResult loaded = service.Load();
    Require(loaded.Loaded() &&
        loaded.Session.ActiveTool == ProjectSessionTool::Fill,
        "Fill tool was not restored from the project session.");
}

void TestBoxToolPersistence()
{
    TemporaryProject project;
    ProjectSessionService service;
    Require(service.SetProjectRoot(project.Root()),
        "Project root should be accepted for Box session test.");
    ProjectSessionData session = ValidSession();
    session.ActiveTool = ProjectSessionTool::Box;
    std::string error;
    Require(service.Save(session, error),
        "Box session save failed: " + error);
    const ProjectSessionLoadResult loaded = service.Load();
    Require(loaded.Loaded() &&
        loaded.Session.ActiveTool == ProjectSessionTool::Box,
        "Box tool was not restored from the project session.");
}

void TestLineToolPersistence()
{
    TemporaryProject project;
    ProjectSessionService service;
    Require(service.SetProjectRoot(project.Root()),
        "Project root should be accepted for Line session test.");
    ProjectSessionData session = ValidSession();
    session.ActiveTool = ProjectSessionTool::Line;
    std::string error;
    Require(service.Save(session, error),
        "Line session save failed: " + error);
    const ProjectSessionLoadResult loaded = service.Load();
    Require(loaded.Loaded() &&
        loaded.Session.ActiveTool == ProjectSessionTool::Line,
        "Line tool was not restored from the project session.");
}

void TestProjectSwitchAndClose()
{
    TemporaryProject first;
    TemporaryProject second;
    ProjectSessionService service;
    Require(service.SetProjectRoot(first.Root()),
        "First project root should be accepted.");
    std::string error;
    Require(service.Save(ValidSession(), error),
        "First project session save failed.");
    Require(service.SetProjectRoot(second.Root()),
        "Second project root should be accepted.");
    Require(service.Load().Status == ProjectSessionLoadStatus::NotFound,
        "Project switch must not leak the previous session.");
    service.ClearProject();
    Require(service.ProjectRoot().empty() && service.SessionPath().empty(),
        "Closing a project should clear service state.");
}
}

int main()
{
    try
    {
        TestAbsentSaveAndReopen();
        TestValidationAndFallbacks();
        TestCameraRestore();
        TestFillToolPersistence();
        TestBoxToolPersistence();
        TestLineToolPersistence();
        TestProjectSwitchAndClose();
        std::cout << "Project session tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Project session tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
