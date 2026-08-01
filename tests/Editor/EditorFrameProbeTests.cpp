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

} // namespace

int main()
{
    try
    {
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
