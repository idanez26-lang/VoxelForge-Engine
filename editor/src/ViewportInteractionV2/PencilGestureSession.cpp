#include "ViewportInteractionV2/PencilGestureSession.h"
namespace VoxelForge::Editor::InteractionV2
{
std::size_t PencilGestureSession::SampleKeyHash::operator()(
    const SampleKey& key) const noexcept
{
    const auto hashPosition = [](const Asset::Voxel::VoxelPosition position)
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Y)) << 11U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Z)) << 22U);
    };
    return hashPosition(key.Center) ^ (hashPosition(key.Normal) * 31U);
}
void PencilGestureSession::Begin(const std::uint64_t id, PencilCompactRequest request) noexcept
{ Reset(); phase_ = PencilGesturePhase::Armed; id_ = id; request_ = std::move(request); lockedSurfaceNormal_ = request_.Placement.Normal; lockedSurfaceCoordinate_ = request_.Placement.Normal.X != 0 ? request_.Placement.Target.X : request_.Placement.Normal.Y != 0 ? request_.Placement.Target.Y : request_.Placement.Target.Z; }
bool PencilGestureSession::Append(PencilCompactPlanPtr plan, const Asset::Voxel::VoxelPosition center, const Asset::Voxel::VoxelPosition normal) noexcept
{ if (!plan || !visited_.insert({center, normal}).second) return false; plans_.push_back(std::move(plan)); lastCenter_=center; lastNormal_=normal; suspended_=false; phase_=PencilGesturePhase::Dragging; return true; }
PencilSurfaceSample PencilGestureSession::ResolveSurfaceSample(
    Asset::Voxel::VoxelPosition target, const Asset::Voxel::VoxelPosition normal) noexcept
{
    PencilSurfaceSample sample{target, normal, false};
    if (!lockedSurfaceNormal_ || *lockedSurfaceNormal_ != normal)
    {
        lockedSurfaceNormal_ = normal;
        lockedSurfaceCoordinate_ = normal.X != 0 ? target.X : normal.Y != 0 ? target.Y : target.Z;
        sample.StartsNewSurfaceSegment = true;
        return sample;
    }
    if (normal.X != 0) sample.Center.X = lockedSurfaceCoordinate_;
    else if (normal.Y != 0) sample.Center.Y = lockedSurfaceCoordinate_;
    else sample.Center.Z = lockedSurfaceCoordinate_;
    return sample;
}
void PencilGestureSession::Complete() noexcept { if (OwnsPointer()) phase_=PencilGesturePhase::Completed; }
void PencilGestureSession::Cancel() noexcept { if (OwnsPointer()) { plans_.clear(); phase_=PencilGesturePhase::Cancelled; } }
bool PencilGestureSession::Suspend() noexcept { if (!OwnsPointer() || suspended_) return false; lastCenter_.reset(); lastNormal_.reset(); suspended_=true; return true; }
void PencilGestureSession::Reset() noexcept { phase_=PencilGesturePhase::Idle; id_=0; request_={}; lastCenter_.reset(); lastNormal_.reset(); lockedSurfaceNormal_.reset(); lockedSurfaceCoordinate_=0; suspended_=false; visited_.clear(); plans_.clear(); }
PencilGesturePhase PencilGestureSession::Phase() const noexcept { return phase_; }
bool PencilGestureSession::OwnsPointer() const noexcept { return phase_==PencilGesturePhase::Armed || phase_==PencilGesturePhase::Dragging; }
std::uint64_t PencilGestureSession::DocumentGeneration() const noexcept { return request_.DocumentGeneration; }
std::uint64_t PencilGestureSession::DocumentRevision() const noexcept { return request_.DocumentRevision; }
const PencilCompactRequest& PencilGestureSession::SourceRequest() const noexcept { return request_; }
const std::vector<PencilCompactPlanPtr>& PencilGestureSession::Plans() const noexcept { return plans_; }
const std::optional<Asset::Voxel::VoxelPosition>& PencilGestureSession::LastCenter() const noexcept { return lastCenter_; }
const std::optional<Asset::Voxel::VoxelPosition>& PencilGestureSession::LastNormal() const noexcept { return lastNormal_; }
bool PencilGestureSession::HasVisited(const Asset::Voxel::VoxelPosition center,
    const Asset::Voxel::VoxelPosition normal) const noexcept
{ return visited_.contains({center, normal}); }
bool PencilGestureSession::IsSuspended() const noexcept { return suspended_; }
} // namespace VoxelForge::Editor::InteractionV2
