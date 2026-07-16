#pragma once

#include "VoxelModel.h"

#include <filesystem>
#include <optional>
#include <string>

namespace VoxelForge::Voxel
{

struct VoxelSerializationResult final
{
    bool Succeeded = false;
    std::string Message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Succeeded;
    }
};

struct VoxelDeserializationResult final
{
    std::optional<VoxelModel> Model;
    std::string Message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Model.has_value();
    }
};

class VoxelModelSerializer final
{
public:
    [[nodiscard]] static VoxelSerializationResult Save(
        const std::filesystem::path& destination,
        const VoxelModel& model);

    [[nodiscard]] static VoxelDeserializationResult Load(
        const std::filesystem::path& source);
};

} // namespace VoxelForge::Voxel
