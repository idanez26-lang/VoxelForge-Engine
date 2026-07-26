#pragma once

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Benchmarks
{

struct AllocationSnapshot final
{
    std::uint64_t AllocationCount = 0U;
    std::uint64_t AllocatedBytes = 0U;
    std::uint64_t PeakLiveBytes = 0U;
    std::uint64_t RetainedBytes = 0U;
};

/// Counts C++ allocations made by the current thread while this scope is
/// active. Nested scopes are supported. Native allocations performed inside
/// SDL or the graphics driver are intentionally outside this counter.
class AllocationScope final
{
public:
    struct State final
    {
        std::uint64_t Epoch = 0U;
        std::uint64_t AllocationCount = 0U;
        std::uint64_t AllocatedBytes = 0U;
        std::uint64_t LiveBytes = 0U;
        std::uint64_t PeakLiveBytes = 0U;
    };

    AllocationScope() noexcept;
    ~AllocationScope();

    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;

    [[nodiscard]] AllocationSnapshot Snapshot() const noexcept;

private:
    State state_{};
    State* previous_ = nullptr;

    friend void RecordAllocation(
        std::size_t size, std::uint64_t& epoch) noexcept;
    friend void RecordDeallocation(
        std::size_t size, std::uint64_t epoch) noexcept;
};

} // namespace VoxelForge::Benchmarks
