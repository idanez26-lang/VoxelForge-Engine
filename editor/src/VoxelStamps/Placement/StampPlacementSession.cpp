#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include <limits>
#include <string>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{
VoxelEditHistoryResult SessionHistoryResult(
    const VoxelEditHistoryResultCode code,
    std::string message)
{
    return {
        .Code = code,
        .Changed = false,
        .Label = "Place Voxel Stamp",
        .Message = std::move(message)};
}
}

StampPlacementSessionResult StampPlacementSession::SelectAsset(
    const VoxelStamp* const stamp,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel,
    const StampFixedPoint targetPivot,
    const StampCollisionPolicy collisionPolicy)
{
    if (stamp == nullptr)
    {
        const bool previewChanged = CurrentPreview() != nullptr;
        const bool changed = Cancel();
        return {
            .Code = StampPlacementSessionResultCode::MissingAsset,
            .Succeeded = false,
            .PlanChanged = changed,
            .PreviewChanged = previewChanged,
            .Diagnostic = StampPlacementDiagnosticCode::MissingStamp};
    }
    return Begin(*stamp, document, documentGeneration, targetSubModel,
        targetPivot, collisionPolicy);
}

StampPlacementSessionResult StampPlacementSession::Begin(
    VoxelStamp stamp,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel,
    const StampFixedPoint targetPivot,
    const StampCollisionPolicy collisionPolicy)
{
    plan_.reset();
    static_cast<void>(preview_.Clear());
    variantGroup_.reset();
    variantAssetCache_ = nullptr;
    variantResolution_.reset();
    variantIdentity_.reset();
    stamp_ = std::move(stamp);
    targetSubModel_ = targetSubModel;
    documentInstanceToken_ =
        reinterpret_cast<std::uintptr_t>(&document);
    documentGeneration_ = documentGeneration;
    transform_ = {};
    transform_.TargetPivot = targetPivot;
    smartPlacementTarget_ = {};
    smartPlacementSuggestion_.reset();
    smartPlacementTemporarilyBypassed_ = false;
    smartPlacementOrientationLocked_ = false;
    smartPlacementAppliedToPreview_ = false;
    smartPlacementAssistRequestedForPlan_ = false;
    collisionPolicy_ = collisionPolicy;
    placementSessionSeed_ = 0U;
    placementOrdinal_ = 0U;
    state_ = StampPlacementSessionState::Active;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::BeginVariantGroup(
    StampVariantGroup group,
    StampAssetCache& assetCache,
    const std::uint64_t placementSessionSeed,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel,
    const StampFixedPoint targetPivot,
    const StampCollisionPolicy collisionPolicy)
{
    stamp_.reset();
    plan_.reset();
    static_cast<void>(preview_.Clear());
    variantGroup_ = std::move(group);
    variantAssetCache_ = &assetCache;
    variantResolution_.reset();
    variantIdentity_.reset();
    targetSubModel_ = targetSubModel;
    documentInstanceToken_ =
        reinterpret_cast<std::uintptr_t>(&document);
    documentGeneration_ = documentGeneration;
    transform_ = {};
    transform_.TargetPivot = targetPivot;
    smartPlacementTarget_ = {};
    smartPlacementSuggestion_.reset();
    smartPlacementTemporarilyBypassed_ = false;
    smartPlacementOrientationLocked_ = false;
    smartPlacementAppliedToPreview_ = false;
    smartPlacementAssistRequestedForPlan_ = false;
    collisionPolicy_ = collisionPolicy;
    placementSessionSeed_ = placementSessionSeed;
    placementOrdinal_ = 0U;
    state_ = StampPlacementSessionState::Active;
    return ResolveCurrentVariant(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::Rebuild(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    return variantGroup_ && !stamp_
        ? ResolveCurrentVariant(document, documentGeneration)
        : BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetTarget(
    const StampFixedPoint targetPivot,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    return UpdateTarget(targetPivot, document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::UpdateTarget(
    const StampFixedPoint targetPivot,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    transform_.TargetPivot = targetPivot;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::TranslateTarget(
    const std::int32_t x,
    const std::int32_t y,
    const std::int32_t z,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    const auto translated = [](const std::int32_t value,
                                const std::int32_t delta,
                                std::int32_t& output) noexcept
    {
        constexpr std::int64_t fixedUnits =
            StampFixedPoint::UnitsPerVoxel;
        const std::int64_t translatedValue =
            static_cast<std::int64_t>(value) +
            static_cast<std::int64_t>(delta) * fixedUnits;
        if (translatedValue < std::numeric_limits<std::int32_t>::min() ||
            translatedValue > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }
        output = static_cast<std::int32_t>(translatedValue);
        return true;
    };
    StampFixedPoint target{};
    if (!translated(transform_.TargetPivot.X, x, target.X) ||
        !translated(transform_.TargetPivot.Y, y, target.Y) ||
        !translated(transform_.TargetPivot.Z, z, target.Z))
    {
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Diagnostic =
                StampPlacementDiagnosticCode::PositionNotRepresentable};
    }
    return SetTarget(target, document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetQuarterRotation(
    const std::uint8_t quarterTurns,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    transform_.QuarterTurns =
        static_cast<std::uint8_t>(quarterTurns % 4U);
    smartPlacementOrientationLocked_ = true;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetGizmoTransform(
    const StampFixedPoint targetPivot,
    const std::uint8_t quarterTurns,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const StampPlacementRotationAxis axis)
{
    if (!IsActive())
    {
        return {};
    }
    if (axis != StampPlacementRotationAxis::VerticalY &&
        axis != StampPlacementRotationAxis::LateralX &&
        axis != StampPlacementRotationAxis::DepthZ)
    {
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Diagnostic = StampPlacementDiagnosticCode::UnsupportedRotation};
    }
    transform_.TargetPivot = targetPivot;
    transform_.RotationAxis = axis;
    transform_.QuarterTurns = static_cast<std::uint8_t>(quarterTurns % 4U);
    smartPlacementOrientationLocked_ = true;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::Rotate90(
    const StampPlacementRotationAxis axis,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const bool clockwise)
{
    if (!IsActive())
    {
        return {};
    }
    if (axis != StampPlacementRotationAxis::VerticalY &&
        axis != StampPlacementRotationAxis::LateralX &&
        axis != StampPlacementRotationAxis::DepthZ)
    {
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Diagnostic = StampPlacementDiagnosticCode::UnsupportedRotation};
    }
    // STAMP-24 : un seul axe actif a la fois. Changer d'axe repart de zero,
    // sinon l'angle courant serait reinterprete sur le nouvel axe et le Stamp
    // sauterait sans que l'utilisateur ait demande cette rotation.
    if (axis != transform_.RotationAxis)
    {
        transform_.RotationAxis = axis;
        transform_.QuarterTurns = 0U;
    }
    const std::uint8_t delta = clockwise ? 1U : 3U;
    transform_.QuarterTurns = static_cast<std::uint8_t>(
        (transform_.QuarterTurns + delta) % 4U);
    smartPlacementOrientationLocked_ = true;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::RotateClockwise(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    return Rotate90(StampPlacementRotationAxis::VerticalY,
        document, documentGeneration, true);
}

StampPlacementSessionResult StampPlacementSession::RotateCounterClockwise(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    return Rotate90(StampPlacementRotationAxis::VerticalY,
        document, documentGeneration, false);
}

StampPlacementSessionResult StampPlacementSession::RotateStep(
    const StampPlacementRotationAxis axis,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const bool clockwise)
{
    if (!IsActive())
    {
        return {};
    }
    if (axis != StampPlacementRotationAxis::VerticalY &&
        axis != StampPlacementRotationAxis::LateralX &&
        axis != StampPlacementRotationAxis::DepthZ)
    {
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Diagnostic = StampPlacementDiagnosticCode::UnsupportedRotation};
    }
    if (rotationStep_ == StampRotationStep::Quarter90)
    {
        return Rotate90(axis, document, documentGeneration, clockwise);
    }

    // STAMP-25 : a 45 degres l'orientation compte huit positions. On les
    // numerote pour que la rotation fasse le tour complet ; les positions
    // paires retombent exactement sur la grille, les impaires sont
    // reechantillonnees.
    if (axis != transform_.RotationAxis)
    {
        transform_.RotationAxis = axis;
        transform_.QuarterTurns = 0U;
        transform_.HalfQuarterStep = false;
    }
    const std::uint8_t current = static_cast<std::uint8_t>(
        transform_.QuarterTurns * 2U + (transform_.HalfQuarterStep ? 1U : 0U));
    const std::uint8_t next = static_cast<std::uint8_t>(
        (current + (clockwise ? 1U : 7U)) % 8U);
    transform_.QuarterTurns = static_cast<std::uint8_t>(next / 2U);
    transform_.HalfQuarterStep = (next % 2U) != 0U;
    smartPlacementOrientationLocked_ = true;
    return BuildCurrent(document, documentGeneration);
}

void StampPlacementSession::SetRotationStep(
    const StampRotationStep step) noexcept
{
    rotationStep_ = step;
}

void StampPlacementSession::ToggleRotationStep() noexcept
{
    rotationStep_ = rotationStep_ == StampRotationStep::Quarter90
        ? StampRotationStep::Eighth45
        : StampRotationStep::Quarter90;
}

StampPlacementSessionResult StampPlacementSession::SetHalfQuarterStep(
    const bool enabled,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    transform_.HalfQuarterStep = enabled;
    // Comme un quart de tour, un demi-cran est une decision d'orientation de
    // l'utilisateur : le placement assiste ne doit plus la remplacer.
    smartPlacementOrientationLocked_ = true;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::ToggleHalfQuarterStep(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    return SetHalfQuarterStep(
        !transform_.HalfQuarterStep, document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetMirror(
    const StampPlacementMirrorMode mirror,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    transform_.Mirror = mirror;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::ToggleMirror(
    const StampPlacementMirrorMode axis,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }

    StampPlacementMirrorMode next = transform_.Mirror;
    switch (axis)
    {
    case StampPlacementMirrorMode::X:
        next = transform_.Mirror == StampPlacementMirrorMode::None
            ? StampPlacementMirrorMode::X
            : transform_.Mirror == StampPlacementMirrorMode::X
            ? StampPlacementMirrorMode::None
            : transform_.Mirror == StampPlacementMirrorMode::Z
            ? StampPlacementMirrorMode::XZ
            : StampPlacementMirrorMode::Z;
        break;
    case StampPlacementMirrorMode::Z:
        next = transform_.Mirror == StampPlacementMirrorMode::None
            ? StampPlacementMirrorMode::Z
            : transform_.Mirror == StampPlacementMirrorMode::Z
            ? StampPlacementMirrorMode::None
            : transform_.Mirror == StampPlacementMirrorMode::X
            ? StampPlacementMirrorMode::XZ
            : StampPlacementMirrorMode::X;
        break;
    case StampPlacementMirrorMode::XZ:
        next = transform_.Mirror == StampPlacementMirrorMode::None
            ? StampPlacementMirrorMode::XZ
            : transform_.Mirror == StampPlacementMirrorMode::XZ
            ? StampPlacementMirrorMode::None
            : transform_.Mirror == StampPlacementMirrorMode::X
            ? StampPlacementMirrorMode::Z
            : StampPlacementMirrorMode::X;
        break;
    case StampPlacementMirrorMode::None:
        next = StampPlacementMirrorMode::None;
        break;
    default:
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Diagnostic = StampPlacementDiagnosticCode::UnsupportedMirror};
    }
    return SetMirror(next, document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::CycleMirror(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    StampPlacementMirrorMode next = StampPlacementMirrorMode::None;
    switch (transform_.Mirror)
    {
    case StampPlacementMirrorMode::None:
        next = StampPlacementMirrorMode::X;
        break;
    case StampPlacementMirrorMode::X:
        next = StampPlacementMirrorMode::Z;
        break;
    case StampPlacementMirrorMode::Z:
        next = StampPlacementMirrorMode::XZ;
        break;
    case StampPlacementMirrorMode::XZ:
        break;
    default:
        break;
    }
    return SetMirror(next, document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::ResetTransform(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    const StampFixedPoint target = transform_.TargetPivot;
    transform_ = {};
    transform_.TargetPivot = target;
    smartPlacementOrientationLocked_ = false;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult
StampPlacementSession::UpdateSmartPlacementContext(
    StampSmartPlacementTargetContext context,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    smartPlacementTarget_ = context;
    if (!IsActive())
    {
        return {
            .Code = StampPlacementSessionResultCode::Succeeded,
            .Succeeded = true};
    }
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetSmartPlacementEnabled(
    const bool enabled,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    smartPlacementEnabled_ = enabled;
    if (!IsActive())
    {
        return {
            .Code = StampPlacementSessionResultCode::Succeeded,
            .Succeeded = true};
    }
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetSmartPlacementMode(
    const StampSmartPlacementMode mode,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    switch (mode)
    {
    case StampSmartPlacementMode::Off:
    case StampSmartPlacementMode::Suggest:
    case StampSmartPlacementMode::PreviewAssist:
        break;
    default:
        return {
            .Code = StampPlacementSessionResultCode::InvalidPlan,
            .Succeeded = false};
    }
    smartPlacementMode_ = mode;
    if (!IsActive())
    {
        return {
            .Code = StampPlacementSessionResultCode::Succeeded,
            .Succeeded = true};
    }
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult
StampPlacementSession::SetSmartPlacementTemporaryBypass(
    const bool temporarilyBypassed,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    smartPlacementTemporarilyBypassed_ = temporarilyBypassed;
    if (!IsActive())
    {
        return {
            .Code = StampPlacementSessionResultCode::Succeeded,
            .Succeeded = true};
    }
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::RenewVariantSeed(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive() || !variantGroup_ || variantAssetCache_ == nullptr)
    {
        return {};
    }
    if (!MatchesDocumentContext(document, documentGeneration))
    {
        const bool previewChanged = CurrentPreview() != nullptr;
        const bool changed = Cancel();
        return {
            .Code = StampPlacementSessionResultCode::DocumentChanged,
            .Succeeded = false,
            .PlanChanged = changed,
            .PreviewChanged = previewChanged};
    }
    placementSessionSeed_ =
        RenewSessionSeed(placementSessionSeed_);
    return ResolveCurrentVariant(document, documentGeneration);
}

StampPlacementSessionPlaceResult StampPlacementSession::PlaceOnce(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    VoxelEditSession& editSession,
    VoxelEditHistory& history)
{
    if (!IsActive())
    {
        return {
            .Status = StampPlacementSessionPlaceStatus::Inactive,
            .History = SessionHistoryResult(
                VoxelEditHistoryResultCode::InvalidOperation,
                "No Stamp placement session is active.")};
    }
    if (!plan_)
    {
        const std::string message = variantResolution_ &&
                !variantResolution_->Succeeded()
            ? std::string(variantResolution_->Message)
            : variantResolution_
            ? "The resolved Smart Variant source asset is unavailable."
            : "The active Stamp placement has no valid plan.";
        return {
            .Status = StampPlacementSessionPlaceStatus::Rejected,
            .History = SessionHistoryResult(
                VoxelEditHistoryResultCode::InvalidOperation, message)};
    }
    if (!MatchesDocumentContext(document, documentGeneration))
    {
        const bool previewChanged = CurrentPreview() != nullptr;
        static_cast<void>(Cancel());
        return {
            .Status = StampPlacementSessionPlaceStatus::DocumentChanged,
            .PreviewChanged = previewChanged,
            .History = SessionHistoryResult(
                VoxelEditHistoryResultCode::InvalidOperation,
                "The active voxel document changed; Stamp placement was cancelled.")};
    }
    if (!IsCurrent(document, documentGeneration))
    {
        const StampPlacementSessionResult refreshed =
            BuildCurrent(document, documentGeneration);
        return {
            .Status = StampPlacementSessionPlaceStatus::PreviewRefreshed,
            .PreviewChanged = refreshed.PreviewChanged,
            .History = SessionHistoryResult(
                VoxelEditHistoryResultCode::InvalidOperation,
                "The document revision changed; the Stamp preview was refreshed.")};
    }
    if (placementOrdinal_ == std::numeric_limits<std::uint64_t>::max())
    {
        return {
            .Status = StampPlacementSessionPlaceStatus::Rejected,
            .History = SessionHistoryResult(
                VoxelEditHistoryResultCode::LimitExceeded,
                "The Stamp placement ordinal is exhausted.")};
    }

    VoxelEditHistoryResult executed = ExecutePlaceVoxelStampOperation(
        *plan_, editSession, history);
    if (executed.Code == VoxelEditHistoryResultCode::NoChange)
    {
        return {
            .Status = StampPlacementSessionPlaceStatus::NoChange,
            .History = std::move(executed)};
    }
    if (!executed)
    {
        return {
            .Status = StampPlacementSessionPlaceStatus::Rejected,
            .History = std::move(executed)};
    }

    ++placementOrdinal_;
    const StampPlacementSessionResult refreshed = variantGroup_
        ? ResolveCurrentVariant(document, documentGeneration)
        : BuildCurrent(document, documentGeneration);
    return {
        .Status = StampPlacementSessionPlaceStatus::Placed,
        .PreviewChanged = refreshed.PreviewChanged,
        .History = std::move(executed)};
}

bool StampPlacementSession::Cancel() noexcept
{
    const bool changed = stamp_.has_value() || plan_.has_value() ||
        preview_.Current() != nullptr ||
        state_ == StampPlacementSessionState::Active;
    stamp_.reset();
    variantGroup_.reset();
    variantAssetCache_ = nullptr;
    variantResolution_.reset();
    variantIdentity_.reset();
    plan_.reset();
    static_cast<void>(preview_.Clear());
    transform_ = {};
    smartPlacementTarget_ = {};
    smartPlacementSuggestion_.reset();
    smartPlacementTemporarilyBypassed_ = false;
    smartPlacementOrientationLocked_ = false;
    smartPlacementAppliedToPreview_ = false;
    smartPlacementAssistRequestedForPlan_ = false;
    collisionPolicy_ = StampCollisionPolicy::Overwrite;
    targetSubModel_ = 0U;
    documentInstanceToken_ = 0U;
    documentGeneration_ = 0U;
    placementSessionSeed_ = 0U;
    placementOrdinal_ = 0U;
    state_ = StampPlacementSessionState::Cancelled;
    return changed;
}

StampPlacementSessionState StampPlacementSession::State() const noexcept
{
    return state_;
}

bool StampPlacementSession::IsActive() const noexcept
{
    return state_ == StampPlacementSessionState::Active &&
        (stamp_.has_value() || variantGroup_.has_value());
}

bool StampPlacementSession::IsCurrent(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration) const noexcept
{
    return IsActive() && plan_ &&
        plan_->IsCurrent(document, documentGeneration, targetSubModel_);
}

const VoxelStamp* StampPlacementSession::ActiveStamp() const noexcept
{
    return stamp_ ? &*stamp_ : nullptr;
}

const StampPlacementPlan* StampPlacementSession::CurrentPlan() const noexcept
{
    return plan_ ? &*plan_ : nullptr;
}

const VoxelPreviewData*
StampPlacementSession::CurrentPreview() const noexcept
{
    return preview_.Current();
}

const StampPlacementCacheKey*
StampPlacementSession::CacheKey() const noexcept
{
    return plan_ ? &plan_->CacheKey : nullptr;
}

StampFixedPoint StampPlacementSession::Target() const noexcept
{
    return transform_.TargetPivot;
}

StampPlacementRotationAxis StampPlacementSession::RotationAxis() const noexcept
{
    return transform_.RotationAxis;
}

std::uint8_t StampPlacementSession::QuarterRotation() const noexcept
{
    return transform_.QuarterTurns;
}

bool StampPlacementSession::HalfQuarterStep() const noexcept
{
    return transform_.HalfQuarterStep;
}

StampRotationStep StampPlacementSession::RotationStep() const noexcept
{
    return rotationStep_;
}

std::uint32_t StampPlacementSession::RotationDegrees() const noexcept
{
    return static_cast<std::uint32_t>(transform_.QuarterTurns) * 90U +
        (transform_.HalfQuarterStep ? 45U : 0U);
}

StampPlacementMirrorMode StampPlacementSession::Mirror() const noexcept
{
    return transform_.Mirror;
}

std::size_t StampPlacementSession::TargetSubModel() const noexcept
{
    return targetSubModel_;
}

std::uint64_t StampPlacementSession::PlacementOrdinal() const noexcept
{
    return placementOrdinal_;
}

bool StampPlacementSession::IsVariantPlacement() const noexcept
{
    return IsActive() && variantGroup_.has_value();
}

const StampVariantGroup*
StampPlacementSession::ActiveVariantGroup() const noexcept
{
    return variantGroup_ ? &*variantGroup_ : nullptr;
}

const StampVariantResolutionReport*
StampPlacementSession::CurrentVariantResolution() const noexcept
{
    return variantResolution_ ? &*variantResolution_ : nullptr;
}

const StampPlacementVariantIdentity*
StampPlacementSession::CurrentVariantIdentity() const noexcept
{
    return variantIdentity_ ? &*variantIdentity_ : nullptr;
}

std::uint64_t StampPlacementSession::PlacementSessionSeed() const noexcept
{
    return placementSessionSeed_;
}

bool StampPlacementSession::SmartPlacementEnabled() const noexcept
{
    return smartPlacementEnabled_;
}

StampSmartPlacementMode StampPlacementSession::SmartPlacementMode() const noexcept
{
    return smartPlacementMode_;
}

bool StampPlacementSession::SmartPlacementTemporarilyBypassed() const noexcept
{
    return smartPlacementTemporarilyBypassed_;
}

bool StampPlacementSession::SmartPlacementOrientationLocked() const noexcept
{
    return smartPlacementOrientationLocked_;
}

bool StampPlacementSession::SmartPlacementAppliedToPreview() const noexcept
{
    return smartPlacementAppliedToPreview_;
}

const StampSmartPlacementSuggestion*
StampPlacementSession::CurrentSmartPlacementSuggestion() const noexcept
{
    return smartPlacementSuggestion_ ? &*smartPlacementSuggestion_ : nullptr;
}

StampPlacementSessionMetrics StampPlacementSession::Metrics() const noexcept
{
    return metrics_;
}

void StampPlacementSession::ResetMetrics() noexcept
{
    metrics_ = {};
}

bool StampPlacementSession::MatchesDocumentContext(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration) const noexcept
{
    return documentInstanceToken_ ==
            reinterpret_cast<std::uintptr_t>(&document) &&
        documentGeneration_ == documentGeneration;
}

StampPlacementSessionResult StampPlacementSession::ResolveCurrentVariant(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!variantGroup_ || variantAssetCache_ == nullptr)
    {
        return {};
    }
    if (!MatchesDocumentContext(document, documentGeneration))
    {
        const bool previewChanged = CurrentPreview() != nullptr;
        const bool changed = Cancel();
        return {
            .Code = StampPlacementSessionResultCode::DocumentChanged,
            .Succeeded = false,
            .PlanChanged = changed,
            .PreviewChanged = previewChanged};
    }

    const bool hadPlan = plan_.has_value();
    const bool previewChanged = CurrentPreview() != nullptr;
    plan_.reset();
    static_cast<void>(preview_.Clear());
    stamp_.reset();
    variantIdentity_.reset();
    try
    {
        variantResolution_ = ResolveVariant(
            *variantGroup_, placementSessionSeed_, placementOrdinal_);
        if (!variantResolution_->Succeeded())
        {
            return {
                .Code =
                    StampPlacementSessionResultCode::VariantResolutionFailed,
                .Succeeded = false,
                .PlanChanged = hadPlan,
                .PreviewChanged = previewChanged,
                .VariantError = variantResolution_->Error};
        }

        StampAssetCacheResult loaded = variantAssetCache_->GetOrLoad(
            variantResolution_->Resolved->Stamp);
        if (!loaded.Succeeded())
        {
            return {
                .Code =
                    StampPlacementSessionResultCode::VariantAssetUnavailable,
                .Succeeded = false,
                .PlanChanged = hadPlan,
                .PreviewChanged = previewChanged,
                .LibraryError = loaded.Error};
        }

        stamp_ = *loaded.Stamp;
        variantIdentity_ = StampPlacementVariantIdentity{
            .GroupId = variantGroup_->Id(),
            .GroupRevision = variantGroup_->Revision(),
            .VariantId = variantResolution_->Resolved->VariantId,
            .StampId = variantResolution_->Resolved->Stamp.Id,
            .ExpectedContentHash =
                variantResolution_->Resolved->Stamp.ContentHash,
            .PlacementSessionSeed = placementSessionSeed_,
            .SelectionSeed = variantResolution_->SelectionSeed,
            .PlacementOrdinal = placementOrdinal_};
        return BuildCurrent(document, documentGeneration);
    }
    catch (...)
    {
        stamp_.reset();
        variantIdentity_.reset();
        variantResolution_.reset();
        return {
            .Code = StampPlacementSessionResultCode::VariantResolutionFailed,
            .Succeeded = false,
            .PlanChanged = hadPlan,
            .PreviewChanged = previewChanged,
            .VariantError = StampVariantResolutionError::AllocationFailure};
    }
}

StampPlacementSessionResult StampPlacementSession::BuildCurrent(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!stamp_)
    {
        return {};
    }
    if (!MatchesDocumentContext(document, documentGeneration))
    {
        const bool previewChanged = CurrentPreview() != nullptr;
        const bool changed = Cancel();
        return {
            .Code = StampPlacementSessionResultCode::DocumentChanged,
            .Succeeded = false,
            .PlanChanged = changed,
            .PreviewChanged = previewChanged};
    }
    ++metrics_.BuildRequests;

    const std::optional<StampSmartPlacementSuggestion> previousSuggestion =
        smartPlacementSuggestion_;
    const StampSmartPlacementSuggestion nextSuggestion = SuggestPlacement({
        .Stamp = &*stamp_,
        .UserTransform = transform_,
        .Target = smartPlacementTarget_,
        .Enabled = smartPlacementEnabled_ &&
            smartPlacementMode_ != StampSmartPlacementMode::Off,
        .TemporarilyBypassed = smartPlacementTemporarilyBypassed_,
        .OrientationLockedByUser = smartPlacementOrientationLocked_});
    const bool assistRequested =
        smartPlacementMode_ == StampSmartPlacementMode::PreviewAssist &&
        nextSuggestion.Available();

    const bool reusableInputs = plan_ &&
        plan_->Stamp == stamp_->Identity() &&
        plan_->Variant == variantIdentity_ &&
        plan_->CollisionPolicy == collisionPolicy_ &&
        plan_->CacheKey.PaletteCapacity == 256U &&
        plan_->CacheKey.ReservedDocumentPaletteIndex == 0U &&
        plan_->CacheKey.ResourceLimits == DefaultStampResourceLimits() &&
        plan_->IsCurrent(document, documentGeneration, targetSubModel_);
    bool reuseCurrentPlan = false;
    bool nextAppliedToPreview = false;
    if (reusableInputs && !assistRequested &&
        plan_->Transform == transform_)
    {
        reuseCurrentPlan = true;
    }
    else if (reusableInputs && assistRequested &&
             plan_->Transform == nextSuggestion.Transform)
    {
        reuseCurrentPlan = true;
        nextAppliedToPreview = true;
    }
    else if (reusableInputs && assistRequested &&
             smartPlacementAssistRequestedForPlan_ &&
             previousSuggestion && *previousSuggestion == nextSuggestion &&
             !smartPlacementAppliedToPreview_ &&
             plan_->Transform == transform_)
    {
        // The identical suggestion was already rejected because it would have
        // turned an allowed manual placement into a refusal. Reuse that safe
        // fallback until an input or document revision changes.
        reuseCurrentPlan = true;
    }

    smartPlacementSuggestion_ = nextSuggestion;
    if (reuseCurrentPlan)
    {
        smartPlacementAppliedToPreview_ = nextAppliedToPreview;
        smartPlacementAssistRequestedForPlan_ = assistRequested;
        ++metrics_.PlanCacheHits;
        const StampPlacementDiagnosticCode firstDiagnostic =
            plan_->Diagnostics.empty()
            ? StampPlacementDiagnosticCode::None
            : plan_->Diagnostics.front().Code;
        const bool succeeded =
            plan_->WorldBounds.Valid && !plan_->Voxels.empty();
        return {
            .Code = succeeded
                ? StampPlacementSessionResultCode::Succeeded
                : StampPlacementSessionResultCode::InvalidPlan,
            .Succeeded = succeeded,
            .PlanChanged = false,
            .PreviewChanged = false,
            .Diagnostic = firstDiagnostic};
    }

    const auto buildPlan = [&](const StampPlacementTransform& transform)
    {
        ++metrics_.PlannerBuilds;
        return StampPlacementPlanner::Build({
            .Stamp = &*stamp_,
            .Variant = variantIdentity_,
            .Document = &document,
            .DocumentGeneration = documentGeneration,
            .TargetSubModel = targetSubModel_,
            .Transform = transform,
            .CollisionPolicy = collisionPolicy_});
    };

    StampPlacementPlan next = buildPlan(transform_);
    smartPlacementAppliedToPreview_ = false;
    if (assistRequested)
    {
        if (nextSuggestion.Transform == transform_)
        {
            smartPlacementAppliedToPreview_ = true;
        }
        else
        {
            ++metrics_.AssistedPlannerBuilds;
            StampPlacementPlan assisted =
                buildPlan(nextSuggestion.Transform);
            if (!next.CanCommit || assisted.CanCommit)
            {
                next = std::move(assisted);
                smartPlacementAppliedToPreview_ = true;
            }
        }
    }
    smartPlacementAssistRequestedForPlan_ = assistRequested;
    const StampPlacementDiagnosticCode firstDiagnostic =
        next.Diagnostics.empty()
        ? StampPlacementDiagnosticCode::None
        : next.Diagnostics.front().Code;
    const bool planChanged = !plan_ || plan_->CacheKey != next.CacheKey ||
        plan_->CanCommit != next.CanCommit ||
        plan_->Diagnostics != next.Diagnostics ||
        plan_->Voxels != next.Voxels ||
        plan_->PaletteMapping != next.PaletteMapping;
    plan_ = std::move(next);
    ++metrics_.PreviewBuilds;
    const bool previewChanged =
        preview_.Activate(BuildStampPreview(*plan_));
    if (previewChanged)
    {
        ++metrics_.PreviewChanges;
    }
    const bool succeeded =
        plan_->WorldBounds.Valid && !plan_->Voxels.empty();
    return {
        .Code = succeeded
            ? StampPlacementSessionResultCode::Succeeded
            : StampPlacementSessionResultCode::InvalidPlan,
        .Succeeded = succeeded,
        .PlanChanged = planChanged,
        .PreviewChanged = previewChanged,
        .Diagnostic = firstDiagnostic};
}

} // namespace VoxelForge::Editor::Stamps
