#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/SmartPlacement/StampSmartPlacementService.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

constexpr std::int32_t Fixed = StampFixedPoint::UnitsPerVoxel;

void Require(const bool condition, const char* const message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {12U, 4U, 12U}});
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-smart-placement-test.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create Smart Placement document fixture.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{21U}, "stamp-smart-placement-21"},
        {{}, {0, 0, 1}, {1U, 1U, 2U}},
        {.RequestedMode = StampPivotMode::Surface,
         .ResolvedMode = StampPivotMode::Surface,
         .LocalPosition = {},
         .LocalNormal = {.Z = 1},
         .AutoPolicyVersion = 1U},
        {},
        {{0U, {31U, 63U, 95U, 255U}}},
        {{{0, 0, 0}, 0U}, {{0, 0, 1}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create Smart Placement Stamp fixture.");
    return *stamp;
}

StampSmartPlacementTargetContext WallTarget(
    const StampFixedPoint point = {6 * Fixed, 0, 6 * Fixed})
{
    return {
        .HasSurface = true,
        .SurfacePoint = point,
        .SurfaceNormal = {.X = 1}};
}

void TestPureSuggestionIsDefaultAndDeterministic()
{
    const VoxelStamp stamp = MakeStamp();
    const StampPlacementTransform userTransform{
        .TargetPivot = {2 * Fixed, 0, 2 * Fixed}};
    const StampSmartPlacementContext context{
        .Stamp = &stamp,
        .UserTransform = userTransform,
        .Target = WallTarget()};

    const auto first = SuggestPlacement(context);
    const auto second = SuggestPlacement(context);
    Require(first == second && first.Available(),
        "Identical Smart Placement contexts must resolve identically.");
    Require(first.Transform.TargetPivot == WallTarget().SurfacePoint &&
                first.Transform.QuarterTurns == 1U &&
                first.Pivot == stamp.Pivot() &&
                first.AlignmentNormal == StampNormal{.X = 1} &&
                first.OrientationSuggested && first.PivotSuggested &&
                first.SurfaceAligned && !first.UsedWorkplane,
        "A vertical wall must deterministically align the Stamp pivot and normal.");

    StampSmartPlacementContext workplane = context;
    workplane.Target = {
        .HasWorkplane = true,
        .WorkplanePoint = {3 * Fixed, 0, 4 * Fixed},
        .WorkplaneNormal = {.Y = 1}};
    const auto workplaneSuggestion = SuggestPlacement(workplane);
    Require(workplaneSuggestion.Available() &&
                workplaneSuggestion.UsedWorkplane &&
                !workplaneSuggestion.SurfaceAligned &&
                workplaneSuggestion.Transform.TargetPivot ==
                    workplane.Target.WorkplanePoint,
        "The workplane must provide a deterministic alignment fallback.");
}

void TestDisableAndTemporaryBypassAreAdvisoryOnly()
{
    const VoxelStamp stamp = MakeStamp();
    StampSmartPlacementContext context{
        .Stamp = &stamp,
        .UserTransform = {.TargetPivot = {2 * Fixed, 0, 2 * Fixed}},
        .Target = WallTarget(),
        .Enabled = false};
    const auto disabled = SuggestPlacement(context);
    Require(!disabled.Available() &&
                disabled.Status ==
                    StampSmartPlacementSuggestionStatus::Disabled &&
                disabled.Transform == context.UserTransform,
        "Global disable must return the untouched user transform.");

    context.Enabled = true;
    context.TemporarilyBypassed = true;
    const auto bypassed = SuggestPlacement(context);
    Require(!bypassed.Available() &&
                bypassed.Status ==
                    StampSmartPlacementSuggestionStatus::TemporarilyBypassed &&
                bypassed.Transform == context.UserTransform,
        "Temporary bypass must return the untouched user transform.");
}

void TestSessionConsumesPreviewAssistAndSupportsSuggestMode()
{
    auto document = MakeDocument();
    const VoxelStamp stamp = MakeStamp();
    StampPlacementSession session;
    const StampFixedPoint manualTarget{2 * Fixed, 0, 2 * Fixed};
    Require(session.Begin(stamp, document, 21U, 0U, manualTarget).Succeeded &&
                session.SmartPlacementEnabled() &&
                session.SmartPlacementMode() ==
                    StampSmartPlacementMode::PreviewAssist &&
                !session.SmartPlacementAppliedToPreview(),
        "Smart Placement must start enabled in Preview Assist mode.");

    Require(session.UpdateSmartPlacementContext(
                WallTarget(), document, 21U).Succeeded &&
                session.CurrentSmartPlacementSuggestion() != nullptr &&
                session.CurrentSmartPlacementSuggestion()->Available() &&
                session.SmartPlacementAppliedToPreview() &&
                session.CurrentPlan()->Transform.TargetPivot ==
                    WallTarget().SurfacePoint &&
                session.CurrentPlan()->Transform.QuarterTurns == 1U &&
                session.Target() == manualTarget &&
                session.QuarterRotation() == 0U,
        "Preview Assist must consume advisory data without rewriting user state.");

    Require(session.SetSmartPlacementMode(
                StampSmartPlacementMode::Suggest, document, 21U).Succeeded &&
                !session.SmartPlacementAppliedToPreview() &&
                session.CurrentSmartPlacementSuggestion()->Available() &&
                session.CurrentPlan()->Transform.TargetPivot == manualTarget &&
                session.CurrentPlan()->Transform.QuarterTurns == 0U,
        "Suggest mode must expose advice without applying it to the preview.");

    Require(session.SetSmartPlacementMode(
                StampSmartPlacementMode::PreviewAssist,
                document, 21U).Succeeded &&
                session.SmartPlacementAppliedToPreview(),
        "Preview Assist must be restorable without changing the Stamp asset.");
}

void TestSessionGlobalDisableAndBypassRestoreManualPlacement()
{
    auto document = MakeDocument();
    const VoxelStamp stamp = MakeStamp();
    StampPlacementSession session;
    const StampFixedPoint manualTarget{2 * Fixed, 0, 2 * Fixed};
    Require(session.Begin(stamp, document, 21U, 0U, manualTarget).Succeeded &&
                session.UpdateSmartPlacementContext(
                    WallTarget(), document, 21U).Succeeded,
        "Unable to start Smart Placement preference fixture.");

    Require(session.SetSmartPlacementEnabled(
                false, document, 21U).Succeeded &&
                !session.SmartPlacementEnabled() &&
                !session.SmartPlacementAppliedToPreview() &&
                session.CurrentSmartPlacementSuggestion()->Status ==
                    StampSmartPlacementSuggestionStatus::Disabled &&
                session.CurrentPlan()->Transform.TargetPivot == manualTarget &&
                session.CurrentPlan()->Transform.QuarterTurns == 0U,
        "Global disable must immediately restore the manual preview.");
    Require(session.SetSmartPlacementEnabled(
                true, document, 21U).Succeeded &&
                session.SmartPlacementAppliedToPreview(),
        "Re-enabling Smart Placement must restore the same suggestion.");

    Require(session.SetSmartPlacementTemporaryBypass(
                true, document, 21U).Succeeded &&
                session.SmartPlacementTemporarilyBypassed() &&
                !session.SmartPlacementAppliedToPreview() &&
                session.CurrentPlan()->Transform.TargetPivot == manualTarget &&
                session.CurrentSmartPlacementSuggestion()->Status ==
                    StampSmartPlacementSuggestionStatus::TemporarilyBypassed,
        "Temporary bypass must restore the manual preview.");
    Require(session.SetSmartPlacementTemporaryBypass(
                false, document, 21U).Succeeded &&
                session.SmartPlacementAppliedToPreview(),
        "Ending temporary bypass must restore the deterministic suggestion.");
}

void TestExplicitUserRotationWins()
{
    auto document = MakeDocument();
    const VoxelStamp stamp = MakeStamp();
    StampPlacementSession session;
    Require(session.Begin(stamp, document, 21U, 0U,
                {4 * Fixed, 0, 4 * Fixed}).Succeeded &&
                session.UpdateSmartPlacementContext(
                    WallTarget(), document, 21U).Succeeded &&
                session.CurrentPlan()->Transform.QuarterTurns == 1U,
        "Unable to build orientation suggestion fixture.");

    Require(session.SetQuarterRotation(3U, document, 21U).Succeeded &&
                session.SmartPlacementOrientationLocked() &&
                session.QuarterRotation() == 3U &&
                session.CurrentSmartPlacementSuggestion()->Transform.
                    QuarterTurns == 3U &&
                session.CurrentPlan()->Transform.QuarterTurns == 3U,
        "An explicit user rotation must win over Smart Placement orientation.");

    Require(session.ResetTransform(document, 21U).Succeeded &&
                !session.SmartPlacementOrientationLocked() &&
                session.CurrentPlan()->Transform.QuarterTurns == 1U,
        "Resetting the user transform must allow orientation assistance again.");
}

void TestSuggestionCannotRefuseAnAllowedPlacement()
{
    auto document = MakeDocument();
    const VoxelStamp stamp = MakeStamp();
    StampPlacementSession session;
    const StampFixedPoint allowedTarget{2 * Fixed, 0, 2 * Fixed};
    Require(session.Begin(
                stamp, document, 21U, 0U, allowedTarget).Succeeded &&
                session.CurrentPlan()->CanCommit,
        "The manual fallback placement must begin allowed.");

    const auto unsafeSuggestion = session.UpdateSmartPlacementContext(
        WallTarget({-100 * Fixed, 0, 0}), document, 21U);
    Require(unsafeSuggestion.Succeeded &&
                session.CurrentSmartPlacementSuggestion()->Available() &&
                !session.SmartPlacementAppliedToPreview() &&
                session.CurrentPlan()->CanCommit &&
                session.CurrentPlan()->Transform.TargetPivot == allowedTarget,
        "Smart Placement must fall back when its suggestion would refuse an allowed placement.");
}

} // namespace

int main()
{
    try
    {
        TestPureSuggestionIsDefaultAndDeterministic();
        TestDisableAndTemporaryBypassAreAdvisoryOnly();
        TestSessionConsumesPreviewAssistAndSupportsSuggestMode();
        TestSessionGlobalDisableAndBypassRestoreManualPlacement();
        TestExplicitUserRotationWins();
        TestSuggestionCannotRefuseAnAllowedPlacement();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
