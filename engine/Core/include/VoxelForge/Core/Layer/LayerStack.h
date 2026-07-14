#pragma once

#include "VoxelForge/Core/Layer/Layer.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace VoxelForge::Core
{

class LayerStack
{
public:
    using LayerPointer = std::unique_ptr<Layer>;
    using Container = std::vector<LayerPointer>;
    using Iterator = Container::iterator;
    using ConstIterator = Container::const_iterator;
    using ReverseIterator = Container::reverse_iterator;
    using ConstReverseIterator = Container::const_reverse_iterator;

    LayerStack() = default;
    ~LayerStack();

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;
    LayerStack(LayerStack&&) = delete;
    LayerStack& operator=(LayerStack&&) = delete;

    Layer& PushLayer(LayerPointer layer);
    Layer& PushOverlay(LayerPointer overlay);
    LayerPointer PopLayer(Layer& layer);
    LayerPointer PopOverlay(Layer& overlay);

    void Clear() noexcept;

    [[nodiscard]] std::size_t GetLayerCount() const noexcept;
    [[nodiscard]] std::size_t GetOverlayCount() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

    Iterator begin() noexcept { return layers_.begin(); }
    Iterator end() noexcept { return layers_.end(); }
    ConstIterator begin() const noexcept { return layers_.begin(); }
    ConstIterator end() const noexcept { return layers_.end(); }
    ReverseIterator rbegin() noexcept { return layers_.rbegin(); }
    ReverseIterator rend() noexcept { return layers_.rend(); }
    ConstReverseIterator rbegin() const noexcept { return layers_.rbegin(); }
    ConstReverseIterator rend() const noexcept { return layers_.rend(); }

private:
    Container layers_;
    std::size_t layerInsertIndex_ = 0;
};

} // namespace VoxelForge::Core
