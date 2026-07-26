#include "StampPlacementBenchmarkMetrics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

namespace VoxelForge::Benchmarks
{
void RecordAllocation(
    std::size_t size, std::uint64_t& epoch) noexcept;
void RecordDeallocation(
    std::size_t size, std::uint64_t epoch) noexcept;

namespace
{

thread_local AllocationScope::State* ActiveState = nullptr;
thread_local std::uint64_t NextEpoch = 1U;

struct alignas(std::max_align_t) AllocationHeader final
{
    void* Base = nullptr;
    std::size_t Size = 0U;
    std::uint64_t Epoch = 0U;
};

[[nodiscard]] void* Allocate(
    const std::size_t requestedSize,
    const std::size_t requestedAlignment)
{
    const std::size_t size = std::max<std::size_t>(requestedSize, 1U);
    const std::size_t alignment =
        std::max<std::size_t>(requestedAlignment, alignof(std::max_align_t));
    if (alignment > std::numeric_limits<std::size_t>::max() -
            sizeof(AllocationHeader) ||
        size > std::numeric_limits<std::size_t>::max() -
            sizeof(AllocationHeader) - alignment)
    {
        throw std::bad_alloc{};
    }

    const std::size_t allocationSize =
        size + sizeof(AllocationHeader) + alignment;
    void* const base = std::malloc(allocationSize);
    if (base == nullptr)
    {
        throw std::bad_alloc{};
    }

    const std::uintptr_t unaligned =
        reinterpret_cast<std::uintptr_t>(base) + sizeof(AllocationHeader);
    const std::uintptr_t aligned =
        (unaligned + alignment - 1U) & ~(alignment - 1U);
    auto* const header = reinterpret_cast<AllocationHeader*>(
        aligned - sizeof(AllocationHeader));
    header->Base = base;
    header->Size = size;
    RecordAllocation(size, header->Epoch);
    return reinterpret_cast<void*>(aligned);
}

void Deallocate(void* const pointer) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }

    auto* const header = reinterpret_cast<AllocationHeader*>(
        reinterpret_cast<std::uintptr_t>(pointer) -
        sizeof(AllocationHeader));
    RecordDeallocation(header->Size, header->Epoch);
    std::free(header->Base);
}

} // namespace

void RecordAllocation(
    const std::size_t size,
    std::uint64_t& epoch) noexcept
{
    epoch = ActiveState != nullptr ? ActiveState->Epoch : 0U;
    if (ActiveState == nullptr)
    {
        return;
    }

    ++ActiveState->AllocationCount;
    ActiveState->AllocatedBytes += size;
    ActiveState->LiveBytes += size;
    ActiveState->PeakLiveBytes = std::max(
        ActiveState->PeakLiveBytes, ActiveState->LiveBytes);
}

void RecordDeallocation(
    const std::size_t size,
    const std::uint64_t epoch) noexcept
{
    if (ActiveState == nullptr || epoch != ActiveState->Epoch)
    {
        return;
    }

    ActiveState->LiveBytes =
        size <= ActiveState->LiveBytes
        ? ActiveState->LiveBytes - size
        : 0U;
}

AllocationScope::AllocationScope() noexcept
{
    state_.Epoch = NextEpoch++;
    if (NextEpoch == 0U)
    {
        NextEpoch = 1U;
    }
    previous_ = ActiveState;
    ActiveState = &state_;
}

AllocationScope::~AllocationScope()
{
    ActiveState = previous_;
}

AllocationSnapshot AllocationScope::Snapshot() const noexcept
{
    return {
        state_.AllocationCount,
        state_.AllocatedBytes,
        state_.PeakLiveBytes,
        state_.LiveBytes};
}

} // namespace VoxelForge::Benchmarks

void* operator new(const std::size_t size)
{
    return VoxelForge::Benchmarks::Allocate(
        size, alignof(std::max_align_t));
}

void* operator new[](const std::size_t size)
{
    return VoxelForge::Benchmarks::Allocate(
        size, alignof(std::max_align_t));
}

void* operator new(
    const std::size_t size,
    const std::align_val_t alignment)
{
    return VoxelForge::Benchmarks::Allocate(
        size, static_cast<std::size_t>(alignment));
}

void* operator new[](
    const std::size_t size,
    const std::align_val_t alignment)
{
    return VoxelForge::Benchmarks::Allocate(
        size, static_cast<std::size_t>(alignment));
}

void* operator new(
    const std::size_t size,
    const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](
    const std::size_t size,
    const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new(
    const std::size_t size,
    const std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](
    const std::size_t size,
    const std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void operator delete(void* const pointer) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](void* const pointer) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete(
    void* const pointer,
    const std::size_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](
    void* const pointer,
    const std::size_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete(
    void* const pointer,
    const std::align_val_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](
    void* const pointer,
    const std::align_val_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete(
    void* const pointer,
    const std::size_t,
    const std::align_val_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](
    void* const pointer,
    const std::size_t,
    const std::align_val_t) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete(
    void* const pointer,
    const std::nothrow_t&) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](
    void* const pointer,
    const std::nothrow_t&) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete(
    void* const pointer,
    const std::align_val_t,
    const std::nothrow_t&) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}

void operator delete[](
    void* const pointer,
    const std::align_val_t,
    const std::nothrow_t&) noexcept
{
    VoxelForge::Benchmarks::Deallocate(pointer);
}
