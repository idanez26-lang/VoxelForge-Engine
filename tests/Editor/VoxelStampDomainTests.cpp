#include "VoxelStamps/VoxelStamp.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace VoxelForge::Editor::Stamps;

namespace
{

void Check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

StampIdentity ValidIdentity()
{
    return {.Id = VoxelForge::Core::UUID{42U}, .ContentHash = "domain-test-hash"};
}

StampBounds ValidBounds()
{
    return {
        .Minimum = {},
        .Maximum = {.X = 1, .Y = 1, .Z = 1},
        .Dimensions = {.X = 2U, .Y = 2U, .Z = 2U}};
}

StampTransform UnitTransform()
{
    return {};
}

StampPivot ValidPivot()
{
    return {
        .RequestedMode = StampPivotMode::Auto,
        .ResolvedMode = StampPivotMode::BottomCenter,
        .LocalPosition = {.X = 256, .Y = 0, .Z = 256},
        .LocalNormal = {.X = 0, .Y = 1, .Z = 0},
        .AutoPolicyVersion = 1U};
}

std::vector<StampPaletteEntry> ValidPalette()
{
    return {
        {.LocalColorId = 0U, .Color = {.Red = 255U, .Green = 0U, .Blue = 0U, .Alpha = 255U}},
        {.LocalColorId = 1U, .Color = {.Red = 0U, .Green = 255U, .Blue = 0U, .Alpha = 255U}}};
}

std::vector<StampVoxel> ValidVoxels()
{
    return {
        {.Position = {.X = 0, .Y = 0, .Z = 0}, .LocalColorId = 0U},
        {.Position = {.X = 1, .Y = 1, .Z = 1}, .LocalColorId = 1U}};
}

} // namespace

int main()
{
    try
    {
        const StampResourceLimits defaults = DefaultStampResourceLimits();
        Check(defaults.SoftVoxelCount == 262144U &&
                  defaults.HardVoxelCount == 2097152U &&
                  defaults.SoftAxisLength == 64U && defaults.HardAxisLength == 512U &&
                  defaults.SoftDecodedBytes == 32U * StampResourceLimits::Mebibyte &&
                  defaults.HardDecodedBytes == 256U * StampResourceLimits::Mebibyte &&
                  defaults.SoftFileBytes == 16U * StampResourceLimits::Mebibyte &&
                  defaults.HardFileBytes == 128U * StampResourceLimits::Mebibyte &&
                  defaults.SoftChunkCount == 32U && defaults.HardChunkCount == 64U,
              "V1 default resource thresholds");

        const auto validStamp = VoxelStamp::TryCreate(
            ValidIdentity(),
            ValidBounds(),
            ValidPivot(),
            UnitTransform(),
            ValidPalette(),
            ValidVoxels());
        Check(validStamp.has_value(), "valid normalized stamp");
        Check(validStamp->Voxels().size() == 2U && validStamp->Palette().size() == 2U,
              "immutable stamp accessors");
        Check(validStamp->Bounds() == ValidBounds() && validStamp->Pivot() == ValidPivot() &&
                  validStamp->Transform() == UnitTransform(),
              "bounds pivot and transform accessors");

        const auto sameStamp = VoxelStamp::TryCreate(
            ValidIdentity(),
            ValidBounds(),
            ValidPivot(),
            UnitTransform(),
            ValidPalette(),
            ValidVoxels());
        Check(sameStamp.has_value() && *sameStamp == *validStamp,
              "stamp identity and value equality");
        StampIdentity differentIdentity = ValidIdentity();
        differentIdentity.Id = VoxelForge::Core::UUID{43U};
        const auto differentStamp = VoxelStamp::TryCreate(
            differentIdentity,
            ValidBounds(),
            ValidPivot(),
            UnitTransform(),
            ValidPalette(),
            ValidVoxels());
        Check(differentStamp.has_value() && *differentStamp != *validStamp,
              "stamp equality includes identity");

        StampIdentity zeroIdentity = ValidIdentity();
        zeroIdentity.Id = VoxelForge::Core::UUID{0U};
        Check(VoxelStamp::Validate(
                  zeroIdentity,
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  ValidVoxels())
                  .Error == StampDomainError::InvalidIdentity,
              "zero UUID is rejected");
        StampIdentity oversizedHashIdentity = ValidIdentity();
        oversizedHashIdentity.ContentHash.assign(129U, 'a');
        Check(VoxelStamp::Validate(
                  oversizedHashIdentity,
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  ValidVoxels())
                  .Error == StampDomainError::InvalidIdentity,
              "oversized content hash is rejected");

        StampBounds invalidBounds = ValidBounds();
        invalidBounds.Minimum.X = 1;
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  invalidBounds,
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  ValidVoxels())
                  .Error == StampDomainError::InvalidBounds,
              "bounds minimum must be normalized");

        auto unnormalizedVoxels = ValidVoxels();
        unnormalizedVoxels[0].Position.X = 1;
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  unnormalizedVoxels)
                  .Error == StampDomainError::NonNormalizedCoordinates,
              "voxel extrema must match normalized bounds");

        auto invalidPalette = ValidPalette();
        invalidPalette[1].LocalColorId = 2U;
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  invalidPalette,
                  ValidVoxels())
                  .Error == StampDomainError::InvalidPaletteEntry,
              "palette IDs must be contiguous");

        auto invalidReferenceVoxels = ValidVoxels();
        invalidReferenceVoxels[1].LocalColorId = 2U;
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  invalidReferenceVoxels)
                  .Error == StampDomainError::InvalidPaletteReference,
              "voxel palette reference");

        auto duplicateVoxels = ValidVoxels();
        duplicateVoxels[1] = duplicateVoxels[0];
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  duplicateVoxels)
                  .Error == StampDomainError::DuplicateVoxelPosition,
              "duplicate stamp voxel");

        StampTransform nonUnitTransform{};
        nonUnitTransform.ScaleY = 2.0;
        Check(ValidateStampTransform(nonUnitTransform).Error ==
                  StampDomainError::UnsupportedScale,
              "V1 rejects scales other than 1:1");
        nonUnitTransform = {};
        nonUnitTransform.ScaleX = 0.5;
        Check(ValidateStampTransform(nonUnitTransform).Error ==
                  StampDomainError::UnsupportedScale,
              "V1 rejects fractional scale");
        nonUnitTransform = {};
        nonUnitTransform.ScaleZ = std::numeric_limits<double>::infinity();
        Check(ValidateStampTransform(nonUnitTransform).Error ==
                  StampDomainError::UnsupportedScale,
              "V1 rejects infinite scale");
        nonUnitTransform = {};
        nonUnitTransform.ScaleX = std::numeric_limits<double>::quiet_NaN();
        Check(ValidateStampTransform(nonUnitTransform).Error ==
                  StampDomainError::UnsupportedScale,
              "V1 rejects NaN scale");
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  nonUnitTransform,
                  ValidPalette(),
                  ValidVoxels())
                  .Error == StampDomainError::UnsupportedScale,
              "stamp invariant rejects non-unit scale");

        StampResourceLimits smallLimits{};
        smallLimits.SoftVoxelCount = 1U;
        smallLimits.HardVoxelCount = 3U;
        const StampResourceUsage softUsage{.VoxelCount = 2U};
        Check(EvaluateStampLimits(softUsage, smallLimits).Status ==
                  StampLimitStatus::SoftLimitWarning,
              "soft limit warns but permits");
        const StampLimitEvaluation softEvaluation =
            EvaluateStampLimits(softUsage, smallLimits);
        Check(softEvaluation.LimitKind == StampResourceLimitKind::VoxelCount &&
                  softEvaluation.ActualValue == 2U && softEvaluation.LimitValue == 1U,
              "soft-limit diagnostics identify the exact exceeded resource");
        const StampResourceUsage hardUsage{.VoxelCount = 4U};
        const StampLimitEvaluation hardEvaluation = EvaluateStampLimits(hardUsage, smallLimits);
        Check(hardEvaluation.Status ==
                  StampLimitStatus::HardLimitExceeded,
              "hard limit refuses");
        Check(hardEvaluation.LimitKind == StampResourceLimitKind::VoxelCount &&
                  hardEvaluation.ActualValue == 4U && hardEvaluation.LimitValue == 3U,
              "hard-limit diagnostics identify the exact exceeded resource");

        const StampResourceUsage equalSoftUsage{
            .VoxelCount = defaults.SoftVoxelCount,
            .LargestAxisLength = defaults.SoftAxisLength,
            .DecodedBytes = defaults.SoftDecodedBytes,
            .FileBytes = defaults.SoftFileBytes,
            .ChunkCount = defaults.SoftChunkCount};
        Check(EvaluateStampLimits(equalSoftUsage, defaults).Status == StampLimitStatus::Accepted,
              "values exactly at soft thresholds are accepted");
        StampResourceLimits equalHardLimits = defaults;
        equalHardLimits.SoftVoxelCount = 2U;
        equalHardLimits.HardVoxelCount = 2U;
        Check(EvaluateStampLimits({.VoxelCount = 2U}, equalHardLimits).Status ==
                  StampLimitStatus::Accepted,
              "values exactly at hard thresholds are accepted");

        StampResourceLimits axisLimits = defaults;
        axisLimits.SoftAxisLength = 1U;
        axisLimits.HardAxisLength = 2U;
        Check(EvaluateStampLimits({.LargestAxisLength = 2U}, axisLimits).Status ==
                  StampLimitStatus::SoftLimitWarning,
              "axis soft warning");
        Check(EvaluateStampLimits({.LargestAxisLength = 3U}, axisLimits).Status ==
                  StampLimitStatus::HardLimitExceeded,
              "axis hard refusal");

        StampResourceLimits decodedLimits = defaults;
        decodedLimits.SoftDecodedBytes = 1U;
        decodedLimits.HardDecodedBytes = 2U;
        Check(EvaluateStampLimits({.DecodedBytes = 2U}, decodedLimits).Status ==
                  StampLimitStatus::SoftLimitWarning,
              "decoded-size soft warning");
        Check(EvaluateStampLimits({.DecodedBytes = 3U}, decodedLimits).Status ==
                  StampLimitStatus::HardLimitExceeded,
              "decoded-size hard refusal");

        StampResourceLimits fileLimits = defaults;
        fileLimits.SoftFileBytes = 1U;
        fileLimits.HardFileBytes = 2U;
        Check(EvaluateStampLimits({.FileBytes = 2U}, fileLimits).Status ==
                  StampLimitStatus::SoftLimitWarning,
              "file-size soft warning");
        Check(EvaluateStampLimits({.FileBytes = 3U}, fileLimits).Status ==
                  StampLimitStatus::HardLimitExceeded,
              "file-size hard refusal");

        StampResourceLimits chunkLimits = defaults;
        chunkLimits.SoftChunkCount = 1U;
        chunkLimits.HardChunkCount = 2U;
        Check(EvaluateStampLimits({.ChunkCount = 2U}, chunkLimits).Status ==
                  StampLimitStatus::SoftLimitWarning,
              "chunk-count soft warning");
        Check(EvaluateStampLimits({.ChunkCount = 3U}, chunkLimits).Status ==
                  StampLimitStatus::HardLimitExceeded,
              "chunk-count hard refusal");

        StampResourceLimits unitHardVoxelLimit = defaults;
        unitHardVoxelLimit.SoftVoxelCount = 1U;
        unitHardVoxelLimit.HardVoxelCount = 1U;
        Check(VoxelStamp::Validate(
                  ValidIdentity(),
                  ValidBounds(),
                  ValidPivot(),
                  UnitTransform(),
                  ValidPalette(),
                  ValidVoxels(),
                  unitHardVoxelLimit)
                  .Error == StampDomainError::ResourceLimitExceeded,
              "hard voxel limit rejects before duplicate-detection allocation");

        const StampSizeEstimate overflowEstimate = EstimateDecodedStampBytes(
            1U, std::numeric_limits<std::uint64_t>::max(), 2U, 0U, 0U);
        Check(overflowEstimate.ArithmeticOverflow,
              "size arithmetic overflow is reported before allocation");
        Check(EstimateDecodedStampBytes(
                  std::numeric_limits<std::uint64_t>::max(), 1U, 1U, 0U, 0U)
                  .ArithmeticOverflow,
              "fixed-plus-voxel decoded-size addition overflow is reported");
        Check(EstimateDecodedStampBytes(
                  0U, 0U, 0U, std::numeric_limits<std::uint64_t>::max(), 2U)
                  .ArithmeticOverflow,
              "palette decoded-size multiplication overflow is reported");
        Check(EstimateDecodedStampBytes(
                  std::numeric_limits<std::uint64_t>::max(), 0U, 0U, 1U, 1U)
                  .ArithmeticOverflow,
              "fixed-plus-palette decoded-size addition overflow is reported");
        Check(EvaluateStampLimits({.ArithmeticOverflow = true}).Status ==
                  StampLimitStatus::ArithmeticOverflow,
              "overflow is never accepted by limits");

        StampResourceLimits invalidLimits = defaults;
        invalidLimits.SoftFileBytes = invalidLimits.HardFileBytes + 1U;
        Check(EvaluateStampLimits({}, invalidLimits).Status ==
                  StampLimitStatus::InvalidConfiguration,
              "injected limits must preserve soft/hard ordering");

        std::cout << "Voxel Stamp domain tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
