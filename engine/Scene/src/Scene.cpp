#include "VoxelForge/Scene/Scene.h"

#include <algorithm>
#include <utility>

namespace VoxelForge::Scene
{

Scene::Scene(std::string name)
    : name_(std::move(name))
{
}

Entity& Scene::CreateEntity(std::string name)
{
    auto entity = std::make_unique<Entity>(std::move(name));
    Entity& reference = *entity;
    entities_.push_back(std::move(entity));
    return reference;
}

bool Scene::DestroyEntity(const Core::UUID& id)
{
    const auto iterator = std::find_if(
        entities_.begin(),
        entities_.end(),
        [&id](const std::unique_ptr<Entity>& entity)
        {
            return entity->GetId() == id;
        });

    if (iterator == entities_.end())
    {
        return false;
    }

    entities_.erase(iterator);
    return true;
}

Entity* Scene::FindEntity(const Core::UUID& id) noexcept
{
    const auto iterator = std::find_if(
        entities_.begin(),
        entities_.end(),
        [&id](const std::unique_ptr<Entity>& entity)
        {
            return entity->GetId() == id;
        });

    return iterator == entities_.end() ? nullptr : iterator->get();
}

const Entity* Scene::FindEntity(const Core::UUID& id) const noexcept
{
    const auto iterator = std::find_if(
        entities_.begin(),
        entities_.end(),
        [&id](const std::unique_ptr<Entity>& entity)
        {
            return entity->GetId() == id;
        });

    return iterator == entities_.end() ? nullptr : iterator->get();
}

const std::string& Scene::GetName() const noexcept
{
    return name_;
}

std::size_t Scene::GetEntityCount() const noexcept
{
    return entities_.size();
}

const std::vector<std::unique_ptr<Entity>>&
Scene::GetEntities() const noexcept
{
    return entities_;
}

} // namespace VoxelForge::Scene
