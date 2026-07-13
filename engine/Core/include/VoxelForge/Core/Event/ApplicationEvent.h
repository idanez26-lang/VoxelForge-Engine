#pragma once
#include "VoxelForge/Core/Event/Event.h"

namespace VoxelForge
{
class AppTickEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(AppTick)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};

class AppUpdateEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(AppUpdate)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};

class AppRenderEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(AppRender)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};
} // namespace VoxelForge
