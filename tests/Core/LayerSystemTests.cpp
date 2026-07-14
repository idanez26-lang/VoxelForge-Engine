#include "VoxelForge/Core/Event/WindowEvent.h"
#include "VoxelForge/Core/Layer/LayerStack.h"

#include <cassert>
#include <memory>
#include <string>

namespace
{

class TestLayer final : public VoxelForge::Core::Layer
{
public:
    explicit TestLayer(std::string name)
        : Layer(std::move(name))
    {
    }

    void OnAttach() override { attached = true; }
    void OnDetach() override { detached = true; }
    void OnUpdate() override { ++updateCount; }

    void OnEvent(VoxelForge::Event& event) override
    {
        receivedEvent = true;
        event.Handled = true;
    }

    bool attached = false;
    bool detached = false;
    bool receivedEvent = false;
    int updateCount = 0;
};

} // namespace

int main()
{
    using namespace VoxelForge::Core;

    LayerStack stack;

    auto layer = std::make_unique<TestLayer>("GameLayer");
    auto* layerAddress = layer.get();
    Layer& storedLayer = stack.PushLayer(std::move(layer));

    auto overlay = std::make_unique<TestLayer>("DebugOverlay");
    Layer& storedOverlay = stack.PushOverlay(std::move(overlay));

    assert(&storedLayer == layerAddress);
    assert(stack.GetLayerCount() == 1);
    assert(stack.GetOverlayCount() == 1);
    assert(layerAddress->attached);

    auto removedOverlay = stack.PopOverlay(storedOverlay);
    assert(removedOverlay != nullptr);
    assert(stack.GetOverlayCount() == 0);

    auto removedLayer = stack.PopLayer(storedLayer);
    assert(removedLayer != nullptr);
    assert(stack.Empty());

    return 0;
}
