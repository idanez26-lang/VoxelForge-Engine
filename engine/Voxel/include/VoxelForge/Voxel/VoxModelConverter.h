#pragma once

#include "VoxelModel.h"

#include "VoxelForge/Asset/Vox/VoxModel.h"

#include <optional>
#include <string>
#include <string_view>

namespace VoxelForge::Voxel
{

enum class VoxModelConversionError
{
    None,
    EmptyModel,
    InvalidDimensions,
    GridTooLarge,
    InvalidVoxel,
    DuplicateVoxel
};

struct VoxModelConversionResult final
{
    bool Succeeded = false;
    VoxModelConversionError Error = VoxModelConversionError::None;
    std::string Message;
    std::optional<VoxelModel> Model;
};

class VoxModelConverter final
{
public:
    [[nodiscard]] static VoxModelConversionResult Convert(
        const Asset::Vox::VoxModel& source,
        std::string_view name = {});
};

} // namespace VoxelForge::Voxel
