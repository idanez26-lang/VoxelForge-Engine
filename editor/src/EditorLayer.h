#pragma once

#include "VoxelForge/Core/Layer/Layer.h"

namespace VoxelForge::Editor
{

class EditorLayer final : public Core::Layer
{
public:
    EditorLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnImGuiRender() override;

private:
    bool showDemoWindow_ = true;
    bool showAboutWindow_ = false;
};

} // namespace VoxelForge::Editor
