#include "SmartTools/SmartToolController.h"

namespace VoxelForge::Editor
{
SmartToolResult SmartToolController::Resolve(
    SmartToolSession& session, const SmartToolRequest& request)
{
    const SmartToolRequestKey key = MakeSmartToolRequestKey(request);
    session.SetActiveGeometry(request.Geometry);
    session.SetAction(request.Action);
    session.SetMode(key.State.Mode);
    session.SetBrush(key.State);
    session.SetPaletteIndex(
        std::optional<std::size_t>{key.State.PaletteIndex});
    session.SetActiveProfileUuid(request.ActiveProfileUuid);
    session.SetWorkplane(request.Workplane);
    if (session.HasPlanFor(key))
    {
        const SmartToolPlanPtr plan = session.PlanForPreview();
        return {SmartToolStatusFrom(plan->BrushResult().Code),
            plan->BrushResult().Code, plan, plan->BrushResult().Error};
    }

    SmartToolResult result = planner_.Plan(request);
    if (result.HasPlan())
    {
        session.SetBrush(result.Plan->BrushState());
        session.SetMode(result.Plan->BrushState().Mode);
        session.SetPlan(result.Plan);
    }
    else session.Clear();
    return result;
}

SmartToolResult SmartToolController::ResolvePreview(
    SmartToolSession& session, const SmartToolRequest& request)
{
    return Resolve(session, request);
}

SmartToolResult SmartToolController::ResolveCommit(
    const SmartToolSession& session) const
{
    const SmartToolPlanPtr plan = session.PlanForCommit();
    if (plan == nullptr)
    {
        // A commit may consume only the immutable plan rendered by preview.
        // Planning here would create a second geometry path at click time.
        return {SmartToolStatus::Error, SmartBrushResultCode::InvalidRequest,
            nullptr, "The rendered Smart Tool plan is missing or stale."};
    }
    return {SmartToolStatusFrom(plan->BrushResult().Code),
        plan->BrushResult().Code, plan, plan->BrushResult().Error};
}
} // namespace VoxelForge::Editor
