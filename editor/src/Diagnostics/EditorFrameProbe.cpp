#include "Diagnostics/EditorFrameProbe.h"

#include <iomanip>
#include <sstream>

namespace VoxelForge::Editor
{

const char* EditorFrameProbeSlotName(const EditorFrameProbeSlot slot) noexcept
{
    switch (slot)
    {
    case EditorFrameProbeSlot::Highlights: return "highlights";
    case EditorFrameProbeSlot::PreviewResolve: return "preview";
    case EditorFrameProbeSlot::MeshSynchronize: return "mesh-sync";
    case EditorFrameProbeSlot::GpuUpload: return "gpu-upload";
    case EditorFrameProbeSlot::ScenePanel: return "scene-panel";
    case EditorFrameProbeSlot::InteractionTick: return "v2-tick";
    case EditorFrameProbeSlot::Stroke: return "stroke";
    }
    return "unknown";
}

void EditorFrameProbe::Add(
    const EditorFrameProbeSlot slot, const double milliseconds) noexcept
{
    SlotStats& stats = accumulating_[static_cast<std::size_t>(slot)];
    stats.Milliseconds += milliseconds;
    ++stats.Calls;
}

std::optional<std::string> EditorFrameProbe::FrameBoundary(
    const double nowMilliseconds)
{
    std::optional<std::string> summary;
    if (frameOpen_)
    {
        ++frameIndex_;
        lastFrame_.Index = frameIndex_;
        lastFrame_.FrameMilliseconds = nowMilliseconds - frameStart_;
        lastFrame_.Slots = accumulating_;
        if (lastFrame_.FrameMilliseconds > worstFrame_.FrameMilliseconds)
            worstFrame_ = lastFrame_;
        if (lastFrame_.FrameMilliseconds >
            DefaultSlowFrameThresholdMilliseconds)
        {
            ++slowFrameCount_;
            if (!summaryEmitted_ ||
                nowMilliseconds - lastSummaryAt_ >=
                    SummaryIntervalMilliseconds)
            {
                summary = BuildSummary(lastFrame_);
                lastSummaryAt_ = nowMilliseconds;
                summaryEmitted_ = true;
            }
        }
    }
    accumulating_ = {};
    frameStart_ = nowMilliseconds;
    frameOpen_ = true;
    return summary;
}

const EditorFrameProbe::FrameReport& EditorFrameProbe::LastFrame()
    const noexcept
{
    return lastFrame_;
}

const EditorFrameProbe::FrameReport& EditorFrameProbe::WorstFrame()
    const noexcept
{
    return worstFrame_;
}

std::uint64_t EditorFrameProbe::SlowFrameCount() const noexcept
{
    return slowFrameCount_;
}

double EditorFrameProbe::SlowFrameThreshold() const noexcept
{
    return DefaultSlowFrameThresholdMilliseconds;
}

void EditorFrameProbe::ResetWorstFrame() noexcept
{
    worstFrame_ = {};
    slowFrameCount_ = 0U;
}

std::string EditorFrameProbe::BuildSummary(const FrameReport& frame) const
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1);
    stream << "[Perf] Frame " << frame.Index << ": "
           << frame.FrameMilliseconds << " ms";
    for (std::size_t index = 0U; index < EditorFrameProbeSlotCount; ++index)
    {
        const SlotStats& stats = frame.Slots[index];
        if (stats.Calls == 0U) continue;
        stream << " | "
               << EditorFrameProbeSlotName(
                      static_cast<EditorFrameProbeSlot>(index))
               << ' ' << stats.Milliseconds << " ms x" << stats.Calls;
    }
    return stream.str();
}

} // namespace VoxelForge::Editor
