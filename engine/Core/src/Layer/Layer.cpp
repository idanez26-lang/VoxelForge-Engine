#include "VoxelForge/Core/Layer/Layer.h"

#include <utility>

namespace VoxelForge::Core
{

Layer::Layer(std::string name)
    : name_(std::move(name))
{
}

const std::string& Layer::GetName() const noexcept
{
    return name_;
}

} // namespace VoxelForge::Core
