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

enum class VoxelLineResultCode : std::uint8_t
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

struct VoxelLineResult final
{
    VoxelLineResultCode Code = VoxelLineResultCode::Failed;
    bool Changed = false;
    std::size_t ChangedVoxelCount = 0U;
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelLineContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    Asset::Voxel::VoxelPosition PointA{};
    Asset::Voxel::VoxelPosition PointB{};
    std::size_t PaletteIndex = 1U;
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

class VoxelLineService final
{
public:
    [[nodiscard]] static std::vector<Asset::Voxel::VoxelPosition>
        CalculatePositions(
            const Asset::Voxel::VoxelDocument& document,
            std::size_t subModelIndex,
            Asset::Voxel::VoxelPosition pointA,
            Asset::Voxel::VoxelPosition pointB);
    [[nodiscard]] static VoxelLineResult Apply(const VoxelLineContext& context);
};

class VoxelLineInteraction final
{
public:
    [[nodiscard]] bool Begin(
        Asset::Voxel::VoxelPosition point,
        std::uint64_t documentGeneration) noexcept;
    [[nodiscard]] bool Update(
        std::optional<Asset::Voxel::VoxelPosition> point) noexcept;
    void Cancel() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> PointA() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> PointB() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;

private:
    std::optional<Asset::Voxel::VoxelPosition> pointA_;
    std::optional<Asset::Voxel::VoxelPosition> pointB_;
    std::uint64_t documentGeneration_ = 0U;
};

[[nodiscard]] const char* VoxelLineResultCodeName(
    VoxelLineResultCode code) noexcept;

} // namespace VoxelForge::Editor
