#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class DragDropImportState
{
    Idle,
    HoverValid,
    HoverInvalid,
    AwaitingConfirmation,
    Importing,
    Completed,
    Cancelled
};

enum class DragDropImportTarget
{
    None,
    AssetBrowser,
    Viewport
};

struct DragDropRect final
{
    float Left = 0.0F;
    float Top = 0.0F;
    float Right = 0.0F;
    float Bottom = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool Contains(float x, float y) const noexcept;
};

struct DragDropImportRequest final
{
    DragDropImportTarget Target = DragDropImportTarget::None;
    std::vector<std::filesystem::path> Paths;
    bool Accepted = false;
    std::string Message;

    [[nodiscard]] bool ShouldOpenViewport() const noexcept
    {
        return Accepted && Target == DragDropImportTarget::Viewport &&
            Paths.size() == 1U;
    }
};

class DragDropImportController final
{
public:
    void Begin(float x, float y) noexcept;
    void UpdatePosition(
        float x,
        float y,
        const DragDropRect& assetBrowser,
        const DragDropRect& viewport) noexcept;
    void AddFile(
        const std::filesystem::path& path,
        float x,
        float y,
        const DragDropRect& assetBrowser,
        const DragDropRect& viewport);
    [[nodiscard]] DragDropImportRequest Complete(bool hasActiveProject);
    void MarkImporting() noexcept;
    void MarkCompleted() noexcept;
    void Cancel() noexcept;
    void Reset() noexcept;

    [[nodiscard]] DragDropImportState State() const noexcept;
    [[nodiscard]] DragDropImportTarget Target() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& Paths() const noexcept;
    [[nodiscard]] std::string HoverMessage() const;

    [[nodiscard]] static bool IsSupportedVox(
        const std::filesystem::path& path);

private:
    void UpdateHoverState() noexcept;
    void SortAndDeduplicate();

    DragDropImportState state_ = DragDropImportState::Idle;
    DragDropImportTarget target_ = DragDropImportTarget::None;
    std::vector<std::filesystem::path> paths_;
    bool containsUnsupported_ = false;
};

} // namespace VoxelForge::Editor
