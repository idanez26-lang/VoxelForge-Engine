#include "VoxelStamps/Workflow/StampPreviewController.h"

#include "VoxelStamps/Placement/StampPlacementPlan.h"

#include <string>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
std::string SessionFailureMessage(
    const Stamps::StampPlacementSessionResult& result)
{
    return result.Diagnostic != Stamps::StampPlacementDiagnosticCode::None
        ? std::string(Stamps::StampPlacementDiagnosticMessage(
            result.Diagnostic))
        : std::string(Stamps::StampPlacementSessionResultMessage(
            result.Code));
}
}

StampPreviewController::StampPreviewController(
    Stamps::StampPlacementSession& placement,
    VoxelDocumentSession& documents,
    VoxelEditHistory& history,
    VoxelEditSession& editSession,
    EditorConsoleService& console,
    bool& sharedEditInProgressFlag,
    HighlightsChangedCallback onHighlightsChanged)
    : placement_(placement),
      documents_(documents),
      history_(history),
      editSession_(editSession),
      console_(console),
      editInProgress_(sharedEditInProgressFlag),
      onHighlightsChanged_(std::move(onHighlightsChanged))
{
}

void StampPreviewController::Move(
    const std::int32_t x, const std::int32_t y, const std::int32_t z)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.TranslateTarget(
            x, y, z, *document, documents_.Generation());
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " +
            SessionFailureMessage(result));
        return;
    }
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }

    const Stamps::StampPlacementPlan* const plan = placement_.CurrentPlan();
    console_.AddMessage(plan != nullptr && plan->Statistics.OverlapCount != 0U
        ? "Live Stamp Preview: overlap is allowed."
        : "Live Stamp Preview: valid preview active.");
}

void StampPreviewController::Rotate(const bool clockwise)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result = clockwise
        ? placement_.RotateClockwise(*document, documents_.Generation())
        : placement_.RotateCounterClockwise(*document, documents_.Generation());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " +
            SessionFailureMessage(result));
        return;
    }

    console_.AddMessage(
        "Live Stamp Preview: rotation " +
        std::to_string(
            static_cast<unsigned int>(placement_.QuarterRotation()) * 90U) +
        " degrees.");
}

void StampPreviewController::Mirror(
    const Stamps::StampPlacementMirrorMode mirror)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.SetMirror(mirror, *document, documents_.Generation());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " +
            SessionFailureMessage(result));
        return;
    }

    const char* label = "None";
    switch (placement_.Mirror())
    {
    case Stamps::StampPlacementMirrorMode::X:
        label = "X";
        break;
    case Stamps::StampPlacementMirrorMode::Z:
        label = "Z";
        break;
    case Stamps::StampPlacementMirrorMode::XZ:
        label = "XZ";
        break;
    case Stamps::StampPlacementMirrorMode::None:
    default:
        break;
    }
    console_.AddMessage(
        "Live Stamp Preview: mirror " + std::string(label) + ".");
}

void StampPreviewController::Place()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (document == nullptr || editInProgress_ || history_.IsBusy())
    {
        return;
    }

    editInProgress_ = true;
    const Stamps::StampPlacementSessionPlaceResult result =
        placement_.PlaceOnce(
            *document, documents_.Generation(), editSession_, history_);
    editInProgress_ = false;
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (result.Status ==
        Stamps::StampPlacementSessionPlaceStatus::PreviewRefreshed)
    {
        console_.AddMessage(
            "Place Stamp: the document changed; preview refreshed. "
            "Click again to place.");
        return;
    }
    if (result.Status ==
        Stamps::StampPlacementSessionPlaceStatus::DocumentChanged)
    {
        console_.AddMessage(
            "Place Stamp: active document changed; placement cancelled.");
        return;
    }
    if (result.Status == Stamps::StampPlacementSessionPlaceStatus::Inactive)
    {
        return;
    }
    if (result.Status == Stamps::StampPlacementSessionPlaceStatus::NoChange)
    {
        console_.AddMessage(
            "Place Stamp: preview already matches the document.");
        return;
    }
    if (!result)
    {
        console_.AddMessage(
            "Place Stamp failed: " + result.History.Message);
        return;
    }

    console_.AddMessage("Placed Stamp: " + result.History.Label);
}

bool StampPreviewController::Refresh()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return false;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.Rebuild(*document, documents_.Generation());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    return result.Succeeded;
}

void StampPreviewController::Clear() noexcept
{
    const bool changed = placement_.Cancel();
    if (changed)
    {
        onHighlightsChanged_();
    }
}

} // namespace VoxelForge::Editor
