#pragma once

#include "ConstraintRequest.h"
#include "ConstraintResult.h"

namespace VoxelForge::Editor
{

/**
 * Central, tool-neutral transformation constraint solver.
 *
 * Solve is deterministic, constant-time, allocation-free, and is the only
 * public entry point callers need. The engine deliberately has no knowledge
 * of editor tools, ImGui, gizmos, rendering, or viewport state.
 */
class ConstraintEngine final
{
public:
    [[nodiscard]] static ConstraintResult Solve(
        const ConstraintRequest& request) noexcept;
};

} // namespace VoxelForge::Editor
