#include "ViewportInteractionV2/SelectionMoveInteractionHandler.h"
#include "ViewportInteractionV2/ViewportInteractionController.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "Psapi.lib")
#endif

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;
namespace V2 = Editor::InteractionV2;

constexpr std::uint64_t Generation = 813U;
constexpr std::size_t InteractionFrames = 36'000U; // 10 minutes at 60 FPS.

struct ProcessMemorySample final
{
    std::uint64_t WorkingSetBytes = 0U;
    std::uint64_t PrivateBytes = 0U;
};

[[nodiscard]] ProcessMemorySample CurrentProcessMemory() noexcept
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) != FALSE)
    {
        return {
            static_cast<std::uint64_t>(counters.WorkingSetSize),
            static_cast<std::uint64_t>(counters.PrivateUsage)};
    }
#endif
    return {};
}

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] std::vector<Position> MakeSparse256()
{
    std::vector<Position> result;
    result.reserve(64U);
    for (std::size_t index = 0U; index < 64U; ++index)
    {
        const auto coordinate = static_cast<std::int32_t>(
            index * 255U / 63U);
        result.push_back({coordinate, coordinate, coordinate});
    }
    return result;
}

[[nodiscard]] std::vector<Position> MakeDense()
{
    std::vector<Position> result;
    result.reserve(4'096U);
    for (std::int32_t x = 0; x < 16; ++x)
        for (std::int32_t y = 0; y < 16; ++y)
            for (std::int32_t z = 0; z < 16; ++z)
                result.push_back({x, y, z});
    return result;
}

[[nodiscard]] Asset::Voxel::VoxelDocument MakeDocument(
    const std::vector<Position>& positions)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {256U, 256U, 256U};
    model.Voxels.reserve(positions.size());
    for (const Position position : positions)
    {
        model.Voxels.push_back({
            static_cast<std::uint8_t>(position.X),
            static_cast<std::uint8_t>(position.Y),
            static_cast<std::uint8_t>(position.Z), 1U});
    }
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "viewport-interaction-v2-soak.vox");
    Require(loaded.Succeeded(), "Unable to build deterministic soak document.");
    return std::move(*loaded.Document);
}

[[nodiscard]] Editor::SelectionBounds BoundsOf(
    const std::vector<Position>& positions)
{
    Require(!positions.empty(), "Soak selections must not be empty.");
    Position minimum = positions.front();
    Position maximum = positions.front();
    for (const Position position : positions)
    {
        minimum.X = std::min(minimum.X, position.X);
        minimum.Y = std::min(minimum.Y, position.Y);
        minimum.Z = std::min(minimum.Z, position.Z);
        maximum.X = std::max(maximum.X, position.X);
        maximum.Y = std::max(maximum.Y, position.Y);
        maximum.Z = std::max(maximum.Z, position.Z);
    }
    return Editor::SelectionBounds::FromCorners(minimum, maximum);
}

struct Scenario final
{
    std::string_view Name;
    std::vector<Position> Positions;
    Asset::Voxel::VoxelDocument Document;
    Editor::SelectionBounds Bounds;

    Scenario(std::string_view name, std::vector<Position> positions)
        : Name(name), Positions(std::move(positions)),
          Document(MakeDocument(Positions)), Bounds(BoundsOf(Positions)) {}
};

struct FrameStatistics final
{
    std::vector<double> Milliseconds;

    void Add(const std::chrono::steady_clock::duration elapsed)
    {
        Milliseconds.push_back(std::chrono::duration<double, std::milli>(
            elapsed).count());
    }

    [[nodiscard]] double Mean() const
    {
        return std::accumulate(Milliseconds.begin(), Milliseconds.end(), 0.0) /
            static_cast<double>(Milliseconds.size());
    }

    [[nodiscard]] double Percentile(const double fraction) const
    {
        std::vector<double> sorted = Milliseconds;
        std::sort(sorted.begin(), sorted.end());
        const auto index = static_cast<std::size_t>(std::ceil(
            fraction * static_cast<double>(sorted.size() - 1U)));
        return sorted[index];
    }

    [[nodiscard]] double Maximum() const
    {
        return *std::max_element(Milliseconds.begin(), Milliseconds.end());
    }

    [[nodiscard]] std::size_t OverBudget() const
    {
        return static_cast<std::size_t>(std::count_if(
            Milliseconds.begin(), Milliseconds.end(),
            [](const double value) { return value > 16.67; }));
    }
};

struct SoakCounters final
{
    std::size_t SelectionActivations = 0U;
    std::size_t MoveActivations = 0U;
    std::size_t CompletedMoves = 0U;
    std::size_t CancelledMoves = 0U;
    std::size_t CommitPlans = 0U;
    std::size_t EmptySelections = 0U;
    std::size_t MaximumCacheEntries = 0U;
    std::size_t ControllerSelectionCycles = 0U;
    std::size_t ControllerEmptyCycles = 0U;
    std::size_t ControllerEscapeCycles = 0U;
    std::size_t ControllerFocusBlocked = 0U;
    std::size_t ControllerCameraBlocked = 0U;
    std::size_t WeakSelectionHandles = 0U;
    std::size_t WeakSourceHandles = 0U;
    std::size_t WeakMoveHandles = 0U;
};

[[nodiscard]] V2::ViewportInputFrame ControllerInput(
    const std::uint64_t frame, const Editor::Vec2 mouse)
{
    V2::ViewportInputFrame result;
    result.Frame = frame;
    result.MouseScreen = mouse;
    result.ViewportHovered = true;
    result.ViewportFocused = true;
    result.SelectionToolActive = true;
    result.Viewport = {0.0F, 0.0F, 1'000.0F, 1'000.0F};
    result.ViewProjection = Editor::UniformScaleMatrix(0.02F);
    result.CameraWorldPosition = {0.0F, 0.0F, 10.0F};
    result.ModelCenter = {};
    result.FramebufferScale = 1.0F;
    result.DocumentGeneration = Generation;
    return result;
}

void SubmitController(V2::ViewportInteractionController& controller,
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    V2::ViewportInputFrame input)
{
    input.DocumentRevision = document.GetRevision();
    controller.SubmitInput(std::move(input));
    controller.Tick(&document, selection);
}

void ExerciseControllerPaths(Asset::Voxel::VoxelDocument& document,
    std::uint64_t& frame,
    SoakCounters& counters)
{
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;

    auto focusBlocked = ControllerInput(frame++, {499.0F, 477.0F});
    focusBlocked.PrimaryPressed = true;
    focusBlocked.PrimaryHeld = true;
    focusBlocked.UiCapturesPointer = true;
    SubmitController(controller, document, selection, focusBlocked);
    Require(controller.Phase() == V2::InteractionPhase::Idle,
        "UI-captured pointer entered a V2 selection gesture.");
    ++counters.ControllerFocusBlocked;

    auto cameraBlocked = ControllerInput(frame++, {499.0F, 477.0F});
    cameraBlocked.PrimaryPressed = true;
    cameraBlocked.PrimaryHeld = true;
    cameraBlocked.CameraActive = true;
    SubmitController(controller, document, selection, cameraBlocked);
    Require(controller.Phase() == V2::InteractionPhase::Idle,
        "Camera-active pointer entered a V2 selection gesture.");
    ++counters.ControllerCameraBlocked;

    auto emptyPress = ControllerInput(frame++, {20.0F, 20.0F});
    emptyPress.PrimaryPressed = true;
    emptyPress.PrimaryHeld = true;
    SubmitController(controller, document, selection, emptyPress);
    auto emptyRelease = ControllerInput(frame++, {40.0F, 40.0F});
    emptyRelease.PrimaryReleased = true;
    SubmitController(controller, document, selection, emptyRelease);
    Require(selection.Empty() &&
        controller.Phase() == V2::InteractionPhase::SelectionReady,
        "Empty screen rectangle did not resolve to an empty selection.");
    ++counters.ControllerEmptyCycles;

    controller.Reset();
    auto press = ControllerInput(frame++, {499.0F, 477.0F});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    SubmitController(controller, document, selection, press);
    auto drag = ControllerInput(frame++, {519.0F, 501.0F});
    drag.PrimaryHeld = true;
    SubmitController(controller, document, selection, drag);
    auto release = ControllerInput(frame++, {519.0F, 501.0F});
    release.PrimaryReleased = true;
    SubmitController(controller, document, selection, release);
    Require(!selection.Empty() &&
        controller.Phase() == V2::InteractionPhase::SelectionReady,
        "Screen rectangle did not produce a stable canonical selection.");
    ++counters.ControllerSelectionCycles;

    controller.Reset();
    auto escapePress = ControllerInput(frame++, {499.0F, 477.0F});
    escapePress.PrimaryPressed = true;
    escapePress.PrimaryHeld = true;
    SubmitController(controller, document, selection, escapePress);
    auto escape = ControllerInput(frame++, {499.0F, 477.0F});
    escape.EscapePressed = true;
    SubmitController(controller, document, selection, escape);
    Require(controller.Phase() == V2::InteractionPhase::Idle &&
        !controller.OwnsPointer() && !controller.Active(),
        "Escape left an active V2 controller session.");
    ++counters.ControllerEscapeCycles;
}

void PrintMetrics(const FrameStatistics& frames,
    const V2::ViewportInteractionMetrics& metrics,
    const SoakCounters& counters,
    const ProcessMemorySample initialMemory,
    const ProcessMemorySample peakMemory,
    const ProcessMemorySample finalMemory)
{
    const std::size_t compactBytes = sizeof(V2::MovePreviewState);
    std::cout
        << "scenario=viewport_interaction_v2_soak"
        << " frames=" << frames.Milliseconds.size()
        << " equivalent_minutes_at_60fps="
        << static_cast<double>(frames.Milliseconds.size()) / 3'600.0
        << " frame_mean_ms=" << frames.Mean()
        << " frame_median_ms=" << frames.Percentile(0.50)
        << " frame_p95_ms=" << frames.Percentile(0.95)
        << " frame_p99_ms=" << frames.Percentile(0.99)
        << " frame_worst_ms=" << frames.Maximum()
        << " frames_over_16_67_ms=" << frames.OverBudget()
        << " selection_activations=" << counters.SelectionActivations
        << " move_activations=" << counters.MoveActivations
        << " completed_moves=" << counters.CompletedMoves
        << " cancelled_moves=" << counters.CancelledMoves
        << " commit_plans=" << counters.CommitPlans
        << " empty_selections=" << counters.EmptySelections
        << " controller_selection_cycles="
        << counters.ControllerSelectionCycles
        << " controller_empty_cycles=" << counters.ControllerEmptyCycles
        << " controller_escape_cycles=" << counters.ControllerEscapeCycles
        << " controller_focus_blocked=" << counters.ControllerFocusBlocked
        << " controller_camera_blocked=" << counters.ControllerCameraBlocked
        << " plans=" << metrics.MovePlanBuilds
        << " plan_reuses=" << metrics.MovePlanReuses
        << " collision_cache_hits=" << metrics.MoveCollisionCacheHits
        << " collision_cache_misses=" << metrics.MoveCollisionCacheMisses
        << " collision_deferred=" << metrics.MoveCollisionDeferred
        << " drag_materialized_voxels="
        << metrics.MoveDestinationCoordinatesMaterialized
        << " drag_visited_voxels=" <<
        (metrics.MoveSelectedCoordinatesVisited -
            metrics.MoveCommitCoordinatesMaterialized)
        << " commit_materialized_voxels="
        << metrics.MoveCommitCoordinatesMaterialized
        << " cache_high_water_entries=" << counters.MaximumCacheEntries
        << " cache_limit=64"
        << " compact_plan_bytes=" << compactBytes
        << " process_initial_working_set_bytes="
        << initialMemory.WorkingSetBytes
        << " process_peak_working_set_bytes=" << peakMemory.WorkingSetBytes
        << " process_final_working_set_bytes="
        << finalMemory.WorkingSetBytes
        << " process_initial_private_bytes=" << initialMemory.PrivateBytes
        << " process_peak_private_bytes=" << peakMemory.PrivateBytes
        << " process_final_private_bytes=" << finalMemory.PrivateBytes
        << " weak_selection_handles=" << counters.WeakSelectionHandles
        << " weak_source_handles=" << counters.WeakSourceHandles
        << " weak_move_handles=" << counters.WeakMoveHandles
        << " source_uploads_observed=" << metrics.MoveSourceUploads
        << " delta_gpu_updates_observed=" << metrics.MoveDeltaGpuUpdates
        << '\n';
}
}

int main()
{
    try
    {
        Scenario small{"small", {{0, 0, 0}, {1, 0, 0}, {2, 0, 0},
            {3, 0, 0}, {4, 0, 0}, {5, 0, 0}, {6, 0, 0}, {7, 0, 0}}};
        Scenario sparse{"sparse_256", MakeSparse256()};
        Scenario dense{"dense", MakeDense()};
        std::array<Scenario*, 3U> scenarios{&small, &sparse, &dense};

        V2::SelectionMoveInteractionHandler handler;
        V2::ViewportInteractionMetrics metrics;
        FrameStatistics frames;
        frames.Milliseconds.reserve(InteractionFrames);
        SoakCounters counters;
        std::uint64_t nextPlanId = 1U;
        std::uint64_t nextControllerFrame = 1U;
        const ProcessMemorySample initialMemory = CurrentProcessMemory();
        ProcessMemorySample peakMemory = initialMemory;
        std::vector<std::weak_ptr<const V2::ScreenSelectionPlan>>
            weakSelections;
        std::vector<std::weak_ptr<const V2::SelectionMoveSourceSnapshot>>
            weakSources;
        std::vector<std::weak_ptr<const V2::MovePreviewState>> weakMoves;
        weakSelections.reserve(512U);
        weakSources.reserve(512U);
        weakMoves.reserve(40'000U);

        Editor::SelectionService selection;
        selection.SetDocumentGeneration(Generation);
        Scenario* current = nullptr;
        V2::SelectionMoveSourceSnapshotPtr source;

        for (std::size_t frame = 0U; frame < InteractionFrames; ++frame)
        {
            const auto begin = std::chrono::steady_clock::now();
            if (frame % 120U == 0U)
            {
                current = scenarios[(frame / 120U) % scenarios.size()];
                static_cast<void>(selection.ApplySortedVolume(
                    current->Positions, current->Bounds,
                    Editor::SelectionMode::Replace));
                auto plan = std::make_shared<V2::ScreenSelectionPlan>();
                plan->PlanId = nextPlanId++;
                plan->DocumentGeneration = Generation;
                plan->DocumentRevision = current->Document.GetRevision();
                plan->Voxels = current->Positions;
                plan->Bounds = current->Bounds;
                weakSelections.push_back(plan);
                source = handler.CaptureMoveSource(current->Document,
                    selection, Generation, plan, metrics);
                Require(source != nullptr, "Soak Move source capture failed.");
                weakSources.push_back(source);
                ++counters.SelectionActivations;
                ++counters.MoveActivations;
            }

            // Simulate slow and rapid axis changes plus pauses. A paused
            // delta must reuse the exact immutable plan, not rebuild it.
            const std::int32_t amount = static_cast<std::int32_t>(
                (frame / 4U) % 7U) - 3;
            Position delta{};
            if ((frame / 17U) % 3U == 0U) delta.X = amount;
            else if ((frame / 17U) % 3U == 1U) delta.Y = amount;
            else delta.Z = amount;
            const auto resolved = handler.ResolveMove(current->Document,
                source, delta, nextPlanId++, metrics);
            Require(resolved.Plan != nullptr, "Soak Move resolve failed.");
            weakMoves.push_back(resolved.Plan);

            if (frame % 41U == 0U)
            {
                const auto paused = handler.ResolveMove(current->Document,
                    source, delta, nextPlanId++, metrics);
                Require(paused.Plan == resolved.Plan,
                    "Unchanged Move delta rebuilt during the soak pause.");
            }

            if (frame % 720U == 0U)
            {
                // A commit is deliberately built only on this release-like
                // boundary. It is not part of interactive frame timing.
                const auto commit = handler.BuildCommit(
                    current->Document, selection, *resolved.Plan, metrics);
                if (resolved.Plan->CanAttemptCommit())
                    Require(commit.has_value(), "Soak commit plan was not exact.");
                ++counters.CommitPlans;
            }

            if (frame % 180U == 179U)
            {
                handler.ClearMoveCache(); // Escape/cancel cleanup boundary.
                Require(handler.MoveCacheEntryCount() == 0U,
                    "Move cache survived soak cancellation.");
                ++counters.CancelledMoves;
            }
            else
            {
                counters.MaximumCacheEntries = std::max(
                    counters.MaximumCacheEntries, handler.MoveCacheEntryCount());
                Require(handler.MoveCacheEntryCount() <= 64U,
                    "Move cache exceeded its fixed budget during soak.");
            }
            if (frame % 1'200U == 0U) ++counters.EmptySelections;
            if (frame % 120U == 119U) ++counters.CompletedMoves;
            if (frame % 120U == 0U)
                ExerciseControllerPaths(
                    small.Document, nextControllerFrame, counters);
            frames.Add(std::chrono::steady_clock::now() - begin);
            const ProcessMemorySample sample = CurrentProcessMemory();
            peakMemory.WorkingSetBytes = std::max(
                peakMemory.WorkingSetBytes, sample.WorkingSetBytes);
            peakMemory.PrivateBytes = std::max(
                peakMemory.PrivateBytes, sample.PrivateBytes);
        }
        handler.ClearMoveCache();
        source.reset();
        Require(handler.MoveCacheEntryCount() == 0U,
            "Move cache remained after soak teardown.");
        Require(metrics.MoveDestinationCoordinatesMaterialized == 0U,
            "Interactive Move materialized destinations during soak.");
        Require(metrics.MoveCollisionCacheHits > 0U,
            "Soak did not exercise unchanged-delta plan reuse.");
        counters.WeakSelectionHandles = static_cast<std::size_t>(
            std::count_if(weakSelections.begin(), weakSelections.end(),
                [](const auto& handle) { return !handle.expired(); }));
        counters.WeakSourceHandles = static_cast<std::size_t>(
            std::count_if(weakSources.begin(), weakSources.end(),
                [](const auto& handle) { return !handle.expired(); }));
        counters.WeakMoveHandles = static_cast<std::size_t>(
            std::count_if(weakMoves.begin(), weakMoves.end(),
                [](const auto& handle) { return !handle.expired(); }));
        Require(counters.WeakSelectionHandles == 0U &&
            counters.WeakSourceHandles == 0U &&
            counters.WeakMoveHandles == 0U,
            "V2 plans or gesture snapshots survived soak teardown.");
        weakSelections.clear();
        weakSources.clear();
        weakMoves.clear();
        weakSelections.shrink_to_fit();
        weakSources.shrink_to_fit();
        weakMoves.shrink_to_fit();
        const ProcessMemorySample finalMemory = CurrentProcessMemory();
        PrintMetrics(frames, metrics, counters, initialMemory,
            peakMemory, finalMemory);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
