#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

// Instrumented stations of the editor frame (PERF-01). Extend as needed.
enum class EditorFrameProbeSlot : std::uint8_t
{
    Highlights,      // EditorWorkspace::UpdateVoxelHighlights, whole body
    PreviewResolve,  // SmartToolController::ResolvePreview calls
    MeshSynchronize, // VoxelDocumentMeshCache::Synchronize
    GpuUpload,       // ViewportRenderer::Upload
    ScenePanel,      // EditorWorkspace::DrawScenePanel, whole body
    InteractionTick, // ViewportInteractionV2 SubmitInput + Tick + presentation
    Stroke,          // EditorWorkspace::ContinueSmartToolStroke
    HighlightsHandoff, // Renderer Configure* tail of UpdateVoxelHighlights
    ViewportRender   // ViewportRenderer::Render inside DrawScenePanel
};

inline constexpr std::size_t EditorFrameProbeSlotCount = 9U;

[[nodiscard]] const char* EditorFrameProbeSlotName(
    EditorFrameProbeSlot slot) noexcept;

// Pure frame-timing aggregator: no clock, no I/O, no UI. The workspace feeds
// slot durations and frame boundaries; the probe aggregates per-frame totals,
// remembers the worst frame, and emits a one-line summary for slow frames
// (rate limited) that the caller routes to the console.
class EditorFrameProbe final
{
public:
    struct SlotStats final
    {
        double Milliseconds = 0.0;
        std::uint32_t Calls = 0U;
    };

    struct FrameReport final
    {
        std::uint64_t Index = 0U;
        double FrameMilliseconds = 0.0;
        std::array<SlotStats, EditorFrameProbeSlotCount> Slots{};
    };

    static constexpr double DefaultSlowFrameThresholdMilliseconds = 20.0;
    static constexpr double SummaryIntervalMilliseconds = 500.0;

    void Add(EditorFrameProbeSlot slot, double milliseconds) noexcept;

    // Closes the frame started at the previous boundary and starts the next
    // one. `nowMilliseconds` is a monotonic timestamp supplied by the caller.
    // Returns a summary line when the closed frame exceeded the threshold and
    // the rate limiter allows it.
    [[nodiscard]] std::optional<std::string> FrameBoundary(
        double nowMilliseconds);

    [[nodiscard]] const FrameReport& LastFrame() const noexcept;
    [[nodiscard]] const FrameReport& WorstFrame() const noexcept;
    [[nodiscard]] std::uint64_t SlowFrameCount() const noexcept;
    [[nodiscard]] double SlowFrameThreshold() const noexcept;
    void ResetWorstFrame() noexcept;

private:
    [[nodiscard]] std::string BuildSummary(const FrameReport& frame) const;

    std::array<SlotStats, EditorFrameProbeSlotCount> accumulating_{};
    FrameReport lastFrame_{};
    FrameReport worstFrame_{};
    std::uint64_t frameIndex_ = 0U;
    std::uint64_t slowFrameCount_ = 0U;
    double frameStart_ = 0.0;
    double lastSummaryAt_ = 0.0;
    bool frameOpen_ = false;
    bool summaryEmitted_ = false;
};

// RAII helper measuring a scope with the steady clock and feeding the probe.
class EditorFrameProbeScope final
{
public:
    EditorFrameProbeScope(
        EditorFrameProbe& probe, const EditorFrameProbeSlot slot) noexcept
        : probe_(probe), slot_(slot),
          start_(std::chrono::steady_clock::now())
    {
    }

    ~EditorFrameProbeScope()
    {
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_);
        probe_.Add(slot_, elapsed.count());
    }

    EditorFrameProbeScope(const EditorFrameProbeScope&) = delete;
    EditorFrameProbeScope& operator=(const EditorFrameProbeScope&) = delete;

private:
    EditorFrameProbe& probe_;
    EditorFrameProbeSlot slot_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace VoxelForge::Editor
