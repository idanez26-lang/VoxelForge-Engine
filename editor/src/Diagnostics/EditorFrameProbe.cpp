#include "Diagnostics/EditorFrameProbe.h"

#include <iomanip>
#include <limits>
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
    case EditorFrameProbeSlot::HighlightsHandoff: return "hl-handoff";
    case EditorFrameProbeSlot::ViewportRender: return "vp-render";
    case EditorFrameProbeSlot::HlPrep: return "hl-prep";
    case EditorFrameProbeSlot::HlTools: return "hl-tools";
    case EditorFrameProbeSlot::HlCursor: return "hl-cursor";
    case EditorFrameProbeSlot::HlSmartPlan: return "hl-plan";
    case EditorFrameProbeSlot::PencilTick: return "pencil-tick";
    case EditorFrameProbeSlot::PencilCommit: return "pencil-commit";
    case EditorFrameProbeSlot::SpSetup: return "sp-setup";
    case EditorFrameProbeSlot::SpPointer: return "sp-pointer";
    case EditorFrameProbeSlot::HoConfigure: return "ho-configure";
    case EditorFrameProbeSlot::HoExact: return "ho-exact";
    case EditorFrameProbeSlot::HoVoxel: return "ho-voxel";
    case EditorFrameProbeSlot::HoTransform: return "ho-transform";
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
        // LOT 5 : chaque frame close alimente l'histogramme du budget. Une
        // duree negative est impossible avec une horloge monotone, mais on la
        // range en bucket 0 plutot que de sortir du tableau.
        {
            const double elapsed = lastFrame_.FrameMilliseconds;
            ++budgetFrameCount_;
            if (elapsed > budgetWorst_) budgetWorst_ = elapsed;
            if (elapsed > OverBudgetThresholdMilliseconds) ++overBudgetCount_;
            if (elapsed > MissedVsyncThresholdMilliseconds) ++missedVsyncCount_;
            const double scaled = elapsed <= 0.0
                ? 0.0 : elapsed / BucketMilliseconds;
            const std::size_t bucket =
                scaled >= static_cast<double>(BucketCount - 1U)
                ? BucketCount - 1U
                : static_cast<std::size_t>(scaled);
            if (bucket == BucketCount - 1U) budgetSaturated_ = true;
            if (buckets_[bucket] != std::numeric_limits<std::uint32_t>::max())
                ++buckets_[bucket];
        }
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

double EditorFrameProbe::Percentile(const double fraction) const noexcept
{
    if (budgetFrameCount_ == 0U) return 0.0;
    // Rang le plus proche, arrondi vers le haut : le p95 de 20 frames est la
    // 19e valeur triee, pas la 19,0e. Sans le ceil, un echantillon court
    // sous-estimerait systematiquement la queue.
    const double exact = fraction * static_cast<double>(budgetFrameCount_);
    std::uint64_t rank = static_cast<std::uint64_t>(exact);
    if (static_cast<double>(rank) < exact) ++rank;
    if (rank == 0U) rank = 1U;
    std::uint64_t cumulative = 0U;
    for (std::size_t bucket = 0U; bucket < BucketCount; ++bucket)
    {
        cumulative += buckets_[bucket];
        if (cumulative < rank) continue;
        // Bucket de debordement : on ne connait pas la valeur exacte, la seule
        // reponse honnete est la pire mesuree.
        if (bucket == BucketCount - 1U) return budgetWorst_;
        // Borne SUPERIEURE de l'intervalle : le percentile rendu est donc
        // pessimiste d'au plus BucketMilliseconds.
        return static_cast<double>(bucket + 1U) * BucketMilliseconds;
    }
    return budgetWorst_;
}

EditorFrameProbe::FrameBudgetReport EditorFrameProbe::BudgetReport()
    const noexcept
{
    FrameBudgetReport report;
    report.FrameCount = budgetFrameCount_;
    report.OverBudgetCount = overBudgetCount_;
    report.MissedVsyncCount = missedVsyncCount_;
    report.BudgetMilliseconds = TargetFrameMilliseconds;
    report.Worst = budgetWorst_;
    report.Saturated = budgetSaturated_;
    if (budgetFrameCount_ != 0U)
    {
        report.OverBudgetRatio = static_cast<double>(overBudgetCount_) /
            static_cast<double>(budgetFrameCount_);
        report.MissedVsyncRatio = static_cast<double>(missedVsyncCount_) /
            static_cast<double>(budgetFrameCount_);
        report.P50 = Percentile(0.50);
        report.P95 = Percentile(0.95);
        report.P99 = Percentile(0.99);
    }
    return report;
}

std::string EditorFrameProbe::BuildBudgetSummary() const
{
    const FrameBudgetReport report = BudgetReport();
    std::ostringstream line;
    line << std::fixed << std::setprecision(2);
    line << "[Perf] Budget 60 FPS (" << report.BudgetMilliseconds << " ms) : "
         << report.FrameCount << " frames";
    if (report.FrameCount == 0U) return line.str();
    line << " | p50 " << report.P50 << " ms"
         << " | p95 " << report.P95 << " ms"
         << " | p99 " << report.P99 << " ms"
         << " | pire " << report.Worst << " ms"
         << " | hors budget " << report.OverBudgetCount << " ("
         << std::setprecision(1) << report.OverBudgetRatio * 100.0 << " %)"
         << " | vsync rate " << report.MissedVsyncCount << " ("
         << report.MissedVsyncRatio * 100.0 << " %)";
    if (report.Saturated) line << " | p99 sature (frames > 64 ms)";
    return line.str();
}

void EditorFrameProbe::ResetBudget() noexcept
{
    buckets_ = {};
    budgetFrameCount_ = 0U;
    overBudgetCount_ = 0U;
    missedVsyncCount_ = 0U;
    budgetWorst_ = 0.0;
    budgetSaturated_ = false;
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
