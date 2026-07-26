#pragma once

#include "BrushEngine/SmartBrushEngine.h"

#include <cstdint>
#include <memory>
#include <string>

namespace VoxelForge::Editor
{
class SmartToolPlan;

// Canonical outcome for SMART-01 clients. The existing SmartBrush result code
// remains available during the Pencil migration, but callers should not need
// to interpret every compatibility code to decide whether an interaction is
// usable, degraded, or failed.
enum class SmartToolStatus : std::uint8_t
{
    Success,
    Warning,
    Error
};

[[nodiscard]] constexpr SmartToolStatus SmartToolStatusFrom(
    const SmartBrushResultCode code) noexcept
{
    switch (code)
    {
    case SmartBrushResultCode::Valid: return SmartToolStatus::Success;
    case SmartBrushResultCode::OutOfBounds: return SmartToolStatus::Warning;
    case SmartBrushResultCode::Unsupported:
    case SmartBrushResultCode::InvalidRequest:
    case SmartBrushResultCode::TechnicalFailure:
        return SmartToolStatus::Error;
    }
    return SmartToolStatus::Error;
}

struct SmartToolResult final
{
    SmartToolStatus Status = SmartToolStatus::Error;
    SmartBrushResultCode Code = SmartBrushResultCode::InvalidRequest;
    std::shared_ptr<const SmartToolPlan> Plan;
    std::string Error;

    [[nodiscard]] bool HasPlan() const noexcept { return Plan != nullptr; }
    [[nodiscard]] bool Succeeded() const noexcept
    { return Status == SmartToolStatus::Success; }
};
} // namespace VoxelForge::Editor
