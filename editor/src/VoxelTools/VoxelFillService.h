#pragma once

#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelSelection/VoxelRaycast.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

enum class VoxelFillResultCode
{
    Applied,
    NoDocument,
    NoHit,
    TargetMissing,
    SameColor,
    InvalidModel,
    InvalidPaletteIndex,
    Blocked,
    Failed
};

struct VoxelFillResult final
{
    VoxelFillResultCode Code = VoxelFillResultCode::Failed;
    bool Changed = false;
    Asset::Voxel::VoxelPosition Position{};
    std::uint8_t PreviousPaletteIndex = 0U;
    std::uint8_t NewPaletteIndex = 0U;
    std::size_t ChangedVoxelCount = 0U;
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelFillContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::optional<VoxelRaycastHit> Hit;
    std::size_t PaletteIndex = 1U;
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

class VoxelFillService final
{
public:
    [[nodiscard]] static VoxelFillResult Apply(
        const VoxelFillContext& context);
};

[[nodiscard]] const char* VoxelFillResultCodeName(
    VoxelFillResultCode code) noexcept;

} // namespace VoxelForge::Editor
