#include "VoxelStamps/StampResourceLimits.h"

#include <array>
#include <limits>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] bool AddWouldOverflow(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    return right > std::numeric_limits<std::uint64_t>::max() - left;
}

[[nodiscard]] bool MultiplyWouldOverflow(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    return left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left;
}

[[nodiscard]] bool HasInvalidOrdering(const StampResourceLimits& limits) noexcept
{
    return limits.SoftVoxelCount > limits.HardVoxelCount ||
           limits.SoftAxisLength > limits.HardAxisLength ||
           limits.SoftDecodedBytes > limits.HardDecodedBytes ||
           limits.SoftFileBytes > limits.HardFileBytes ||
           limits.SoftChunkCount > limits.HardChunkCount;
}

} // namespace

const StampResourceLimits& DefaultStampResourceLimits() noexcept
{
    static constexpr StampResourceLimits limits{};
    return limits;
}

StampSizeEstimate EstimateDecodedStampBytes(
    const std::uint64_t fixedBytes,
    const std::uint64_t voxelCount,
    const std::uint64_t voxelBytes,
    const std::uint64_t paletteEntryCount,
    const std::uint64_t paletteEntryBytes) noexcept
{
    StampSizeEstimate estimate{.DecodedBytes = fixedBytes};

    if (MultiplyWouldOverflow(voxelCount, voxelBytes))
    {
        estimate.ArithmeticOverflow = true;
        return estimate;
    }

    const std::uint64_t voxelTotal = voxelCount * voxelBytes;
    if (AddWouldOverflow(estimate.DecodedBytes, voxelTotal))
    {
        estimate.ArithmeticOverflow = true;
        return estimate;
    }
    estimate.DecodedBytes += voxelTotal;

    if (MultiplyWouldOverflow(paletteEntryCount, paletteEntryBytes))
    {
        estimate.ArithmeticOverflow = true;
        return estimate;
    }

    const std::uint64_t paletteTotal = paletteEntryCount * paletteEntryBytes;
    if (AddWouldOverflow(estimate.DecodedBytes, paletteTotal))
    {
        estimate.ArithmeticOverflow = true;
        return estimate;
    }
    estimate.DecodedBytes += paletteTotal;
    return estimate;
}

StampLimitEvaluation EvaluateStampLimits(
    const StampResourceUsage& usage,
    const StampResourceLimits& limits) noexcept
{
    if (HasInvalidOrdering(limits))
    {
        return {StampLimitStatus::InvalidConfiguration, StampResourceLimitKind::None, 0U, 0U,
                "Stamp resource limits have an invalid soft/hard ordering."};
    }

    if (usage.ArithmeticOverflow)
    {
        return {StampLimitStatus::ArithmeticOverflow, StampResourceLimitKind::None, 0U, 0U,
                "Stamp size arithmetic overflowed before allocation."};
    }

    const auto evaluate = [](const std::uint64_t actual,
                             const std::uint64_t soft,
                             const std::uint64_t hard,
                             const StampResourceLimitKind kind,
                             const std::string_view hardMessage,
                             const std::string_view softMessage)
        -> StampLimitEvaluation {
        if (actual > hard)
        {
            return {StampLimitStatus::HardLimitExceeded, kind, actual, hard, hardMessage};
        }
        if (actual > soft)
        {
            return {StampLimitStatus::SoftLimitWarning, kind, actual, soft, softMessage};
        }
        return {};
    };

    const std::array<StampLimitEvaluation, 5U> evaluations{
        evaluate(usage.VoxelCount, limits.SoftVoxelCount, limits.HardVoxelCount,
                 StampResourceLimitKind::VoxelCount, "Stamp exceeds the hard voxel-count limit.",
                 "Stamp exceeds the soft voxel-count limit."),
        evaluate(usage.LargestAxisLength, limits.SoftAxisLength, limits.HardAxisLength,
                 StampResourceLimitKind::AxisLength, "Stamp exceeds the hard axis-length limit.",
                 "Stamp exceeds the soft axis-length limit."),
        evaluate(usage.DecodedBytes, limits.SoftDecodedBytes, limits.HardDecodedBytes,
                 StampResourceLimitKind::DecodedBytes, "Stamp exceeds the hard decoded-size limit.",
                 "Stamp exceeds the soft decoded-size limit."),
        evaluate(usage.FileBytes, limits.SoftFileBytes, limits.HardFileBytes,
                 StampResourceLimitKind::FileBytes, "Stamp exceeds the hard file-size limit.",
                 "Stamp exceeds the soft file-size limit."),
        evaluate(usage.ChunkCount, limits.SoftChunkCount, limits.HardChunkCount,
                 StampResourceLimitKind::ChunkCount, "Stamp exceeds the hard chunk-count limit.",
                 "Stamp exceeds the soft chunk-count limit.")};

    for (const StampLimitEvaluation& evaluation : evaluations)
    {
        if (evaluation.Status == StampLimitStatus::HardLimitExceeded)
        {
            return evaluation;
        }
    }
    for (const StampLimitEvaluation& evaluation : evaluations)
    {
        if (evaluation.Status == StampLimitStatus::SoftLimitWarning)
        {
            return evaluation;
        }
    }

    return {StampLimitStatus::Accepted, StampResourceLimitKind::None, 0U, 0U,
            "Stamp resource usage is within configured limits."};
}

} // namespace VoxelForge::Editor::Stamps
