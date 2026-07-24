#include "Selection/SelectionService.h"
#include "VoxelStamps/Capture/StampCaptureService.h"
#include "VoxelStamps/Format/VfstampWriter.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;
using Asset::Voxel::VoxelDocument;
using Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({
        .Dimensions = {.X = 8U, .Y = 8U, .Z = 8U},
        .Voxels = {
            {.X = 2U, .Y = 1U, .Z = 3U, .ColorIndex = 5U},
            {.X = 5U, .Y = 3U, .Z = 4U, .ColorIndex = 9U},
            {.X = 2U, .Y = 3U, .Z = 3U, .ColorIndex = 5U},
            {.X = 7U, .Y = 7U, .Z = 7U, .ColorIndex = 11U}}});
    source.Palette[5U] = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U};
    source.Palette[9U] = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U};
    source.Palette[11U] = {.Red = 70U, .Green = 80U, .Blue = 90U, .Alpha = 255U};
    const auto built = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, std::filesystem::path{"stamp-capture-test.vox"});
    Require(built.Succeeded(), "Capture fixture document must build.");
    return std::move(*built.Document);
}

VoxelDocument MakeLargeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    Asset::Vox::VoxModelMetadata model{.Dimensions = {.X = 16U, .Y = 8U, .Z = 2U}};
    for (std::uint8_t z = 0U; z < 2U; ++z)
        for (std::uint8_t y = 0U; y < 8U; ++y)
            for (std::uint8_t x = 0U; x < 16U; ++x)
                model.Voxels.push_back({.X = x, .Y = y, .Z = z, .ColorIndex = 1U});
    source.Models.push_back(std::move(model));
    source.Palette[1U] = {.Red = 1U, .Green = 2U, .Blue = 3U, .Alpha = 255U};
    const auto built = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, std::filesystem::path{"stamp-capture-large-test.vox"});
    Require(built.Succeeded(), "Large capture fixture document must build.");
    return std::move(*built.Document);
}

StampCaptureRequest Request(
    const VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t generation,
    const std::uint64_t revision)
{
    return {
        .Document = document,
        .Selection = selection,
        .StampId = Core::UUID{0xC0FFEEU},
        .ExpectedDocumentGeneration = generation,
        .ExpectedDocumentRevision = revision};
}

void TestSparseCaptureNormalizationPaletteAndImmutability()
{
    VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(7U);
    const std::vector<VoxelPosition> selected{{5, 3, 4}, {2, 1, 3}, {2, 3, 3}, {5, 3, 4}};
    Require(selection.Apply(selected, SelectionMode::Replace), "Fixture selection must apply.");

    const std::uint64_t revisionBefore = document.GetRevision();
    const std::uint64_t voxelsBefore = document.GetVoxelCount();
    const auto paletteBefore = document.GetPalette();
    const std::vector<VoxelPosition> selectionBefore(
        selection.Voxels().begin(), selection.Voxels().end());
    const StampCaptureResult result = StampCaptureService::Capture(
        Request(document, selection, 7U, revisionBefore));

    Require(result.IsSuccess(), "A non-empty sparse selection must capture.");
    Require(document.GetRevision() == revisionBefore && document.GetVoxelCount() == voxelsBefore &&
                document.GetPalette() == paletteBefore,
        "Capture must not mutate the document.");
    Require(std::vector<VoxelPosition>(selection.Voxels().begin(), selection.Voxels().end()) ==
                selectionBefore,
        "Capture must not mutate the source selection.");
    Require(result.Stamp->Bounds().Dimensions == StampDimensions{.X = 4U, .Y = 3U, .Z = 2U},
        "Capture bounds must cover the complete non-rectangular selection extent.");
    Require(result.Stamp->Voxels().size() == 3U &&
                result.Stamp->Voxels()[0] == StampVoxel{.Position = {.X = 0, .Y = 0, .Z = 0}, .LocalColorId = 0U} &&
                result.Stamp->Voxels()[1] == StampVoxel{.Position = {.X = 0, .Y = 2, .Z = 0}, .LocalColorId = 0U} &&
                result.Stamp->Voxels()[2] == StampVoxel{.Position = {.X = 3, .Y = 2, .Z = 1}, .LocalColorId = 0U},
        "Capture must preserve holes, deduplicate input and normalize local coordinates.");
    Require(result.Stamp->Palette().size() == 1U &&
                result.Stamp->Palette()[0].SourcePaletteIndex == 5U &&
                result.Stamp->Palette()[0].Color == Asset::Vox::VoxColor{10U, 20U, 30U, 255U},
        "Capture must merge equal RGBA colors and exclude unused source palette entries.");
    Require(result.Statistics == StampCaptureStatistics{
                .SelectedVoxelCount = 3U,
                .CapturedVoxelCount = 3U,
                .PaletteEntryCount = 1U,
                .HasSelectionBounds = true,
                .SourceMinimum = {2, 1, 3},
                .SourceMaximum = {5, 3, 4},
                .Dimensions = {.X = 4U, .Y = 3U, .Z = 2U}} &&
                result.Diagnostic.Code == StampCaptureError::None &&
                result.Diagnostic.Message == StampCaptureErrorMessage(StampCaptureError::None),
        "Successful capture statistics and diagnostics must be stable and complete.");
    const VfstampWriteResult written = WriteVfstampBytes(*result.Stamp);
    Require(result.Validation.IsValid() && written.IsSuccess() &&
                written.LogicalContentHash == result.LogicalContentHash &&
                CalculateVfstampLogicalContentHash(*result.Stamp) == result.LogicalContentHash,
        "Validation and logical hash must remain coherent with canonical writing.");
    const StampCaptureResult repeated = StampCaptureService::Capture(
        Request(document, selection, 7U, revisionBefore));
    Require(repeated.IsSuccess() && *repeated.Stamp == *result.Stamp &&
                repeated.LogicalContentHash == result.LogicalContentHash,
        "Repeated capture of the same snapshot must be strictly equivalent.");
}

void TestSnapshotFailures()
{
    VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(4U);
    static_cast<void>(selection.Select({2, 1, 3}));

    StampCaptureResult result = StampCaptureService::Capture(Request(document, selection, 5U, 0U));
    Require(result.Error == StampCaptureError::GenerationMismatch &&
                result.ExpectedDocumentGeneration == 5U && result.ActualDocumentGeneration == 4U,
        "Generation mismatch must be a structured capture failure.");

    static_cast<void>(document.ReplaceVoxelColor({2, 1, 3}, 9U));
    result = StampCaptureService::Capture(Request(document, selection, 4U, 0U));
    Require(result.Error == StampCaptureError::RevisionMismatch &&
                result.ExpectedDocumentRevision == 0U &&
                result.ActualDocumentRevision == document.GetRevision(),
        "Revision mismatch must be detected before output construction.");

    SelectionService missingSelection;
    missingSelection.SetDocumentGeneration(4U);
    static_cast<void>(missingSelection.Select({1, 1, 1}));
    result = StampCaptureService::Capture(
        Request(document, missingSelection, 4U, document.GetRevision()));
    Require(result.Error == StampCaptureError::MissingSelectedVoxel,
        "A selected missing voxel must never yield a partial Stamp.");

    SelectionService negativeSelection;
    negativeSelection.SetDocumentGeneration(4U);
    static_cast<void>(negativeSelection.Select({-1, 0, 0}));
    result = StampCaptureService::Capture(
        Request(document, negativeSelection, 4U, document.GetRevision()));
    Require(result.Error == StampCaptureError::MissingSelectedVoxel &&
                result.Diagnostic.Message == StampCaptureErrorMessage(result.Error),
        "Negative source coordinates unsupported by VoxelDocument must fail cleanly.");
}

void TestInputAndSingleVoxelFailures()
{
    VoxelDocument document = MakeDocument();
    SelectionService empty;
    empty.SetDocumentGeneration(8U);
    StampCaptureResult result = StampCaptureService::Capture(Request(document, empty, 8U, 0U));
    Require(result.Error == StampCaptureError::EmptySelection &&
                result.Statistics.SelectedVoxelCount == 0U &&
                result.Diagnostic.Message == StampCaptureErrorMessage(result.Error),
        "Empty selections must produce a stable structured failure.");

    SelectionService single;
    single.SetDocumentGeneration(8U);
    static_cast<void>(single.Select({2, 1, 3}));
    StampCaptureRequest invalidModel = Request(document, single, 8U, 0U);
    invalidModel.SubModelIndex = 1U;
    result = StampCaptureService::Capture(invalidModel);
    Require(result.Error == StampCaptureError::InvalidSubModel,
        "Invalid sub-models must be rejected before capture output allocation.");

    StampCaptureRequest invalidIdentity = Request(document, single, 8U, 0U);
    invalidIdentity.StampId = Core::UUID{0U};
    result = StampCaptureService::Capture(invalidIdentity);
    Require(result.Error == StampCaptureError::InvalidIdentity,
        "Capture must require a caller-provided non-zero UUID.");

    result = StampCaptureService::Capture(Request(document, single, 8U, 0U));
    Require(result.IsSuccess() && result.Stamp->Bounds().Dimensions ==
                StampDimensions{.X = 1U, .Y = 1U, .Z = 1U} &&
                result.Statistics.CapturedVoxelCount == 1U &&
                result.Statistics.PaletteEntryCount == 1U,
        "A single voxel must capture as a normalized one-cell Stamp.");
}

void TestPaletteCompactionWithDistinctColors()
{
    VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(9U);
    static_cast<void>(selection.Apply(
        std::vector<VoxelPosition>{{2, 1, 3}, {7, 7, 7}}, SelectionMode::Replace));
    const StampCaptureResult result = StampCaptureService::Capture(
        Request(document, selection, 9U, 0U));
    Require(result.IsSuccess() && result.Stamp->Palette().size() == 2U &&
                result.Stamp->Palette()[0] == StampPaletteEntry{
                    .LocalColorId = 0U,
                    .Color = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U},
                    .HasSourcePaletteIndex = true,
                    .SourcePaletteIndex = 5U} &&
                result.Stamp->Palette()[1] == StampPaletteEntry{
                    .LocalColorId = 1U,
                    .Color = {.Red = 70U, .Green = 80U, .Blue = 90U, .Alpha = 255U},
                    .HasSourcePaletteIndex = true,
                    .SourcePaletteIndex = 11U} &&
                result.Stamp->Voxels()[0].LocalColorId == 0U &&
                result.Stamp->Voxels()[1].LocalColorId == 1U,
        "Capture must retain each distinct used RGBA color, including source index 11.");
}

void TestOverridesAndPreflightLimits()
{
    VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(12U);
    static_cast<void>(selection.Apply(
        std::vector<VoxelPosition>{{2, 1, 3}, {5, 3, 4}}, SelectionMode::Replace));

    StampCaptureRequest overridden = Request(document, selection, 12U, 0U);
    overridden.PivotContext.RequestedMode = StampPivotMode::Corner;
    overridden.PivotContext.GridDirection = {.X = 1, .Y = -1, .Z = 0};
    StampCaptureResult result = StampCaptureService::Capture(overridden);
    Require(result.IsSuccess() && result.Stamp->Pivot().RequestedMode == StampPivotMode::Corner &&
                result.Stamp->Pivot().ResolvedMode == StampPivotMode::Corner &&
                result.Stamp->Pivot().LocalPosition == StampFixedPoint{.X = 1024, .Y = 0, .Z = 0},
        "An explicit pivot override must take precedence over Auto policy.");

    StampCaptureRequest limited = Request(document, selection, 12U, 0U);
    limited.Limits.SoftVoxelCount = 1U;
    limited.Limits.HardVoxelCount = 1U;
    result = StampCaptureService::Capture(limited);
    Require(result.Error == StampCaptureError::ResourceLimitExceeded &&
                result.LimitEvaluation.LimitKind == StampResourceLimitKind::VoxelCount,
        "Hard limits must reject capture before output vectors are allocated.");

    StampCaptureRequest soft = Request(document, selection, 12U, 0U);
    soft.Limits.SoftVoxelCount = 1U;
    soft.Limits.HardVoxelCount = 4U;
    result = StampCaptureService::Capture(soft);
    Require(result.IsSuccess() && result.LimitEvaluation.HasWarning() &&
                result.Validation.HasWarnings(),
        "Soft limits must preserve capture while reporting a structured warning.");
}

void TestFixedPointOverflowAndLargeSelection()
{
    VoxelDocument document = MakeDocument();
    SelectionService oversized;
    oversized.SetDocumentGeneration(2U);
    static_cast<void>(oversized.Apply(
        std::vector<VoxelPosition>{{0, 0, 0}, {9000000, 0, 0}}, SelectionMode::Replace));
    const StampCaptureResult overflow = StampCaptureService::Capture(
        Request(document, oversized, 2U, 0U));
    Require(overflow.Error == StampCaptureError::ArithmeticOverflow &&
                overflow.Statistics.SelectedVoxelCount == 2U &&
                overflow.Statistics.CapturedVoxelCount == 0U,
        "Fixed-point extent overflow must fail before source reads or output allocation.");

    VoxelDocument large = MakeLargeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(3U);
    std::vector<VoxelPosition> positions;
    positions.reserve(256U);
    for (std::int32_t z = 0; z < 2; ++z)
        for (std::int32_t y = 0; y < 8; ++y)
            for (std::int32_t x = 0; x < 16; ++x)
                positions.push_back({x, y, z});
    static_cast<void>(selection.Apply(positions, SelectionMode::Replace));
    StampCaptureRequest request = Request(large, selection, 3U, 0U);
    request.Limits.SoftVoxelCount = 256U;
    request.Limits.HardVoxelCount = 256U;
    const StampCaptureResult captured = StampCaptureService::Capture(request);
    Require(captured.IsSuccess() && captured.Statistics.SelectedVoxelCount == 256U &&
                captured.Statistics.CapturedVoxelCount == 256U,
        "A large sparse-document selection under the hard limit must capture successfully.");
}

} // namespace

int main()
{
    try
    {
        TestSparseCaptureNormalizationPaletteAndImmutability();
        TestSnapshotFailures();
        TestInputAndSingleVoxelFailures();
        TestPaletteCompactionWithDistinctColors();
        TestOverridesAndPreflightLimits();
        TestFixedPointOverflowAndLargeSelection();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
