#include "VoxelPaintBrushTool.h"

#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <exception>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
SmartBrushState GeometryState(const SmartBrushState state) noexcept
{
    SmartBrushState normalized = state;
    // SmartBrushEngine currently resolves shared geometry in Add mode only.
    // This copy is deliberately private to Paint and never changes UI state.
    normalized.Mode = SmartBrushMode::Add;
    return normalized;
}

VoxelPaintBrushEvaluation Refused(
    const VoxelPaintBrushResultCode code,
    std::optional<Asset::Voxel::VoxelPosition> target = std::nullopt,
    std::string error = {})
{
    return {code, target, {}, {}, {}, {}, {}, std::move(error)};
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
}

VoxelPaintBrushEvaluation VoxelPaintBrushTool::Evaluate(
    const VoxelPaintBrushContext& context)
{
    if (context.Document == nullptr)
        return Refused(VoxelPaintBrushResultCode::NoDocument);
    Asset::Voxel::VoxelDocument& document = *context.Document;
    if (context.Blocked)
        return Refused(VoxelPaintBrushResultCode::Blocked);
    if (!context.Hit || context.Hit->Face == VoxelHitFace::None)
        return Refused(VoxelPaintBrushResultCode::NoHit);

    const Asset::Voxel::VoxelPosition target{
        static_cast<std::int32_t>(context.Hit->Coordinates.X),
        static_cast<std::int32_t>(context.Hit->Coordinates.Y),
        static_cast<std::int32_t>(context.Hit->Coordinates.Z)};
    if (context.Hit->SubModelIndex != context.SubModelIndex ||
        context.Hit->DocumentRevision != document.GetRevision() ||
        document.GetModel(context.SubModelIndex) == nullptr)
    {
        return Refused(VoxelPaintBrushResultCode::InvalidModel, target,
            "The Paint target does not belong to the active document state.");
    }
    if (context.State.PaletteIndex == 0U || context.State.PaletteIndex > 255U)
        return Refused(VoxelPaintBrushResultCode::InvalidPaletteIndex, target);
    const auto dimensions = document.GetDimensions(context.SubModelIndex);
    if (!dimensions)
        return Refused(VoxelPaintBrushResultCode::InvalidModel, target);

    const Asset::Voxel::VoxelPosition normal =
        context.State.Dimension == SmartBrushDimension::Surface2D
        ? VoxelHitFaceIntegerNormal(context.Hit->Face)
        : Asset::Voxel::VoxelPosition{};
    SmartBrushResult brush = SmartBrushEngine::Resolve({
        *dimensions,
        GeometryState(context.State),
        {target, normal},
        [&document, subModelIndex = context.SubModelIndex](
            const Asset::Voxel::VoxelPosition position)
        {
            return document.HasVoxel(position, subModelIndex);
        }});

    if (brush.Code != SmartBrushResultCode::Valid &&
        brush.Code != SmartBrushResultCode::OutOfBounds)
    {
        return Refused(VoxelPaintBrushResultCode::Failed, target, brush.Error);
    }

    try
    {
        VoxelPaintBrushEvaluation evaluation;
        evaluation.Target = target;
        evaluation.Statistics.Total = brush.Statistics.Total;
        evaluation.Statistics.Clipped = brush.Statistics.Clipped;
        evaluation.Positions = std::move(brush.Positions);
        evaluation.RenderPlan = std::move(brush.RenderPlan);
        if (brush.Code == SmartBrushResultCode::OutOfBounds)
        {
            evaluation.Code = VoxelPaintBrushResultCode::TargetOutOfBounds;
            evaluation.Statistics.Ignored = evaluation.Statistics.Total -
                evaluation.Statistics.Clipped;
            return evaluation;
        }
        evaluation.PaintablePositions.reserve(evaluation.Positions.size());
        evaluation.IgnoredPositions.reserve(evaluation.Positions.size());
        for (const Asset::Voxel::VoxelPosition position : evaluation.Positions)
        {
            const auto voxel = document.GetVoxel(position, context.SubModelIndex);
            if (voxel && voxel->PaletteIndex != context.State.PaletteIndex)
                evaluation.PaintablePositions.push_back(position);
            else
                evaluation.IgnoredPositions.push_back(position);
        }
        evaluation.Statistics.Painted = evaluation.PaintablePositions.size();
        evaluation.Statistics.Ignored = evaluation.IgnoredPositions.size();
        evaluation.Code = evaluation.PaintablePositions.empty()
            ? VoxelPaintBrushResultCode::NoChange
            : VoxelPaintBrushResultCode::Applied;
        return evaluation;
    }
    catch (const std::exception& exception)
    {
        return Refused(VoxelPaintBrushResultCode::Failed, target,
            exception.what());
    }
    catch (...)
    {
        return Refused(VoxelPaintBrushResultCode::Failed, target,
            "Paint Brush evaluation failed.");
    }
}

VoxelPaintBrushResult VoxelPaintBrushTool::Apply(
    const VoxelPaintBrushContext& context)
{
    const std::uint64_t revision = context.Document
        ? context.Document->GetRevision() : 0U;
    const VoxelPaintBrushEvaluation evaluation = Evaluate(context);
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

    Asset::Voxel::VoxelDocument& document = *context.Document;
    Voxel::VoxelModel* model = context.EditSession->ActiveVoxelModel();
    Voxel::VoxelGrid* grid = model == nullptr
        ? nullptr : model->GetGrid(context.SubModelIndex);
    if (context.EditSession->ActiveVoxelDocument() != &document ||
        grid == nullptr)
    {
        VoxelPaintBrushEvaluation invalid = evaluation;
        invalid.Code = VoxelPaintBrushResultCode::InvalidModel;
        invalid.Error = "The editable document and compatibility grid are unavailable.";
        return ResultFromEvaluation(invalid, revision);
    }

    std::vector<VoxelChange> changes;
    try
    {
        changes.reserve(evaluation.PaintablePositions.size());
        for (const Asset::Voxel::VoxelPosition position : evaluation.PaintablePositions)
        {
            const auto documentVoxel = document.GetVoxel(
                position, context.SubModelIndex);
            const Voxel::Voxel* compatibilityVoxel = grid->Get(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z));
            if (!documentVoxel || compatibilityVoxel == nullptr ||
                !compatibilityVoxel->IsOccupied() ||
                compatibilityVoxel->ColorIndex != documentVoxel->PaletteIndex)
            {
                VoxelPaintBrushEvaluation invalid = evaluation;
                invalid.Code = VoxelPaintBrushResultCode::Failed;
                invalid.Error = "VoxelDocument and the editable compatibility grid diverged.";
                return ResultFromEvaluation(invalid, revision);
            }
            changes.push_back({
                context.SubModelIndex, position, true,
                documentVoxel->PaletteIndex, true,
                static_cast<std::uint8_t>(context.State.PaletteIndex)});
        }
    }
    catch (const std::exception& exception)
    {
        VoxelPaintBrushEvaluation failure = evaluation;
        failure.Code = VoxelPaintBrushResultCode::Failed;
        failure.Error = exception.what();
        return ResultFromEvaluation(failure, revision);
    }

    const VoxelEditHistoryResult applied = context.History->Execute(
        *context.EditSession,
        VoxelEditOperation{"Paint Brush", std::move(changes)});
    if (!applied)
    {
        VoxelPaintBrushEvaluation failure = evaluation;
        failure.Code = VoxelPaintBrushResultCode::Failed;
        failure.Error = applied.Message;
        return ResultFromEvaluation(failure, document.GetRevision());
    }

    const std::uint64_t revisionAfter = document.GetRevision();
    bool synchronized = revisionAfter == revision + 1U && document.IsDirty();
    for (const Asset::Voxel::VoxelPosition position : evaluation.PaintablePositions)
    {
        const auto voxel = document.GetVoxel(position, context.SubModelIndex);
        const Voxel::Voxel* compatibilityVoxel = grid->Get(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z));
        synchronized &= voxel &&
            voxel->PaletteIndex == context.State.PaletteIndex &&
            compatibilityVoxel != nullptr && compatibilityVoxel->IsOccupied() &&
            compatibilityVoxel->ColorIndex == context.State.PaletteIndex;
    }
    if (!synchronized)
    {
        return {VoxelPaintBrushResultCode::Failed, true,
            evaluation.Target.value_or(Asset::Voxel::VoxelPosition{}),
            evaluation.Statistics, revision, revisionAfter,
            "The applied Paint Brush edit failed its consistency check."};
    }
    return {VoxelPaintBrushResultCode::Applied, true,
        evaluation.Target.value_or(Asset::Voxel::VoxelPosition{}),
        evaluation.Statistics, revision, revisionAfter, {}};
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
