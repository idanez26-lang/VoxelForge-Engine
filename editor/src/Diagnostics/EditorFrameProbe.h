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
    ViewportRender,  // ViewportRenderer::Render inside DrawScenePanel
    HlPrep,          // UpdateVoxelHighlights: hover/selection preparation
    HlTools,         // UpdateVoxelHighlights: smart/eraser/fill tool chain
    HlCursor,        // UpdateVoxelHighlights: universal cursor + tail
    HlSmartPlan,     // Smart branch: plan/exact resolution up to presentation
    PencilTick,      // Pencil V2: SubmitInput + Tick
    PencilCommit,    // Pencil V2: CommitPencilViewportInteractionV2
    SpSetup,         // DrawScenePanel: header/toolbar/camera/gizmo, pre-render
    SpPointer,       // DrawScenePanel: render+image+pointer interaction block
    // LOT 4a : decoupage fin de HighlightsHandoff. Les quatre appels que la
    // sonde englobe sont tous du bookkeeping CPU a court-circuit ; aucun ne
    // devrait couter 20 ms. Ces sous-sondes disent lequel ment.
    HoConfigure,     // Handoff: ViewportRenderer::ConfigureHighlights
    HoExact,         // Handoff: branche preview exacte (chunks ou monolithe)
    HoVoxel,         // Handoff: ConfigureVoxelPreview
    HoTransform      // Handoff: validation + ConfigureTransformPreview
};

inline constexpr std::size_t EditorFrameProbeSlotCount = 21U;

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

    // LOT 5 : gate 60 FPS. Jusqu'ici la sonde ne journalisait QUE les frames
    // au-dela de 20 ms : on voyait les pics et rien d'autre — ni la mediane, ni
    // la proportion de frames hors budget. Impossible de dire si une session
    // etait fluide, seulement qu'elle avait eu des accidents.
    //
    // L'histogramme ci-dessous est a pas fixe : borne memoire connue, aucune
    // allocation, aucun tri, cout constant par frame. La contrepartie est
    // assumee et documentee dans BudgetReport : un percentile est renvoye comme
    // la BORNE SUPERIEURE de son intervalle, donc legerement pessimiste, a
    // BucketMilliseconds pres.
    static constexpr double TargetFrameMilliseconds = 1000.0 / 60.0;
    // La presentation est synchronisee sur l'ecran : au repos une frame dure
    // 16,7 ms par CONSTRUCTION, avec une gigue de quelques dixiemes. Un seuil
    // strict a 16,67 ms compte donc cette gigue comme un depassement et rend le
    // taux inexploitable — mesure faite, il annoncait 65 % de frames hors
    // budget sur une session dont la mediane au repos etait exactement le
    // vsync. La tolerance ne compte que ce qui depasse VRAIMENT le budget.
    static constexpr double BudgetToleranceMilliseconds = 1.5;
    static constexpr double OverBudgetThresholdMilliseconds =
        TargetFrameMilliseconds + BudgetToleranceMilliseconds;
    // Un intervalle de vsync entierement rate, sans ambiguite possible.
    static constexpr double MissedVsyncThresholdMilliseconds =
        2.0 * TargetFrameMilliseconds - BudgetToleranceMilliseconds;
    static constexpr double BucketMilliseconds = 0.25;
    static constexpr std::size_t BucketCount = 257U; // 256 x 0,25 ms = 64 ms, + debordement

    struct FrameBudgetReport final
    {
        std::uint64_t FrameCount = 0U;
        std::uint64_t OverBudgetCount = 0U;
        double OverBudgetRatio = 0.0; // 0..1
        // Frames ayant rate un intervalle de vsync entier : le symptome que
        // l'utilisateur ressent comme un decrochage.
        std::uint64_t MissedVsyncCount = 0U;
        double MissedVsyncRatio = 0.0;
        double BudgetMilliseconds = TargetFrameMilliseconds;
        double P50 = 0.0;
        double P95 = 0.0;
        double P99 = 0.0;
        double Worst = 0.0;
        // Vrai si au moins une frame a depasse la portee de l'histogramme : les
        // percentiles restent valides, mais P99 peut etre sature a Worst.
        bool Saturated = false;
        // LATENCE-01 : retard du surlignage sur le pointeur, en FRAMES.
        // Le debit ne dit rien de ce retard : une session peut tenir 60 FPS et
        // afficher un surlignage vieux de trois frames, ce que l'utilisateur
        // ressent comme une desynchronisation du curseur. Mesure faite le
        // 06/08/2026 : 0,3 % de frames hors budget, et pourtant la desynchro
        // ressentie est inchangee — d'ou cette sonde.
        std::uint64_t HighlightLatencySamples = 0U;
        double HighlightLatencyMeanFrames = 0.0;
        std::uint64_t HighlightLatencyMaxFrames = 0U;
    };

    void Add(EditorFrameProbeSlot slot, double milliseconds) noexcept;

    // LATENCE-01 : nombre de frames ecoulees entre la demande de resolution du
    // surlignage et sa resolution effective. Zero signifie « resolu dans la
    // frame meme », ce qui est l'objectif.
    void NoteHighlightLatency(std::uint64_t frames) noexcept;

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

    // LOT 5 : etat du budget depuis le dernier ResetBudget(). Pur calcul, sans
    // effet de bord : peut etre appele a tout moment.
    [[nodiscard]] FrameBudgetReport BudgetReport() const noexcept;
    [[nodiscard]] std::string BuildBudgetSummary() const;
    void ResetBudget() noexcept;

private:
    [[nodiscard]] std::string BuildSummary(const FrameReport& frame) const;
    [[nodiscard]] double Percentile(double fraction) const noexcept;

    std::array<SlotStats, EditorFrameProbeSlotCount> accumulating_{};
    FrameReport lastFrame_{};
    FrameReport worstFrame_{};
    std::uint64_t frameIndex_ = 0U;
    std::uint64_t slowFrameCount_ = 0U;
    double frameStart_ = 0.0;
    double lastSummaryAt_ = 0.0;
    bool frameOpen_ = false;
    bool summaryEmitted_ = false;
    // LOT 5 : histogramme du budget.
    std::array<std::uint32_t, BucketCount> buckets_{};
    std::uint64_t budgetFrameCount_ = 0U;
    std::uint64_t overBudgetCount_ = 0U;
    std::uint64_t missedVsyncCount_ = 0U;
    std::uint64_t highlightLatencySamples_ = 0U;
    std::uint64_t highlightLatencyTotal_ = 0U;
    std::uint64_t highlightLatencyMax_ = 0U;
    double budgetWorst_ = 0.0;
    bool budgetSaturated_ = false;
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
