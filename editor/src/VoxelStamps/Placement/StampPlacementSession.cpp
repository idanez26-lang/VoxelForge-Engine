#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include <limits>
#include <utility>

namespace VoxelForge::Editor::Stamps
{

StampPlacementSessionResult StampPlacementSession::Begin(
    VoxelStamp stamp,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel,
    const StampFixedPoint targetPivot,
    const StampCollisionPolicy collisionPolicy)
{
    stamp_ = std::move(stamp);
    targetSubModel_ = targetSubModel;
    transform_ = {};
    transform_.TargetPivot = targetPivot;
    collisionPolicy_ = collisionPolicy;
    placementOrdinal_ = 0U;
    state_ = StampPlacementSessionState::Active;
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::Rebuild(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!IsActive())
    {
        return {};
    }
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::SetTarget(
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
    return BuildCurrent(document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::RotateClockwise(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    return SetQuarterRotation(
        static_cast<std::uint8_t>((transform_.QuarterTurns + 1U) % 4U),
        document, documentGeneration);
}

StampPlacementSessionResult StampPlacementSession::RotateCounterClockwise(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    return SetQuarterRotation(
        static_cast<std::uint8_t>((transform_.QuarterTurns + 3U) % 4U),
        document, documentGeneration);
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

bool StampPlacementSession::Cancel() noexcept
{
    const bool changed = stamp_.has_value() || plan_.has_value() ||
        preview_.Current() != nullptr ||
        state_ == StampPlacementSessionState::Active;
    stamp_.reset();
    plan_.reset();
    static_cast<void>(preview_.Clear());
    transform_ = {};
    collisionPolicy_ = StampCollisionPolicy::Overwrite;
    targetSubModel_ = 0U;
    placementOrdinal_ = 0U;
    state_ = StampPlacementSessionState::Cancelled;
    return changed;
}

void StampPlacementSession::MarkPlacementCommitted() noexcept
{
    if (IsActive() &&
        placementOrdinal_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++placementOrdinal_;
    }
}

StampPlacementSessionState StampPlacementSession::State() const noexcept
{
    return state_;
}

bool StampPlacementSession::IsActive() const noexcept
{
    return state_ == StampPlacementSessionState::Active &&
        stamp_.has_value();
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

std::uint8_t StampPlacementSession::QuarterRotation() const noexcept
{
    return transform_.QuarterTurns;
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

StampPlacementSessionResult StampPlacementSession::BuildCurrent(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration)
{
    if (!stamp_)
    {
        return {};
    }
    StampPlacementPlan next = StampPlacementPlanner::Build({
        .Stamp = &*stamp_,
        .Document = &document,
        .DocumentGeneration = documentGeneration,
        .TargetSubModel = targetSubModel_,
        .Transform = transform_,
        .CollisionPolicy = collisionPolicy_});
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
    const bool previewChanged =
        preview_.Activate(StampLivePreviewBuilder::Build(*plan_));
    return {
        .Succeeded = plan_->WorldBounds.Valid && !plan_->Voxels.empty(),
        .PlanChanged = planChanged,
        .PreviewChanged = previewChanged,
        .Diagnostic = firstDiagnostic};
}

} // namespace VoxelForge::Editor::Stamps
