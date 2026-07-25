#pragma once

#include "VoxelStamps/Capture/StampCaptureService.h"
#include "VoxelStamps/Library/IStampCatalogStore.h"
#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class SaveSelectionAsStampStatus
{
    Ready,
    Cancelled,
    CompleteSuccess,
    PartialSuccess,
    Refused,
    CaptureFailed,
    InstallFailed,
};

struct SaveSelectionAsStampDraft final
{
    std::string Name;
    bool ConfirmSoftLimit = false;
};

struct SaveSelectionAsStampBeginRequest final
{
    std::filesystem::path ProjectRoot;
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    const SelectionService* Selection = nullptr;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t SubModelIndex = 0U;
    StampPivotContext PivotContext{};
    StampResourceLimits Limits = DefaultStampResourceLimits();
};

struct SaveSelectionAsStampCurrentContext final
{
    std::filesystem::path ProjectRoot;
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    const SelectionService* Selection = nullptr;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
};

struct SaveSelectionAsStampResult final
{
    SaveSelectionAsStampStatus Status = SaveSelectionAsStampStatus::Refused;
    std::string Message;
    bool RequiresSoftLimitConfirmation = false;
    std::optional<StampAssetReference> InstalledAsset;
    StampCaptureResult Capture;
    StampLibraryResult Installation;
    StampCatalogResult Catalogue;

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Status == SaveSelectionAsStampStatus::CompleteSuccess ||
               Status == SaveSelectionAsStampStatus::PartialSuccess;
    }
};

/// UI-independent STAMP-08 application workflow. Begin captures an immutable
/// selection snapshot immediately; Save only installs that captured Stamp and
/// refreshes the derived catalogue. It never mutates the document or selection.
class SaveSelectionAsStampWorkflow final
{
public:
    SaveSelectionAsStampWorkflow(
        IStampLibraryRepository& library,
        IStampCatalogStore& catalogueStore);

    [[nodiscard]] SaveSelectionAsStampResult Begin(
        const SaveSelectionAsStampBeginRequest& request);
    [[nodiscard]] SaveSelectionAsStampResult ValidateDraft(
        const SaveSelectionAsStampDraft& draft) const;
    [[nodiscard]] SaveSelectionAsStampResult Save(
        const SaveSelectionAsStampDraft& draft,
        const SaveSelectionAsStampCurrentContext& current);
    [[nodiscard]] SaveSelectionAsStampResult Cancel() noexcept;

    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] bool RequiresSoftLimitConfirmation() const noexcept;

private:
    struct Snapshot final
    {
        std::filesystem::path ProjectRoot;
        const Asset::Voxel::VoxelDocument* Document = nullptr;
        std::uint64_t DocumentGeneration = 0U;
        std::uint64_t DocumentRevision = 0U;
        std::vector<Asset::Voxel::VoxelPosition> SelectionVoxels;
        StampCaptureResult Capture;
        bool RequiresSoftLimitConfirmation = false;
    };

    [[nodiscard]] static std::string ValidateName(std::string_view name);
    [[nodiscard]] static bool SameProjectRoot(
        const std::filesystem::path& left,
        const std::filesystem::path& right) noexcept;

    IStampLibraryRepository& library_;
    IStampCatalogStore& catalogueStore_;
    std::optional<Snapshot> snapshot_;
    bool saving_ = false;
};

} // namespace VoxelForge::Editor::Stamps
