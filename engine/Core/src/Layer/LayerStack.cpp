#include "VoxelForge/Core/Layer/LayerStack.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace VoxelForge::Core
{

LayerStack::~LayerStack()
{
    Clear();
}

Layer& LayerStack::PushLayer(LayerPointer layer)
{
    if (!layer)
        throw std::invalid_argument("LayerStack::PushLayer received a null layer.");

    auto position = layers_.begin() + static_cast<std::ptrdiff_t>(layerInsertIndex_);
    auto inserted = layers_.insert(position, std::move(layer));
    ++layerInsertIndex_;
    (*inserted)->OnAttach();
    return *(*inserted);
}

Layer& LayerStack::PushOverlay(LayerPointer overlay)
{
    if (!overlay)
        throw std::invalid_argument("LayerStack::PushOverlay received a null overlay.");

    layers_.push_back(std::move(overlay));
    layers_.back()->OnAttach();
    return *layers_.back();
}

LayerStack::LayerPointer LayerStack::PopLayer(Layer& layer)
{
    const auto firstOverlay =
        layers_.begin() + static_cast<std::ptrdiff_t>(layerInsertIndex_);

    const auto iterator = std::find_if(
        layers_.begin(),
        firstOverlay,
        [&layer](const LayerPointer& candidate)
        {
            return candidate.get() == &layer;
        });

    if (iterator == firstOverlay)
        return nullptr;

    (*iterator)->OnDetach();
    LayerPointer removed = std::move(*iterator);
    layers_.erase(iterator);
    --layerInsertIndex_;
    return removed;
}

LayerStack::LayerPointer LayerStack::PopOverlay(Layer& overlay)
{
    const auto firstOverlay =
        layers_.begin() + static_cast<std::ptrdiff_t>(layerInsertIndex_);

    const auto iterator = std::find_if(
        firstOverlay,
        layers_.end(),
        [&overlay](const LayerPointer& candidate)
        {
            return candidate.get() == &overlay;
        });

    if (iterator == layers_.end())
        return nullptr;

    (*iterator)->OnDetach();
    LayerPointer removed = std::move(*iterator);
    layers_.erase(iterator);
    return removed;
}

void LayerStack::Clear() noexcept
{
    while (!layers_.empty())
    {
        LayerPointer& layer = layers_.back();

        if (layer)
            layer->OnDetach();

        layers_.pop_back();
    }

    layerInsertIndex_ = 0;
}

std::size_t LayerStack::GetLayerCount() const noexcept
{
    return layerInsertIndex_;
}

std::size_t LayerStack::GetOverlayCount() const noexcept
{
    return layers_.size() - layerInsertIndex_;
}

bool LayerStack::Empty() const noexcept
{
    return layers_.empty();
}

} // namespace VoxelForge::Core
