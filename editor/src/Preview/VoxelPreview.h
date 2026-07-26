#pragma once

#include "VoxelForge/Asset/Vox/VoxModel.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Core/UUID.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

/// Shared, renderer-neutral preview contract.  A preview is an immutable
/// voxel snapshot: it never retains a document pointer and cannot edit it.
enum class VoxelPreviewState : std::uint8_t { Valid, Overlap, Invalid };

struct VoxelPreviewFixedPoint final
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Z = 0;
    [[nodiscard]] bool operator==(const VoxelPreviewFixedPoint&) const noexcept = default;
};

struct VoxelPreviewBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
    [[nodiscard]] bool operator==(const VoxelPreviewBounds&) const noexcept = default;
};

struct VoxelPreviewPivot final
{
    VoxelPreviewFixedPoint LocalPosition{};
    std::int8_t NormalX = 0;
    std::int8_t NormalY = 0;
    std::int8_t NormalZ = 0;
    [[nodiscard]] bool operator==(const VoxelPreviewPivot&) const noexcept = default;
};

/// Neutral exact-grid transform contract. QuarterTurns is resolved by the
/// domain planner; preview consumers only retain it as immutable metadata.
struct VoxelPreviewTransform final
{
    VoxelPreviewFixedPoint TargetPivot{};
    std::uint8_t QuarterTurns = 0U;
    [[nodiscard]] bool operator==(const VoxelPreviewTransform&) const noexcept = default;
};

struct VoxelPreviewVoxel final
{
    Asset::Voxel::VoxelPosition Position{};
    Asset::Vox::VoxColor Color{};
    bool OverlapsExisting = false;

    [[nodiscard]] bool operator==(const VoxelPreviewVoxel&) const noexcept = default;
};

struct VoxelPreviewSourceVoxel final
{
    Asset::Voxel::VoxelPosition LocalPosition{};
    Asset::Vox::VoxColor Color{};
};

struct VoxelPreviewData final
{
    Core::UUID SourceId{0U};
    std::string SourceRevision;
    std::vector<VoxelPreviewVoxel> Voxels;
    VoxelPreviewBounds LocalBounds{};
    VoxelPreviewBounds WorldBounds{};
    VoxelPreviewPivot Pivot{};
    /// Exact fixed-point pivot target used to derive the discrete ghost cells.
    /// It is preserved even when the renderer only consumes grid cells.
    VoxelPreviewTransform Transform{};
    VoxelPreviewState State = VoxelPreviewState::Invalid;
    std::uint64_t Revision = 0U;

    [[nodiscard]] bool IsActive() const noexcept
    {
        return SourceId.Value() != 0U && !Voxels.empty();
    }
};

struct VoxelPreviewBuildRequest final
{
    Core::UUID SourceId{0U};
    std::string SourceRevision;
    VoxelPreviewBounds Bounds{};
    VoxelPreviewPivot Pivot{};
    VoxelPreviewFixedPoint TargetPivot{};
    std::span<const VoxelPreviewSourceVoxel> Voxels;
    bool ForceInvalid = false;
};

/// Generic fixed-point transformation builder. It knows neither VoxelStamp,
/// editable documents, ImGui nor a renderer. Domain adapters classify overlaps
/// after this exact snapshot has been constructed.
class VoxelPreviewBuilder final
{
public:
    [[nodiscard]] static VoxelPreviewData Build(const VoxelPreviewBuildRequest& request) noexcept;
};

/// Owns one preview snapshot and increments Revision only when the prepared
/// data actually changes.  Future Smart Tools, selection, prefabs and AI can
/// use this same session without depending on the concrete renderer.
class VoxelPreviewSession final
{
public:
    [[nodiscard]] bool Activate(VoxelPreviewData preview);
    [[nodiscard]] bool Clear() noexcept;
    [[nodiscard]] const VoxelPreviewData* Current() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

private:
    std::optional<VoxelPreviewData> preview_;
    std::uint64_t revision_ = 0U;
};

} // namespace VoxelForge::Editor
