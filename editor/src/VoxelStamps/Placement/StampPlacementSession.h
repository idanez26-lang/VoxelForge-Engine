#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Library/StampAssetCache.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/SmartPlacement/StampSmartPlacementService.h"
#include "VoxelStamps/Variants/StampVariantResolver.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{

enum class StampPlacementSessionResultCode : std::uint8_t
{
    Succeeded,
    Inactive,
    MissingAsset,
    VariantResolutionFailed,
    VariantAssetUnavailable,
    DocumentChanged,
    InvalidPlan
};

[[nodiscard]] constexpr std::string_view StampPlacementSessionResultMessage(
    const StampPlacementSessionResultCode code) noexcept
{
    switch (code)
    {
    case StampPlacementSessionResultCode::Succeeded:
        return "Stamp placement session is active.";
    case StampPlacementSessionResultCode::Inactive:
        return "No Stamp placement session is active.";
    case StampPlacementSessionResultCode::MissingAsset:
        return "The selected Stamp asset is unavailable.";
    case StampPlacementSessionResultCode::VariantResolutionFailed:
        return "The active Smart Variant group has no resolvable variant.";
    case StampPlacementSessionResultCode::VariantAssetUnavailable:
        return "The resolved Smart Variant source asset is unavailable.";
    case StampPlacementSessionResultCode::DocumentChanged:
        return "The active voxel document changed; Stamp placement was cancelled.";
    case StampPlacementSessionResultCode::InvalidPlan:
        return "The Stamp placement plan is invalid.";
    }
    return "Unknown Stamp placement session status.";
}

enum class StampPlacementSessionState : std::uint8_t
{
    Empty,
    Active,
    Cancelled
};

/// STAMP-25 : pas de rotation choisi par l'artiste. Il ne decrit qu'un
/// increment : dans les deux cas la rotation fait le tour complet, en quatre
/// crans (90) ou en huit (45). Les positions paires restent des permutations
/// exactes de la grille ; les impaires sont reechantillonnees.
enum class StampRotationStep : std::uint8_t
{
    Quarter90,
    Eighth45
};

struct StampPlacementSessionResult final
{
    StampPlacementSessionResultCode Code =
        StampPlacementSessionResultCode::Inactive;
    bool Succeeded = false;
    bool PlanChanged = false;
    bool PreviewChanged = false;
    StampPlacementDiagnosticCode Diagnostic =
        StampPlacementDiagnosticCode::None;
    StampVariantResolutionError VariantError =
        StampVariantResolutionError::None;
    StampLibraryError LibraryError = StampLibraryError::None;
};

enum class StampPlacementSessionPlaceStatus : std::uint8_t
{
    Placed,
    PreviewRefreshed,
    NoChange,
    Inactive,
    DocumentChanged,
    Rejected
};

struct StampPlacementSessionPlaceResult final
{
    StampPlacementSessionPlaceStatus Status =
        StampPlacementSessionPlaceStatus::Inactive;
    bool PreviewChanged = false;
    VoxelEditHistoryResult History{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Status == StampPlacementSessionPlaceStatus::Placed;
    }
};

/// STAMP-22 counters expose proportional work without introducing timers into
/// product code. They let tests and diagnostics prove that an unchanged idle
/// preview reuses its immutable plan and snapshot.
struct StampPlacementSessionMetrics final
{
    std::uint64_t BuildRequests = 0U;
    std::uint64_t PlannerBuilds = 0U;
    std::uint64_t AssistedPlannerBuilds = 0U;
    std::uint64_t PlanCacheHits = 0U;
    std::uint64_t PreviewBuilds = 0U;
    std::uint64_t PreviewChanges = 0U;
};

/// UI-independent owner of one active Stamp placement. It owns the selected
/// Stamp or Variant group, transform, current immutable plan, preview snapshot,
/// cache key and placement ordinal. No document pointer is retained. A cache
/// passed to BeginVariantGroup must outlive the active placement session.
class StampPlacementSession final
{
public:
    [[nodiscard]] StampPlacementSessionResult SelectAsset(
        const VoxelStamp* stamp,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U,
        StampFixedPoint targetPivot = {},
        StampCollisionPolicy collisionPolicy =
            StampCollisionPolicy::Overwrite);
    [[nodiscard]] StampPlacementSessionResult Begin(
        VoxelStamp stamp,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel,
        StampFixedPoint targetPivot,
        StampCollisionPolicy collisionPolicy =
            StampCollisionPolicy::Overwrite);
    [[nodiscard]] StampPlacementSessionResult BeginVariantGroup(
        StampVariantGroup group,
        StampAssetCache& assetCache,
        std::uint64_t placementSessionSeed,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U,
        StampFixedPoint targetPivot = {},
        StampCollisionPolicy collisionPolicy =
            StampCollisionPolicy::Overwrite);
    [[nodiscard]] StampPlacementSessionResult Rebuild(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetTarget(
        StampFixedPoint targetPivot,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult UpdateTarget(
        StampFixedPoint targetPivot,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult TranslateTarget(
        std::int32_t x,
        std::int32_t y,
        std::int32_t z,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetQuarterRotation(
        std::uint8_t quarterTurns,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    // STAMP-24 : l'axe reste optionnel pour les appelants historiques (gizmo
    // vertical) mais devient explicite des que l'utilisateur choisit X ou Z.
    [[nodiscard]] StampPlacementSessionResult SetGizmoTransform(
        StampFixedPoint targetPivot,
        std::uint8_t quarterTurns,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        StampPlacementRotationAxis axis =
            StampPlacementRotationAxis::VerticalY);
    [[nodiscard]] StampPlacementSessionResult Rotate90(
        StampPlacementRotationAxis axis,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        bool clockwise = true);
    [[nodiscard]] StampPlacementSessionResult RotateClockwise(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult RotateCounterClockwise(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    // STAMP-25 : avance d'un cran dans le sens demande. Le cran vaut 90 ou 45
    // degres selon RotationStep ; a 45 degres la rotation parcourt les huit
    // positions avant de revenir a zero.
    [[nodiscard]] StampPlacementSessionResult RotateStep(
        StampPlacementRotationAxis axis,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        bool clockwise = true);
    // Le pas est un reglage d'outil : il ne change pas le plan courant, donc
    // aucune reconstruction n'est necessaire.
    void SetRotationStep(StampRotationStep step) noexcept;
    void ToggleRotationStep() noexcept;
    // Demi-cran de 45 degres cumule avec les quarts de tour. Le Stamp est
    // alors reechantillonne : le plan devient approximatif et le nombre de
    // cellules differe du nombre de voxels source.
    [[nodiscard]] StampPlacementSessionResult SetHalfQuarterStep(
        bool enabled,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult ToggleHalfQuarterStep(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetMirror(
        StampPlacementMirrorMode mirror,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult ToggleMirror(
        StampPlacementMirrorMode axis,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult CycleMirror(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult ResetTransform(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult UpdateSmartPlacementContext(
        StampSmartPlacementTargetContext context,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetSmartPlacementEnabled(
        bool enabled,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetSmartPlacementMode(
        StampSmartPlacementMode mode,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult
        SetSmartPlacementTemporaryBypass(
            bool temporarilyBypassed,
            const Asset::Voxel::VoxelDocument& document,
            std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult RenewVariantSeed(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionPlaceResult PlaceOnce(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        VoxelEditSession& editSession,
        VoxelEditHistory& history);
    [[nodiscard]] bool Cancel() noexcept;

    [[nodiscard]] StampPlacementSessionState State() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsCurrent(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration) const noexcept;
    [[nodiscard]] const VoxelStamp* ActiveStamp() const noexcept;
    [[nodiscard]] const StampPlacementPlan* CurrentPlan() const noexcept;
    [[nodiscard]] const VoxelPreviewData* CurrentPreview() const noexcept;
    [[nodiscard]] const StampPlacementCacheKey* CacheKey() const noexcept;
    [[nodiscard]] StampFixedPoint Target() const noexcept;
    [[nodiscard]] StampPlacementRotationAxis RotationAxis() const noexcept;
    [[nodiscard]] std::uint8_t QuarterRotation() const noexcept;
    [[nodiscard]] bool HalfQuarterStep() const noexcept;
    [[nodiscard]] StampRotationStep RotationStep() const noexcept;
    /// Angle courant en degres, tours entiers exclus (0, 45, 90 ... 315).
    [[nodiscard]] std::uint32_t RotationDegrees() const noexcept;
    [[nodiscard]] StampPlacementMirrorMode Mirror() const noexcept;
    [[nodiscard]] std::size_t TargetSubModel() const noexcept;
    [[nodiscard]] std::uint64_t PlacementOrdinal() const noexcept;
    [[nodiscard]] bool IsVariantPlacement() const noexcept;
    [[nodiscard]] const StampVariantGroup* ActiveVariantGroup() const noexcept;
    [[nodiscard]] const StampVariantResolutionReport*
        CurrentVariantResolution() const noexcept;
    [[nodiscard]] const StampPlacementVariantIdentity*
        CurrentVariantIdentity() const noexcept;
    [[nodiscard]] std::uint64_t PlacementSessionSeed() const noexcept;
    [[nodiscard]] bool SmartPlacementEnabled() const noexcept;
    [[nodiscard]] StampSmartPlacementMode SmartPlacementMode() const noexcept;
    [[nodiscard]] bool SmartPlacementTemporarilyBypassed() const noexcept;
    [[nodiscard]] bool SmartPlacementOrientationLocked() const noexcept;
    [[nodiscard]] bool SmartPlacementAppliedToPreview() const noexcept;
    [[nodiscard]] const StampSmartPlacementSuggestion*
        CurrentSmartPlacementSuggestion() const noexcept;
    [[nodiscard]] StampPlacementSessionMetrics Metrics() const noexcept;
    void ResetMetrics() noexcept;

private:
    [[nodiscard]] StampPlacementSessionResult BuildCurrent(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult ResolveCurrentVariant(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] bool MatchesDocumentContext(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration) const noexcept;

    StampPlacementSessionState state_ = StampPlacementSessionState::Empty;
    std::optional<VoxelStamp> stamp_;
    std::optional<StampVariantGroup> variantGroup_;
    StampAssetCache* variantAssetCache_ = nullptr;
    std::optional<StampVariantResolutionReport> variantResolution_;
    std::optional<StampPlacementVariantIdentity> variantIdentity_;
    std::optional<StampPlacementPlan> plan_;
    VoxelPreviewSession preview_;
    StampPlacementTransform transform_{};
    StampRotationStep rotationStep_ = StampRotationStep::Quarter90;
    StampSmartPlacementTargetContext smartPlacementTarget_{};
    std::optional<StampSmartPlacementSuggestion> smartPlacementSuggestion_;
    StampSmartPlacementMode smartPlacementMode_ =
        StampSmartPlacementMode::PreviewAssist;
    bool smartPlacementEnabled_ = true;
    bool smartPlacementTemporarilyBypassed_ = false;
    bool smartPlacementOrientationLocked_ = false;
    bool smartPlacementAppliedToPreview_ = false;
    bool smartPlacementAssistRequestedForPlan_ = false;
    StampPlacementSessionMetrics metrics_{};
    StampCollisionPolicy collisionPolicy_ = StampCollisionPolicy::Overwrite;
    std::size_t targetSubModel_ = 0U;
    std::uintptr_t documentInstanceToken_ = 0U;
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t placementSessionSeed_ = 0U;
    std::uint64_t placementOrdinal_ = 0U;
};

} // namespace VoxelForge::Editor::Stamps
