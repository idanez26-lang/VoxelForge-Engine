#include "StampPlacementBenchmarkMetrics.h"

#include "Commands/Voxel/VoxelEditSession.h"
#include "ViewportRenderer.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Palette/PaletteMappingEngine.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/MeshVertex.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Benchmarks;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t BenchmarkDocumentGeneration = 1U;

std::string BenchmarkRendererBackend;
std::string BenchmarkShaderFormats;

struct Dataset final
{
    std::size_t VoxelCount = 0U;
    std::uint32_t Width = 0U;
    std::uint32_t Height = 0U;
    std::uint32_t Depth = 0U;
    std::size_t Iterations = 0U;
};

constexpr std::array<Dataset, 6U> Datasets{{
    {64U, 4U, 4U, 4U, 20U},
    {512U, 8U, 8U, 8U, 15U},
    {4'096U, 16U, 16U, 16U, 10U},
    {32'768U, 32U, 32U, 32U, 7U},
    {131'072U, 64U, 64U, 32U, 5U},
    {262'144U, 64U, 64U, 64U, 5U},
}};

struct PhaseAggregate final
{
    std::string Name;
    std::size_t VoxelCount = 0U;
    std::uint64_t Calls = 0U;
    std::uint64_t TotalNanoseconds = 0U;
    std::uint64_t MaximumNanoseconds = 0U;
    std::uint64_t AllocationCount = 0U;
    std::uint64_t AllocatedBytes = 0U;
    std::uint64_t MaximumPeakLiveBytes = 0U;
    std::uint64_t MaximumRetainedBytes = 0U;
    std::uint64_t LogicalCopyCount = 0U;
    std::uint64_t LogicalCopyBytes = 0U;
    std::uint64_t ProcessedVoxels = 0U;

    void Add(
        const std::uint64_t elapsedNanoseconds,
        const AllocationSnapshot allocations,
        const std::uint64_t logicalCopyCount,
        const std::uint64_t logicalCopyBytes,
        const std::uint64_t processedVoxels) noexcept
    {
        ++Calls;
        TotalNanoseconds += elapsedNanoseconds;
        MaximumNanoseconds =
            std::max(MaximumNanoseconds, elapsedNanoseconds);
        AllocationCount += allocations.AllocationCount;
        AllocatedBytes += allocations.AllocatedBytes;
        MaximumPeakLiveBytes = std::max(
            MaximumPeakLiveBytes, allocations.PeakLiveBytes);
        MaximumRetainedBytes = std::max(
            MaximumRetainedBytes, allocations.RetainedBytes);
        LogicalCopyCount += logicalCopyCount;
        LogicalCopyBytes += logicalCopyBytes;
        ProcessedVoxels += processedVoxels;
    }

    [[nodiscard]] double MeanMicroseconds() const noexcept
    {
        return Calls == 0U
            ? 0.0
            : static_cast<double>(TotalNanoseconds) /
                static_cast<double>(Calls) / 1'000.0;
    }

    [[nodiscard]] double MaximumMicroseconds() const noexcept
    {
        return static_cast<double>(MaximumNanoseconds) / 1'000.0;
    }
};

[[nodiscard]] std::uint64_t ElapsedNanoseconds(
    const Clock::time_point start,
    const Clock::time_point finish) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            finish - start).count());
}

[[noreturn]] void Fail(const std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        Fail(message);
    }
}

[[nodiscard]] std::uint64_t SaturatingMultiply(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

[[nodiscard]] std::uint64_t GridCellCount(const Dataset& dataset) noexcept
{
    return SaturatingMultiply(
        SaturatingMultiply(dataset.Width, dataset.Height),
        dataset.Depth);
}

[[nodiscard]] Asset::Voxel::VoxelDocument MakeDocument(
    const Dataset& dataset)
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({
        .Dimensions = {
            dataset.Width, dataset.Height, dataset.Depth}});
    const Asset::Voxel::VoxDocumentLoadResult loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(
            source, "stamp-16-benchmark.vox");
    Require(
        loaded.Succeeded() && loaded.Document.has_value(),
        "Unable to create the benchmark document.");
    return std::move(*loaded.Document);
}

[[nodiscard]] Voxel::VoxelModel MakeCompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U;
         index < document.GetPalette().size(); ++index)
    {
        const Asset::Voxel::VoxelColor color =
            document.GetPalette()[index];
        Require(
            model.Palette().Set(
                index,
                {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize the benchmark compatibility palette.");
    }

    const auto* const source = document.GetModel(0U);
    Require(source != nullptr, "Benchmark document has no model.");
    const Asset::Voxel::VoxelDimensions dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(
        grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize the benchmark compatibility grid.");
    model.AddGrid(std::move(grid));
    return model;
}

[[nodiscard]] VoxelStamp MakeStamp(const Dataset& dataset)
{
    std::vector<StampVoxel> voxels;
    voxels.reserve(dataset.VoxelCount);
    for (std::uint32_t z = 0U; z < dataset.Depth; ++z)
    {
        for (std::uint32_t y = 0U; y < dataset.Height; ++y)
        {
            for (std::uint32_t x = 0U; x < dataset.Width; ++x)
            {
                voxels.push_back({
                    {static_cast<std::int32_t>(x),
                     static_cast<std::int32_t>(y),
                     static_cast<std::int32_t>(z)},
                    0U});
            }
        }
    }
    Require(
        voxels.size() == dataset.VoxelCount,
        "Benchmark dataset dimensions do not match its voxel count.");

    StampValidationResult validation{};
    const std::optional<VoxelStamp> stamp = VoxelStamp::TryCreate(
        {Core::UUID{
             0x5354414d50000000ULL +
             static_cast<std::uint64_t>(dataset.VoxelCount)},
         "stamp-16-" + std::to_string(dataset.VoxelCount)},
        {{0, 0, 0},
         {static_cast<std::int32_t>(dataset.Width - 1U),
          static_cast<std::int32_t>(dataset.Height - 1U),
          static_cast<std::int32_t>(dataset.Depth - 1U)},
         {dataset.Width, dataset.Height, dataset.Depth}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {},
        {{0U, {13U, 37U, 73U, 255U}}},
        std::move(voxels),
        DefaultStampResourceLimits(),
        &validation);
    Require(
        stamp.has_value() && validation.IsValid(),
        "Unable to create the benchmark Stamp.");
    return *stamp;
}

[[nodiscard]] Voxel::VoxelPalette MakeRenderPalette(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelPalette palette;
    for (std::size_t index = 0U;
         index < document.GetPalette().size(); ++index)
    {
        const Asset::Voxel::VoxelColor color =
            document.GetPalette()[index];
        Require(
            palette.Set(
                index,
                {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to prepare the benchmark render palette.");
    }
    return palette;
}

[[nodiscard]] StampPlacementPlannerRequest MakePlannerRequest(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document) noexcept
{
    return {
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = BenchmarkDocumentGeneration,
        .TargetSubModel = 0U,
        .Transform = {},
        .CollisionPolicy = StampCollisionPolicy::Overwrite};
}

[[nodiscard]] PaletteMappingRequest MakePaletteRequest(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document) noexcept
{
    return {
        .StampPalette = stamp.Palette(),
        .StampVoxels = stamp.Voxels(),
        .DocumentPalette = document.GetPaletteSnapshot()};
}

[[nodiscard]] std::uint64_t PalettePlanCopyCount(
    const PaletteMappingResult& result) noexcept
{
    return 1U +
        static_cast<std::uint64_t>(
            result.Plan.LocalToDocument.size() +
            result.Plan.ReusedColors.size() +
            result.Plan.AddedColors.size());
}

[[nodiscard]] std::uint64_t PalettePlanCopyBytes(
    const PaletteMappingResult& result) noexcept
{
    return sizeof(Asset::Voxel::VoxelDocumentPaletteSnapshot) +
        SaturatingMultiply(
            result.Plan.LocalToDocument.size(),
            sizeof(PaletteMappingEntry)) +
        SaturatingMultiply(
            result.Plan.ReusedColors.size() +
                result.Plan.AddedColors.size(),
            sizeof(PlannedPaletteColor));
}

[[nodiscard]] std::uint64_t PlanCopyBytes(
    const StampPlacementPlan& plan) noexcept
{
    return sizeof(Asset::Voxel::VoxelDocumentPaletteSnapshot) +
        SaturatingMultiply(
            plan.Voxels.size(), sizeof(StampPlannedVoxel)) +
        SaturatingMultiply(
            plan.Diagnostics.size(), sizeof(StampPlacementDiagnostic));
}

[[nodiscard]] std::uint64_t PreviewCopyBytes(
    const VoxelPreviewData& preview) noexcept
{
    return SaturatingMultiply(
        preview.Voxels.size(), sizeof(VoxelPreviewVoxel));
}

[[nodiscard]] std::uint64_t OperationCopyBytes(
    const VoxelEditOperation& operation) noexcept
{
    std::uint64_t bytes = SaturatingMultiply(
        operation.Changes.size(), sizeof(VoxelChange));
    if (operation.PaletteChange)
    {
        bytes += sizeof(VoxelPaletteChange);
    }
    return bytes;
}

struct TransactionCopyEstimate final
{
    std::uint64_t Count = 0U;
    std::uint64_t Bytes = 0U;
};

[[nodiscard]] TransactionCopyEstimate EstimateTransactionCopies(
    const Dataset& dataset,
    const std::size_t changeCount,
    const bool hasPaletteChange) noexcept
{
    const std::uint64_t cells = GridCellCount(dataset);
    const std::uint64_t paletteCopies = hasPaletteChange ? 3U : 2U;
    return {
        static_cast<std::uint64_t>(changeCount) + cells + paletteCopies,
        SaturatingMultiply(changeCount, sizeof(VoxelChange)) +
            SaturatingMultiply(cells, sizeof(Voxel::Voxel)) +
            SaturatingMultiply(
                paletteCopies,
                sizeof(Asset::Voxel::VoxelDocumentPaletteSnapshot))};
}

template <typename Function>
void Measure(
    PhaseAggregate& aggregate,
    Function&& function,
    const std::uint64_t logicalCopyCount,
    const std::uint64_t logicalCopyBytes,
    const std::uint64_t processedVoxels)
{
    AllocationSnapshot allocationSnapshot{};
    const Clock::time_point start = Clock::now();
    {
        AllocationScope allocations;
        function();
        allocationSnapshot = allocations.Snapshot();
    }
    const Clock::time_point finish = Clock::now();
    aggregate.Add(
        ElapsedNanoseconds(start, finish),
        allocationSnapshot,
        logicalCopyCount,
        logicalCopyBytes,
        processedVoxels);
}

class BenchmarkSession final : public VoxelEditSession
{
public:
    BenchmarkSession(
        Asset::Voxel::VoxelDocument& document,
        const std::size_t voxelCount)
        : document_(&document),
          model_(MakeCompatibilityModel(document)),
          voxelCount_(voxelCount)
    {
    }

    [[nodiscard]] std::uint64_t VoxelModelGeneration()
        const noexcept override
    {
        return BenchmarkDocumentGeneration;
    }

    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }

    [[nodiscard]] Asset::Voxel::VoxelDocument*
        ActiveVoxelDocument() noexcept override
    {
        return document_;
    }

    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    {
        AllocationSnapshot allocationSnapshot{};
        const Clock::time_point start = Clock::now();
        Mesh::VoxelDocumentMeshSyncResult synchronized{};
        {
            AllocationScope allocations;
            synchronized = cache_.Synchronize(
                *document_, BenchmarkDocumentGeneration);
            allocationSnapshot = allocations.Snapshot();
        }
        const Clock::time_point finish = Clock::now();
        LastMeshNanoseconds = ElapsedNanoseconds(start, finish);

        const Mesh::MeshData* const mesh = cache_.Mesh();
        const std::uint64_t retainedBytes = mesh == nullptr
            ? 0U
            : SaturatingMultiply(
                  mesh->VertexCount(), sizeof(Mesh::MeshVertex)) +
                SaturatingMultiply(
                    mesh->IndexCount(), sizeof(std::uint32_t));
        allocationSnapshot.RetainedBytes = std::max(
            allocationSnapshot.RetainedBytes, retainedBytes);
        Require(
            meshAggregate_ != nullptr,
            "Mesh benchmark phase was not selected.");
        meshAggregate_->Add(
            LastMeshNanoseconds,
            allocationSnapshot,
            0U,
            0U,
            voxelCount_);
        return synchronized.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(synchronized.Message);
    }

    void CompleteVoxelEdit() noexcept override
    {
        ++CompletedEdits;
    }

    [[nodiscard]] const Mesh::MeshData* Mesh() const noexcept
    {
        return cache_.Mesh();
    }

    void SelectMeshAggregate(PhaseAggregate& aggregate) noexcept
    {
        meshAggregate_ = &aggregate;
    }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
    std::size_t voxelCount_ = 0U;
    PhaseAggregate* meshAggregate_ = nullptr;
    std::uint64_t LastMeshNanoseconds = 0U;
    std::size_t CompletedEdits = 0U;
};

[[nodiscard]] std::uint64_t AdjustedCompositeNanoseconds(
    const std::uint64_t total,
    const std::uint64_t mesh) noexcept
{
    return mesh <= total ? total - mesh : 0U;
}

void MeasureTransaction(
    const Dataset& dataset,
    const VoxelStamp& stamp,
    PhaseAggregate& composite,
    PhaseAggregate& meshExecute,
    PhaseAggregate& meshUndo,
    PhaseAggregate& meshRedo,
    PhaseAggregate& undo,
    PhaseAggregate& redo,
    PhaseAggregate& rendererUpload,
    ViewportRenderer& renderer)
{
    for (std::size_t iteration = 0U;
         iteration < dataset.Iterations; ++iteration)
    {
        Asset::Voxel::VoxelDocument document = MakeDocument(dataset);
        BenchmarkSession session(document, dataset.VoxelCount);
        const StampPlacementPlan plan = StampPlacementPlanner::Build(
            MakePlannerRequest(stamp, document));
        PlaceVoxelStampPreparation prepared =
            PreparePlaceVoxelStampOperation(plan);
        Require(
            plan.CanCommit && prepared.IsReady(),
            "Benchmark transaction preparation failed.");

        const std::size_t changeCount =
            prepared.Operation.Changes.size();
        const bool hasPaletteChange =
            prepared.Operation.PaletteChange != nullptr;
        const TransactionCopyEstimate transactionCopies =
            EstimateTransactionCopies(
                dataset, changeCount, hasPaletteChange);
        VoxelEditHistory history({
            .MaximumCommandCount = 4U,
            .MaximumEstimatedMemory =
                1024U * 1024U * 1024U});

        session.SelectMeshAggregate(meshExecute);
        AllocationSnapshot executeAllocations{};
        const Clock::time_point executeStart = Clock::now();
        VoxelEditHistoryResult executeResult{};
        {
            AllocationScope allocations;
            executeResult = history.Execute(
                session, std::move(prepared.Operation));
            executeAllocations = allocations.Snapshot();
        }
        const Clock::time_point executeFinish = Clock::now();
        Require(
            static_cast<bool>(executeResult),
            "Benchmark placement transaction failed.");
        executeAllocations.RetainedBytes = std::max<std::uint64_t>(
            executeAllocations.RetainedBytes,
            history.EstimatedMemory());
        composite.Add(
            AdjustedCompositeNanoseconds(
                ElapsedNanoseconds(executeStart, executeFinish),
                session.LastMeshNanoseconds),
            executeAllocations,
            transactionCopies.Count,
            transactionCopies.Bytes,
            dataset.VoxelCount);

        session.SelectMeshAggregate(meshUndo);
        AllocationSnapshot undoAllocations{};
        const Clock::time_point undoStart = Clock::now();
        VoxelEditHistoryResult undoResult{};
        {
            AllocationScope allocations;
            undoResult = history.Undo(session);
            undoAllocations = allocations.Snapshot();
        }
        const Clock::time_point undoFinish = Clock::now();
        Require(
            static_cast<bool>(undoResult),
            "Benchmark Undo failed.");
        undoAllocations.RetainedBytes = std::max<std::uint64_t>(
            undoAllocations.RetainedBytes,
            history.EstimatedMemory());
        undo.Add(
            ElapsedNanoseconds(undoStart, undoFinish),
            undoAllocations,
            transactionCopies.Count,
            transactionCopies.Bytes,
            dataset.VoxelCount);

        session.SelectMeshAggregate(meshRedo);
        AllocationSnapshot redoAllocations{};
        const Clock::time_point redoStart = Clock::now();
        VoxelEditHistoryResult redoResult{};
        {
            AllocationScope allocations;
            redoResult = history.Redo(session);
            redoAllocations = allocations.Snapshot();
        }
        const Clock::time_point redoFinish = Clock::now();
        Require(
            static_cast<bool>(redoResult),
            "Benchmark Redo failed.");
        redoAllocations.RetainedBytes = std::max<std::uint64_t>(
            redoAllocations.RetainedBytes,
            history.EstimatedMemory());
        redo.Add(
            ElapsedNanoseconds(redoStart, redoFinish),
            redoAllocations,
            transactionCopies.Count,
            transactionCopies.Bytes,
            dataset.VoxelCount);

        const Mesh::MeshData* const mesh = session.Mesh();
        Require(mesh != nullptr, "Benchmark mesh was not built.");
        const Voxel::VoxelPalette palette = MakeRenderPalette(document);
        const std::uint64_t uploadBytes =
            SaturatingMultiply(mesh->VertexCount(), 40U) +
            SaturatingMultiply(
                mesh->IndexCount(), sizeof(std::uint32_t));
        if (iteration == 0U)
        {
            Require(
                renderer.Upload(
                    *mesh, palette,
                    {static_cast<float>(dataset.Width) * 0.5F,
                     static_cast<float>(dataset.Height) * 0.5F,
                     static_cast<float>(dataset.Depth) * 0.5F}),
                "Benchmark renderer warm-up failed: " +
                    renderer.LastError());
        }
        AllocationSnapshot uploadAllocations{};
        const Clock::time_point uploadStart = Clock::now();
        bool uploaded = false;
        {
            AllocationScope allocations;
            uploaded = renderer.Upload(
                *mesh, palette,
                {static_cast<float>(dataset.Width) * 0.5F,
                 static_cast<float>(dataset.Height) * 0.5F,
                 static_cast<float>(dataset.Depth) * 0.5F});
            uploadAllocations = allocations.Snapshot();
        }
        const Clock::time_point uploadFinish = Clock::now();
        Require(
            uploaded,
            "Benchmark renderer upload failed: " +
                renderer.LastError());
        uploadAllocations.RetainedBytes = std::max(
            uploadAllocations.RetainedBytes, uploadBytes);
        rendererUpload.Add(
            ElapsedNanoseconds(uploadStart, uploadFinish),
            uploadAllocations,
            static_cast<std::uint64_t>(mesh->VertexCount()) + 2U,
            uploadBytes,
            dataset.VoxelCount);
    }
}

void MeasurePurePipeline(
    const Dataset& dataset,
    const VoxelStamp& stamp,
    Asset::Voxel::VoxelDocument& document,
    PhaseAggregate& palette,
    PhaseAggregate& planner,
    PhaseAggregate& preview,
    PhaseAggregate& operation)
{
    PaletteMappingResult paletteResult{};
    StampPlacementPlan placementPlan{};
    VoxelPreviewData previewData{};
    PlaceVoxelStampPreparation preparation{};

    for (std::size_t iteration = 0U;
         iteration < dataset.Iterations; ++iteration)
    {
        paletteResult = {};
        Measure(
            palette,
            [&]()
            {
                paletteResult = PaletteMappingEngine::Plan(
                    MakePaletteRequest(stamp, document));
            },
            0U,
            0U,
            dataset.VoxelCount);
        Require(
            paletteResult.IsSuccess(),
            "Palette benchmark planning failed.");
        palette.LogicalCopyCount += PalettePlanCopyCount(paletteResult);
        palette.LogicalCopyBytes += PalettePlanCopyBytes(paletteResult);

        placementPlan = {};
        Measure(
            planner,
            [&]()
            {
                placementPlan = StampPlacementPlanner::Build(
                    MakePlannerRequest(stamp, document));
            },
            0U,
            0U,
            dataset.VoxelCount);
        Require(
            placementPlan.CanCommit,
            "Placement planner benchmark failed.");
        planner.LogicalCopyCount +=
            static_cast<std::uint64_t>(
                placementPlan.Voxels.size() +
                placementPlan.Diagnostics.size() + 1U);
        planner.LogicalCopyBytes += PlanCopyBytes(placementPlan);

        previewData = {};
        Measure(
            preview,
            [&]()
            {
                previewData =
                    StampLivePreviewBuilder::Build(placementPlan);
            },
            0U,
            0U,
            dataset.VoxelCount);
        Require(
            previewData.Voxels.size() == dataset.VoxelCount,
            "Preview benchmark did not retain every voxel.");
        preview.LogicalCopyCount +=
            static_cast<std::uint64_t>(previewData.Voxels.size());
        preview.LogicalCopyBytes += PreviewCopyBytes(previewData);

        preparation = {};
        Measure(
            operation,
            [&]()
            {
                preparation =
                    PreparePlaceVoxelStampOperation(placementPlan);
            },
            0U,
            0U,
            dataset.VoxelCount);
        Require(
            preparation.IsReady() &&
                preparation.Operation.Changes.size() ==
                    dataset.VoxelCount,
            "Placement operation benchmark failed.");
        operation.LogicalCopyCount +=
            static_cast<std::uint64_t>(
                preparation.Operation.Changes.size()) +
            (preparation.Operation.PaletteChange ? 1U : 0U);
        operation.LogicalCopyBytes +=
            OperationCopyBytes(preparation.Operation);
    }
}

[[nodiscard]] std::vector<PhaseAggregate> RunBenchmarks()
{
    if (!Renderer::Renderer::Initialize({
            .ApplicationName = "VoxelForge STAMP-16 Benchmark",
            .EnableValidation = false,
            .EnableVSync = false}))
    {
        Fail(
            "Unable to initialize the benchmark GPU device: " +
            Renderer::Renderer::GetLastError());
    }

    ViewportRenderer renderer;
    BenchmarkRendererBackend =
        Renderer::Renderer::GetBackendName();
    BenchmarkShaderFormats =
        Renderer::Renderer::GetShaderFormatsDescription();
    std::vector<PhaseAggregate> results;
    results.reserve(Datasets.size() * 11U);
    try
    {
        for (const Dataset& dataset : Datasets)
        {
            const VoxelStamp stamp = MakeStamp(dataset);
            Asset::Voxel::VoxelDocument document =
                MakeDocument(dataset);
            PhaseAggregate palette{
                "PaletteMappingEngine", dataset.VoxelCount};
            PhaseAggregate planner{
                "StampPlacementPlanner", dataset.VoxelCount};
            PhaseAggregate preview{
                "Preview", dataset.VoxelCount};
            PhaseAggregate operation{
                "PlaceVoxelStampOperation", dataset.VoxelCount};
            PhaseAggregate composite{
                "CompositeTransaction", dataset.VoxelCount};
            PhaseAggregate meshExecute{
                "MeshRebuild.Execute", dataset.VoxelCount};
            PhaseAggregate meshUndo{
                "MeshRebuild.Undo", dataset.VoxelCount};
            PhaseAggregate meshRedo{
                "MeshRebuild.Redo", dataset.VoxelCount};
            PhaseAggregate upload{
                "RendererUpload", dataset.VoxelCount};
            PhaseAggregate undo{
                "Undo", dataset.VoxelCount};
            PhaseAggregate redo{
                "Redo", dataset.VoxelCount};

            MeasurePurePipeline(
                dataset, stamp, document,
                palette, planner, preview, operation);
            MeasureTransaction(
                dataset, stamp,
                composite,
                meshExecute, meshUndo, meshRedo,
                undo, redo, upload, renderer);

            results.push_back(std::move(palette));
            results.push_back(std::move(planner));
            results.push_back(std::move(preview));
            results.push_back(std::move(operation));
            results.push_back(std::move(composite));
            results.push_back(std::move(meshExecute));
            results.push_back(std::move(meshUndo));
            results.push_back(std::move(meshRedo));
            results.push_back(std::move(upload));
            results.push_back(std::move(undo));
            results.push_back(std::move(redo));
        }
    }
    catch (...)
    {
        renderer.Shutdown();
        Renderer::Renderer::Shutdown();
        throw;
    }
    renderer.Shutdown();
    Renderer::Renderer::Shutdown();
    return results;
}

void WriteCsv(
    std::ostream& output,
    const std::span<const PhaseAggregate> results)
{
    output
        << "phase,voxels,calls,mean_us,max_us,total_us,"
           "allocations,allocated_bytes,peak_live_bytes,retained_bytes,"
           "logical_copies,logical_copy_bytes,processed_voxels\n";
    output << std::fixed << std::setprecision(3);
    for (const PhaseAggregate& result : results)
    {
        output
            << result.Name << ','
            << result.VoxelCount << ','
            << result.Calls << ','
            << result.MeanMicroseconds() << ','
            << result.MaximumMicroseconds() << ','
            << static_cast<double>(result.TotalNanoseconds) / 1'000.0
            << ','
            << result.AllocationCount << ','
            << result.AllocatedBytes << ','
            << result.MaximumPeakLiveBytes << ','
            << result.MaximumRetainedBytes << ','
            << result.LogicalCopyCount << ','
            << result.LogicalCopyBytes << ','
            << result.ProcessedVoxels << '\n';
    }
}

void WriteJson(
    std::ostream& output,
    const std::span<const PhaseAggregate> results)
{
    output << "{\n"
           << "  \"schema\": \"VoxelForge.STAMP16.Performance.v1\",\n"
           << "  \"clock\": \"steady_clock\",\n"
           << "  \"renderer_backend\": \""
           << BenchmarkRendererBackend << "\",\n"
           << "  \"shader_formats\": \""
           << BenchmarkShaderFormats << "\",\n"
           << "  \"compiler\": \"MSVC "
#if defined(_MSC_FULL_VER)
           << _MSC_FULL_VER
#else
           << "unknown"
#endif
           << "\",\n"
           << "  \"build\": \""
#if defined(NDEBUG)
           << "Release"
#else
           << "Debug"
#endif
           << "\",\n"
           << "  \"allocation_scope\": "
              "\"C++ operator new on benchmark thread; SDL/driver native "
              "allocations excluded\",\n"
           << "  \"copy_scope\": "
              "\"logical payload-copy estimate from public data contracts\",\n"
           << "  \"rows\": [\n";
    for (std::size_t index = 0U; index < results.size(); ++index)
    {
        const PhaseAggregate& result = results[index];
        output << "    {\n"
               << "      \"phase\": \"" << result.Name << "\",\n"
               << "      \"voxels\": " << result.VoxelCount << ",\n"
               << "      \"calls\": " << result.Calls << ",\n"
               << "      \"mean_us\": " << std::fixed
               << std::setprecision(3) << result.MeanMicroseconds()
               << ",\n"
               << "      \"max_us\": "
               << result.MaximumMicroseconds() << ",\n"
               << "      \"total_us\": "
               << static_cast<double>(result.TotalNanoseconds) / 1'000.0
               << ",\n"
               << "      \"allocations\": "
               << result.AllocationCount << ",\n"
               << "      \"allocated_bytes\": "
               << result.AllocatedBytes << ",\n"
               << "      \"peak_live_bytes\": "
               << result.MaximumPeakLiveBytes << ",\n"
               << "      \"retained_bytes\": "
               << result.MaximumRetainedBytes << ",\n"
               << "      \"logical_copies\": "
               << result.LogicalCopyCount << ",\n"
               << "      \"logical_copy_bytes\": "
               << result.LogicalCopyBytes << ",\n"
               << "      \"processed_voxels\": "
               << result.ProcessedVoxels << "\n"
               << "    }"
               << (index + 1U == results.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

struct Arguments final
{
    std::optional<std::filesystem::path> Csv;
    std::optional<std::filesystem::path> Json;
};

[[nodiscard]] Arguments ParseArguments(
    const int count,
    char* arguments[])
{
    Arguments result{};
    for (int index = 1; index < count; ++index)
    {
        const std::string_view argument(arguments[index]);
        const auto readPath =
            [&](std::optional<std::filesystem::path>& destination)
            {
                if (index + 1 >= count)
                {
                    Fail("Benchmark output option requires a path.");
                }
                destination = std::filesystem::path(arguments[++index]);
            };
        if (argument == "--csv")
        {
            readPath(result.Csv);
        }
        else if (argument == "--json")
        {
            readPath(result.Json);
        }
        else
        {
            Fail("Unknown benchmark argument: " + std::string(argument));
        }
    }
    return result;
}

void WriteFile(
    const std::filesystem::path& path,
    const std::span<const PhaseAggregate> results,
    const bool json)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(output.is_open(), "Unable to open benchmark output file.");
    if (json)
    {
        WriteJson(output, results);
    }
    else
    {
        WriteCsv(output, results);
    }
    Require(output.good(), "Unable to write benchmark output file.");
}

} // namespace

int main(const int count, char* arguments[])
{
    try
    {
        const Arguments parsed = ParseArguments(count, arguments);
        const std::vector<PhaseAggregate> results = RunBenchmarks();
        WriteCsv(std::cout, results);
        if (parsed.Csv)
        {
            WriteFile(*parsed.Csv, results, false);
        }
        if (parsed.Json)
        {
            WriteFile(*parsed.Json, results, true);
        }
    }
    catch (const std::exception& error)
    {
        Renderer::Renderer::Shutdown();
        std::cerr << "STAMP-16 benchmark failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
