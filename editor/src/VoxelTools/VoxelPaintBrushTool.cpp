#include "VoxelPaintBrushTool.h"

#include "SmartTools/SmartToolController.h"
#include "VoxelTools/VoxelPencilTool.h"

#include <exception>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
VoxelPaintBrushEvaluation Refused(
    const VoxelPaintBrushResultCode code,
    std::optional<Asset::Voxel::VoxelPosition> target = std::nullopt,
    std::string error = {})
{
    return {code, target, {}, {}, {}, {}, {}, std::move(error), nullptr};
}

VoxelPaintBrushResult ResultFromEvaluation(
    const VoxelPaintBrushEvaluation& evaluation,
    const std::uint64_t revision)
{
    return {
        evaluation.Code, false, evaluation.Target.value_or(
            Asset::Voxel::VoxelPosition{}), evaluation.Statistics,
        revision, revision, evaluation.Error};
}

struct PlannedPaint final
{
    VoxelPaintBrushEvaluation Evaluation;
    SmartToolPlanPtr Plan;
    std::uint64_t SourceGeneration = 0U;
};

VoxelPaintBrushEvaluation EvaluationFromPlan(
    SmartToolPlanPtr plan,
    const std::optional<Asset::Voxel::VoxelPosition> target)
{
    VoxelPaintBrushEvaluation evaluation;
    evaluation.Target = target;
    evaluation.RenderPlan = plan->BrushResult().RenderPlan;
    evaluation.Statistics = {plan->Statistics().Total,
        plan->Statistics().Changed, plan->Statistics().Unchanged,
        plan->Statistics().Clipped};
    evaluation.Positions.reserve(plan->Cells().size());
    evaluation.PaintablePositions.reserve(plan->Statistics().Changed);
    evaluation.IgnoredPositions.reserve(plan->Statistics().Unchanged);
    for (const SmartToolPlanCell& cell : plan->Cells())
    {
        if (cell.OutOfBounds()) continue;
        evaluation.Positions.push_back(cell.WorldPosition);
        if (cell.HasChange())
            evaluation.PaintablePositions.push_back(cell.WorldPosition);
        else
            evaluation.IgnoredPositions.push_back(cell.WorldPosition);
    }
    if (plan->BrushResult().Code == SmartBrushResultCode::OutOfBounds)
        evaluation.Code = VoxelPaintBrushResultCode::TargetOutOfBounds;
    else if (plan->BrushResult().Code != SmartBrushResultCode::Valid)
    {
        evaluation.Code = VoxelPaintBrushResultCode::Failed;
        evaluation.Error = plan->BrushResult().Error;
    }
    else
        evaluation.Code = plan->HasChanges()
            ? VoxelPaintBrushResultCode::Applied
            : VoxelPaintBrushResultCode::NoChange;
    evaluation.Plan = std::move(plan);
    return evaluation;
}

PlannedPaint ResolvePaint(const VoxelPaintBrushContext& context)
{
    if (context.Document == nullptr)
        return {Refused(VoxelPaintBrushResultCode::NoDocument), nullptr, 0U};
    Asset::Voxel::VoxelDocument& document = *context.Document;
    if (context.Blocked)
        return {Refused(VoxelPaintBrushResultCode::Blocked), nullptr, 0U};
    if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
        return {Refused(VoxelPaintBrushResultCode::NoHit), nullptr, 0U};

    const Asset::Voxel::VoxelPosition target{
        static_cast<std::int32_t>(context.Hit->Coordinates.X),
        static_cast<std::int32_t>(context.Hit->Coordinates.Y),
        static_cast<std::int32_t>(context.Hit->Coordinates.Z)};
    if (context.Hit->SubModelIndex != context.SubModelIndex ||
        context.Hit->DocumentRevision != document.GetRevision() ||
        document.GetModel(context.SubModelIndex) == nullptr)
    {
        return {Refused(VoxelPaintBrushResultCode::InvalidModel, target,
            "The Paint target does not belong to the active document state."),
            nullptr, 0U};
    }
    if (context.State.PaletteIndex == 0U || context.State.PaletteIndex > 255U)
    {
        return {Refused(
            VoxelPaintBrushResultCode::InvalidPaletteIndex, target), nullptr, 0U};
    }
    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    if (!dimensions)
        return {Refused(VoxelPaintBrushResultCode::InvalidModel, target),
            nullptr, 0U};

    SmartBrushState state = context.State;
    state.Mode = SmartBrushMode::Paint;
    const Asset::Voxel::VoxelPosition normal =
        state.Dimension == SmartBrushDimension::Surface2D
        ? VoxelHitFaceIntegerNormal(context.Hit->Face)
        : Asset::Voxel::VoxelPosition{};
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = SmartAction::Paint;
    request.BrushRequest = {*dimensions, state, {target, normal}, {}};
    request.ReadVoxel =
        [&document, subModelIndex = context.SubModelIndex](
            const Asset::Voxel::VoxelPosition position)
        {
            const auto voxel = document.GetVoxel(position, subModelIndex);
            return SmartToolVoxelState{
                voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
        };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(&document);
    request.SourceRevision = document.GetRevision();
    request.SourceGeneration = context.EditSession
        ? context.EditSession->VoxelModelGeneration() : 0U;
    request.SourceSubModelIndex = context.SubModelIndex;

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult planned = controller.ResolvePreview(session, request);
    if (!planned.HasPlan())
    {
        return {Refused(VoxelPaintBrushResultCode::Failed, target,
            planned.Error), nullptr, request.SourceGeneration};
    }

    VoxelPaintBrushEvaluation evaluation =
        EvaluationFromPlan(planned.Plan, target);
    return {std::move(evaluation), planned.Plan, request.SourceGeneration};
}
}

VoxelPaintBrushEvaluation VoxelPaintBrushTool::Evaluate(
    const VoxelPaintBrushContext& context)
{
    try
    {
        return ResolvePaint(context).Evaluation;
    }
    catch (const std::exception& exception)
    {
        return Refused(
            VoxelPaintBrushResultCode::Failed, std::nullopt, exception.what());
    }
    catch (...)
    {
        return Refused(VoxelPaintBrushResultCode::Failed, std::nullopt,
            "Paint Brush evaluation failed.");
    }
}

VoxelPaintBrushResult VoxelPaintBrushTool::Apply(
    const VoxelPaintBrushContext& context)
{
    const std::uint64_t revision = context.Document
        ? context.Document->GetRevision() : 0U;
    if (context.Plan == nullptr)
        return {VoxelPaintBrushResultCode::Failed, false, {}, {},
            revision, revision,
            "Paint Brush requires the exact plan produced by preview."};
    PlannedPaint planned;
    try
    {
        planned = {EvaluationFromPlan(
            context.Plan, context.Plan->Placement().Target),
            context.Plan, context.Plan->CacheKey().SourceGeneration};
    }
    catch (const std::exception& exception)
    {
        return {VoxelPaintBrushResultCode::Failed, false, {}, {},
            revision, revision, exception.what()};
    }
    catch (...)
    {
        return {VoxelPaintBrushResultCode::Failed, false, {}, {},
            revision, revision, "Paint Brush plan adaptation failed."};
    }
    const VoxelPaintBrushEvaluation& evaluation = planned.Evaluation;
    if (!evaluation.IsResolved()) return ResultFromEvaluation(evaluation, revision);
    if (evaluation.Code == VoxelPaintBrushResultCode::NoChange)
        return ResultFromEvaluation(evaluation, revision);
    if (context.EditSession == nullptr || context.History == nullptr ||
        context.Document == nullptr)
    {
        VoxelPaintBrushEvaluation invalid = evaluation;
        invalid.Code = VoxelPaintBrushResultCode::InvalidModel;
        invalid.Error = "Paint requires an editable model and voxel history.";
        return ResultFromEvaluation(invalid, revision);
    }

    const VoxelToolResult applied = VoxelPencilTool::Apply({
        planned.Plan,
        {context.EditSession, context.Document, context.SubModelIndex,
            planned.SourceGeneration, context.History,
            std::optional<std::size_t>{context.State.PaletteIndex},
            nullptr, nullptr}});
    if (applied.Code != VoxelToolResultCode::Applied)
    {
        return {VoxelPaintBrushResultCode::Failed, applied.Changed,
            applied.Position, evaluation.Statistics, revision,
            applied.RevisionAfter, applied.Error};
    }
    return {VoxelPaintBrushResultCode::Applied, true,
        evaluation.Target.value_or(Asset::Voxel::VoxelPosition{}),
        evaluation.Statistics, revision, applied.RevisionAfter, {}};
}

const char* VoxelPaintBrushResultCodeName(
    const VoxelPaintBrushResultCode code) noexcept
{
    switch (code)
    {
    case VoxelPaintBrushResultCode::Applied: return "Applied";
    case VoxelPaintBrushResultCode::NoChange: return "No change";
    case VoxelPaintBrushResultCode::NoDocument: return "No document";
    case VoxelPaintBrushResultCode::NoHit: return "No hit";
    case VoxelPaintBrushResultCode::TargetOutOfBounds: return "Out of bounds";
    case VoxelPaintBrushResultCode::InvalidModel: return "Invalid model";
    case VoxelPaintBrushResultCode::InvalidPaletteIndex: return "Invalid palette";
    case VoxelPaintBrushResultCode::Blocked: return "Blocked";
    case VoxelPaintBrushResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
