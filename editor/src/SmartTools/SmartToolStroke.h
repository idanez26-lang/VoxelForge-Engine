#pragma once

#include "SmartTools/SmartToolPlan.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace VoxelForge::Editor
{
// A deterministic grid segment. It has no knowledge of input events, ImGui,
// rendering, or document mutation.
class SmartToolStrokeInterpolator final
{
public:
    [[nodiscard]] static std::vector<Asset::Voxel::VoxelPosition> Sample(
        Asset::Voxel::VoxelPosition from, Asset::Voxel::VoxelPosition to);
};

struct SmartToolStrokeContext final
{
    std::uintptr_t DocumentIdentity = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::uint64_t DocumentGeneration = 0U;
    std::size_t SubModelIndex = 0U;
    std::function<SmartToolVoxelState(Asset::Voxel::VoxelPosition)> ReadSourceVoxel;
};

enum class SmartToolStrokeSurfacePolicy : std::uint8_t
{
    Unlocked,
    LockPencilSurface
};

// Accumulates planner-owned cells without mutating the document. A single
// logical change is retained for every world voxel: its initial Before state
// is immutable, while the latest planner-provided After state wins.
class SmartToolStroke final
{
public:
    [[nodiscard]] bool Begin(SmartToolStrokeContext context,
        SmartAction action, Asset::Voxel::VoxelPosition target,
        Asset::Voxel::VoxelPosition normal,
        SmartToolStrokeSurfacePolicy surfacePolicy =
            SmartToolStrokeSurfacePolicy::Unlocked);
    void Cancel() noexcept;
    void Suspend() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;
    [[nodiscard]] SmartAction Action() const noexcept;
    [[nodiscard]] const SmartToolStrokeContext& Context() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

    // Returns the next planner targets. A changed surface normal and a prior
    // invalid target deliberately begin a new segment instead of bridging
    // unrelated surfaces or an invalid gap.
    [[nodiscard]] std::vector<Asset::Voxel::VoxelPosition> Advance(
        Asset::Voxel::VoxelPosition target, Asset::Voxel::VoxelPosition normal);

    [[nodiscard]] SmartToolVoxelState ReadVoxel(
        Asset::Voxel::VoxelPosition position) const;
    [[nodiscard]] bool Accumulate(const SmartToolPlan& plan);
    // Replaces the pending edit with one complete, source-relative plan. This
    // is intentionally separate from Accumulate(): ordinary Pencil strokes
    // union sampled segments, whereas a Face depth drag is a single mutable
    // extrusion whose latest depth is the only pending result.
    [[nodiscard]] bool ReplaceWithPlan(const SmartToolPlan& plan);
    [[nodiscard]] std::vector<Asset::Voxel::VoxelDocumentChange> Changes() const;
    // LOT 4b : vue sans copie sur la meme liste triee que Changes(). Le trait
    // est reconstruit et retrie a chaque frame par le compositeur de preview,
    // alors qu'il ne change qu'a l'ajout d'une cellule ; cette vue est
    // memoisee sur revision_. Le contenu est identique a Changes(), a la
    // copie pres. La reference reste valide jusqu'a la prochaine mutation.
    [[nodiscard]] std::span<const Asset::Voxel::VoxelDocumentChange>
        ChangesView() const;
    [[nodiscard]] std::vector<Asset::Voxel::VoxelDocumentChange> PreviewChanges(
        const SmartToolPlan& nextPlan) const;
    [[nodiscard]] bool HasChanges() const noexcept;

private:
    struct PositionHash final
    {
        [[nodiscard]] std::size_t operator()(
            const Asset::Voxel::VoxelPosition position) const noexcept;
    };
    struct AccumulatedCell final
    {
        Asset::Voxel::VoxelDocumentChange Change{};
    };

    [[nodiscard]] bool MatchesContext(const SmartToolPlan& plan) const noexcept;
    static void MergeChange(
        std::unordered_map<Asset::Voxel::VoxelPosition, AccumulatedCell, PositionHash>& cells,
        const Asset::Voxel::VoxelDocumentChange& change);
    void SetAnchor(Asset::Voxel::VoxelPosition target,
        Asset::Voxel::VoxelPosition normal) noexcept;
    [[nodiscard]] static bool IsUnitAxisNormal(
        Asset::Voxel::VoxelPosition normal) noexcept;
    void LockPencilSurface(Asset::Voxel::VoxelPosition target,
        Asset::Voxel::VoxelPosition normal) noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition ConstrainToPencilSurface(
        Asset::Voxel::VoxelPosition target) const noexcept;

    SmartToolStrokeContext context_{};
    SmartAction action_ = SmartAction::Add;
    SmartToolStrokeSurfacePolicy surfacePolicy_ =
        SmartToolStrokeSurfacePolicy::Unlocked;
    bool active_ = false;
    bool suspended_ = false;
    std::optional<Asset::Voxel::VoxelPosition> lastTarget_;
    std::optional<Asset::Voxel::VoxelPosition> lastNormal_;
    std::optional<Asset::Voxel::VoxelPosition> pencilSurfaceNormal_;
    std::int32_t pencilSurfaceCoordinate_ = 0;
    std::unordered_map<Asset::Voxel::VoxelPosition, AccumulatedCell, PositionHash>
        cells_;
    std::uint64_t revision_ = 0U;
    // LOT 4b : memoisation de la liste triee. Mutable parce que Changes() est
    // et doit rester const : c'est un cache, pas un etat observable.
    mutable std::vector<Asset::Voxel::VoxelDocumentChange> changesCache_;
    mutable std::uint64_t changesCacheRevision_ = 0U;
    mutable bool changesCacheValid_ = false;
};
} // namespace VoxelForge::Editor
