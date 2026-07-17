#pragma once

#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

struct VoxelSpherePreview final
{
    Asset::Voxel::VoxelPosition Center{};
    std::uint32_t Radius = 0U;

    [[nodiscard]] bool operator==(const VoxelSpherePreview&) const noexcept = default;
};

enum class VoxelSphereResultCode : std::uint8_t
{
    Applied,
    NoDocument,
    NoTarget,
    InvalidModel,
    InvalidPaletteIndex,
    NoChanges,
    Blocked,
    Failed
};

struct VoxelSphereResult final
{
    VoxelSphereResultCode Code = VoxelSphereResultCode::Failed;
    bool Changed = false;
    VoxelSpherePreview Sphere{};
    std::size_t ChangedVoxelCount = 0U;
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelSphereContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    Asset::Voxel::VoxelPosition Center{};
    Asset::Voxel::VoxelPosition RadiusPoint{};
    std::size_t PaletteIndex = 1U;
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

class VoxelSphereService final
{
public:
    [[nodiscard]] static std::uint32_t CalculateRadius(
        Asset::Voxel::VoxelPosition center,
        Asset::Voxel::VoxelPosition radiusPoint) noexcept;
    [[nodiscard]] static std::vector<Asset::Voxel::VoxelPosition>
        CalculatePositions(
            const Asset::Voxel::VoxelDocument& document,
            std::size_t subModelIndex,
            Asset::Voxel::VoxelPosition center,
            Asset::Voxel::VoxelPosition radiusPoint);
    [[nodiscard]] static VoxelSphereResult Apply(
        const VoxelSphereContext& context);
};

class VoxelSphereInteraction final
{
public:
    [[nodiscard]] bool Begin(
        Asset::Voxel::VoxelPosition center,
        std::uint64_t documentGeneration) noexcept;
    [[nodiscard]] bool Update(
        std::optional<Asset::Voxel::VoxelPosition> radiusPoint) noexcept;
    void Cancel() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> Center() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> RadiusPoint() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;

private:
    std::optional<Asset::Voxel::VoxelPosition> center_;
    std::optional<Asset::Voxel::VoxelPosition> radiusPoint_;
    std::uint64_t documentGeneration_ = 0U;
};

[[nodiscard]] const char* VoxelSphereResultCodeName(
    VoxelSphereResultCode code) noexcept;

} // namespace VoxelForge::Editor
