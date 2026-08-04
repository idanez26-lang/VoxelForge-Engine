#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeDocument(
    const Asset::Vox::VoxDimensions dimensions = {16U, 4U, 4U})
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = dimensions});
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-placement-session-test.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create Stamp placement session document.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp(
    const std::uint64_t id = 16U,
    const char* const hash = "stamp-16")
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{id}, hash},
        {{}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}},
        {},
        {{0U, {11U, 22U, 33U, 255U}},
         {1U, {44U, 55U, 66U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create Stamp placement session fixture.");
    return *stamp;
}

VoxelStamp MakeLargeSixSidedStamp()
{
    constexpr std::int32_t width = 18;
    constexpr std::int32_t height = 24;
    constexpr std::int32_t depth = 49;
    constexpr std::size_t expectedVoxelCount = 4633U;
    std::vector<StampVoxel> voxels;
    voxels.reserve(expectedVoxelCount);
    std::size_t interiorAdded = 0U;
    for (std::int32_t x = 0; x < width; ++x)
    {
        for (std::int32_t y = 0; y < height; ++y)
        {
            for (std::int32_t z = 0; z < depth; ++z)
            {
                const bool boundary = x == 0 || x == width - 1 ||
                    y == 0 || y == height - 1 || z == 0 || z == depth - 1;
                if (boundary || interiorAdded < 9U)
                {
                    voxels.push_back({{x, y, z}, 0U});
                    if (!boundary) ++interiorAdded;
                }
            }
        }
    }
    Require(voxels.size() == expectedVoxelCount,
        "Large Stamp fixture must match the real 4633-voxel creation.");

    constexpr std::int32_t fixed = StampFixedPoint::UnitsPerVoxel;
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x4633U}, "large-six-sided-stamp"},
        {{}, {width - 1, height - 1, depth - 1},
         {static_cast<std::uint32_t>(width),
          static_cast<std::uint32_t>(height),
          static_cast<std::uint32_t>(depth)}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {9 * fixed, 12 * fixed, 24 * fixed}},
        {},
        {{0U, {40U, 180U, 105U, 255U}}},
        std::move(voxels), DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create the large six-sided Stamp fixture.");
    return *stamp;
}

class EditSession final : public VoxelEditSession
{
public:
    EditSession(
        Asset::Voxel::VoxelDocument& document,
        const std::uint64_t generation)
        : Document(&document), Generation(generation)
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return nullptr;
    }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return Document;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++Completions; }

    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::uint64_t Generation = 0U;
    std::size_t Rebuilds = 0U;
    std::size_t Completions = 0U;
};

void TestSelectionUpdateAndTransientState()
{
    auto document = MakeDocument();
    const VoxelStamp first = MakeStamp();
    const VoxelStamp second = MakeStamp(17U, "stamp-16-second");
    StampPlacementSession session;
    Require(session.State() == StampPlacementSessionState::Empty &&
                !session.IsActive(),
        "A placement session must start empty.");

    const auto missing = session.SelectAsset(nullptr, document, 16U);
    Require(missing.Code == StampPlacementSessionResultCode::MissingAsset &&
                !missing.Succeeded && !session.IsActive() &&
                session.CurrentPreview() == nullptr,
        "Selecting a missing Stamp must fail without a preview.");

    const auto selected = session.SelectAsset(
        &first, document, 16U, 0U,
        {2 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    Require(selected.Code == StampPlacementSessionResultCode::Succeeded &&
                selected.Succeeded && session.IsActive() &&
                session.ActiveStamp() != nullptr &&
                session.ActiveStamp()->Identity() == first.Identity() &&
                session.CurrentPlan() != nullptr &&
                session.CurrentPreview() != nullptr &&
                document.GetVoxelCount() == 0U,
        "Selecting an asset must start preview without placing it.");

    const auto updated = session.UpdateTarget(
        {4 * StampFixedPoint::UnitsPerVoxel, 0, 0}, document, 16U);
    Require(updated.Succeeded && updated.PlanChanged &&
                session.Target().X ==
                    4 * StampFixedPoint::UnitsPerVoxel &&
                document.GetVoxelCount() == 0U,
        "UpdateTarget must move only the active preview.");
    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY,
                document, 16U).Succeeded &&
                session.ToggleMirror(
                    StampPlacementMirrorMode::X, document, 16U).Succeeded,
        "Transient transform setup failed.");
    const StampFixedPoint transformedTarget = session.Target();
    const auto revisionBeforeReset = document.GetRevision();
    Require(session.ResetTransform(document, 16U).Succeeded &&
                session.Target() == transformedTarget &&
                session.RotationAxis() ==
                    StampPlacementRotationAxis::VerticalY &&
                session.QuarterRotation() == 0U &&
                session.Mirror() == StampPlacementMirrorMode::None &&
                document.GetRevision() == revisionBeforeReset &&
                document.GetVoxelCount() == 0U,
        "ResetTransform must keep the target and reset preview state only.");
    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY,
                document, 16U).Succeeded &&
                session.ToggleMirror(
                    StampPlacementMirrorMode::X, document, 16U).Succeeded,
        "Unable to restore non-neutral state before switching assets.");

    const auto switched = session.SelectAsset(
        &second, document, 16U, 0U,
        {6 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    Require(switched.Succeeded && session.IsActive() &&
                session.ActiveStamp()->Identity() == second.Identity() &&
                session.QuarterRotation() == 0U &&
                session.Mirror() == StampPlacementMirrorMode::None &&
                session.PlacementOrdinal() == 0U &&
                document.GetVoxelCount() == 0U,
        "Selecting another asset must start neutral transient state.");

    Require(session.Cancel() && !session.IsActive() &&
                session.State() == StampPlacementSessionState::Cancelled &&
                session.ActiveStamp() == nullptr &&
                session.CurrentPlan() == nullptr &&
                session.CurrentPreview() == nullptr &&
                document.GetVoxelCount() == 0U,
        "Cancel must release all transient placement state.");
}

void TestContinuousPlacementAndIndependentUndo()
{
    auto document = MakeDocument();
    EditSession editSession(document, 16U);
    VoxelEditHistory history;
    StampPlacementSession session;
    const VoxelStamp stamp = MakeStamp();
    Require(session.Begin(stamp, document, 16U, 0U, {}).Succeeded,
        "Continuous placement session must begin.");

    const auto revisionBefore = document.GetRevision();
    const auto first = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(static_cast<bool>(first) &&
                first.History.Code == VoxelEditHistoryResultCode::Applied &&
                document.GetRevision() == revisionBefore + 1U &&
                document.GetVoxelCount() == 2U &&
                session.PlacementOrdinal() == 1U &&
                session.IsActive() && session.CurrentPreview() != nullptr &&
                history.UndoCount() == 1U && editSession.Rebuilds == 1U &&
                editSession.Completions == 1U,
        "First click must be one atomic placement and keep the session active.");

    Require(session.UpdateTarget(
                {3 * StampFixedPoint::UnitsPerVoxel, 0, 0},
                document, 16U).Succeeded,
        "Unable to move the persistent preview.");
    const auto second = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(static_cast<bool>(second) &&
                document.GetVoxelCount() == 4U &&
                session.PlacementOrdinal() == 2U &&
                history.UndoCount() == 2U && editSession.Rebuilds == 2U &&
                editSession.Completions == 2U,
        "Second click must create a separate atomic history item.");

    Require(history.Undo(editSession) &&
                document.GetVoxel({0, 0, 0}).has_value() &&
                document.GetVoxel({1, 0, 0}).has_value() &&
                !document.GetVoxel({3, 0, 0}).has_value() &&
                !document.GetVoxel({4, 0, 0}).has_value() &&
                history.UndoCount() == 1U && history.RedoCount() == 1U,
        "Undo must remove only the latest placement occurrence.");
    Require(history.Redo(editSession) &&
                document.GetVoxelCount() == 4U &&
                history.UndoCount() == 2U && history.RedoCount() == 0U,
        "Redo must restore only the latest placement occurrence.");

    const auto revisionBeforeCancel = document.GetRevision();
    const auto countBeforeCancel = document.GetVoxelCount();
    Require(session.Cancel() && !session.IsActive() &&
                document.GetRevision() == revisionBeforeCancel &&
                document.GetVoxelCount() == countBeforeCancel &&
                history.UndoCount() == 2U,
        "Esc-equivalent Cancel must not mutate document or history.");
}

void TestGizmoTransformPlacesTheCompleteStampAtomically()
{
    auto document = MakeDocument();
    EditSession editSession(document, 16U);
    VoxelEditHistory history;
    StampPlacementSession session;
    const VoxelStamp stamp = MakeStamp();
    constexpr std::int32_t fixed = StampFixedPoint::UnitsPerVoxel;
    const StampFixedPoint target{6 * fixed, 0, 2 * fixed};
    Require(session.Begin(stamp, document, 16U, 0U, {}).Succeeded &&
                session.SetGizmoTransform(
                    target, 1U, document, 16U).Succeeded &&
                session.Target() == target &&
                session.QuarterRotation() == 1U &&
                session.CurrentPlan() != nullptr &&
                session.CurrentPlan()->Statistics.TotalVoxelCount == 2U &&
                session.CurrentPlan()->Statistics.ChangedVoxelCount == 2U,
        "The gizmo must move and rotate the complete Stamp preview.");

    const auto placed = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(placed && document.GetVoxelCount() == 2U &&
                document.GetVoxel({6, 0, 2}).has_value() &&
                document.GetVoxel({6, 0, 1}).has_value() &&
                history.UndoCount() == 1U && editSession.Rebuilds == 1U,
        "Gizmo confirmation must place every Stamp voxel in one history operation.");
    Require(history.Undo(editSession) && document.GetVoxelCount() == 0U &&
                history.UndoCount() == 0U && history.RedoCount() == 1U,
        "One Undo must remove the complete gizmo-placed Stamp.");
}

void TestOrdinalOnlyAdvancesAfterSuccessfulPlacement()
{
    auto document = MakeDocument();
    EditSession editSession(document, 16U);
    VoxelEditHistory history;
    StampPlacementSession session;
    const VoxelStamp stamp = MakeStamp();
    Require(session.Begin(stamp, document, 16U, 0U,
                {2 * StampFixedPoint::UnitsPerVoxel, 0, 0}).Succeeded,
        "Ordinal fixture must begin.");
    Require(document.SetVoxel({12, 0, 0}, 1U).Succeeded,
        "Unable to make the preview revision stale.");

    const auto refreshed = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(refreshed.Status ==
                StampPlacementSessionPlaceStatus::PreviewRefreshed &&
                session.PlacementOrdinal() == 0U &&
                history.UndoCount() == 0U && editSession.Rebuilds == 0U &&
                document.GetVoxelCount() == 1U,
        "A stale click must refresh without consuming an ordinal.");
    const auto placed = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(static_cast<bool>(placed) &&
                session.PlacementOrdinal() == 1U &&
                history.UndoCount() == 1U && document.GetVoxelCount() == 3U,
        "The confirmed second click must consume exactly one ordinal.");
    const auto noChange = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(noChange.Status == StampPlacementSessionPlaceStatus::NoChange &&
                session.PlacementOrdinal() == 1U &&
                history.UndoCount() == 1U && editSession.Rebuilds == 1U,
        "No-change placement must not consume an ordinal or history item.");

    auto limitedDocument = MakeDocument();
    EditSession limitedEditSession(limitedDocument, 16U);
    VoxelEditHistory limitedHistory({
        .MaximumCommandCount = 100U,
        .MaximumEstimatedMemory = 1U});
    StampPlacementSession limitedSession;
    Require(limitedSession.Begin(
                stamp, limitedDocument, 16U, 0U, {}).Succeeded,
        "Limited history fixture must begin.");
    const auto refused = limitedSession.PlaceOnce(
        limitedDocument, 16U, limitedEditSession, limitedHistory);
    Require(refused.Status == StampPlacementSessionPlaceStatus::Rejected &&
                refused.History.Code ==
                    VoxelEditHistoryResultCode::LimitExceeded &&
                limitedSession.PlacementOrdinal() == 0U &&
                limitedDocument.GetVoxelCount() == 0U &&
                limitedHistory.UndoCount() == 0U &&
                limitedEditSession.Rebuilds == 0U,
        "Rejected placement must not consume ordinal, document or history.");
}

void TestDocumentSwitchCancelsAndRestartIsNeutral()
{
    auto firstDocument = MakeDocument();
    auto secondDocument = MakeDocument();
    EditSession secondEditSession(secondDocument, 16U);
    VoxelEditHistory history;
    const VoxelStamp stamp = MakeStamp();
    StampPlacementSession session;
    Require(session.Begin(stamp, firstDocument, 16U, 0U, {}).Succeeded,
        "Document-switch fixture must begin.");

    const auto switched = session.PlaceOnce(
        secondDocument, 16U, secondEditSession, history);
    Require(switched.Status ==
                StampPlacementSessionPlaceStatus::DocumentChanged &&
                switched.PreviewChanged && !session.IsActive() &&
                session.State() == StampPlacementSessionState::Cancelled &&
                session.CurrentPreview() == nullptr &&
                firstDocument.GetVoxelCount() == 0U &&
                secondDocument.GetVoxelCount() == 0U &&
                history.UndoCount() == 0U,
        "Switching documents must cancel instead of retargeting the session.");

    StampPlacementSession generationSession;
    Require(generationSession.Begin(
                stamp, firstDocument, 16U, 0U, {}).Succeeded,
        "Generation-switch fixture must begin.");
    const auto generationChanged = generationSession.UpdateTarget(
        {StampFixedPoint::UnitsPerVoxel, 0, 0}, firstDocument, 17U);
    Require(generationChanged.Code ==
                StampPlacementSessionResultCode::DocumentChanged &&
                !generationSession.IsActive() &&
                generationSession.CurrentPreview() == nullptr,
        "A document generation change must cancel the active session.");

    {
        StampPlacementSession previousProcessSession;
        Require(previousProcessSession.SelectAsset(
                    &stamp, firstDocument, 16U).Succeeded &&
                    previousProcessSession.RotateClockwise(
                        firstDocument, 16U).Succeeded &&
                    previousProcessSession.SetMirror(
                        StampPlacementMirrorMode::XZ,
                        firstDocument, 16U).Succeeded,
            "Transient restart fixture setup failed.");
    }
    StampPlacementSession restartedSession;
    Require(restartedSession.SelectAsset(
                &stamp, firstDocument, 16U).Succeeded &&
                restartedSession.QuarterRotation() == 0U &&
                restartedSession.Mirror() == StampPlacementMirrorMode::None &&
                restartedSession.PlacementOrdinal() == 0U,
        "A new application session must not restore transient transforms.");
}

void TestRealScaleSixSidedStampRotatesPlacesAndUndoes()
{
    auto document = MakeDocument({64U, 64U, 64U});
    EditSession editSession(document, 16U);
    VoxelEditHistory history;
    StampPlacementSession session;
    const VoxelStamp stamp = MakeLargeSixSidedStamp();
    constexpr std::int32_t fixed = StampFixedPoint::UnitsPerVoxel;
    const StampFixedPoint target{32 * fixed, 32 * fixed, 32 * fixed};

    Require(session.Begin(stamp, document, 16U, 0U, target).Succeeded &&
                session.CurrentPlan() != nullptr &&
                session.CurrentPlan()->Statistics.TotalVoxelCount == 4633U &&
                session.CurrentPlan()->Statistics.ChangedVoxelCount == 4633U,
        "The real-scale Stamp preview must contain every source voxel.");
    const auto originalBounds = session.CurrentPlan()->WorldBounds;
    Require(session.SetGizmoTransform(
                target, 1U, document, 16U).Succeeded &&
                session.QuarterRotation() == 1U &&
                session.CurrentPlan() != nullptr &&
                session.CurrentPlan()->WorldBounds != originalBounds &&
                session.CurrentPlan()->Statistics.TotalVoxelCount == 4633U &&
                session.CurrentPlan()->Statistics.ChangedVoxelCount == 4633U,
        "Rotate Y must rotate every cell of a real-scale Stamp.");

    const auto placed = session.PlaceOnce(
        document, 16U, editSession, history);
    Require(placed && document.GetVoxelCount() == 4633U &&
                history.UndoCount() == 1U && editSession.Rebuilds == 1U &&
                editSession.Completions == 1U,
        "One confirmation must place all 4633 Stamp voxels atomically.");
    Require(history.Undo(editSession) && document.GetVoxelCount() == 0U &&
                history.UndoCount() == 0U && history.RedoCount() == 1U,
        "One Undo must remove the complete 4633-voxel Stamp.");
}

} // namespace

int main()
{
    try
    {
        TestSelectionUpdateAndTransientState();
        TestContinuousPlacementAndIndependentUndo();
        TestGizmoTransformPlacesTheCompleteStampAtomically();
        TestOrdinalOnlyAdvancesAfterSuccessfulPlacement();
        TestDocumentSwitchCancelsAndRestartIsNeutral();
        TestRealScaleSixSidedStampRotatesPlacesAndUndoes();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
