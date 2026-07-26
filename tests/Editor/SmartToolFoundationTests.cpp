#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartBrushPreviewResolver.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace VoxelForge::Editor;

namespace
{
void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

SmartToolRequest Request(const SmartAction action = SmartAction::Add)
{
    SmartBrushState state;
    state.Size = 2;
    state.Mode = action == SmartAction::Erase ? SmartBrushMode::Erase : SmartBrushMode::Add;
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = action;
    request.BrushRequest.Dimensions = {8U, 8U, 8U};
    request.BrushRequest.State = state;
    request.BrushRequest.Placement = {{3, 3, 3}, {0, 1, 0}};
    const VoxelForge::Asset::Voxel::VoxelPosition occupied{3, 3, 3};
    request.BrushRequest.IsOccupied = [occupied](
        const VoxelForge::Asset::Voxel::VoxelPosition position)
    {
        return position == occupied;
    };
    request.SourceIdentity = 42U;
    request.SourceRevision = 7U;
    request.SourceGeneration = 11U;
    return request;
}
}

int main()
{
    try
    {
        static_assert(std::is_const_v<SmartToolPlanPtr::element_type>);

        SmartToolController controller;
        SmartToolSession session;
        SmartToolSessionState sessionState;
        sessionState.ToolActive = true;
        sessionState.Geometry = SmartGeometry::Pencil;
        sessionState.ActiveProfileUuid = "profile-wood";
        sessionState.PaletteIndex = 17U;
        sessionState.Brush.Size = 3;
        sessionState.Action = SmartAction::Add;
        sessionState.Mode = SmartBrushMode::Add;
        sessionState.Selection.push_back({1, 2, 3});
        sessionState.Workplane = SmartBrushPlacement{{2, 4, 6}, {0, 1, 0}};
        session.SetState(sessionState);
        Require(session.State().ActiveProfileUuid == "profile-wood" &&
            session.State().PaletteIndex == 17U &&
            session.State().Selection.size() == 1U &&
            session.State().Workplane.has_value(),
            "session value state was not retained");
        const SmartToolRequest add = Request();
        SmartToolRequest profileA = add;
        profileA.ActiveProfileUuid = "profile-a";
        profileA.BrushRequest.State.PaletteIndex = 17U;
        const SmartToolResult first = controller.ResolvePreview(session, profileA);
        Require(first.HasPlan(), "planner did not create an add plan");
        Require(first.Code == SmartBrushResultCode::Valid &&
            first.Status == SmartToolStatus::Success,
            "add plan did not propagate success");
        Require(first.Plan->BrushResult().Statistics.Total > 0U,
            "add plan has no cells");
        Require(first.Plan->BrushState().Mode == SmartBrushMode::Add,
            "planner did not normalize the add action");
        Require(first.Plan->ActiveProfileUuid() == "profile-a" &&
            first.Plan->BrushState().PaletteIndex == 17U,
            "plan did not snapshot profile and palette values");
        Require(session.PlanForPreview().get() == session.PlanForCommit().get(),
            "preview and commit do not share plan identity");
        Require(first.Plan->PlanId() != 0U &&
            first.Plan->PlanId() == first.Plan->Revision() &&
            !first.Plan->Cells().empty(),
            "immutable plan metadata or resolved cells missing");
        Require(session.State().PaletteIndex == first.Plan->BrushState().PaletteIndex &&
            !session.State().Workplane.has_value(),
            "ordinary hit placement was stored as a workplane");
        const SmartBrushPreviewResult preview = SmartBrushPreviewResolver::Resolve(
            *first.Plan, {1.0F, 1.0F, 1.0F, 1.0F});
        Require(preview.Statistics.IsConsistent(),
            "plan-backed preview statistics are inconsistent");

        SmartToolRequest profileB = profileA;
        profileB.ActiveProfileUuid = "profile-b";
        profileB.BrushRequest.State.PaletteIndex = 17U;
        Require(first.Plan->ActiveProfileUuid() == "profile-a" &&
            first.Plan->BrushState().PaletteIndex == 17U,
            "a later profile or palette change mutated a rendered plan");
        const SmartToolResult cached = controller.ResolveCommit(session);
        Require(cached.Plan.get() == first.Plan.get(),
            "same request was planned twice");
        SmartToolSession profileBSession;
        const SmartToolResult profileBPreview = controller.ResolvePreview(
            profileBSession, profileB);
        Require(profileBPreview.HasPlan() &&
            profileBPreview.Plan.get() != first.Plan.get(),
            "a profile identity change reused the profile A plan");
        const std::size_t cellCountBeforeExternalMutation = first.Plan->Cells().size();
        const VoxelForge::Asset::Voxel::VoxelPosition firstCellBeforeExternalMutation =
            first.Plan->Cells().front().Position;
        profileA.BrushRequest.State.Size = 9;
        Require(first.Plan->Cells().size() == cellCountBeforeExternalMutation &&
            first.Plan->Cells().front().Position == firstCellBeforeExternalMutation,
            "plan cells retained mutable request-owned storage");

        SmartToolRequest moved = add;
        moved.BrushRequest.Placement.Target = {4, 3, 3};
        const SmartToolResult changed = controller.ResolvePreview(session, moved);
        Require(changed.HasPlan() && changed.Plan.get() != first.Plan.get(),
            "changed placement did not replace plan");

        const SmartToolResult erasePreview = controller.ResolvePreview(
            session, Request(SmartAction::Erase));
        const SmartToolResult erase = controller.ResolveCommit(session);
        Require(erase.HasPlan() &&
            erase.Plan->BrushState().Mode == SmartBrushMode::Erase,
            "planner did not normalize the erase action");
        Require(session.State().Mode == SmartBrushMode::Erase,
            "session did not retain normalized erase mode");
        Require(erase.Plan->BrushResult().Statistics.Existing > 0U,
            "erase plan lost occupancy information");
        Require(erase.Plan.get() == erasePreview.Plan.get(),
            "commit did not consume the plan rendered by preview");

        session.Clear();
        const SmartToolResult missingCommit = controller.ResolveCommit(session);
        Require(!missingCommit.HasPlan() &&
            missingCommit.Code == SmartBrushResultCode::InvalidRequest,
            "commit planned a replacement instead of refusing a missing preview plan");

        SmartToolRequest workplane = Request();
        workplane.Workplane = workplane.BrushRequest.Placement;
        const SmartToolResult workplanePlan = controller.ResolvePreview(session, workplane);
        Require(workplanePlan.HasPlan() && session.State().Workplane.has_value() &&
            session.State().Workplane->Target ==
                workplane.BrushRequest.Placement.Target,
            "real workplane was not retained in the session");

        SmartToolRequest unsupported = Request();
        unsupported.Geometry = SmartGeometry::Face;
        const SmartToolResult rejected = controller.ResolvePreview(session, unsupported);
        Require(!rejected.HasPlan() &&
            rejected.Code == SmartBrushResultCode::Unsupported &&
            rejected.Status == SmartToolStatus::Error,
            "unsupported geometry was accepted");
        Require(session.PlanForPreview() == nullptr,
            "unsupported request retained a stale session plan");

        SmartToolRequest noIdentity = Request();
        noIdentity.SourceIdentity = 0U;
        noIdentity.SourceRevision = 0U;
        noIdentity.SourceGeneration = 0U;
        const SmartToolResult identityFree = controller.ResolvePreview(session, noIdentity);
        Require(identityFree.Status == SmartToolStatus::Error &&
            !identityFree.HasPlan() && !identityFree.Error.empty() &&
            session.PlanForPreview() == nullptr,
            "controller accepted a request without a source identity");

        SmartToolRequest noChange = Request();
        noChange.BrushRequest.IsOccupied = [](const auto) { return true; };
        const SmartToolResult ignored = controller.ResolvePreview(session, noChange);
        Require(ignored.Succeeded() && ignored.HasPlan(),
            "valid no-change plan was rejected");
        const bool hasActionableCell = std::any_of(ignored.Plan->Cells().begin(),
            ignored.Plan->Cells().end(), [](const SmartToolPlanCell& cell)
            {
                return cell.Operation != SmartToolCellOperation::Ignore;
            });
        Require(!hasActionableCell, "empty actionable plan was not preserved");

        std::cout << "Smart Tool foundation tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
