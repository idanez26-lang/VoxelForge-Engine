#pragma once

#include <string>

namespace VoxelForge
{
class Event;
}

namespace VoxelForge::Core
{

class Layer
{
public:
    explicit Layer(std::string name = "Layer");
    virtual ~Layer() = default;

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&) = delete;
    Layer& operator=(Layer&&) = delete;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate() {}
    virtual void OnEvent(VoxelForge::Event&) {}
    virtual void OnImGuiRender() {}

    [[nodiscard]] const std::string& GetName() const noexcept;

private:
    std::string name_;
};

} // namespace VoxelForge::Core
