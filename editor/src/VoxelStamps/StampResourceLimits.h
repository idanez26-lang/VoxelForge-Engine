#pragma once

#include <cstdint>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{

struct StampResourceLimits final
{
    static constexpr std::uint64_t Mebibyte = 1024U * 1024U;

    std::uint64_t SoftVoxelCount = 262144U;
    std::uint64_t HardVoxelCount = 2097152U;
    std::uint32_t SoftAxisLength = 64U;
    std::uint32_t HardAxisLength = 512U;
    std::uint64_t SoftDecodedBytes = 32U * Mebibyte;
    std::uint64_t HardDecodedBytes = 256U * Mebibyte;
    std::uint64_t SoftFileBytes = 16U * Mebibyte;
    std::uint64_t HardFileBytes = 128U * Mebibyte;
    std::uint32_t SoftChunkCount = 32U;
    std::uint32_t HardChunkCount = 64U;
};

[[nodiscard]] const StampResourceLimits& DefaultStampResourceLimits() noexcept;

struct StampResourceUsage final
{
    std::uint64_t VoxelCount = 0U;
    std::uint32_t LargestAxisLength = 0U;
    std::uint64_t DecodedBytes = 0U;
    std::uint64_t FileBytes = 0U;
    std::uint32_t ChunkCount = 0U;
    bool ArithmeticOverflow = false;
};

struct StampSizeEstimate final
{
    std::uint64_t DecodedBytes = 0U;
    bool ArithmeticOverflow = false;
};

// Computes fixed bytes + voxelCount * voxelBytes + paletteCount * paletteBytes.
// It never wraps: overflow is reported instead of producing an undersized value.
[[nodiscard]] StampSizeEstimate EstimateDecodedStampBytes(
    std::uint64_t fixedBytes,
    std::uint64_t voxelCount,
    std::uint64_t voxelBytes,
    std::uint64_t paletteEntryCount,
    std::uint64_t paletteEntryBytes) noexcept;

enum class StampLimitStatus
{
    Accepted,
    SoftLimitWarning,
    HardLimitExceeded,
    ArithmeticOverflow,
    InvalidConfiguration
};

enum class StampResourceLimitKind
{
    None,
    VoxelCount,
    AxisLength,
    DecodedBytes,
    FileBytes,
    ChunkCount
};

struct StampLimitEvaluation final
{
    StampLimitStatus Status = StampLimitStatus::Accepted;
    StampResourceLimitKind LimitKind = StampResourceLimitKind::None;
    std::uint64_t ActualValue = 0U;
    std::uint64_t LimitValue = 0U;
    std::string_view Message{"Stamp resource usage is within configured limits."};

    [[nodiscard]] bool IsAllowed() const noexcept
    {
        return Status == StampLimitStatus::Accepted ||
               Status == StampLimitStatus::SoftLimitWarning;
    }

    [[nodiscard]] bool HasWarning() const noexcept
    {
        return Status == StampLimitStatus::SoftLimitWarning;
    }
};

// Soft-limit excess is allowed with a warning. Hard-limit excess and arithmetic
// overflow are refused before any caller allocates a representation from the data.
[[nodiscard]] StampLimitEvaluation EvaluateStampLimits(
    const StampResourceUsage& usage,
    const StampResourceLimits& limits = DefaultStampResourceLimits()) noexcept;

} // namespace VoxelForge::Editor::Stamps
