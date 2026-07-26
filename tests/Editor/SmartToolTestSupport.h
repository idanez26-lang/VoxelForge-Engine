#pragma once

// Shared test-only bridge for the post-SMART-02 Pencil contract. It uses the
// production Controller -> Session -> Planner path before producing the plan
// and execution context consumed by VoxelPencilTool.
#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartToolController.h"
#include "VoxelTools/VoxelPencilTool.h"

namespace VoxelForge::Editor::TestSupport
{
inline SmartToolPlanPtr PlanPencil(VoxelEditSession& session,
    Asset::Voxel::VoxelDocument& document, const std::size_t subModelIndex,
    const SmartBrushState& state, const Asset::Voxel::VoxelPosition target,
    const Asset::Voxel::VoxelPosition normal)
{
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions) return nullptr;
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = state.Mode == SmartBrushMode::Erase
        ? SmartAction::Erase : SmartAction::Add;
    request.BrushRequest = {*dimensions, state, {target, normal},
        [&document, subModelIndex](const Asset::Voxel::VoxelPosition position)
        { return document.HasVoxel(position, subModelIndex); }};
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = session.VoxelModelGeneration();
    request.SourceSubModelIndex = subModelIndex;
    SmartToolController controller;
    SmartToolSession smartSession;
    return controller.ResolvePreview(smartSession, request).Plan;
}

inline VoxelPencilContext MakePencilContext(VoxelEditSession& session,
    Asset::Voxel::VoxelDocument& document, const std::size_t subModelIndex,
    const SmartBrushState& state, const Asset::Voxel::VoxelPosition target,
    const Asset::Voxel::VoxelPosition normal,
    VoxelEditHistory* const history = nullptr)
{
    return {PlanPencil(session, document, subModelIndex, state, target, normal),
        {&session, &document, subModelIndex, session.VoxelModelGeneration(), history,
            std::optional<std::size_t>{state.PaletteIndex}, nullptr, nullptr}};
}
} // namespace VoxelForge::Editor::TestSupport
