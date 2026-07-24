#pragma once

#include "Selection/SelectionService.h"
#include "VoxelStamps/Capture/StampAutoPivotResolver.h"
#include "VoxelStamps/Validation/StampValidationService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{

enum class StampCaptureError : std::uint8_t
{
    None,
    EmptySelection,
    GenerationMismatch,
    RevisionMismatch,
    InvalidSubModel,
    InvalidIdentity,
    InvalidSelectionBounds,
    MissingSelectedVoxel,
    ResourceLimitExceeded,
    ArithmeticOverflow,
    DomainValidationFailed,
    AllocationFailure
};

[[nodiscard]] constexpr std::string_view StampCaptureErrorMessage(
    const StampCaptureError error) noexcept
{
    switch (error)
    {
    case StampCaptureError::None: return "Voxel selection captured.";
    case StampCaptureError::EmptySelection: return "Cannot capture an empty voxel selection.";
    case StampCaptureError::GenerationMismatch: return "Selection belongs to a different document generation.";
    case StampCaptureError::RevisionMismatch: return "Document revision changed before capture completed.";
    case StampCaptureError::InvalidSubModel: return "Requested source sub-model does not exist.";
    case StampCaptureError::InvalidIdentity: return "Capture requires a non-zero caller-provided Stamp UUID.";
    case StampCaptureError::InvalidSelectionBounds: return "Selection bounds cannot be represented as Stamp dimensions.";
    case StampCaptureError::MissingSelectedVoxel: return "A selected source voxel is missing from the document.";
    case StampCaptureError::ResourceLimitExceeded: return "Capture exceeds a configured hard Stamp resource limit.";
    case StampCaptureError::ArithmeticOverflow: return "Capture dimensions or resource arithmetic overflowed before allocation.";
    case StampCaptureError::DomainValidationFailed: return "Captured data violates immutable Stamp domain invariants.";
    case StampCaptureError::AllocationFailure: return "Capture output allocation failed.";
    }
    return "Unknown Stamp capture result.";
}

struct StampCaptureStatistics final
{
    std::size_t SelectedVoxelCount = 0U;
    std::size_t CapturedVoxelCount = 0U;
    std::size_t PaletteEntryCount = 0U;
    bool HasSelectionBounds = false;
    Asset::Voxel::VoxelPosition SourceMinimum{};
    Asset::Voxel::VoxelPosition SourceMaximum{};
    StampDimensions Dimensions{};

    [[nodiscard]] bool operator==(const StampCaptureStatistics&) const noexcept = default;
};

struct StampCaptureDiagnostic final
{
    StampCaptureError Code = StampCaptureError::None;
    std::string_view Message{StampCaptureErrorMessage(StampCaptureError::None)};

    [[nodiscard]] bool operator==(const StampCaptureDiagnostic&) const noexcept = default;
};

/// All identity and snapshot facts are explicit: capture never generates an
/// identifier and therefore remains a pure, deterministic transformation.
struct StampCaptureRequest final
{
    const Asset::Voxel::VoxelDocument& Document;
    const SelectionService& Selection;
    Core::UUID StampId{0U};
    std::uint64_t ExpectedDocumentGeneration = 0U;
    std::uint64_t ExpectedDocumentRevision = 0U;
    std::size_t SubModelIndex = 0U;
    StampPivotContext PivotContext{};
    StampResourceLimits Limits = DefaultStampResourceLimits();
};

/// Owns all output data. It never retains document or selection spans.
struct StampCaptureResult final
{
    StampCaptureError Error = StampCaptureError::None;
    StampCaptureDiagnostic Diagnostic{};
    StampCaptureStatistics Statistics{};
    std::optional<VoxelStamp> Stamp;
    std::string LogicalContentHash;
    StampValidationReport Validation;
    StampLimitEvaluation LimitEvaluation;
    std::uint64_t ExpectedDocumentGeneration = 0U;
    std::uint64_t ActualDocumentGeneration = 0U;
    std::uint64_t ExpectedDocumentRevision = 0U;
    std::uint64_t ActualDocumentRevision = 0U;

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Error == StampCaptureError::None && Stamp.has_value();
    }
};

class StampCaptureService final
{
public:
    [[nodiscard]] static StampCaptureResult Capture(
        const StampCaptureRequest& request) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
