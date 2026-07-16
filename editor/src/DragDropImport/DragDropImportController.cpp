#include "DragDropImportController.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_set>

namespace VoxelForge::Editor
{
namespace
{
std::string FoldPath(const std::filesystem::path& path)
{
    std::string value = path.generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
#endif
    return value;
}

std::filesystem::path NormalizedAbsolute(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}
}

bool DragDropRect::IsValid() const noexcept
{
    return Right > Left && Bottom > Top;
}

bool DragDropRect::Contains(const float x, const float y) const noexcept
{
    return IsValid() && x >= Left && x <= Right && y >= Top && y <= Bottom;
}

void DragDropImportController::Begin(const float, const float) noexcept
{
    if (state_ == DragDropImportState::AwaitingConfirmation ||
        state_ == DragDropImportState::Importing)
        return;
    Reset();
    state_ = DragDropImportState::HoverValid;
}

void DragDropImportController::UpdatePosition(
    const float x,
    const float y,
    const DragDropRect& assetBrowser,
    const DragDropRect& viewport) noexcept
{
    if (state_ != DragDropImportState::HoverValid &&
        state_ != DragDropImportState::HoverInvalid)
        return;
    if (assetBrowser.Contains(x, y))
        target_ = DragDropImportTarget::AssetBrowser;
    else if (viewport.Contains(x, y))
        target_ = DragDropImportTarget::Viewport;
    else
        target_ = DragDropImportTarget::None;
    UpdateHoverState();
}

void DragDropImportController::AddFile(
    const std::filesystem::path& path,
    const float x,
    const float y,
    const DragDropRect& assetBrowser,
    const DragDropRect& viewport)
{
    if (state_ != DragDropImportState::HoverValid &&
        state_ != DragDropImportState::HoverInvalid)
        return;
    paths_.push_back(NormalizedAbsolute(path));
    containsUnsupported_ |= !IsSupportedVox(path);
    UpdatePosition(x, y, assetBrowser, viewport);
}

DragDropImportRequest DragDropImportController::Complete(
    const bool hasActiveProject)
{
    SortAndDeduplicate();
    DragDropImportRequest request;
    request.Target = target_;
    request.Paths = paths_;
    if (target_ == DragDropImportTarget::None)
    {
        state_ = DragDropImportState::Cancelled;
        return request;
    }
    if (!hasActiveProject)
    {
        request.Message = "Open or create a project before importing models.";
        state_ = DragDropImportState::Cancelled;
        return request;
    }
    if (paths_.empty() || containsUnsupported_)
    {
        request.Message = "Unsupported file type";
        state_ = DragDropImportState::Cancelled;
        return request;
    }
    request.Accepted = true;
    state_ = DragDropImportState::AwaitingConfirmation;
    return request;
}

void DragDropImportController::MarkImporting() noexcept
{
    if (state_ == DragDropImportState::AwaitingConfirmation)
        state_ = DragDropImportState::Importing;
}

void DragDropImportController::MarkCompleted() noexcept
{
    state_ = DragDropImportState::Completed;
    target_ = DragDropImportTarget::None;
}

void DragDropImportController::Cancel() noexcept
{
    state_ = DragDropImportState::Cancelled;
    target_ = DragDropImportTarget::None;
}

void DragDropImportController::Reset() noexcept
{
    state_ = DragDropImportState::Idle;
    target_ = DragDropImportTarget::None;
    paths_.clear();
    containsUnsupported_ = false;
}

DragDropImportState DragDropImportController::State() const noexcept
{
    return state_;
}

DragDropImportTarget DragDropImportController::Target() const noexcept
{
    return target_;
}

const std::vector<std::filesystem::path>&
DragDropImportController::Paths() const noexcept
{
    return paths_;
}

std::string DragDropImportController::HoverMessage() const
{
    if (state_ == DragDropImportState::HoverInvalid)
        return "Unsupported file type";
    if (state_ != DragDropImportState::HoverValid ||
        target_ == DragDropImportTarget::None)
        return {};
    if (target_ == DragDropImportTarget::AssetBrowser || paths_.size() != 1U)
        return "Drop VOX models to import";
    return "Drop VOX model to import";
}

bool DragDropImportController::IsSupportedVox(
    const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ".vox";
}

void DragDropImportController::UpdateHoverState() noexcept
{
    state_ = containsUnsupported_
        ? DragDropImportState::HoverInvalid
        : DragDropImportState::HoverValid;
}

void DragDropImportController::SortAndDeduplicate()
{
    std::sort(paths_.begin(), paths_.end(),
        [](const auto& left, const auto& right)
        {
            return FoldPath(left) < FoldPath(right);
        });
    std::unordered_set<std::string> seen;
    std::erase_if(paths_, [&seen](const std::filesystem::path& path)
    {
        return !seen.insert(FoldPath(path)).second;
    });
}

} // namespace VoxelForge::Editor
