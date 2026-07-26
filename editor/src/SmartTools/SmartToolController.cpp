#include "SmartTools/SmartToolController.h"

namespace VoxelForge::Editor
{
SmartToolResult SmartToolController::Resolve(
    SmartToolSession& session, const SmartToolRequest& request)
{
    session.SetActiveGeometry(request.Geometry);
    session.SetAction(request.Action);
    session.SetMode(request.BrushRequest.State.Mode);
    session.SetBrush(request.BrushRequest.State);
    session.SetPaletteIndex(
        std::optional<std::size_t>{request.BrushRequest.State.PaletteIndex});
    session.SetActiveProfileUuid(request.ActiveProfileUuid);
    session.SetWorkplane(request.Workplane);
    const SmartToolRequestKey key = MakeSmartToolRequestKey(request);
    if (session.HasPlanFor(key))
    {
        const SmartToolPlanPtr plan = session.PlanForPreview();
        session.SetBrush(plan->BrushState());
        session.SetMode(plan->BrushState().Mode);
        return {SmartToolStatusFrom(plan->BrushResult().Code),
            plan->BrushResult().Code, plan, plan->BrushResult().Error};
    }

    SmartToolResult result = planner_.Plan(request);
    if (result.HasPlan())
    {
        session.SetPlan(result.Plan);
        session.SetBrush(result.Plan->BrushState());
        session.SetMode(result.Plan->BrushState().Mode);
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
