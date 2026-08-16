#pragma once

// VF-STAB-01 bug 2 — single authority for the persistent highlight GPU buffer
// capacity decision, shared by both upload paths in ViewportRenderer.
//
// The legacy path and the Interaction V2 path write the SAME
// highlightVertexBuffer_/highlightIndexBuffer_ members. The capacity trackers
// (interactionV2HighlightVertexCapacity_/IndexCapacity_) describe those shared
// buffers. The safety of an in-place upload (SDL_UploadToGPUBuffer with cycle
// == false) rests on ONE invariant the callers must uphold:
//
//     the tracked capacity always equals the real GPU buffer's byte size.
//
// Under that invariant, PlanHighlightBuffer().Replace == false implies
// requiredBytes <= Capacity == real buffer size, so the upload never overflows.
// The bug was the legacy path breaking the invariant: it recreated the shared
// buffer at an exact (smaller) size without updating the tracker, so the V2 path
// later reused a too-small buffer. See ViewportRenderer::UploadLegacyHighlights.

#include <algorithm>
#include <cstddef>
#include <limits>

namespace VoxelForge::Editor
{

// Grows a persistent GPU buffer capacity to fit `requiredBytes`, never shrinking
// below the tracked capacity or a 4096-byte floor, and never past SIZE_MAX.
[[nodiscard]] inline std::size_t GrowHighlightCapacity(
    const std::size_t trackedCapacity,
    const std::size_t requiredBytes) noexcept
{
    std::size_t capacity = std::max<std::size_t>(trackedCapacity, 4096U);
    while (capacity < requiredBytes &&
           capacity <= std::numeric_limits<std::size_t>::max() / 2U)
        capacity *= 2U;
    return std::max(capacity, requiredBytes);
}

struct HighlightBufferDecision final
{
    std::size_t Capacity = 0U;
    bool Replace = false;
};

// Decides whether the persistent highlight buffer must be recreated to hold
// `requiredBytes`, given the currently tracked capacity and whether a buffer
// already exists. Replace == false is only ever returned when the grown
// capacity equals the tracked capacity, which (under the caller invariant) is
// the real buffer size — and GrowHighlightCapacity guarantees Capacity >=
// requiredBytes, so reuse is always in-bounds.
[[nodiscard]] inline HighlightBufferDecision PlanHighlightBuffer(
    const std::size_t trackedCapacity,
    const bool hasBuffer,
    const std::size_t requiredBytes) noexcept
{
    const std::size_t capacity =
        GrowHighlightCapacity(trackedCapacity, requiredBytes);
    return HighlightBufferDecision{
        capacity, !hasBuffer || capacity != trackedCapacity};
}

} // namespace VoxelForge::Editor
