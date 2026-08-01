// Transform tool adapters of EditorWorkspace (VF-0260 lot 6).
// Pure code motion from EditorWorkspace.cpp: the Begin/Update/Apply/Cancel
// choreography for Move, Duplicate, Rotate, Mirror, Scale and Align, plus the
// Transform panel appliers and the constraint bridges. Behaviour unchanged.

#include "EditorWorkspace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace VoxelForge::Editor
{

TransformPanelSource EditorWorkspace::CurrentTransformPanelSource() noexcept
{
    TransformPanelSource source;
    const Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const SelectionBounds selectionBounds = selectionService_.EditableBounds();
    source.Available = document != nullptr && !selectionService_.Empty() &&
        selectionBounds.Valid && selectionService_.DocumentGeneration() ==
            voxelDocumentSession_.Generation();
    source.SourceBounds = transformPreviewModel_.IsActive()
        ? transformPreviewModel_.SourceBounds() : selectionBounds;
    if (!source.Available) return source;

    if (!transformGizmoManager_.IsDragging())
        static_cast<void>(transformPivotManager_.UpdateFromBounds(
            selectionBounds, voxelModelCenter_));
    if (!transformPivotManager_.HasValidPivot())
    {
        source.Available = false;
        return source;
    }
    source.Pivot = transformPivotManager_.GetPivot();

    if (transformPreviewModel_.IsActive() &&
        voxelToolState_.IsRotateActive())
    {
        source.RotationPreview = TransformPanelRotation{
            voxelRotateAxis_, voxelRotateQuarterTurns_};
    }
    if (transformPreviewModel_.IsActive() &&
        voxelToolState_.IsScaleActive())
    {
        if (voxelScaleTargetDimensions_)
            source.ScalePreviewDimensions = *voxelScaleTargetDimensions_;
        else if (transformGizmoManager_.IsDragging())
        {
            const Asset::Voxel::VoxelDimensions dimensions =
                transformGizmoManager_.TargetDimensions();
            if (dimensions.X > 0U && dimensions.Y > 0U && dimensions.Z > 0U)
                source.ScalePreviewDimensions = dimensions;
        }
    }
    return source;
}

bool EditorWorkspace::ApplyTransformPanelPosition(const Vec3 position)
{
    const TransformPanelPositionEdit edit =
        transformPanelViewModel_.PreparePosition(
            CurrentTransformPanelSource(), position);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document)
    {
        transformPanelStatusMessage_ = "The active document is unavailable.";
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            0U, TransformPreviewCollisionPolicy::IgnoreSource) ||
        !transformPreviewModel_.SetDelta(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            ConstrainMoveDelta(edit.Delta)) || !ApplyVoxelMove())
    {
        transformPanelStatusMessage_ = voxelMoveStatusMessage_.empty()
            ? "Position could not be applied."
            : voxelMoveStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyTransformPanelRotation(const Vec3 degrees)
{
    const TransformPanelRotationEdit edit =
        transformPanelViewModel_.PrepareRotation(
            CurrentTransformPanelSource(), degrees);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!BeginVoxelRotatePreview(edit.Axis, edit.QuarterTurns) ||
        !ApplyVoxelRotate())
    {
        transformPanelStatusMessage_ = voxelRotateStatusMessage_.empty()
            ? "Rotation could not be applied."
            : voxelRotateStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyTransformPanelScale(const Vec3 scale)
{
    const TransformPanelScaleEdit edit = transformPanelViewModel_.PrepareScale(
        CurrentTransformPanelSource(), scale);
    if (edit.Code == TransformPanelEditCode::NoChange)
    {
        transformPanelStatusMessage_.clear();
        return true;
    }
    if (!edit.Ready())
    {
        transformPanelStatusMessage_ = edit.Message;
        return false;
    }

    CancelTransformGizmoInteraction();
    if (!UpdateVoxelScalePreview(
            VoxelScaleMode::Uniform, edit.TargetDimensions) ||
        !ApplyVoxelScale())
    {
        transformPanelStatusMessage_ = voxelScaleStatusMessage_.empty()
            ? "Scale could not be applied."
            : voxelScaleStatusMessage_;
        return false;
    }
    transformPanelStatusMessage_.clear();
    return true;
}

Asset::Voxel::VoxelPosition EditorWorkspace::ConstrainMoveDelta(
    const Asset::Voxel::VoxelPosition delta) const noexcept
{
    ConstraintRequest request;
    request.Transform.Position = {
        static_cast<float>(delta.X),
        static_cast<float>(delta.Y),
        static_cast<float>(delta.Z)};
    request.Settings = constraintSettings_;
    const Vec3 constrained = ConstraintEngine::Solve(request).Transform.Position;
    return {
        static_cast<std::int32_t>(std::lround(constrained.X)),
        static_cast<std::int32_t>(std::lround(constrained.Y)),
        static_cast<std::int32_t>(std::lround(constrained.Z))};
}

std::int32_t EditorWorkspace::ConstrainRotationQuarterTurns(
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns) const noexcept
{
    ConstraintRequest request;
    const float degrees = static_cast<float>(quarterTurns) * 90.0F;
    if (axis == VoxelRotationAxis::X)
        request.Transform.RotationDegrees.X = degrees;
    else if (axis == VoxelRotationAxis::Y)
        request.Transform.RotationDegrees.Y = degrees;
    else
        request.Transform.RotationDegrees.Z = degrees;
    request.Settings = constraintSettings_;
    const Vec3 constrained =
        ConstraintEngine::Solve(request).Transform.RotationDegrees;
    const float axisDegrees = axis == VoxelRotationAxis::X
        ? constrained.X
        : axis == VoxelRotationAxis::Y ? constrained.Y : constrained.Z;
    return static_cast<std::int32_t>(std::lround(axisDegrees / 90.0F));
}

bool EditorWorkspace::ApplyVoxelMove()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const Asset::Voxel::VoxelPosition constrainedDelta =
        ConstrainMoveDelta(transformPreviewModel_.Delta());
    if (constrainedDelta != transformPreviewModel_.Delta() &&
        !transformPreviewModel_.SetDelta(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            constrainedDelta))
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelMoveStatusMessage_ = "Move constraint could not be applied.";
        UpdateVoxelHighlights();
        return false;
    }

    MoveVoxelSelectionResult prepared = MoveVoxelSelectionOperation::Build(
        *document, selectionService_, voxelDocumentSession_.Generation(),
        transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelMoveStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelMoveStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Move failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelMoveStatusMessage_.clear();
    AddConsoleMessage("[Edit] Moved " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    return true;
}

bool EditorWorkspace::ApplyVoxelDuplicate()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (document == nullptr || voxelEditInProgress_ ||
        voxelEditHistory_.IsBusy())
    {
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    DuplicateVoxelSelectionResult prepared =
        DuplicateVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelDuplicateStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelDuplicateStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Duplicate failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelDuplicateStatusMessage_.clear();
    AddConsoleMessage("[Edit] Duplicated " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    return true;
}

bool EditorWorkspace::BeginVoxelRotatePreview(
    const VoxelRotationDirection direction)
{
    return BeginVoxelRotatePreview(VoxelRotationAxis::Y,
        direction == VoxelRotationDirection::Clockwise ? 1 : -1);
}

bool EditorWorkspace::BeginVoxelRotatePreview(
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanRotateSelection())
    {
        voxelRotateStatusMessage_ = "Select voxels before using Rotate";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::int32_t constrainedQuarterTurns =
        ConstrainRotationQuarterTurns(axis, quarterTurns);

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsRotateActive() &&
        voxelRotateAxis_ == axis &&
        voxelRotateQuarterTurns_ == constrainedQuarterTurns &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExplicitDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelRotateStatusMessage_ = "Rotate preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelRotationGeometry geometry =
        RotateVoxelSelectionOperation::BuildGeometry(
            selectionService_.Voxels(), selectionService_.EditableBounds(),
            axis, constrainedQuarterTurns);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelRotateStatusMessage_ = geometry.Message.empty()
            ? "Rotate preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelRotateAxis_ = axis;
    voxelRotateQuarterTurns_ = constrainedQuarterTurns;
    voxelRotateDirection_ = constrainedQuarterTurns < 0
        ? VoxelRotationDirection::CounterClockwise
        : VoxelRotationDirection::Clockwise;
    voxelRotateStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelRotate()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelRotate();
        return false;
    }

    RotateVoxelSelectionResult prepared =
        RotateVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelRotateAxis_,
            voxelRotateQuarterTurns_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelRotateStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelRotateStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Rotate failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelRotateStatusMessage_.clear();
    AddConsoleMessage("[Edit] Rotated " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelRotate() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelRotateStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelMirrorPreview(const VoxelMirrorAxis axis)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanMirrorSelection())
    {
        voxelMirrorStatusMessage_ = "Select voxels before using Mirror";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsMirrorActive() && voxelMirrorAxis_ == axis &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExplicitDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelMirrorStatusMessage_ =
            "Mirror preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelMirrorGeometry geometry =
        MirrorVoxelSelectionOperation::BuildGeometry(
            selectionService_.Voxels(), selectionService_.EditableBounds(),
            axis);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelMirrorStatusMessage_ = geometry.Message.empty()
            ? "Mirror preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelMirrorAxis_ = axis;
    voxelMirrorStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelMirror()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelMirror();
        return false;
    }

    MirrorVoxelSelectionResult prepared =
        MirrorVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelMirrorAxis_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (prepared.Code == MirrorVoxelSelectionResultCode::NoChange)
    {
        voxelMirrorStatusMessage_ = "Mirror has no visible effect";
        AddConsoleMessage("[Edit] Mirror has no visible effect.");
        UpdateVoxelHighlights();
        return true;
    }
    if (!prepared.Ready())
    {
        voxelMirrorStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelMirrorStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Mirror failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelMirrorStatusMessage_.clear();
    AddConsoleMessage("[Edit] Mirrored " +
        std::to_string(selectionService_.Count()) + " voxel(s) on " +
        VoxelMirrorAxisName(voxelMirrorAxis_) + ".");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelMirror() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelMirrorStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelScalePreview(const VoxelScaleMode mode)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanScaleSelection())
    {
        voxelScaleStatusMessage_ = "Select voxels before using Scale";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (voxelToolState_.IsScaleActive() && voxelScaleMode_ == mode &&
        transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        transformPreviewModel_.HasExpandedDestinations())
        return true;
    if (!transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelScaleStatusMessage_ = "Scale preview could not capture selection";
        UpdateVoxelHighlights();
        return false;
    }
    const VoxelScaleGeometry geometry =
        ScaleVoxelSelectionOperation::BuildGeometry(
            transformPreviewModel_.SourceVoxels(),
            selectionService_.EditableBounds(), mode);
    if (!geometry.Valid() ||
        !transformPreviewModel_.SetExplicitVoxelDestinations(
            *document, selectionService_, generation, geometry.Destinations))
    {
        voxelScaleStatusMessage_ = geometry.Message.empty()
            ? "Scale preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }
    voxelScaleMode_ = mode;
    voxelScaleTargetDimensions_.reset();
    voxelScaleStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::UpdateVoxelScalePreview(
    const VoxelScaleMode mode,
    const Asset::Voxel::VoxelDimensions targetDimensions)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    const std::uint64_t generation = voxelDocumentSession_.Generation();
    if (!document || !CanScaleSelection())
    {
        voxelScaleStatusMessage_ = "Scale preview source is no longer valid";
        return false;
    }
    if (!transformPreviewModel_.IsValidFor(
            *document, selectionService_, generation) &&
        !transformPreviewModel_.BeginPreview(
            *document, selectionService_, generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource))
    {
        voxelScaleStatusMessage_ = "Scale preview source is no longer valid";
        return false;
    }
    if (voxelScaleTargetDimensions_ == targetDimensions &&
        transformPreviewModel_.HasExpandedDestinations())
        return true;

    const VoxelScaleGeometry geometry =
        ScaleVoxelSelectionOperation::BuildGeometry(
            transformPreviewModel_.SourceVoxels(),
            transformPreviewModel_.SourceBounds(), mode, targetDimensions);
    if (!geometry.Valid())
    {
        voxelScaleStatusMessage_ = geometry.Message.empty()
            ? "Scale preview could not be built" : geometry.Message;
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelScaleTargetDimensions_.reset();
        return false;
    }
    const bool rebuilt = transformPreviewModel_.SetExplicitVoxelDestinations(
        *document, selectionService_, generation, geometry.Destinations);
    const auto current = transformPreviewModel_.Voxels();
    const bool alreadyMatches = current.size() == geometry.Destinations.size() &&
        std::equal(current.begin(), current.end(), geometry.Destinations.begin(),
            [](const TransformPreviewVoxel& voxel,
               const TransformPreviewDestinationVoxel& destination)
            {
                return voxel.SourcePosition == destination.SourcePosition &&
                    voxel.PreviewPosition == destination.DestinationPosition &&
                    voxel.Value == destination.Value;
            });
    if (!rebuilt && !alreadyMatches)
    {
        voxelScaleStatusMessage_ = "Scale preview could not be built";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        voxelScaleTargetDimensions_.reset();
        return false;
    }
    voxelScaleMode_ = mode;
    voxelScaleTargetDimensions_ = targetDimensions;
    voxelScaleStatusMessage_.clear();
    return true;
}

bool EditorWorkspace::ApplyVoxelScale()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelScale();
        return false;
    }

    ScaleVoxelSelectionResult prepared = voxelScaleTargetDimensions_
        ? ScaleVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelScaleMode_,
            *voxelScaleTargetDimensions_)
        : ScaleVoxelSelectionOperation::Build(
            *document, selectionService_, voxelDocumentSession_.Generation(),
            transformPreviewModel_, voxelScaleMode_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelScaleTargetDimensions_.reset();
    if (!prepared.Ready())
    {
        voxelScaleStatusMessage_ = prepared.Message;
        if (!prepared.Message.empty())
            AddConsoleMessage("[Edit] " + prepared.Message);
        UpdateVoxelHighlights();
        return false;
    }

    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelScaleStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Scale failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelScaleStatusMessage_.clear();
    AddConsoleMessage("[Edit] Scaled selection " +
        std::string(VoxelScaleModeName(voxelScaleMode_)) + " to " +
        std::to_string(selectionService_.Count()) + " voxel(s).");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelScale() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelScaleTargetDimensions_.reset();
    voxelScaleStatusMessage_.clear();
}

bool EditorWorkspace::BeginVoxelAlignPreview(
    const VoxelAlignDirection direction)
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || !CanAlignSelection())
    {
        voxelAlignStatusMessage_ = "Select voxels before using Align";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const auto dimensions = document->GetDimensions(0U);
    const auto delta = dimensions
        ? AlignVoxelSelectionOperation::CalculateDelta(
            selectionService_.EditableBounds(), *dimensions, direction)
        : std::nullopt;
    if (!delta)
    {
        voxelAlignStatusMessage_ = "Align could not calculate a valid delta";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    const std::uint64_t generation = voxelDocumentSession_.Generation();
    const bool previewStarted = transformPreviewModel_.BeginPreview(
        *document, selectionService_, generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource);
    const bool previewPositioned = previewStarted &&
        (*delta == Asset::Voxel::VoxelPosition{} ||
         transformPreviewModel_.SetDelta(
             *document, selectionService_, generation, *delta));
    if (!previewPositioned)
    {
        voxelAlignStatusMessage_ = "Align preview could not be built";
        static_cast<void>(transformPreviewModel_.CancelPreview());
        UpdateVoxelHighlights();
        return false;
    }

    voxelAlignDirection_ = direction;
    voxelAlignStatusMessage_.clear();
    voxelMoveStatusMessage_.clear();
    voxelDuplicateStatusMessage_.clear();
    voxelRotateStatusMessage_.clear();
    voxelMirrorStatusMessage_.clear();
    voxelScaleStatusMessage_.clear();
    UpdateVoxelHighlights();
    return true;
}

bool EditorWorkspace::ApplyVoxelAlign()
{
    Asset::Voxel::VoxelDocument* document =
        voxelDocumentSession_.ActiveDocument();
    if (!document || voxelEditInProgress_ || voxelEditHistory_.IsBusy())
    {
        CancelVoxelAlign();
        return false;
    }

    MoveVoxelSelectionResult prepared = AlignVoxelSelectionOperation::Build(
        *document, selectionService_, voxelDocumentSession_.Generation(),
        transformPreviewModel_);
    static_cast<void>(transformPreviewModel_.CancelPreview());
    if (!prepared.Ready())
    {
        voxelAlignStatusMessage_ =
            prepared.Code == MoveVoxelSelectionResultCode::NoChange
            ? "Selection is already aligned to this face"
            : prepared.Message;
        if (!voxelAlignStatusMessage_.empty())
            AddConsoleMessage("[Edit] " + voxelAlignStatusMessage_);
        UpdateVoxelHighlights();
        return false;
    }

    prepared.Operation.Label = "Align Voxels";
    voxelEditInProgress_ = true;
    const VoxelEditHistoryResult result = voxelEditHistory_.Execute(
        *this, std::move(prepared.Operation));
    voxelEditInProgress_ = false;
    if (!result)
    {
        voxelAlignStatusMessage_ = result.Message;
        AddConsoleMessage("[Edit] Align failed: " + result.Message);
        UpdateVoxelHighlights();
        return false;
    }
    ApplyVoxelHistorySelection(result);
    voxelAlignStatusMessage_.clear();
    AddConsoleMessage("[Edit] Aligned " +
        std::to_string(selectionService_.Count()) + " voxel(s) " +
        VoxelAlignDirectionName(voxelAlignDirection_) + ".");
    UpdateVoxelHighlights();
    return true;
}

void EditorWorkspace::CancelVoxelAlign() noexcept
{
    static_cast<void>(transformPreviewModel_.CancelPreview());
    voxelAlignStatusMessage_.clear();
}

void EditorWorkspace::CancelTransformGizmoInteraction() noexcept
{
    const TransformGizmoMode mode = transformGizmoManager_.Mode();
    const bool rotate = mode ==
        TransformGizmoMode::Rotate;
    const bool scale = mode ==
        TransformGizmoMode::Scale;
    const bool changed = static_cast<bool>(
        transformGizmoManager_.CancelInteraction());
    const bool previewCancelled = transformPreviewModel_.CancelPreview();
    if (rotate) voxelRotateStatusMessage_.clear();
    if (scale)
    {
        voxelScaleTargetDimensions_.reset();
        voxelScaleStatusMessage_.clear();
    }
    if (changed || previewCancelled) UpdateVoxelHighlights();
}

} // namespace VoxelForge::Editor
