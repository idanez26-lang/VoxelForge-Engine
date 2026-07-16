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

enum class VoxelEraserResultCode
{
    Applied,
    NoDocument,
    NoHit,
    TargetMissing,
    InvalidModel,
    Blocked,
    Failed
};

struct VoxelEraserResult final
{
    VoxelEraserResultCode Code = VoxelEraserResultCode::Failed;
    bool Changed = false;
    Asset::Voxel::VoxelPosition Position{};
    std::uint8_t RemovedPaletteIndex = 0U;
    std::size_t SubModelIndex = 0U;
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelEraserContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::optional<VoxelRaycastHit> Hit;
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

class VoxelEraserTool final
{
public:
    [[nodiscard]] static VoxelEraserResult Apply(
        const VoxelEraserContext& context);
};

[[nodiscard]] const char* VoxelEraserResultCodeName(
    VoxelEraserResultCode code) noexcept;

} // namespace VoxelForge::Editor
