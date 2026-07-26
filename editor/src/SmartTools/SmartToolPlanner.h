#pragma once

#include "SmartTools/SmartToolResult.h"
#include "SmartTools/SmartToolRequest.h"

#include <cstdint>

namespace VoxelForge::Editor
{
// The sole SMART-01 geometry adapter. Future geometries are deliberately
// rejected instead of being silently approximated by Pencil.
class SmartToolPlanner final
{
public:
    [[nodiscard]] SmartToolResult Plan(const SmartToolRequest& request);

private:
    std::uint64_t nextPlanId_ = 1U;
};
} // namespace VoxelForge::Editor
