#include "Welcome/ProjectDeletionService.h"

#include "VoxelForge/Project/ProjectSerializer.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <ShObjIdl_core.h>
#endif

namespace VoxelForge::Editor
{
namespace
{
std::string PathKey(const std::filesystem::path& path)
{
    std::string result = path.lexically_normal().generic_string();
#if defined(_WIN32)
    std::transform(result.begin(), result.end(), result.begin(),
        [](const unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
#endif
    return result;
}

bool SamePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    return PathKey(left) == PathKey(right);
}

bool IsAncestorOrSame(
    const std::filesystem::path& candidate,
    const std::filesystem::path& path)
{
    const std::filesystem::path normalizedCandidate =
        candidate.lexically_normal();
    const std::filesystem::path normalizedPath = path.lexically_normal();
    auto candidatePart = normalizedCandidate.begin();
    auto pathPart = normalizedPath.begin();
    for (; candidatePart != normalizedCandidate.end();
         ++candidatePart, ++pathPart)
    {
        if (pathPart == normalizedPath.end() ||
            PathKey(*candidatePart) != PathKey(*pathPart))
            return false;
    }
    return true;
}

std::optional<std::filesystem::path> CanonicalExisting(
    const std::filesystem::path& path,
    std::error_code& error)
{
    error.clear();
    std::filesystem::path canonical = std::filesystem::canonical(path, error);
    if (error) return std::nullopt;
    return canonical.lexically_normal();
}

bool PathContainsSymbolicLink(
    const std::filesystem::path& path,
    std::error_code& error)
{
    error.clear();
    std::filesystem::path current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error || std::filesystem::is_symlink(status)) return true;
    }
    return false;
}

bool DirectoryTreeContainsSymbolicLink(
    const std::filesystem::path& root,
    std::error_code& error)
{
    error.clear();
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        if (std::filesystem::is_symlink(iterator->symlink_status(error)))
            return true;
        iterator.increment(error);
    }
    return error != std::error_code{};
}

ProjectDeletionResult Failure(
    const ProjectDeletionStatus status,
    std::string message,
    std::filesystem::path root = {})
{
    return {status, std::move(root), std::move(message)};
}
}

ProjectDeletionService::ProjectDeletionService(
    IProjectRecycleBin& recycleBin) noexcept
    : recycleBin_(&recycleBin)
{
}

std::vector<std::filesystem::path>
ProjectDeletionService::DefaultProtectedRoots()
{
    std::vector<std::filesystem::path> roots;
    std::error_code error;
    std::filesystem::path candidate = std::filesystem::current_path(error);
    if (error) return roots;

    while (!candidate.empty())
    {
        const bool repositoryRoot =
            std::filesystem::is_directory(candidate / "editor", error) &&
            !error &&
            std::filesystem::is_directory(candidate / "engine", error) &&
            !error &&
            std::filesystem::is_regular_file(candidate / "CMakeLists.txt", error) &&
            !error;
        if (repositoryRoot)
        {
            roots.push_back(candidate);
            roots.push_back(candidate / "assets");
            roots.push_back(candidate / "Assets");
            break;
        }
        const std::filesystem::path parent = candidate.parent_path();
        if (parent == candidate) break;
        candidate = parent;
        error.clear();
    }
    return roots;
}

ProjectDeletionResult ProjectDeletionService::DeleteProject(
    const ProjectDeletionRequest& request) const
{
    if (!request.Confirmed)
        return Failure(ProjectDeletionStatus::ConfirmationRequired,
            "Project deletion requires explicit confirmation.");
    if (request.ProjectFilePath.empty() ||
        request.ProjectFilePath.is_relative())
        return Failure(ProjectDeletionStatus::InvalidPath,
            "The project path must be a non-empty absolute path.");

    std::error_code error;
    if (PathContainsSymbolicLink(request.ProjectFilePath.parent_path(), error))
        return Failure(ProjectDeletionStatus::SymbolicLink,
            "Project paths containing symbolic links cannot be deleted safely.");
    const std::filesystem::file_status originalStatus =
        std::filesystem::symlink_status(request.ProjectFilePath, error);
    if (error || !std::filesystem::exists(originalStatus))
        return Failure(ProjectDeletionStatus::ProjectNotFound,
            "The selected project file does not exist.");
    if (std::filesystem::is_symlink(originalStatus))
        return Failure(ProjectDeletionStatus::SymbolicLink,
            "Project paths containing symbolic links cannot be deleted safely.");
    const auto projectFile = CanonicalExisting(request.ProjectFilePath, error);
    if (!projectFile || !std::filesystem::is_regular_file(*projectFile, error) ||
        error)
        return Failure(ProjectDeletionStatus::ProjectNotFound,
            "The selected project file does not exist.");

    const std::filesystem::path projectRoot = projectFile->parent_path();
    if (!std::filesystem::is_directory(projectRoot, error) || error)
        return Failure(ProjectDeletionStatus::InvalidPath,
            "The selected project root is not a directory.", projectRoot);
    if (projectRoot == projectRoot.root_path())
        return Failure(ProjectDeletionStatus::DangerousPath,
            "A drive root can never be deleted as a project.", projectRoot);
    if (DirectoryTreeContainsSymbolicLink(projectRoot, error))
        return Failure(ProjectDeletionStatus::SymbolicLink,
            "Projects containing symbolic links cannot be deleted safely.",
            projectRoot);

    std::string serializerError;
    const auto projectData = Project::ProjectSerializer::Load(
        *projectFile, serializerError);
    if (!projectData)
        return Failure(ProjectDeletionStatus::InvalidProject,
            "The selected project file is invalid: " + serializerError,
            projectRoot);
    const auto serializedRoot = CanonicalExisting(projectData->RootPath, error);
    if (!serializedRoot || !SamePath(*serializedRoot, projectRoot) ||
        projectFile->filename() != projectData->Name + ".vfproject")
        return Failure(ProjectDeletionStatus::IdentityMismatch,
            "The project file does not match the selected project folder.",
            projectRoot);

    if (request.ActiveProjectRoot)
    {
        const auto activeRoot = CanonicalExisting(
            *request.ActiveProjectRoot, error);
        if (activeRoot && IsAncestorOrSame(projectRoot, *activeRoot))
            return Failure(ProjectDeletionStatus::ActiveProject,
                "The currently open project cannot be deleted. Close it first.",
                projectRoot);
    }
    for (const std::filesystem::path& protectedRoot : request.ProtectedRoots)
    {
        const auto canonicalProtected = CanonicalExisting(protectedRoot, error);
        if (canonicalProtected &&
            IsAncestorOrSame(projectRoot, *canonicalProtected))
            return Failure(ProjectDeletionStatus::DangerousPath,
                "The selected folder contains a protected VoxelForge path.",
                projectRoot);
    }

    std::string recycleError;
    if (recycleBin_ == nullptr ||
        !recycleBin_->MoveDirectory(projectRoot, recycleError))
        return Failure(ProjectDeletionStatus::RecycleBinFailed,
            recycleError.empty()
                ? "Windows could not move the project to the Recycle Bin."
                : std::move(recycleError),
            projectRoot);
    return {ProjectDeletionStatus::Success, projectRoot,
        "Project moved to the Recycle Bin."};
}

bool WindowsProjectRecycleBin::MoveDirectory(
    const std::filesystem::path& directory,
    std::string& error)
{
    error.clear();
#if defined(_WIN32)
    const HRESULT initialization =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE)
    {
        error = "Unable to initialize the Windows Recycle Bin service.";
        return false;
    }

    IFileOperation* operation = nullptr;
    IShellItem* item = nullptr;
    bool succeeded = false;
    do
    {
        HRESULT result = CoCreateInstance(
            CLSID_FileOperation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&operation));
        if (FAILED(result))
        {
            error = "Unable to create the Windows Recycle Bin operation.";
            break;
        }

        result = operation->SetOperationFlags(
            FOF_ALLOWUNDO |
            FOF_NOCONFIRMATION |
            FOF_NOERRORUI |
            FOF_SILENT |
            FOFX_RECYCLEONDELETE);
        if (FAILED(result))
        {
            error = "Unable to require Recycle Bin-only deletion.";
            break;
        }

        result = SHCreateItemFromParsingName(
            directory.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(result))
        {
            error = "Windows could not resolve the validated project folder.";
            break;
        }
        result = operation->DeleteItem(item, nullptr);
        if (FAILED(result))
        {
            error = "Windows refused to queue the project for recycling.";
            break;
        }
        result = operation->PerformOperations();
        if (FAILED(result))
        {
            error = "Windows could not move the project to the Recycle Bin.";
            break;
        }

        BOOL aborted = FALSE;
        result = operation->GetAnyOperationsAborted(&aborted);
        if (FAILED(result) || aborted != FALSE)
        {
            error = aborted != FALSE
                ? "The Recycle Bin operation was cancelled."
                : "Windows could not verify the Recycle Bin operation.";
            break;
        }
        succeeded = true;
    } while (false);

    if (item != nullptr) item->Release();
    if (operation != nullptr) operation->Release();
    if (uninitialize) CoUninitialize();
    return succeeded;
#else
    static_cast<void>(directory);
    error = "Project deletion is only available through the Windows Recycle Bin.";
    return false;
#endif
}

} // namespace VoxelForge::Editor
