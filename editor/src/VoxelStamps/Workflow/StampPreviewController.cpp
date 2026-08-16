#include "VoxelStamps/Workflow/StampPreviewController.h"

#include "VoxelStamps/Placement/StampPlacementPlan.h"
#include "VoxelStamps/Workflow/ScopedEditInProgress.h"

#include <cstddef>
#include <limits>
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

const char* MirrorLabel(
    const Stamps::StampPlacementMirrorMode mirror) noexcept
{
    switch (mirror)
    {
    case Stamps::StampPlacementMirrorMode::X: return "X";
    case Stamps::StampPlacementMirrorMode::Z: return "Z";
    case Stamps::StampPlacementMirrorMode::XZ: return "XZ";
    case Stamps::StampPlacementMirrorMode::None:
    default: return "None";
    }
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
            x, y, z, *document, editSession_.VoxelModelGeneration());
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

bool StampPreviewController::ApplyGizmoDelta(
    const Stamps::StampFixedPoint baseTarget,
    const std::uint8_t baseQuarterTurns,
    const Asset::Voxel::VoxelPosition translation,
    const std::int32_t quarterTurnDelta,
    const Stamps::StampPlacementRotationAxis axis)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return false;
    }

    const auto targetComponent = [](const std::int32_t base,
                                    const std::int32_t delta,
                                    std::int32_t& output) noexcept
    {
        constexpr std::int64_t fixedUnits =
            Stamps::StampFixedPoint::UnitsPerVoxel;
        const std::int64_t value =
            static_cast<std::int64_t>(base) +
            static_cast<std::int64_t>(delta) * fixedUnits;
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }
        output = static_cast<std::int32_t>(value);
        return true;
    };
    Stamps::StampFixedPoint target{};
    if (!targetComponent(baseTarget.X, translation.X, target.X) ||
        !targetComponent(baseTarget.Y, translation.Y, target.Y) ||
        !targetComponent(baseTarget.Z, translation.Z, target.Z))
    {
        return false;
    }
    const std::int32_t turns =
        (static_cast<std::int32_t>(baseQuarterTurns) + quarterTurnDelta) % 4;
    const std::uint8_t normalizedTurns = static_cast<std::uint8_t>(
        turns < 0 ? turns + 4 : turns);
    const Stamps::StampPlacementSessionResult result =
        placement_.SetGizmoTransform(
            target, normalizedTurns, *document,
            editSession_.VoxelModelGeneration(), axis);
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    return result.Succeeded;
}

void StampPreviewController::Rotate(
    const bool clockwise,
    const Stamps::StampPlacementRotationAxis axis)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    // STAMP-25 : le cran vaut 90 ou 45 degres selon le reglage de la session.
    const Stamps::StampPlacementSessionResult result = placement_.RotateStep(
        axis, *document, editSession_.VoxelModelGeneration(), clockwise);
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

    std::string message = "Live Stamp Preview: rotation " +
        std::string(Stamps::StampRotationAxisLabel(
            placement_.RotationAxis())) + " " +
        std::to_string(placement_.RotationDegrees()) + " degrees.";
    // Une position impaire (45, 135, 225, 315) n'est pas une symetrie de la
    // grille : on annonce l'ecart source -> cellules a chaque cran.
    const Stamps::StampPlacementPlan* const plan = placement_.CurrentPlan();
    if (plan != nullptr && plan->Statistics.ApproximateRotation)
    {
        message += " Approximate: " +
            std::to_string(plan->Statistics.TotalVoxelCount) +
            " source voxels resampled into " +
            std::to_string(plan->Statistics.PlannedVoxelCount) + " cells.";
    }
    console_.AddMessage(message);
}

void StampPreviewController::ToggleRotationStep()
{
    if (!placement_.IsActive())
    {
        return;
    }
    placement_.ToggleRotationStep();
    console_.AddMessage(
        placement_.RotationStep() == Stamps::StampRotationStep::Eighth45
            ? "Live Stamp Preview: rotation step 45 degrees (8 positions)."
            : "Live Stamp Preview: rotation step 90 degrees (exact grid).");
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
        placement_.SetMirror(
            mirror, *document, editSession_.VoxelModelGeneration());
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
        "Live Stamp Preview: mirror " +
        std::string(MirrorLabel(placement_.Mirror())) + ".");
}

void StampPreviewController::ToggleMirror(
    const Stamps::StampPlacementMirrorMode axis)
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.ToggleMirror(
            axis, *document, editSession_.VoxelModelGeneration());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " + SessionFailureMessage(result));
        return;
    }
    console_.AddMessage(
        "Live Stamp Preview: mirror " +
        std::string(MirrorLabel(placement_.Mirror())) + ".");
}

void StampPreviewController::CycleMirror()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.CycleMirror(*document, editSession_.VoxelModelGeneration());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " + SessionFailureMessage(result));
        return;
    }
    console_.AddMessage(
        "Live Stamp Preview: mirror " +
        std::string(MirrorLabel(placement_.Mirror())) + ".");
}

void StampPreviewController::ResetTransform()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.ResetTransform(*document, editSession_.VoxelModelGeneration());
    if (result.PreviewChanged)
    {
        onHighlightsChanged_();
    }
    if (!result.Succeeded)
    {
        console_.AddMessage(
            "Live Stamp Preview: " + SessionFailureMessage(result));
        return;
    }
    console_.AddMessage("Live Stamp Preview: transform reset.");
}

bool StampPreviewController::Place()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (document == nullptr || editInProgress_ || history_.IsBusy())
    {
        console_.AddMessage(
            "Place Stamp failed: the document or Undo history is busy.");
        return false;
    }

    console_.AddMessage("Place Stamp: command received.");
    Stamps::StampPlacementSessionPlaceResult result;
    {
        // VF-STAB-01 bug 4: the shared reentrancy flag is now cleared by RAII,
        // so an exception from PlaceOnce (e.g. std::bad_alloc on a large plan)
        // no longer leaks `true` and permanently blocks future edits. The
        // exception still propagates past the guard; it is not swallowed.
        const ScopedEditInProgress editGuard{editInProgress_};
        result = placement_.PlaceOnce(
            *document, editSession_.VoxelModelGeneration(),
            editSession_, history_);
        // A UI confirmation is a single user action. If an unrelated document
        // revision made the immutable plan stale, refresh once and immediately
        // execute the now-current plan instead of requiring a mysterious second
        // click.
        if (result.Status ==
            Stamps::StampPlacementSessionPlaceStatus::PreviewRefreshed)
        {
            result = placement_.PlaceOnce(
                *document, editSession_.VoxelModelGeneration(),
                editSession_, history_);
        }
    }
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
        return false;
    }
    if (result.Status ==
        Stamps::StampPlacementSessionPlaceStatus::DocumentChanged)
    {
        console_.AddMessage(
            "Place Stamp: active document changed; placement cancelled.");
        return false;
    }
    if (result.Status == Stamps::StampPlacementSessionPlaceStatus::Inactive)
    {
        return false;
    }
    if (result.Status == Stamps::StampPlacementSessionPlaceStatus::NoChange)
    {
        console_.AddMessage(
            "Place Stamp: preview already matches the document.");
        return false;
    }
    if (!result)
    {
        console_.AddMessage(
            "Place Stamp failed: " + result.History.Message);
        return false;
    }

    console_.AddMessage("Placed Stamp: " + result.History.Label);
    return true;
}

bool StampPreviewController::Refresh()
{
    Asset::Voxel::VoxelDocument* const document = documents_.ActiveDocument();
    if (!placement_.IsActive() || document == nullptr)
    {
        return false;
    }

    const Stamps::StampPlacementSessionResult result =
        placement_.Rebuild(*document, editSession_.VoxelModelGeneration());
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
