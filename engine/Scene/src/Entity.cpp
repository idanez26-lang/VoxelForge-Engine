#include "VoxelForge/Scene/Entity.h"

#include <utility>

namespace VoxelForge::Scene
{

Entity::Entity(std::string name)
    : name_(std::move(name))
{
}

const Core::UUID& Entity::GetId() const noexcept
{
    return id_;
}

const std::string& Entity::GetName() const noexcept
{
    return name_;
}

void Entity::SetName(std::string name)
{
    name_ = std::move(name);
}

TransformComponent& Entity::GetTransform() noexcept
{
    return transform_;
}

const TransformComponent& Entity::GetTransform() const noexcept
{
    return transform_;
}

MetadataComponent& Entity::GetMetadata() noexcept
{
    return metadata_;
}

const MetadataComponent& Entity::GetMetadata() const noexcept
{
    return metadata_;
}

ForgeDNAComponent& Entity::GetForgeDNA() noexcept
{
    return forgeDNA_;
}

const ForgeDNAComponent& Entity::GetForgeDNA() const noexcept
{
    return forgeDNA_;
}

} // namespace VoxelForge::Scene
