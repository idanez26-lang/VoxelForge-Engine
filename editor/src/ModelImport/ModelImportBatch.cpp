#include "ModelImport/ModelImportBatch.h"

#include <utility>

namespace VoxelForge::Editor
{

void ModelImportBatch::Begin(std::vector<std::filesystem::path> sources)
{
    pendingPaths_ = std::move(sources);
    successfulPaths_.clear();
    pendingCollision_.reset();
    pendingIndex_ = 0U;
    requestedCount_ = pendingPaths_.size();
    completedCount_ = 0U;
    skippedCount_ = 0U;
    failedCount_ = 0U;
}

void ModelImportBatch::Reset() noexcept
{
    pendingPaths_.clear();
    successfulPaths_.clear();
    pendingCollision_.reset();
    pendingIndex_ = 0U;
    requestedCount_ = 0U;
    completedCount_ = 0U;
    skippedCount_ = 0U;
    failedCount_ = 0U;
}

bool ModelImportBatch::HasPending() const noexcept
{
    return pendingIndex_ < pendingPaths_.size();
}

const std::filesystem::path& ModelImportBatch::CurrentSource() const
{
    return pendingPaths_[pendingIndex_];
}

ModelImportBatchStep ModelImportBatch::Classify(const ModelImportResult& result)
{
    if (result.Status == ModelImportStatus::Collision)
    {
        pendingCollision_ = result;
        return ModelImportBatchStep::Collision;
    }
    if (result.Status == ModelImportStatus::Cancelled)
    {
        pendingCollision_.reset();
        return ModelImportBatchStep::Cancelled;
    }

    ModelImportBatchStep step = ModelImportBatchStep::Failed;
    if (result.Succeeded())
    {
        ++completedCount_;
        successfulPaths_.push_back(result.DestinationPath);
        step = ModelImportBatchStep::Imported;
    }
    else if (result.Status == ModelImportStatus::Skipped)
    {
        ++skippedCount_;
        step = ModelImportBatchStep::Skipped;
    }
    else
    {
        ++failedCount_;
    }
    ++pendingIndex_;
    pendingCollision_.reset();
    return step;
}

std::size_t ModelImportBatch::RequestedCount() const noexcept
{
    return requestedCount_;
}

std::size_t ModelImportBatch::CompletedCount() const noexcept
{
    return completedCount_;
}

std::size_t ModelImportBatch::SkippedCount() const noexcept
{
    return skippedCount_;
}

std::size_t ModelImportBatch::FailedCount() const noexcept
{
    return failedCount_;
}

const std::vector<std::filesystem::path>&
ModelImportBatch::SuccessfulPaths() const noexcept
{
    return successfulPaths_;
}

const std::optional<ModelImportResult>&
ModelImportBatch::PendingCollision() const noexcept
{
    return pendingCollision_;
}

bool ModelImportBatch::HasSingleSuccess() const noexcept
{
    return requestedCount_ == 1U && successfulPaths_.size() == 1U;
}

} // namespace VoxelForge::Editor
