#pragma once

#include "VoxelForge/Core/UUID.h"
#include "VoxelForge/Scene/Components.h"

#include <string>

namespace VoxelForge::Scene
{

class Entity final
{
public:
    explicit Entity(std::string name);

    [[nodiscard]] const Core::UUID& GetId() const noexcept;
    [[nodiscard]] const std::string& GetName() const noexcept;
    void SetName(std::string name);

    [[nodiscard]] TransformComponent& GetTransform() noexcept;
    [[nodiscard]] const TransformComponent& GetTransform() const noexcept;

    [[nodiscard]] MetadataComponent& GetMetadata() noexcept;
    [[nodiscard]] const MetadataComponent& GetMetadata() const noexcept;

    [[nodiscard]] ForgeDNAComponent& GetForgeDNA() noexcept;
    [[nodiscard]] const ForgeDNAComponent& GetForgeDNA() const noexcept;

private:
    Core::UUID id_{};
    std::string name_;
    TransformComponent transform_{};
    MetadataComponent metadata_{};
    ForgeDNAComponent forgeDNA_{};
};

} // namespace VoxelForge::Scene
