#include "SmartTools/SmartBrushPreviewResolver.h"
#include "SmartTools/SmartToolController.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;

struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(
        const Asset::Voxel::VoxelPosition position) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Y)) << 11U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Z)) << 22U);
    }
};

using VoxelStates = std::unordered_map<
    Asset::Voxel::VoxelPosition, SmartToolVoxelState, PositionHash>;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

SmartToolRequest Request(const SmartAction action, const VoxelStates& voxels,
    const Asset::Voxel::VoxelPosition target = {2, 2, 2},
    const std::uint8_t targetPalette = 7U)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = action;
    request.BrushRequest.Dimensions = {8U, 8U, 8U};
    request.BrushRequest.State.Shape = SmartBrushShape::Cube;
    request.BrushRequest.State.Size = 1;
    request.BrushRequest.State.PaletteIndex = targetPalette;
    request.BrushRequest.Placement = {target, {0, 1, 0}};
    request.ReadVoxel = [&voxels](const Asset::Voxel::VoxelPosition position)
    {
        const auto found = voxels.find(position);
        return found == voxels.end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0x025U;
    request.SourceRevision = 42U;
    request.SourceGeneration = 11U;
    request.SourceSubModelIndex = 0U;
    return request;
}

const SmartToolPlanCell& OnlyCell(const SmartToolResult& result)
{
    Require(result.HasPlan() && result.Plan->Cells().size() == 1U,
        "Expected exactly one resolved plan cell.");
    return result.Plan->Cells().front();
}

void TestEmptyPlan()
{
    SmartToolController controller;
    SmartToolSession session;
    SmartToolRequest request = Request(SmartAction::Add, VoxelStates{});
    request.BrushRequest.State.Size = 0;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan() && result.Plan->Cells().empty() &&
            result.Plan->Statistics().Total == 0U &&
            result.Plan->Statistics().IsConsistent() &&
            !result.Plan->HasChanges(),
        "An empty planner result was not represented by an autonomous empty plan.");
}

void TestCompleteBeforeAfterContract()
{
    SmartToolController controller;
    SmartToolSession session;

    const VoxelStates empty;
    const auto added = controller.ResolvePreview(session, Request(SmartAction::Add, empty));
    const auto& afterOnly = OnlyCell(added);
    Require(!afterOnly.Before.Exists && afterOnly.After == SmartToolVoxelState{true, 7U} &&
            afterOnly.Operation == SmartToolCellOperation::Add &&
            afterOnly.PreviewState == SmartToolPlanPreviewState::Added &&
            afterOnly.HasChange() && afterOnly.FinalVoxel(),
        "Add did not produce an after-only, final voxel plan cell.");

    const VoxelStates occupied{{{2, 2, 2}, {true, 3U}}};
    const auto erased = controller.ResolvePreview(session, Request(SmartAction::Erase, occupied));
    const auto& beforeOnly = OnlyCell(erased);
    Require(beforeOnly.Before == SmartToolVoxelState{true, 3U} && !beforeOnly.After.Exists &&
            beforeOnly.Operation == SmartToolCellOperation::Erase &&
            beforeOnly.PreviewState == SmartToolPlanPreviewState::Erased &&
            beforeOnly.HasChange() && !beforeOnly.FinalVoxel(),
        "Erase did not produce a before-only plan cell.");

    const auto painted = controller.ResolvePreview(session, Request(SmartAction::Paint, occupied));
    const auto& complete = OnlyCell(painted);
    Require(complete.Before == SmartToolVoxelState{true, 3U} &&
            complete.After == SmartToolVoxelState{true, 7U} &&
            complete.Operation == SmartToolCellOperation::Paint &&
            complete.PreviewState == SmartToolPlanPreviewState::Painted &&
            complete.ExistingVoxel() && complete.FinalVoxel(),
        "Paint did not retain complete Before/After palette state.");
}

void TestReplaceNoChangeOverlapAndDiagnostics()
{
    SmartToolController controller;
    SmartToolSession session;
    const VoxelStates occupied{{{2, 2, 2}, {true, 3U}}};

    auto replace = Request(SmartAction::Replace, occupied, {2, 2, 2}, 9U);
    replace.ReplacePaletteIndex = static_cast<std::uint8_t>(3U);
    const auto replaced = controller.ResolvePreview(session, replace);
    const auto& replaceCell = OnlyCell(replaced);
    Require(replaceCell.Before == SmartToolVoxelState{true, 3U} &&
            replaceCell.After == SmartToolVoxelState{true, 9U} &&
            replaceCell.Operation == SmartToolCellOperation::Replace &&
            replaceCell.PreviewState == SmartToolPlanPreviewState::Replaced,
        "Replace did not use the requested source and target palette states.");

    auto noChange = Request(SmartAction::Paint, occupied, {2, 2, 2}, 3U);
    const auto ignored = controller.ResolvePreview(session, noChange);
    const auto& ignoredCell = OnlyCell(ignored);
    Require(!ignoredCell.HasChange() &&
            ignoredCell.Diagnostic == SmartToolPlanCellDiagnostic::NoChange &&
            HasSmartToolPlanCellFlag(ignoredCell.Flags, SmartToolPlanCellFlag::NoChange) &&
            ignored.Plan->Statistics().Unchanged == 1U && !ignored.Plan->HasChanges(),
        "Painting with the existing palette did not remain an explicit no-change.");

    const auto overlap = controller.ResolvePreview(session, Request(SmartAction::Add, occupied));
    const auto& overlapCell = OnlyCell(overlap);
    Require(!overlapCell.HasChange() && overlapCell.Overlap() &&
            overlapCell.Diagnostic == SmartToolPlanCellDiagnostic::Overlap &&
            overlap.Plan->Statistics().Overlaps == 1U,
        "Add overlap did not retain its diagnostic state.");

    const auto outside = controller.ResolvePreview(session,
        Request(SmartAction::Add, VoxelStates{}, {-1, 2, 2}));
    Require(outside.HasPlan() && outside.Code == SmartBrushResultCode::OutOfBounds &&
            outside.Plan->Statistics().Clipped > 0U &&
            outside.Plan->Statistics().IsConsistent(),
        "Out-of-bounds planning did not retain clipped diagnostics.");
    const auto& clipped = outside.Plan->Cells().front();
    Require(clipped.OutOfBounds() &&
            clipped.PreviewState == SmartToolPlanPreviewState::Clipped &&
            clipped.Diagnostic == SmartToolPlanCellDiagnostic::OutOfBounds,
        "Clipped plan cell is missing its explicit out-of-bounds diagnostic.");
}

void TestPreviewAndSessionInvalidation()
{
    SmartToolController controller;
    SmartToolSession session;
    const VoxelStates occupied{{{2, 2, 2}, {true, 5U}}};
    const auto result = controller.ResolvePreview(session,
        Request(SmartAction::Paint, occupied, {2, 2, 2}, 8U));
    Require(result.HasPlan(), "Paint preview plan was not created.");
    const SmartBrushPreviewResult preview = SmartBrushPreviewResolver::Resolve(
        *result.Plan, {0.1F, 0.2F, 0.3F, 1.0F});
    Require(preview.IsAvailable() && preview.GhostVoxels.size() == 1U &&
            preview.GhostVoxels.front().State == GhostVoxelState::Painted &&
            preview.AffectedPositions == std::vector<Asset::Voxel::VoxelPosition>{{2, 2, 2}},
        "Preview did not consume the exact plan state for paint.");
    Require(controller.ResolveCommit(session).Plan.get() == result.Plan.get(),
        "Commit boundary did not retain the previewed plan instance.");

    session.SetAction(SmartAction::Erase);
    const auto stale = controller.ResolveCommit(session);
    Require(!stale.HasPlan() && stale.Code == SmartBrushResultCode::InvalidRequest,
        "A changed session state retained a stale plan for commit.");
}
}

int main()
{
    try
    {
        TestEmptyPlan();
        TestCompleteBeforeAfterContract();
        TestReplaceNoChangeOverlapAndDiagnostics();
        TestPreviewAndSessionInvalidation();
        std::cout << "Smart Tool plan state tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
