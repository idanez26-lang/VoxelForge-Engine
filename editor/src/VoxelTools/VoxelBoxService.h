#pragma once

#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

struct VoxelBoxBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};

    [[nodiscard]] bool operator==(const VoxelBoxBounds&) const noexcept = default;
};

enum class VoxelBoxResultCode : std::uint8_t
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

struct VoxelBoxResult final
{
    VoxelBoxResultCode Code = VoxelBoxResultCode::Failed;
    bool Changed = false;
    VoxelBoxBounds Bounds{};
    std::size_t ChangedVoxelCount = 0U;
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelBoxContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    Asset::Voxel::VoxelPosition CornerA{};
    Asset::Voxel::VoxelPosition CornerB{};
    std::size_t PaletteIndex = 1U;
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

class VoxelBoxService final
{
public:
    static constexpr std::int32_t MaximumExtentPerAxis = 64;

    [[nodiscard]] static std::optional<VoxelBoxBounds> CalculateBounds(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t subModelIndex,
        Asset::Voxel::VoxelPosition cornerA,
        Asset::Voxel::VoxelPosition cornerB) noexcept;
    [[nodiscard]] static VoxelBoxResult Apply(const VoxelBoxContext& context);
};

class VoxelBoxInteraction final
{
public:
    [[nodiscard]] bool Begin(
        Asset::Voxel::VoxelPosition corner,
        std::uint64_t documentGeneration) noexcept;
    [[nodiscard]] bool Update(
        std::optional<Asset::Voxel::VoxelPosition> corner) noexcept;
    void Cancel() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> CornerA() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition> CornerB() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;

private:
    std::optional<Asset::Voxel::VoxelPosition> cornerA_;
    std::optional<Asset::Voxel::VoxelPosition> cornerB_;
    std::uint64_t documentGeneration_ = 0U;
};

[[nodiscard]] const char* VoxelBoxResultCodeName(VoxelBoxResultCode code) noexcept;

} // namespace VoxelForge::Editor
