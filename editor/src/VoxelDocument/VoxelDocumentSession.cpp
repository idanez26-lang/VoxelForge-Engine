#include "VoxelDocumentSession.h"

#include <system_error>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
bool IsWithin(
    const std::filesystem::path& candidate,
    const std::filesystem::path& parent)
{
    const std::filesystem::path relative = candidate.lexically_relative(parent);
    if (relative.empty()) return candidate == parent;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

VoxelDocumentSessionResult Failure(
    const VoxelDocumentSessionError error,
    std::string message,
    const Asset::Voxel::VoxelDocumentError documentError =
        Asset::Voxel::VoxelDocumentError::None)
{
    return {error, documentError, std::move(message)};
}
}

bool VoxelDocumentSession::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
        return false;
    const std::filesystem::path assets =
        std::filesystem::weakly_canonical(canonical / "Assets", error);
    if (error || !std::filesystem::is_directory(assets, error) || error)
        return false;
    if (projectRoot_ != canonical)
    {
        Close();
        projectRoot_ = canonical;
        assetsRoot_ = assets;
    }
    return true;
}

void VoxelDocumentSession::ClearProject() noexcept
{
    Close();
    projectRoot_.clear();
    assetsRoot_.clear();
}

VoxelDocumentSessionResult VoxelDocumentSession::Open(
    const std::filesystem::path& internalAssetPath,
    std::optional<std::string> assetId)
{
    std::filesystem::path resolved;
    const VoxelDocumentSessionResult validation =
        ResolveInternalAsset(internalAssetPath, resolved);
    if (!validation.Succeeded()) return validation;
    return Adopt(
        Asset::Voxel::VoxDocumentLoader{}.Load(
            resolved, std::move(assetId)));
}

VoxelDocumentSessionResult VoxelDocumentSession::OpenInspected(
    const std::filesystem::path& internalAssetPath,
    const Asset::Vox::VoxModel& inspectedModel,
    std::optional<std::string> assetId)
{
    std::filesystem::path resolved;
    const VoxelDocumentSessionResult validation =
        ResolveInternalAsset(internalAssetPath, resolved);
    if (!validation.Succeeded()) return validation;
    return Adopt(
        Asset::Voxel::VoxDocumentLoader{}.Build(
            inspectedModel, resolved, std::move(assetId)));
}

void VoxelDocumentSession::Close() noexcept
{
    if (activeDocument_)
    {
        activeDocument_.reset();
        ++generation_;
    }
}

bool VoxelDocumentSession::HasActiveDocument() const noexcept
{
    return activeDocument_ != nullptr;
}

Asset::Voxel::VoxelDocument* VoxelDocumentSession::ActiveDocument() noexcept
{
    return activeDocument_.get();
}

const Asset::Voxel::VoxelDocument*
VoxelDocumentSession::ActiveDocument() const noexcept
{
    return activeDocument_.get();
}

const std::filesystem::path& VoxelDocumentSession::SourcePath() const noexcept
{
    static const std::filesystem::path EmptyPath;
    return activeDocument_ ? activeDocument_->SourcePath() : EmptyPath;
}

bool VoxelDocumentSession::IsDirty() const noexcept
{
    return activeDocument_ && activeDocument_->IsDirty();
}

std::uint64_t VoxelDocumentSession::Revision() const noexcept
{
    return activeDocument_ ? activeDocument_->GetRevision() : 0U;
}

std::uint64_t VoxelDocumentSession::Generation() const noexcept
{
    return generation_;
}

VoxelDocumentSessionResult VoxelDocumentSession::ResolveInternalAsset(
    const std::filesystem::path& candidate,
    std::filesystem::path& resolved) const
{
    if (projectRoot_.empty() || assetsRoot_.empty())
        return Failure(VoxelDocumentSessionError::NoProject,
            "No project is configured for the voxel document session.");
    std::error_code error;
    const auto linkStatus = std::filesystem::symlink_status(candidate, error);
    if (error || std::filesystem::is_symlink(linkStatus) ||
        !std::filesystem::is_regular_file(linkStatus))
        return Failure(VoxelDocumentSessionError::InvalidAsset,
            "Voxel document source must be a regular, non-symbolic file.");
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(candidate, error);
    if (error || !IsWithin(canonical, assetsRoot_))
        return Failure(VoxelDocumentSessionError::ExternalAsset,
            "Voxel document source must be an internal project asset.");
    resolved = canonical;
    return {};
}

VoxelDocumentSessionResult VoxelDocumentSession::Adopt(
    Asset::Voxel::VoxDocumentLoadResult loaded)
{
    if (!loaded.Succeeded())
        return Failure(
            VoxelDocumentSessionError::DocumentLoadFailed,
            loaded.Message,
            loaded.Error);
    auto replacement = std::make_unique<Asset::Voxel::VoxelDocument>(
        std::move(*loaded.Document));
    activeDocument_ = std::move(replacement);
    ++generation_;
    return {};
}

} // namespace VoxelForge::Editor
