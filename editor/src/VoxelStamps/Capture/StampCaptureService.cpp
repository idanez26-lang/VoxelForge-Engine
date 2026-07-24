#include "VoxelStamps/Capture/StampCaptureService.h"

#include "VoxelStamps/Format/VfstampWriter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] bool CalculateDimensions(
    const SelectionBounds& selectionBounds,
    StampDimensions& dimensions) noexcept;

[[nodiscard]] StampCaptureResult Failure(
    const StampCaptureRequest& request,
    const StampCaptureError error) noexcept
{
    StampCaptureStatistics statistics{
        .SelectedVoxelCount = request.Selection.Count(),
        .HasSelectionBounds = request.Selection.Bounds().Valid,
        .SourceMinimum = request.Selection.Bounds().Minimum,
        .SourceMaximum = request.Selection.Bounds().Maximum};
    StampDimensions dimensions{};
    if (CalculateDimensions(request.Selection.Bounds(), dimensions))
    {
        statistics.Dimensions = dimensions;
    }
    return {
        .Error = error,
        .Diagnostic = {.Code = error, .Message = StampCaptureErrorMessage(error)},
        .Statistics = statistics,
        .ExpectedDocumentGeneration = request.ExpectedDocumentGeneration,
        .ActualDocumentGeneration = request.Selection.DocumentGeneration(),
        .ExpectedDocumentRevision = request.ExpectedDocumentRevision,
        .ActualDocumentRevision = request.Document.GetRevision()};
}

[[nodiscard]] bool CalculateDimensions(
    const SelectionBounds& selectionBounds,
    StampDimensions& dimensions) noexcept
{
    if (!selectionBounds.Valid) return false;
    const auto axis = [](const std::int32_t minimum, const std::int32_t maximum,
                         std::uint32_t& destination) noexcept {
        const std::int64_t span = static_cast<std::int64_t>(maximum) -
            static_cast<std::int64_t>(minimum) + 1;
        if (span <= 0 || span > std::numeric_limits<std::int32_t>::max())
        {
            return false;
        }
        destination = static_cast<std::uint32_t>(span);
        return true;
    };
    return axis(selectionBounds.Minimum.X, selectionBounds.Maximum.X, dimensions.X) &&
        axis(selectionBounds.Minimum.Y, selectionBounds.Maximum.Y, dimensions.Y) &&
        axis(selectionBounds.Minimum.Z, selectionBounds.Maximum.Z, dimensions.Z);
}

[[nodiscard]] StampBounds MakeBounds(const StampDimensions dimensions) noexcept
{
    return {
        .Minimum = {},
        .Maximum = {
            .X = static_cast<std::int32_t>(dimensions.X - 1U),
            .Y = static_cast<std::int32_t>(dimensions.Y - 1U),
            .Z = static_cast<std::int32_t>(dimensions.Z - 1U)},
        .Dimensions = dimensions};
}

[[nodiscard]] StampLocalPosition NormalizePosition(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelPosition minimum) noexcept
{
    return {
        .X = static_cast<std::int32_t>(
            static_cast<std::int64_t>(position.X) - minimum.X),
        .Y = static_cast<std::int32_t>(
            static_cast<std::int64_t>(position.Y) - minimum.Y),
        .Z = static_cast<std::int32_t>(
            static_cast<std::int64_t>(position.Z) - minimum.Z)};
}

[[nodiscard]] StampLimitEvaluation PreflightLimits(
    const std::size_t voxelCount,
    const StampDimensions dimensions,
    const StampResourceLimits& limits) noexcept
{
    const StampSizeEstimate estimate = EstimateDecodedStampBytes(
        0U, voxelCount, sizeof(StampVoxel),
        std::min<std::size_t>(voxelCount, 256U), sizeof(StampPaletteEntry));
    return EvaluateStampLimits(
        {.VoxelCount = voxelCount,
         .LargestAxisLength = std::max({dimensions.X, dimensions.Y, dimensions.Z}),
         .DecodedBytes = estimate.DecodedBytes,
         .ArithmeticOverflow = estimate.ArithmeticOverflow},
        limits);
}

[[nodiscard]] bool FitsFixedPoint(const StampDimensions dimensions) noexcept
{
    constexpr std::uint32_t maximumDimension = static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max() / StampFixedPoint::UnitsPerVoxel);
    return dimensions.X <= maximumDimension && dimensions.Y <= maximumDimension &&
        dimensions.Z <= maximumDimension;
}

} // namespace

StampCaptureResult StampCaptureService::Capture(
    const StampCaptureRequest& request) noexcept
{
    try
    {
        if (request.Selection.Empty())
        {
            return Failure(request, StampCaptureError::EmptySelection);
        }
        if (request.Selection.DocumentGeneration() != request.ExpectedDocumentGeneration)
        {
            return Failure(request, StampCaptureError::GenerationMismatch);
        }
        if (request.Document.GetRevision() != request.ExpectedDocumentRevision)
        {
            return Failure(request, StampCaptureError::RevisionMismatch);
        }
        if (request.Document.GetModel(request.SubModelIndex) == nullptr)
        {
            return Failure(request, StampCaptureError::InvalidSubModel);
        }
        if (request.StampId.Value() == 0U)
        {
            return Failure(request, StampCaptureError::InvalidIdentity);
        }

        StampDimensions dimensions{};
        const SelectionBounds selectionBounds = request.Selection.Bounds();
        if (!CalculateDimensions(selectionBounds, dimensions))
        {
            return Failure(request, StampCaptureError::InvalidSelectionBounds);
        }
        const StampBounds bounds = MakeBounds(dimensions);
        if (!FitsFixedPoint(dimensions))
        {
            return Failure(request, StampCaptureError::ArithmeticOverflow);
        }
        const StampLimitEvaluation preflight = PreflightLimits(
            request.Selection.Count(), dimensions, request.Limits);
        if (preflight.Status == StampLimitStatus::ArithmeticOverflow)
        {
            StampCaptureResult result = Failure(request, StampCaptureError::ArithmeticOverflow);
            result.LimitEvaluation = preflight;
            return result;
        }
        if (!preflight.IsAllowed())
        {
            StampCaptureResult result = Failure(request, StampCaptureError::ResourceLimitExceeded);
            result.LimitEvaluation = preflight;
            return result;
        }

        std::array<bool, 256U> sourcePaletteMapped{};
        std::array<std::uint8_t, 256U> localPaletteIds{};
        std::vector<StampPaletteEntry> palette;
        palette.reserve(std::min<std::size_t>(request.Selection.Count(), 256U));
        const auto& documentPalette = request.Document.GetPalette();
        for (const Asset::Voxel::VoxelPosition position : request.Selection.Voxels())
        {
            const std::optional<Asset::Voxel::Voxel> voxel =
                request.Document.GetVoxel(position, request.SubModelIndex);
            if (!voxel)
            {
                return Failure(request, StampCaptureError::MissingSelectedVoxel);
            }
            const std::uint8_t sourceIndex = voxel->PaletteIndex;
            if (sourcePaletteMapped[sourceIndex]) continue;
            sourcePaletteMapped[sourceIndex] = true;
            const Asset::Voxel::VoxelColor color = documentPalette[sourceIndex];
            const auto existing = std::find_if(
                palette.begin(), palette.end(), [color](const StampPaletteEntry& entry) {
                    return entry.Color == color;
                });
            if (existing != palette.end())
            {
                localPaletteIds[sourceIndex] = existing->LocalColorId;
                continue;
            }
            const std::uint8_t localId = static_cast<std::uint8_t>(palette.size());
            localPaletteIds[sourceIndex] = localId;
            palette.push_back({.LocalColorId = localId,
                               .Color = color,
                               .HasSourcePaletteIndex = true,
                               .SourcePaletteIndex = sourceIndex});
        }

        std::vector<StampVoxel> voxels;
        voxels.reserve(request.Selection.Count());
        for (const Asset::Voxel::VoxelPosition position : request.Selection.Voxels())
        {
            const std::optional<Asset::Voxel::Voxel> voxel =
                request.Document.GetVoxel(position, request.SubModelIndex);
            // The first pass already verified every selected voxel. Recheck to
            // prevent a partial snapshot if a caller mutates the document while
            // capture is executing.
            if (!voxel)
            {
                return Failure(request, StampCaptureError::MissingSelectedVoxel);
            }
            voxels.push_back({
                .Position = NormalizePosition(position, selectionBounds.Minimum),
                .LocalColorId = localPaletteIds[voxel->PaletteIndex]});
        }

        if (request.Selection.DocumentGeneration() != request.ExpectedDocumentGeneration)
        {
            return Failure(request, StampCaptureError::GenerationMismatch);
        }
        if (request.Document.GetRevision() != request.ExpectedDocumentRevision)
        {
            return Failure(request, StampCaptureError::RevisionMismatch);
        }

        StampPivotContext pivotContext = request.PivotContext;
        pivotContext.Bounds = bounds;
        const StampPivot pivot = ResolveAutoPivot(pivotContext);
        StampValidationResult domainValidation{};
        const auto stamp = VoxelStamp::TryCreate(
            {.Id = request.StampId, .ContentHash = {}}, bounds, pivot, {},
            std::move(palette), std::move(voxels), request.Limits, &domainValidation);
        if (!stamp)
        {
            StampCaptureResult result = Failure(request, StampCaptureError::DomainValidationFailed);
            result.Validation.Diagnostics.push_back({
                .Category = StampDiagnosticCategory::Domain,
                .Severity = StampDiagnosticSeverity::Error,
                .Explanation = std::string(domainValidation.Message)});
            result.LimitEvaluation = preflight;
            return result;
        }

        StampCaptureResult result{};
        result.Diagnostic = {.Code = StampCaptureError::None,
                             .Message = StampCaptureErrorMessage(StampCaptureError::None)};
        result.Stamp = std::move(*stamp);
        result.Statistics = {
            .SelectedVoxelCount = request.Selection.Count(),
            .CapturedVoxelCount = result.Stamp->Voxels().size(),
            .PaletteEntryCount = result.Stamp->Palette().size(),
            .HasSelectionBounds = true,
            .SourceMinimum = selectionBounds.Minimum,
            .SourceMaximum = selectionBounds.Maximum,
            .Dimensions = dimensions};
        result.LogicalContentHash = CalculateVfstampLogicalContentHash(*result.Stamp);
        result.Validation = ValidateStampForWrite(*result.Stamp, request.Limits);
        result.LimitEvaluation = preflight;
        result.ExpectedDocumentGeneration = request.ExpectedDocumentGeneration;
        result.ActualDocumentGeneration = request.Selection.DocumentGeneration();
        result.ExpectedDocumentRevision = request.ExpectedDocumentRevision;
        result.ActualDocumentRevision = request.Document.GetRevision();
        if (!result.Validation.IsValid())
        {
            result.Error = StampCaptureError::DomainValidationFailed;
            result.Diagnostic = {.Code = result.Error,
                                 .Message = StampCaptureErrorMessage(result.Error)};
            result.Stamp.reset();
            result.LogicalContentHash.clear();
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Failure(request, StampCaptureError::AllocationFailure);
    }
}

} // namespace VoxelForge::Editor::Stamps
