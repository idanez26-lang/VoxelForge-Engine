#pragma once

#include "SmartTools/SmartToolPlanner.h"
#include "SmartTools/SmartToolSession.h"

namespace VoxelForge::Editor
{
// Coordinates session reuse only. Geometry remains exclusively in the planner.
class SmartToolController final
{
public:
    [[nodiscard]] SmartToolResult Resolve(
        SmartToolSession& session, const SmartToolRequest& request);
    // Explicit consumer boundaries. Both route through the same session cache
    // and therefore return the exact same immutable SmartToolPlan instance.
    [[nodiscard]] SmartToolResult ResolvePreview(
        SmartToolSession& session, const SmartToolRequest& request);
    [[nodiscard]] SmartToolResult ResolveCommit(
        SmartToolSession& session, const SmartToolRequest& request);

private:
    SmartToolPlanner planner_;
};
} // namespace VoxelForge::Editor
