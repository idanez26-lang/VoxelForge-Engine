#pragma once

#include "ModelImport/ModelImportService.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace VoxelForge::Editor
{

// Outcome of classifying one service result inside a batch.
enum class ModelImportBatchStep : std::uint8_t
{
    Imported,  // Success (imported, replaced or renamed) — queue advanced.
    Skipped,   // Collision skipped by the user — queue advanced.
    Failed,    // Import error — queue advanced.
    Collision, // Waiting for a user decision — queue NOT advanced.
    Cancelled  // The user cancelled the whole batch.
};

// Pure state machine for a multi-file model import: queue, counters and
// per-result classification. No service call, no I/O, no UI: EditorWorkspace
// pumps ModelImportService and reacts to each ModelImportBatchStep, then
// reads the end-of-batch decisions below.
class ModelImportBatch final
{
public:
    void Begin(std::vector<std::filesystem::path> sources);
    void Reset() noexcept;

    [[nodiscard]] bool HasPending() const noexcept;
    [[nodiscard]] const std::filesystem::path& CurrentSource() const;
    [[nodiscard]] ModelImportBatchStep Classify(
        const ModelImportResult& result);

    [[nodiscard]] std::size_t RequestedCount() const noexcept;
    [[nodiscard]] std::size_t CompletedCount() const noexcept;
    [[nodiscard]] std::size_t SkippedCount() const noexcept;
    [[nodiscard]] std::size_t FailedCount() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>&
        SuccessfulPaths() const noexcept;
    [[nodiscard]] const std::optional<ModelImportResult>&
        PendingCollision() const noexcept;

    // True when exactly one import was requested and it succeeded — drives
    // both the drop-to-viewport auto-open and the "open imported model" offer.
    [[nodiscard]] bool HasSingleSuccess() const noexcept;

private:
    std::vector<std::filesystem::path> pendingPaths_;
    std::vector<std::filesystem::path> successfulPaths_;
    std::optional<ModelImportResult> pendingCollision_;
    std::size_t pendingIndex_ = 0U;
    std::size_t requestedCount_ = 0U;
    std::size_t completedCount_ = 0U;
    std::size_t skippedCount_ = 0U;
    std::size_t failedCount_ = 0U;
};

} // namespace VoxelForge::Editor
