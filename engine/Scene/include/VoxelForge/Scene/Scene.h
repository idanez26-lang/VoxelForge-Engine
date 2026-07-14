#pragma once

#include "VoxelForge/Scene/Entity.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Scene
{

class Scene final
{
public:
    explicit Scene(std::string name);

    Entity& CreateEntity(std::string name);
    bool DestroyEntity(const Core::UUID& id);

    [[nodiscard]] Entity* FindEntity(const Core::UUID& id) noexcept;
    [[nodiscard]] const Entity* FindEntity(const Core::UUID& id) const noexcept;

    [[nodiscard]] const std::string& GetName() const noexcept;
    [[nodiscard]] std::size_t GetEntityCount() const noexcept;

    [[nodiscard]] const std::vector<std::unique_ptr<Entity>>&
    GetEntities() const noexcept;

private:
    std::string name_;
    std::vector<std::unique_ptr<Entity>> entities_;
};

} // namespace VoxelForge::Scene
