#include "ModelImport/ModelImportBatch.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

ModelImportResult ResultFor(
    const ModelImportStatus status,
    const std::filesystem::path& source,
    const std::filesystem::path& destination = {})
{
    ModelImportResult result;
    result.Status = status;
    result.SourcePath = source;
    result.DestinationPath = destination;
    return result;
}

void TestBeginResetsEverything()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox"});
    static_cast<void>(batch.Classify(
        ResultFor(ModelImportStatus::Imported, "a.vox", "Assets/a.vox")));

    batch.Begin({"c.vox"});
    Require(batch.RequestedCount() == 1U, "Begin should reset the request count.");
    Require(batch.CompletedCount() == 0U, "Begin should reset the counters.");
    Require(batch.SuccessfulPaths().empty(),
        "Begin should clear previous successes.");
    Require(batch.HasPending() && batch.CurrentSource() == "c.vox",
        "Begin should queue the new sources.");
}

void TestSuccessSkipFailureAdvanceTheQueue()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox", "c.vox"});

    Require(batch.Classify(ResultFor(
            ModelImportStatus::Imported, "a.vox", "Assets/Models/a.vox")) ==
            ModelImportBatchStep::Imported,
        "Imported result should classify as Imported.");
    Require(batch.CurrentSource() == "b.vox", "Success should advance.");

    Require(batch.Classify(ResultFor(ModelImportStatus::Skipped, "b.vox")) ==
            ModelImportBatchStep::Skipped,
        "Skipped result should classify as Skipped.");
    Require(batch.CurrentSource() == "c.vox", "Skip should advance.");

    Require(batch.Classify(ResultFor(ModelImportStatus::Failed, "c.vox")) ==
            ModelImportBatchStep::Failed,
        "Failed result should classify as Failed.");
    Require(!batch.HasPending(), "The queue should be exhausted.");
    Require(batch.CompletedCount() == 1U && batch.SkippedCount() == 1U &&
            batch.FailedCount() == 1U,
        "Counters should reflect one of each outcome.");
    Require(batch.SuccessfulPaths().size() == 1U &&
            batch.SuccessfulPaths().front() == "Assets/Models/a.vox",
        "Only the imported destination should be recorded.");
}

void TestReplacedAndRenamedCountAsSuccess()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox"});
    Require(batch.Classify(ResultFor(
            ModelImportStatus::Replaced, "a.vox", "Assets/a.vox")) ==
            ModelImportBatchStep::Imported,
        "Replaced should count as a success.");
    Require(batch.Classify(ResultFor(
            ModelImportStatus::Renamed, "b.vox", "Assets/b (1).vox")) ==
            ModelImportBatchStep::Imported,
        "Renamed should count as a success.");
    Require(batch.CompletedCount() == 2U,
        "Both successes should be counted.");
}

void TestCollisionPausesWithoutAdvancing()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox"});

    Require(batch.Classify(ResultFor(
            ModelImportStatus::Collision, "a.vox", "Assets/a.vox")) ==
            ModelImportBatchStep::Collision,
        "Collision result should classify as Collision.");
    Require(batch.HasPending() && batch.CurrentSource() == "a.vox",
        "Collision must not advance the queue.");
    Require(batch.PendingCollision().has_value() &&
            batch.PendingCollision()->DestinationPath == "Assets/a.vox",
        "The collision should be exposed for the dialog.");

    // The user answered (e.g. Rename): the retried import succeeds.
    Require(batch.Classify(ResultFor(
            ModelImportStatus::Renamed, "a.vox", "Assets/a (1).vox")) ==
            ModelImportBatchStep::Imported,
        "The retried import should classify as Imported.");
    Require(!batch.PendingCollision().has_value(),
        "A resolved collision should be cleared.");
    Require(batch.CurrentSource() == "b.vox",
        "The queue should advance after the resolution.");
}

void TestCancellationKeepsCollisionCleared()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox"});
    static_cast<void>(batch.Classify(
        ResultFor(ModelImportStatus::Collision, "a.vox", "Assets/a.vox")));
    Require(batch.Classify(ResultFor(ModelImportStatus::Cancelled, "a.vox")) ==
            ModelImportBatchStep::Cancelled,
        "Cancelled result should classify as Cancelled.");
    Require(!batch.PendingCollision().has_value(),
        "Cancellation should clear the pending collision.");
}

void TestSingleSuccessDecision()
{
    ModelImportBatch single;
    single.Begin({"a.vox"});
    static_cast<void>(single.Classify(
        ResultFor(ModelImportStatus::Imported, "a.vox", "Assets/a.vox")));
    Require(single.HasSingleSuccess(),
        "One request with one success should report a single success.");

    ModelImportBatch pair;
    pair.Begin({"a.vox", "b.vox"});
    static_cast<void>(pair.Classify(
        ResultFor(ModelImportStatus::Imported, "a.vox", "Assets/a.vox")));
    static_cast<void>(pair.Classify(
        ResultFor(ModelImportStatus::Skipped, "b.vox")));
    Require(!pair.HasSingleSuccess(),
        "Two requests should never report a single success.");

    ModelImportBatch failed;
    failed.Begin({"a.vox"});
    static_cast<void>(failed.Classify(
        ResultFor(ModelImportStatus::Failed, "a.vox")));
    Require(!failed.HasSingleSuccess(),
        "A failed single import should not report a success.");
}

void TestResetClearsState()
{
    ModelImportBatch batch;
    batch.Begin({"a.vox", "b.vox"});
    static_cast<void>(batch.Classify(
        ResultFor(ModelImportStatus::Imported, "a.vox", "Assets/a.vox")));
    batch.Reset();
    Require(!batch.HasPending(), "Reset should empty the queue.");
    Require(batch.SuccessfulPaths().empty() && batch.RequestedCount() == 0U &&
            batch.CompletedCount() == 0U,
        "Reset should clear successes and counters.");
    Require(!batch.PendingCollision().has_value(),
        "Reset should clear any pending collision.");
}

} // namespace

int main()
{
    try
    {
        TestBeginResetsEverything();
        TestSuccessSkipFailureAdvanceTheQueue();
        TestReplacedAndRenamedCountAsSuccess();
        TestCollisionPausesWithoutAdvancing();
        TestCancellationKeepsCollisionCleared();
        TestSingleSuccessDecision();
        TestResetClearsState();
        std::cout << "Model import batch tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Model import batch tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
