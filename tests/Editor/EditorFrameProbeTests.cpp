#include "Diagnostics/EditorFrameProbe.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestFastFramesProduceNoSummary()
{
    EditorFrameProbe probe;
    Require(!probe.FrameBoundary(0.0).has_value(),
        "The first boundary only opens a frame.");
    probe.Add(EditorFrameProbeSlot::Highlights, 2.0);
    Require(!probe.FrameBoundary(10.0).has_value(),
        "A 10 ms frame is below the threshold.");
    Require(probe.LastFrame().FrameMilliseconds == 10.0,
        "The frame duration should come from the boundaries.");
    Require(probe.LastFrame()
            .Slots[static_cast<std::size_t>(EditorFrameProbeSlot::Highlights)]
            .Calls == 1U,
        "Slot calls should be counted.");
    Require(probe.SlowFrameCount() == 0U, "No slow frame should be counted.");
}

void TestSlowFrameEmitsSummaryWithBreakdown()
{
    EditorFrameProbe probe;
    static_cast<void>(probe.FrameBoundary(0.0));
    probe.Add(EditorFrameProbeSlot::Highlights, 30.0);
    probe.Add(EditorFrameProbeSlot::Highlights, 10.0);
    probe.Add(EditorFrameProbeSlot::GpuUpload, 1.5);
    const auto summary = probe.FrameBoundary(50.0);
    Require(summary.has_value(), "A 50 ms frame should emit a summary.");
    Require(summary->find("highlights") != std::string::npos &&
            summary->find("x2") != std::string::npos &&
            summary->find("gpu-upload") != std::string::npos,
        "The summary should list active slots with call counts: " + *summary);
    Require(summary->find("preview") == std::string::npos,
        "Idle slots should not clutter the summary.");
    Require(probe.SlowFrameCount() == 1U, "The slow frame should be counted.");
}

void TestSummaryRateLimiting()
{
    EditorFrameProbe probe;
    static_cast<void>(probe.FrameBoundary(0.0));
    Require(probe.FrameBoundary(100.0).has_value(),
        "The first slow frame should emit.");
    Require(!probe.FrameBoundary(200.0).has_value(),
        "A slow frame 100 ms later should be rate limited.");
    Require(probe.SlowFrameCount() == 2U,
        "Rate-limited frames still count as slow.");
    Require(probe.FrameBoundary(800.0).has_value(),
        "After the interval, summaries flow again.");
}

void TestWorstFrameTrackingAndReset()
{
    EditorFrameProbe probe;
    static_cast<void>(probe.FrameBoundary(0.0));
    static_cast<void>(probe.FrameBoundary(15.0));
    static_cast<void>(probe.FrameBoundary(75.0));
    static_cast<void>(probe.FrameBoundary(80.0));
    Require(probe.WorstFrame().FrameMilliseconds == 60.0,
        "The worst frame should be retained.");
    probe.ResetWorstFrame();
    Require(probe.WorstFrame().FrameMilliseconds == 0.0 &&
            probe.SlowFrameCount() == 0U,
        "Reset should clear the worst frame and the slow counter.");
}

void TestSlotAccumulationPerFrame()
{
    EditorFrameProbe probe;
    static_cast<void>(probe.FrameBoundary(0.0));
    probe.Add(EditorFrameProbeSlot::PreviewResolve, 1.0);
    probe.Add(EditorFrameProbeSlot::PreviewResolve, 2.0);
    static_cast<void>(probe.FrameBoundary(5.0));
    const auto& preview = probe.LastFrame().Slots[static_cast<std::size_t>(
        EditorFrameProbeSlot::PreviewResolve)];
    Require(preview.Calls == 2U && preview.Milliseconds == 3.0,
        "Slot durations should accumulate within the frame.");
    static_cast<void>(probe.FrameBoundary(10.0));
    const auto& cleared = probe.LastFrame().Slots[static_cast<std::size_t>(
        EditorFrameProbeSlot::PreviewResolve)];
    Require(cleared.Calls == 0U && cleared.Milliseconds == 0.0,
        "Slots should reset at each boundary.");
}

// LOT 5 : le gate 60 FPS. On verifie sur un echantillon dont la reponse est
// calculable a la main, sinon le test ne prouve rien.
void TestBudgetPercentilesAndOverBudgetRatio()
{
    EditorFrameProbe probe;
    Require(probe.BudgetReport().FrameCount == 0U &&
            probe.BudgetReport().P50 == 0.0,
        "An untouched probe should report an empty budget.");

    // 100 frames : 90 a 10 ms (dans le budget), 10 a 30 ms (hors budget).
    // Attendu : p50 dans le premier groupe, p95 et p99 dans le second,
    // 10 % hors budget. Les percentiles sont rendus comme la borne superieure
    // de leur intervalle de 0,25 ms, donc 10 ms tombe dans ]9,75 ; 10,00].
    double now = 0.0;
    static_cast<void>(probe.FrameBoundary(now));
    for (int frame = 0; frame < 100; ++frame)
    {
        now += frame < 90 ? 10.0 : 30.0;
        static_cast<void>(probe.FrameBoundary(now));
    }
    const auto report = probe.BudgetReport();
    Require(report.FrameCount == 100U,
        "The budget histogram lost frames.");
    Require(report.OverBudgetCount == 10U,
        "Exactly the ten 30 ms frames should count as over budget.");
    Require(report.OverBudgetRatio > 0.099 && report.OverBudgetRatio < 0.101,
        "The over-budget ratio should be 10 %.");
    Require(report.P50 > 9.7 && report.P50 <= 10.25,
        "p50 should land in the 10 ms group.");
    Require(report.P95 > 29.7 && report.P95 <= 30.25,
        "p95 should land in the 30 ms group.");
    Require(report.P99 > 29.7 && report.P99 <= 30.25,
        "p99 should land in the 30 ms group.");
    Require(report.Worst > 29.9 && report.Worst < 30.1,
        "The worst frame should be the exact measured value, not a bucket.");
    Require(!report.Saturated,
        "Thirty milliseconds is well inside the histogram range.");

    // Une frame enorme sature le dernier bucket : les percentiles restent
    // valides et le drapeau doit le dire au lieu de mentir sur une valeur.
    now += 500.0;
    static_cast<void>(probe.FrameBoundary(now));
    const auto saturated = probe.BudgetReport();
    Require(saturated.Saturated && saturated.Worst > 499.0,
        "A frame beyond the histogram range should raise the saturation flag.");

    probe.ResetBudget();
    Require(probe.BudgetReport().FrameCount == 0U &&
            probe.BudgetReport().Worst == 0.0 &&
            !probe.BudgetReport().Saturated,
        "ResetBudget() should clear the histogram entirely.");
}

} // namespace

int main()
{
    try
    {
        TestBudgetPercentilesAndOverBudgetRatio();
        TestFastFramesProduceNoSummary();
        TestSlowFrameEmitsSummaryWithBreakdown();
        TestSummaryRateLimiting();
        TestWorstFrameTrackingAndReset();
        TestSlotAccumulationPerFrame();
        std::cout << "Editor frame probe tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Editor frame probe tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
